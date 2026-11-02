#pragma once

#include "core/decoded_image.h"
#include <vips/vips8>

namespace insidious::core {
// Internal shared renderer; callers retain ownership of the input pixels.
[[nodiscard]] DecodedImage renderVipsImage(vips::VImage image, const DecodeRequest &request);
}
