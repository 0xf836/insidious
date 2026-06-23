#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"

namespace insidious::core {

class VipsImageDecoder {
public:
    [[nodiscard]] DecodedImage decode(
        const ImageEntry &entry,
        const DecodeRequest &request,
        const QString &archivePassword = {}) const;
};

[[nodiscard]] QString cacheKeyFor(const ImageEntry &entry, const DecodeRequest &request);

} // namespace insidious::core
