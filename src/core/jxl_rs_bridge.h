#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
struct InsidiousJxl;
struct InsidiousJxlInfo {
    int width, height, bands, orientation, bytesPerSample, animated;
    uint32_t totalPlays;
    const unsigned char *icc;
    size_t iccSize;
};
// Source and callback remain alive until close; negative read is a terminal I/O error.
using InsidiousJxlRead = int64_t (*)(void *, unsigned char *, size_t);
// Status: 0 = success, 1 = decode error, 2 = raster/sample limit exceeded.
int insidious_jxl_open(InsidiousJxlRead read, void *context, size_t size, int scanOnly,
    size_t sampleLimit, InsidiousJxl **handle, InsidiousJxlInfo *info, char *error, size_t errorSize);
int insidious_jxl_decode(InsidiousJxl *handle, unsigned char *pixels, size_t size,
    double *durationMs, int *hasMore, char *error, size_t errorSize);
int insidious_jxl_finish(InsidiousJxl *handle, const unsigned char **exif, size_t *size,
    char *error, size_t errorSize);
void insidious_jxl_close(InsidiousJxl *handle);
}
