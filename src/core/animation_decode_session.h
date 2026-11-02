#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"
#include <memory>

namespace insidious::core {
struct AnimationInfo {
    QSize canvasSize;
    QVector<int> delaysMs;
    int totalPlays = 1; // Zero means infinite.
    [[nodiscard]] bool animated() const { return delaysMs.size() > 1; }
};

class AnimationDecodeSession {
public:
    static constexpr qint64 SourceByteLimit = 256LL * 1024 * 1024;
    static constexpr qint64 RasterByteLimit = 384LL * 1024 * 1024;
    AnimationDecodeSession();
    ~AnimationDecodeSession();
    AnimationDecodeSession(const AnimationDecodeSession &) = delete;
    AnimationDecodeSession &operator=(const AnimationDecodeSession &) = delete;
    [[nodiscard]] bool open(const ImageEntry &entry, const QString &password = {});
    [[nodiscard]] const AnimationInfo &info() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] bool advance();
    [[nodiscard]] bool restart();
    [[nodiscard]] int frameIndex() const;
    [[nodiscard]] DecodedImage render(const DecodeRequest &request, qint64 retainedDisplayBytes = 0, int frameIndex = -1) const;
    [[nodiscard]] static bool candidate(const ImageEntry &entry);
private:
    struct Data;
    std::unique_ptr<Data> d;
};
}
