#pragma once

#include <cstddef>

extern "C" {

struct InsidiousJxl;
struct InsidiousJxlInfo {
    int width;
    int height;
    int bands;
    int orientation;
    const unsigned char *icc;
    size_t iccSize;
};

// Input bytes must remain alive and unchanged until close; ICC is borrowed from the handle.
// Status: 0 = success, 1 = decode error, 2 = raster/sample limit exceeded.
int insidious_jxl_open(const unsigned char *data, size_t size, size_t sampleLimit,
    InsidiousJxl **handle, InsidiousJxlInfo *info, char *error, size_t errorSize);
int insidious_jxl_decode(InsidiousJxl *handle, unsigned char *pixels, size_t size,
    char *error, size_t errorSize);
void insidious_jxl_close(InsidiousJxl *handle);

}
