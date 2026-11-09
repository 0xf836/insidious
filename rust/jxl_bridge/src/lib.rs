use std::ffi::{c_char, c_int, c_void};
use std::io::{self, IoSliceMut};
mod metadata;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::{ptr, slice};

use jxl::api::{
    AlphaMode, Endianness, Event, ExtraChannel, JxlBitDepth, JxlBitstreamInput, JxlColorType,
    JxlDataFormat, JxlDecoder, JxlDecoderOptions, JxlOutputBuffer, JxlParallelRunner,
    JxlParallelRunnerFun, JxlPixelFormat,
};
use rayon::prelude::*;

struct ParallelRunner;

impl JxlParallelRunner for ParallelRunner {
    fn run(&mut self, num: usize, fun: &JxlParallelRunnerFun<'_>) -> jxl::error::Result<()> {
        (0..num).into_par_iter().try_for_each(fun)
    }

    fn num_threads(&self) -> usize {
        rayon::current_num_threads()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    #[test]
    fn runner_executes_each_task_and_propagates_errors() {
        let counts: Vec<_> = (0..97).map(|_| AtomicUsize::new(0)).collect();
        ParallelRunner
            .run_ordered(counts.len(), Some(4), &|i| {
                counts[i].fetch_add(1, Ordering::Relaxed);
                Ok(())
            })
            .unwrap();
        assert!(counts.iter().all(|n| n.load(Ordering::Relaxed) == 1));
        assert!(
            ParallelRunner
                .run(4, &|_| Err(jxl::error::Error::InvalidSignature))
                .is_err()
        );
    }

    #[test]
    fn input_callback_errors_are_terminal_io_errors() {
        unsafe extern "C" fn failed(_: *mut c_void, _: *mut u8, _: usize) -> i64 {
            -1
        }
        let mut input = Input {
            read: failed,
            context: ptr::null_mut(),
            remaining: 12,
            metadata: metadata::Collector::default(),
        };
        let mut buffer = [0u8; 12];
        assert!(input.read(&mut [IoSliceMut::new(&mut buffer)]).is_err());
    }
}

pub type ReadCallback = unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i64;

struct Input {
    read: ReadCallback,
    context: *mut c_void,
    remaining: usize,
    metadata: metadata::Collector,
}
impl JxlBitstreamInput for Input {
    fn available_bytes(&mut self) -> io::Result<usize> {
        Ok(self.remaining)
    }
    fn read(&mut self, bufs: &mut [IoSliceMut<'_>]) -> io::Result<usize> {
        let mut total = 0;
        for buf in bufs {
            let mut offset = 0;
            while offset < buf.len() {
                // SAFETY: the caller retains the context; the callback may write only the supplied slice.
                let n = unsafe {
                    (self.read)(self.context, buf[offset..].as_mut_ptr(), buf.len() - offset)
                };
                if n < 0 || n as usize > buf.len() - offset {
                    return Err(io::Error::other("JPEG XL source read failed"));
                }
                let n = n as usize;
                if n == 0 {
                    self.remaining = 0;
                    return Ok(total);
                }
                self.metadata.feed(&buf[offset..offset + n]);
                self.remaining = self.remaining.saturating_sub(n);
                total += n;
                offset += n;
            }
        }
        Ok(total)
    }
}

#[repr(C)]
pub struct InsidiousJxlInfo {
    width: c_int,
    height: c_int,
    bands: c_int,
    orientation: c_int,
    bytes_per_sample: c_int,
    animated: c_int,
    total_plays: u32,
    icc: *const u8,
    icc_size: usize,
}

pub struct InsidiousJxl {
    decoder: JxlDecoder,
    input: Input,
    width: usize,
    height: usize,
    bands: usize,
    bytes_per_sample: usize,
    scan_only: bool,
    icc: Vec<u8>,
}

fn report(
    error: *mut c_char,
    error_size: usize,
    action: impl FnOnce() -> Result<(), jxl::error::Error>,
) -> c_int {
    let (status, message) = match catch_unwind(AssertUnwindSafe(action)) {
        Ok(Ok(())) => return 0,
        Ok(Err(error)) => {
            let status = match error {
                jxl::error::Error::ImageSizeTooLarge(..)
                | jxl::error::Error::ImageDimensionTooLarge(..)
                | jxl::error::Error::SizeOverflow => 2,
                _ => 1,
            };
            (status, error.to_string())
        }
        Err(_) => (1, "JPEG XL decoder panicked".to_owned()),
    };
    if !error.is_null() && error_size > 0 {
        let count = message.len().min(error_size - 1);
        // SAFETY: the caller supplies a writable error buffer of error_size bytes.
        unsafe {
            ptr::copy_nonoverlapping(message.as_ptr(), error.cast::<u8>(), count);
            *error.add(count) = 0;
        }
    }
    status
}

/// # Safety
/// All output pointers must be writable; callback and context must remain valid until close.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn insidious_jxl_open(
    read: ReadCallback,
    context: *mut c_void,
    size: usize,
    scan_only: c_int,
    sample_limit: usize,
    handle: *mut *mut InsidiousJxl,
    info: *mut InsidiousJxlInfo,
    error: *mut c_char,
    error_size: usize,
) -> c_int {
    report(error, error_size, || {
        if handle.is_null() || info.is_null() {
            return Err(jxl::error::Error::ApiUsageError("invalid bridge arguments"));
        }
        // SAFETY: the caller supplies writable output storage.
        unsafe {
            *handle = ptr::null_mut();
        }
        let mut input = Input {
            read,
            context,
            remaining: size,
            metadata: metadata::Collector::default(),
        };
        let mut options = JxlDecoderOptions::default();
        options.adjust_orientation = true;
        options.alpha_mode = AlphaMode::Unpremultiplied;
        options.sample_limit = Some(sample_limit);
        options.scan_frames_only = scan_only != 0;
        let mut decoder = JxlDecoder::new(options);
        if decoder.process(&mut input, None, None)? != Event::BasicInfo {
            return Err(jxl::error::Error::ApiUsageError("truncated JPEG XL header"));
        }
        let basic = decoder.basic_info().unwrap();
        let (width, height) = basic.size;
        if width == 0 || height == 0 || width > c_int::MAX as usize || height > c_int::MAX as usize
        {
            return Err(jxl::error::Error::ImageSizeTooLarge(width, height));
        }
        let orientation = basic.orientation as c_int;
        let transposed = basic.orientation.is_transposing();
        let animated = basic.animation.is_some();
        let total_plays = basic.animation.as_ref().map_or(1, |a| a.num_loops);
        let bytes_per_sample = match basic.bit_depth {
            JxlBitDepth::Int { bits_per_sample } if bits_per_sample <= 8 => 1,
            _ => 2,
        };
        let alpha = basic
            .extra_channels
            .iter()
            .any(|channel| channel.ec_type == ExtraChannel::Alpha);
        let gray = decoder.embedded_color_profile().unwrap().channels() == 1;
        let color_type = match (gray, alpha) {
            (true, false) => JxlColorType::Grayscale,
            (true, true) => JxlColorType::GrayscaleAlpha,
            (false, false) => JxlColorType::Rgb,
            (false, true) => JxlColorType::Rgba,
        };
        let bands = color_type.samples_per_pixel();
        width
            .checked_mul(height)
            .and_then(|n| n.checked_mul(bands * bytes_per_sample))
            .filter(|n| *n <= isize::MAX as usize)
            .ok_or(jxl::error::Error::SizeOverflow)?;
        decoder.set_pixel_format(JxlPixelFormat {
            color_type,
            color_data_format: Some(if bytes_per_sample == 1 {
                JxlDataFormat::U8 { bit_depth: 8 }
            } else {
                JxlDataFormat::U16 {
                    endianness: Endianness::native(),
                    bit_depth: 16,
                }
            }),
            extra_channel_format: vec![None; basic.extra_channels.len()],
        })?;
        let icc = decoder
            .output_color_profile()
            .and_then(|profile| profile.try_as_icc())
            .map(|profile| profile.into_owned())
            .ok_or(jxl::error::Error::ApiUsageError(
                "unsupported output color profile",
            ))?;
        let state = Box::new(InsidiousJxl {
            decoder,
            input,
            scan_only: scan_only != 0,
            bytes_per_sample,
            width,
            height,
            bands,
            icc,
        });
        // SAFETY: caller supplies writable outputs; the boxed profile remains alive until close.
        unsafe {
            *info = InsidiousJxlInfo {
                width: if transposed { height } else { width } as c_int,
                height: if transposed { width } else { height } as c_int,
                bands: bands as c_int,
                orientation,
                bytes_per_sample: bytes_per_sample as c_int,
                animated: animated as c_int,
                total_plays,
                icc: state.icc.as_ptr(),
                icc_size: state.icc.len(),
            };
            *handle = Box::into_raw(state);
        }
        Ok(())
    })
}

/// # Safety
/// Handle must be open, its source alive, and pixels writable for size bytes (unless scanning).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn insidious_jxl_decode(
    handle: *mut InsidiousJxl,
    pixels: *mut u8,
    size: usize,
    duration_ms: *mut f64,
    has_more: *mut c_int,
    error: *mut c_char,
    error_size: usize,
) -> c_int {
    report(error, error_size, || {
        if handle.is_null()
            || duration_ms.is_null()
            || has_more.is_null()
            || size > isize::MAX as usize
        {
            return Err(jxl::error::Error::ApiUsageError("invalid bridge arguments"));
        }
        // SAFETY: caller owns a live, exclusively accessed handle from open.
        let state = unsafe { &mut *handle };
        let required = state.width * state.height * state.bands * state.bytes_per_sample;
        if !state.scan_only && (pixels.is_null() || size < required) {
            return Err(jxl::error::Error::ApiUsageError(
                "output buffer is too small",
            ));
        }
        // SAFETY: an active decode receives a distinct writable buffer; scan mode never exposes pixels.
        let output = if state.scan_only {
            &mut [][..]
        } else {
            unsafe { slice::from_raw_parts_mut(pixels, required) }
        };
        let mut output = if state.scan_only {
            None
        } else {
            Some(JxlOutputBuffer::new(
                output,
                state.height,
                state.width * state.bands * state.bytes_per_sample,
            ))
        };
        let mut runner = ParallelRunner;
        loop {
            match state.decoder.process(
                &mut state.input,
                output.as_mut().map(slice::from_mut),
                Some(&mut runner),
            )? {
                Event::FrameHeader => {
                    // SAFETY: the caller provides writable result pointers.
                    unsafe {
                        *duration_ms = state
                            .decoder
                            .frame_header()
                            .and_then(|f| f.duration)
                            .unwrap_or(-1.0);
                    }
                }
                Event::FrameComplete { has_more_frames } => {
                    if state
                        .decoder
                        .scanned_frames()
                        .last()
                        .is_some_and(|frame| frame.duration_ticks == u32::MAX)
                    {
                        // SAFETY: the caller provides writable result storage.
                        unsafe {
                            *duration_ms = -1.0;
                        }
                    }
                    // SAFETY: the caller provides writable result storage.
                    unsafe {
                        *has_more = has_more_frames as c_int;
                    }
                    return Ok(());
                }
                _ => return Err(jxl::error::Error::ApiUsageError("truncated JPEG XL frame")),
            }
        }
    })
}

