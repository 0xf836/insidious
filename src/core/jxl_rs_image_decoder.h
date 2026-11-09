#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"

namespace insidious::core {

[[nodiscard]] DecodedImage decodeJxlRs(const ImageEntry &entry, const DecodeRequest &request,
    const QString &archivePassword);

} // namespace insidious::core
