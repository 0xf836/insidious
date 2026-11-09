use std::ffi::{c_char, c_int};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::{ptr, slice};

use jxl::api::{
    AlphaMode, Event, ExtraChannel, JxlColorType, JxlDataFormat, JxlDecoder, JxlDecoderOptions,
    JxlOutputBuffer, JxlParallelRunner, JxlParallelRunnerFun, JxlPixelFormat,
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
}

#[repr(C)]
pub struct InsidiousJxlInfo {
    width: c_int,
    height: c_int,
    bands: c_int,
    orientation: c_int,
    icc: *const u8,
    icc_size: usize,
}

pub struct InsidiousJxl {
    decoder: JxlDecoder,
    // The C++ caller retains the immutable QByteArray until this handle is closed.
    input: *const u8,
    input_size: usize,
    width: usize,
    height: usize,
    bands: usize,
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
/// All output pointers must be writable. Input bytes must remain valid and immutable until close.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn insidious_jxl_open(
    data: *const u8,
    size: usize,
    sample_limit: usize,
    handle: *mut *mut InsidiousJxl,
    info: *mut InsidiousJxlInfo,
    error: *mut c_char,
    error_size: usize,
) -> c_int {
    report(error, error_size, || {
        if data.is_null() || size > isize::MAX as usize || handle.is_null() || info.is_null() {
            return Err(jxl::error::Error::ApiUsageError("invalid bridge arguments"));
        }
        // SAFETY: checked non-null pointers; the caller guarantees the input allocation and output handle.
        let mut input = unsafe {
            *handle = ptr::null_mut();
            slice::from_raw_parts(data, size)
        };
        let mut options = JxlDecoderOptions::default();
        options.adjust_orientation = false;
        options.alpha_mode = AlphaMode::Unpremultiplied;
        options.sample_limit = Some(sample_limit);
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
            .and_then(|n| n.checked_mul(bands))
            .filter(|n| *n <= isize::MAX as usize)
            .ok_or(jxl::error::Error::SizeOverflow)?;
        decoder.set_pixel_format(JxlPixelFormat {
            color_type,
            color_data_format: Some(JxlDataFormat::U8 { bit_depth: 8 }),
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
            input: input.as_ptr(),
            input_size: input.len(),
            width,
            height,
            bands,
            icc,
        });
        // SAFETY: caller supplies writable outputs; the boxed profile remains alive until close.
        unsafe {
            *info = InsidiousJxlInfo {
                width: width as c_int,
                height: height as c_int,
                bands: bands as c_int,
                orientation,
                icc: state.icc.as_ptr(),
                icc_size: state.icc.len(),
            };
            *handle = Box::into_raw(state);
        }
        Ok(())
    })
}

/// # Safety
/// Handle must be open, its input alive, and pixels writable for size bytes. Call only once per handle.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn insidious_jxl_decode(
    handle: *mut InsidiousJxl,
    pixels: *mut u8,
    size: usize,
    error: *mut c_char,
    error_size: usize,
) -> c_int {
    report(error, error_size, || {
        if handle.is_null() || pixels.is_null() || size > isize::MAX as usize {
            return Err(jxl::error::Error::ApiUsageError("invalid bridge arguments"));
        }
        // SAFETY: caller owns a live, exclusively accessed handle from open.
        let state = unsafe { &mut *handle };
        let required = state.width * state.height * state.bands;
        if size < required {
            return Err(jxl::error::Error::ApiUsageError(
                "output buffer is too small",
            ));
        }
        // SAFETY: caller retains input bytes and provides a distinct writable output allocation.
        let (mut input, output) = unsafe {
            (
                slice::from_raw_parts(state.input, state.input_size),
                slice::from_raw_parts_mut(pixels, required),
            )
        };
        let mut output = JxlOutputBuffer::new(output, state.height, state.width * state.bands);
        let mut runner = ParallelRunner;
        loop {
            match state.decoder.process(
                &mut input,
                Some(slice::from_mut(&mut output)),
                Some(&mut runner),
            )? {
                Event::FrameHeader => {}
                Event::FrameComplete { .. } => return Ok(()),
                _ => return Err(jxl::error::Error::ApiUsageError("truncated JPEG XL frame")),
            }
        }
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