/// # Safety
/// Handle and output pointers must be valid; returned metadata lives until close.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn insidious_jxl_finish(
    handle: *mut InsidiousJxl,
    exif: *mut *const u8,
    size: *mut usize,
    error: *mut c_char,
    error_size: usize,
) -> c_int {
    report(error, error_size, || {
        if handle.is_null() || exif.is_null() || size.is_null() {
            return Err(jxl::error::Error::ApiUsageError("invalid bridge arguments"));
        }
        // SAFETY: caller owns an exclusively accessed live handle.
        let state = unsafe { &mut *handle };
        let mut buffer = [0u8; 64 * 1024];
        while state
            .input
            .read(&mut [IoSliceMut::new(&mut buffer)])
            .map_err(jxl::error::Error::IOError)?
            != 0
        {}
        state.input.metadata.finish();
        // SAFETY: caller provides writable outputs; data remains owned by handle.
        unsafe {
            *exif = state.input.metadata.exif.as_ptr();
            *size = state.input.metadata.exif.len();
        }
        Ok(())
    })
}

/// # Safety
/// Handle must be null or an unclosed handle returned by open, with no calls in progress.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn insidious_jxl_close(handle: *mut InsidiousJxl) {
    if !handle.is_null() {
        // SAFETY: ownership of this allocation is returned exactly once by the caller.
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe { drop(Box::from_raw(handle)) }));
    }
}
