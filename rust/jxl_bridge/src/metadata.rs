use std::io::Read;

const LIMIT: usize = 8 * 1024 * 1024;

/// Observe container bytes without asking the codec to buffer unbounded auxiliary boxes.
#[derive(Default)]
pub struct Collector {
    header: Vec<u8>,
    payload: Vec<u8>,
    remaining: u64,
    kind: [u8; 4],
    disabled: bool,
    capture: bool,
    open_ended: bool,
    pub exif: Vec<u8>,
}

impl Collector {
    pub fn feed(&mut self, mut data: &[u8]) {
        while !data.is_empty() && !self.disabled {
            if self.remaining != 0 {
                let n = self.remaining.min(data.len() as u64) as usize;
                if self.capture {
                    if self.payload.len().saturating_add(n) <= LIMIT {
                        self.payload.extend_from_slice(&data[..n]);
                    } else {
                        self.capture = false;
                        self.payload.clear();
                    }
                }
                data = &data[n..];
                self.remaining -= n as u64;
                if self.remaining == 0 {
                    self.complete();
                }
                continue;
            }
            let target = if self.header.len() >= 8 && self.header[..4] == [0, 0, 0, 1] {
                16
            } else {
                8
            };
            let n = (target - self.header.len()).min(data.len());
            self.header.extend_from_slice(&data[..n]);
            data = &data[n..];
            if self.header.len() < target {
                continue;
            }
            if self.header.starts_with(&[0xff, 0x0a]) {
                self.disabled = true;
                break;
            }
            let short = u32::from_be_bytes(self.header[..4].try_into().unwrap());
            if short == 1 && target == 8 {
                continue;
            }
            let size = if short == 1 {
                u64::from_be_bytes(self.header[8..16].try_into().unwrap())
            } else {
                short as u64
            };
            self.kind.copy_from_slice(&self.header[4..8]);
            let header_size = self.header.len() as u64;
            self.header.clear();
            if size != 0 && size < header_size {
                self.disabled = true;
                break;
            }
            self.open_ended = size == 0;
            self.remaining = if size == 0 {
                u64::MAX
            } else {
                size - header_size
            };
            self.capture = self.exif.is_empty()
                && (self.kind == *b"Exif" || self.kind == *b"brob")
                && (size == 0 || self.remaining <= LIMIT as u64);
            if self.remaining == 0 {
                self.complete();
            }
        }
    }

    fn complete(&mut self) {
        if self.capture {
            if self.kind == *b"Exif" {
                self.exif = std::mem::take(&mut self.payload);
            } else if self.payload.starts_with(b"Exif") {
                let mut decoded = Vec::new();
                let result = brotli_decompressor::Decompressor::new(&self.payload[4..], 4096)
                    .take((LIMIT + 1) as u64)
                    .read_to_end(&mut decoded);
                if result.is_ok() && decoded.len() <= LIMIT {
                    self.exif = decoded;
                }
            }
        }
        self.payload.clear();
        self.capture = false;
    }

    pub fn finish(&mut self) {
        if self.open_ended && self.remaining != 0 && self.capture {
            self.complete();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn compressed_metadata_is_bounded_and_invalid_metadata_is_optional() {
        let mut small = Collector::default();
        let mut payload = b"Exif".to_vec();
        payload.extend_from_slice(&[
            139, 5, 128, 98, 111, 117, 110, 100, 101, 100, 32, 69, 88, 73, 70, 3,
        ]);
        small.feed(&((payload.len() + 8) as u32).to_be_bytes());
        small.feed(b"brob");
        small.feed(&payload);
        assert_eq!(small.exif, b"bounded EXIF");
        let mut large = Collector::default();
        let mut payload = b"Exif".to_vec();
        payload.extend_from_slice(&[
            203, 255, 255, 63, 0, 36, 240, 226, 177, 64, 114, 239, 255, 0, 0, 16, 120, 3,
        ]);
        large.feed(&((payload.len() + 8) as u32).to_be_bytes());
        large.feed(b"brob");
        large.feed(&payload);
        assert!(large.exif.is_empty());
        let mut partial = Collector::default();
        partial.feed(b"\0\0\0\x20Exifshort");
        partial.finish();
        assert!(partial.exif.is_empty());
    }
    #[test]
    fn captures_chunked_and_extended_boxes_with_limits() {
        let mut c = Collector::default();
        for byte in b"\0\0\0\x01Exif\0\0\0\0\0\0\0\x14test" {
            c.feed(&[*byte]);
        }
        assert_eq!(c.exif, b"test");
        let mut c = Collector::default();
        c.feed(&((LIMIT + 9) as u32).to_be_bytes());
        c.feed(b"Exif");
        c.feed(&[0; 1024]);
        assert!(c.payload.is_empty());
    }
}
