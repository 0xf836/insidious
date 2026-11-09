#pragma once

#include "core/decoded_image.h"
#include "core/image_entry.h"
#include "core/jxl_rs_bridge.h"
#include <memory>
#include <vips/vips8>

namespace insidious::core {

class JxlRasterDecoder {
public:
    static constexpr qint64 NativeByteLimit = 384LL * 1024 * 1024;
    JxlRasterDecoder();
    ~JxlRasterDecoder();
    JxlRasterDecoder(const JxlRasterDecoder &) = delete;
    JxlRasterDecoder &operator=(const JxlRasterDecoder &) = delete;
    bool open(const ImageEntry &entry, const QString &password, bool scanOnly = false);
    void close();
    bool openBytes(const QByteArray &bytes, bool scanOnly = false);
    bool readFrame(vips::VImage *image, double *durationMs, bool *hasMore,
        qint64 nativeLimit = NativeByteLimit);
    bool finishMetadata(QVector<ImageMetadataField> *fields);
    const InsidiousJxlInfo &info() const;
    QString errorMessage() const;
    DecodeErrorReason errorReason() const;
private:
    struct Data;
    std::unique_ptr<Data> d;
    bool initialize(qint64 sourceSize, bool scanOnly);
};

[[nodiscard]] DecodedImage decodeJxlRs(const ImageEntry &entry, const DecodeRequest &request,
    const QString &archivePassword);
} // namespace insidious::core
