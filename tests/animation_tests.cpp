#include "core/animation_decode_session.h"
#include "core/archive_collection_source.h"
#include "core/vips_image_decoder.h"
#include "ui/animation_player.h"
#include <QFileInfo>
#include <QColorSpace>
#include <QSignalSpy>
#include <QSettings>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <archive.h>
#include <archive_entry.h>
#include <vips/vips8>

namespace insidious::ui {
class AnimationTests : public QObject {
    Q_OBJECT
    static core::ImageEntry fixture(const QString &path, int loop = 2)
    {
        auto first = vips::VImage::black(32, 24, vips::VImage::option()->set("bands", 4))
                         .new_from_image({ 255, 0, 0, 128 })
                         .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
        auto frames = first.join(first.new_from_image({ 0, 0, 255, 255 }), VIPS_DIRECTION_VERTICAL);
        frames.set(VIPS_META_PAGE_HEIGHT, 24);
        frames.set("delay", std::vector<int> { 10000, 20000 });
        frames.set("loop", loop);
        frames.write_to_file(path.toUtf8().constData());
        core::ImageEntry entry;
        entry.absolutePath = path;
        entry.displayName = QFileInfo(path).fileName();
        entry.fileSize = QFileInfo(path).size();
        return entry;
    }
    static void deadline(AnimationPlayer &player)
    {
        player.m_timer.stop();
        QMetaObject::invokeMethod(&player.m_timer, "timeout", Qt::DirectConnection);
    }
private slots:
    void initTestCase() { QVERIFY(VIPS_INIT("animation-tests") == 0); }
    void cleanupTestCase() { vips_shutdown(); }
    void sequence_data()
    {
        QTest::addColumn<QString>("extension");
        QTest::addColumn<int>("archiveType");
        for (const auto ext : { "gif", "webp", "jxl" }) {
            for (int archive = 0; archive < 3; ++archive) {
                QTest::newRow(qPrintable(QString::fromLatin1(ext) + QString::number(archive))) << QString::fromLatin1(ext) << archive;
            }
        }
    }
    void sequence()
    {
        QFETCH(QString, extension);
        QFETCH(int, archiveType);
        QTemporaryDir dir;
        auto entry = fixture(dir.filePath("sample." + extension));
        if (archiveType) {
            QFile input(entry.absolutePath);
            QVERIFY(input.open(QIODevice::ReadOnly));
            auto bytes = input.readAll();
            const auto path = dir.filePath(archiveType == 1 ? "sample.zip" : "sample.7z");
            auto *writer = archive_write_new();
            if (archiveType == 1) { archive_write_set_format_zip(writer); }
            else { archive_write_set_format_7zip(writer); }
            QCOMPARE(archive_write_open_filename(writer, path.toUtf8().constData()), ARCHIVE_OK);
            auto *header = archive_entry_new();
            archive_entry_set_pathname(header, entry.displayName.toUtf8().constData());
            archive_entry_set_filetype(header, AE_IFREG);
            archive_entry_set_perm(header, 0644);
            archive_entry_set_size(header, bytes.size());
            QCOMPARE(archive_write_header(writer, header), ARCHIVE_OK);
            QCOMPARE(archive_write_data(writer, bytes.constData(), bytes.size()), bytes.size());
            archive_entry_free(header);
            archive_write_free(writer);
            entry.source = core::ImageEntrySource::Archive;
            entry.archivePath = path;
            entry.archiveEntryPath = entry.displayName;
        }
        core::AnimationDecodeSession session;
        QVERIFY2(session.open(entry), qPrintable(session.errorMessage()));
        QCOMPARE(session.info().canvasSize, QSize(32, 24));
        QCOMPARE(session.info().delaysMs, QVector<int>({10000, 20000}));
        QCOMPARE(session.info().totalPlays, 2);
        core::DecodeRequest request;
        request.viewportSize = QSize(32, 24);
        for (int loop = 0; loop < 3; ++loop) {
            if (loop) { QVERIFY(session.restart()); QVERIFY(session.advance()); }
            const auto red = session.render(request);
            QVERIFY2(red.ok(), qPrintable(red.errorMessage));
            QVERIFY(red.image.pixelColor(10, 10).red() > 245);
            QVERIFY(session.advance());
            const auto blue = session.render(request);
            QVERIFY2(blue.ok(), qPrintable(blue.errorMessage));
            QVERIFY(blue.image.pixelColor(10, 10).blue() > 245);
            // A prepared next frame must not destroy the displayed native frame.
            const auto previous = session.render(request, blue.image.sizeInBytes(), 0);
            QVERIFY(previous.image.pixelColor(10, 10).red() > 245);
            request.rotationDegrees = 90;
            const auto rotated = session.render(request);
            QVERIFY(rotated.ok());
            QCOMPARE(rotated.fullDisplaySize, QSize(18, 24));
            request.rotationDegrees = 0;
            const auto limited = session.render(request, core::AnimationDecodeSession::RasterByteLimit);
            QCOMPARE(limited.errorReason, core::DecodeErrorReason::RenderTooLarge);
        }
    }
    void highZoom()
    {
        QTemporaryDir dir;
        const auto entry = fixture(dir.filePath("sample.webp"));
        core::AnimationDecodeSession session;
        QVERIFY(session.open(entry));
        core::DecodeRequest request;
        request.viewportSize = QSize(100, 80);
        request.fitToWindow = false;
        request.scaleFactor = 256.0;
        request.rotationDegrees = 90;
        request.regionRendering = false;
        const auto first = session.render(request);
        QVERIFY2(first.ok(), qPrintable(first.errorMessage));
        QVERIFY(first.regionBased);
        QVERIFY(first.image.sizeInBytes() < 4 * 1024 * 1024);
        QVERIFY(session.advance());
        request.panOffset = QPointF(256, -256);
        const auto next = session.render(request, first.image.sizeInBytes());
        QVERIFY2(next.ok(), qPrintable(next.errorMessage));
        QVERIFY(next.sourceRegion != first.sourceRegion);
        QVERIFY(next.image.pixelColor(0, 0).blue() > 245);
        request.maxDecodedBytes = 1;
        QCOMPARE(session.render(request).errorReason, core::DecodeErrorReason::RenderTooLarge);
    }
    void playback()
    {
        QTemporaryDir dir;
        auto entry = fixture(dir.filePath("sample.gif"));
        core::DecodeRequest request;
        request.viewportSize = QSize(32, 24);
        AnimationPlayer player;
        QSignalSpy frames(&player, &AnimationPlayer::frameReady);
        player.open(entry, request, {});
        QTRY_VERIFY(player.available() && player.m_next.has_value());
        QCOMPARE(player.frameIndex(), 0);
        player.toggle();
        QVERIFY(!player.playing());
        const int remaining = player.m_remaining;
        player.setSuspended(true);
        player.setSuspended(false);
        QVERIFY(!player.m_timer.isActive());
        QCOMPARE(player.m_remaining, remaining);
        player.toggle();
        QVERIFY(player.m_timer.isActive());
        deadline(player);
        QCOMPARE(player.frameIndex(), 1);
        QTRY_VERIFY(player.m_next.has_value());
        // Rerender the last frame after the next loop has already been prepared.
        request.rotationDegrees = 90;
        player.setView(request);
        QTRY_VERIFY(!player.m_busy && !player.m_renderPending && player.m_next.has_value());
        QVERIFY(qvariant_cast<core::DecodedImage>(frames.last().at(0)).image.pixelColor(8, 8).blue() > 245);
        deadline(player);
        QCOMPARE(player.frameIndex(), 0);
        QTRY_VERIFY(player.m_next.has_value());
        deadline(player);
        QCOMPARE(player.frameIndex(), 1);
        deadline(player);
        QVERIFY(player.m_finished);
        player.toggle();
        QTRY_VERIFY(!player.m_busy && player.frameIndex() == 0);
        QVERIFY(player.playing());
        player.stop();
        QVERIFY(!player.available());
        QTRY_VERIFY(!player.m_busy);
    }
    void staticDiscoveryReleasesSession()
    {
        QTemporaryDir dir;
        for (const auto suffix : {"gif", "webp", "jxl"}) {
            core::ImageEntry entry;
            entry.absolutePath = dir.filePath(QStringLiteral("still.") + QString::fromLatin1(suffix));
            vips::VImage::black(32, 24).write_to_file(entry.absolutePath.toUtf8().constData());
            AnimationPlayer player;
            core::DecodeRequest request;
            request.viewportSize = QSize(32, 24);
            player.open(entry, request, {});
            QTRY_VERIFY(!player.m_busy && !player.m_openPending);
            bool released = false;
            QSemaphore done;
            QMetaObject::invokeMethod(player.m_worker, [&] { released = !player.m_worker->session; done.release(); });
            QVERIFY(done.tryAcquire(1, 5000));
            QVERIFY(released);
            QVERIFY(!player.available());
        }
    }
    void replaySinglePlay()
    {
        QTemporaryDir dir;
        const auto entry = fixture(dir.filePath("one.gif"), 1);
        core::DecodeRequest request;
        request.viewportSize = QSize(32, 24);
        AnimationPlayer player;
        player.open(entry, request, {});
        QTRY_VERIFY(player.m_next.has_value());
        deadline(player);
        QCOMPARE(player.frameIndex(), 1);
        deadline(player);
        QVERIFY(player.m_finished);
        QSemaphore entered, release;
        QMetaObject::invokeMethod(player.m_worker, [&] { entered.release(); release.acquire(); });
        {
            const auto unblock = qScopeGuard([&] { release.release(); });
            QVERIFY(entered.tryAcquire(1, 5000));
            player.toggle();
            QVERIFY(player.m_restarting);
            QVERIFY(!player.m_timer.isActive());
            deadline(player); // A queued stale timeout must not finish restart.
            QVERIFY(!player.m_finished);
        }
        QTRY_VERIFY(!player.m_restarting && player.m_next.has_value());
        QVERIFY(player.playing());
        QCOMPARE(player.frameIndex(), 0);
        QVERIFY(player.m_timer.isActive());
        deadline(player);
        QCOMPARE(player.frameIndex(), 1);
        deadline(player);
        QVERIFY(player.m_finished);
    }
    void compositing_data()
    {
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QVector<QColor>>("expected");
        // Pillow-encoded partial frames; explicit canvas colors use transparent disposal backgrounds.
        QTest::newRow("gif") << QStringLiteral("gif") << QByteArray::fromBase64("R0lGODlhCAAEAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQEBwAAACwAAAAACAAEAAAIDAABCBxIsKDBgwQDAgAh+QQJCwAAACwAAAAACAAEAIEAAAAA/wAAAAAAAAAIEgADCAwAoCCAgQQNIjR4cCDDgAAh+QQFCQAAACwAAAAACAAEAIEAAAAAAP8AAAAAAAAIEgABCAQQoGCAgQQNIjR4cCDDgAA7")
            << QVector<QColor> { QColor(255, 0, 0, 255), QColor(255, 0, 0, 255), QColor(0, 255, 0, 255), QColor(255, 0, 0, 255), QColor(0, 0, 0, 0), QColor(0, 0, 255, 255) };
        QTest::newRow("webp") << QStringLiteral("webp") << QByteArray::fromBase64("UklGRrQAAABXRUJQVlA4WAoAAAACAAAABwAAAwAAQU5JTQYAAAAAAAAAAABBTk1GKAAAAAAAAAAAAAcAAAMAAEYAAANWUDhMDwAAAC8HwAAABxD9j/4HIqL/AQBBTk1GKAAAAAAAAAAAAAMAAAMAAG4AAAFWUDhMDwAAAC8DwAAAB9D/iP4HIqL/AQBBTk1GKAAAAAIAAAAAAAMAAAMAAFoAAABWUDhMDwAAAC8DwAAABxDR//4HIqL/AQA=")
            << QVector<QColor> { QColor(255, 0, 0, 255), QColor(255, 0, 0, 255), QColor(0, 255, 0, 255), QColor(0, 0, 0, 0), QColor(0, 0, 0, 0), QColor(0, 0, 255, 255) };
    }
    void compositing()
    {
        QFETCH(QString, extension);
        QFETCH(QByteArray, bytes);
        QFETCH(QVector<QColor>, expected);
        QTemporaryDir dir;
        core::ImageEntry entry;
        entry.absolutePath = dir.filePath("partial." + extension);
        QFile file(entry.absolutePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        core::AnimationDecodeSession session;
        QVERIFY2(session.open(entry), qPrintable(session.errorMessage()));
        core::DecodeRequest request;
        request.viewportSize = QSize(8, 4);
        for (int i = 0; i < 3; ++i) {
            if (i) { QVERIFY(session.advance()); }
            const auto frame = session.render(request);
            QVERIFY(frame.ok());
            QCOMPARE(frame.image.pixelColor(1, 1), expected[2 * i]);
            QCOMPARE(frame.image.pixelColor(6, 1), expected[2 * i + 1]);
        }
    }
    void limitsAndStatic()
    {
        QTemporaryDir dir;
        core::ImageEntry entry;
        entry.absolutePath = dir.filePath("huge.gif");
        QFile file(entry.absolutePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(core::AnimationDecodeSession::SourceByteLimit + 1));
        file.close();
        core::AnimationDecodeSession session;
        QVERIFY(!session.open(entry));
        QVERIFY(!session.errorMessage().isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("broken");
        file.close();
        QVERIFY(!session.open(entry));
        auto image = vips::VImage::black(32, 24);
        image.write_to_file(entry.absolutePath.toUtf8().constData());
        QVERIFY(session.open(entry));
        QVERIFY(!session.info().animated());
    }
    void repeatedLoopsReleasePixels()
    {
        QTemporaryDir dir;
        const auto entry = fixture(dir.filePath("sample.jxl"), 0);
        core::AnimationDecodeSession session;
        QVERIFY(session.open(entry));
        core::DecodeRequest request;
        request.viewportSize = QSize(32, 24);
        qint64 warmMemory = 0;
        for (int loop = 0; loop < 100; ++loop) {
            QVERIFY(session.restart());
            QVERIFY(session.advance());
            QVERIFY(session.render(request).ok());
            QVERIFY(session.advance());
            QVERIFY(session.render(request).ok());
            if (loop == 10) { warmMemory = vips_tracked_get_mem(); }
        }
        QVERIFY(vips_tracked_get_mem() <= warmMemory + 2 * 1024 * 1024);
    }
    void nativeJxl()
    {
        QTemporaryDir dir;
        for (int bands : {1, 2, 3, 4}) {
            for (bool highDepth : {false, true}) {
                const bool gray = bands <= 2;
                std::vector<double> values = gray ? std::vector<double>{32768} : std::vector<double>{32768, 16384, 0};
                if (bands == 2 || bands == 4) { values.push_back(32768); }
                auto first = vips::VImage::black(32, 24, vips::VImage::option()->set("bands", bands))
                    .cast(VIPS_FORMAT_USHORT).new_from_image(values)
                    .copy(vips::VImage::option()->set("interpretation", gray ? VIPS_INTERPRETATION_GREY16 : VIPS_INTERPRETATION_RGB16));
                if (!highDepth) { first = first.colourspace(gray ? VIPS_INTERPRETATION_B_W : VIPS_INTERPRETATION_sRGB); }
                if (!gray) {
                    const auto profile = QColorSpace(QColorSpace::SRgb).iccProfile();
                    vips_image_set_blob_copy(first.get_image(), VIPS_META_ICC_NAME, profile.constData(), profile.size());
                }
                auto frames = first.join(first, VIPS_DIRECTION_VERTICAL);
                frames.set(VIPS_META_PAGE_HEIGHT, 24);
                frames.set("delay", std::vector<int>{80, 120});
                core::ImageEntry entry;
                entry.absolutePath = dir.filePath(QString::number(bands) + QString::number(highDepth) + ".jxl");
                frames.write_to_file(entry.absolutePath.toUtf8().constData(), vips::VImage::option()->set("lossless", true)->set("effort", 1)->set("bitdepth", highDepth ? 16 : 8));
                core::AnimationDecodeSession session;
                QVERIFY2(session.open(entry), qPrintable(session.errorMessage()));
                for (bool cm : {false, true}) {
                    core::DecodeRequest request;
                    request.viewportSize = QSize(12, 16);
                    request.colorManagementEnabled = cm;
                    request.rotationDegrees = 90;
                    request.fitToWindow = false;
                    request.regionRendering = true;
                    const auto image = session.render(request);
                    QVERIFY2(image.ok(), qPrintable(image.errorMessage));
                    QVERIFY(image.regionBased);
                    const auto pixel = image.image.pixelColor(4, 4);
                    QVERIFY(std::abs(pixel.red() - 128) <= 2);
                    QVERIFY(std::abs(pixel.green() - (gray ? 128 : 64)) <= 2);
                    QVERIFY(std::abs(pixel.alpha() - ((bands == 2 || bands == 4) ? 128 : 255)) <= 2);
                }
            }
        }
    }
    void jxlMultipageDoesNotStartPlayback()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("pages.jxl");
        auto first = vips::VImage::black(32, 24);
        auto pages = first.join(first.new_from_image({255}), VIPS_DIRECTION_VERTICAL);
        pages.set(VIPS_META_PAGE_HEIGHT, 24);
        pages.write_to_file(path.toUtf8().constData());
        core::ImageEntry entry;
        entry.absolutePath = path;
        entry.fileSize = QFileInfo(path).size();
        core::AnimationDecodeSession session;
        QVERIFY2(session.open(entry), qPrintable(session.errorMessage()));
        QVERIFY(!session.info().animated());
        core::DecodeRequest request;
        request.viewportSize = QSize(32, 24);
        const auto poster = core::VipsImageDecoder().decode(entry, request);
        QVERIFY2(poster.ok(), qPrintable(poster.errorMessage));
        QVERIFY(!poster.animated);
    }
    void existingSpaceShortcut()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("shortcuts/nextImage/customized", true);
        settings.setValue("shortcuts/nextImage/sequences", QStringList { "Space" });
        auto loaded = core::AppSettings::read(settings);
        const auto id = core::shortcutCommandId(core::ShortcutCommand::ToggleAnimationPlayback);
        QVERIFY(loaded.shortcuts.bindings.value(id).sequences.isEmpty());
        loaded.write(settings);
        QVERIFY(core::AppSettings::read(settings).shortcuts.bindings.value(id).sequences.isEmpty());
    }
    void staleSource()
    {
        QTemporaryDir dir;
        auto entry = fixture(dir.filePath("sample.jxl"));
        core::DecodeRequest request;
        request.viewportSize = QSize(32, 24);
        AnimationPlayer player;
        QSignalSpy frames(&player, &AnimationPlayer::frameReady);
        for (int i = 0; i < 20; ++i) { player.open(entry, request, {}); player.stop(); }
        QTRY_VERIFY(!player.m_busy);
        QCOMPARE(frames.size(), 0);
        player.open(entry, request, {});
        QTRY_VERIFY(player.available());
        player.setSuspended(true);
        QTRY_VERIFY(!player.m_busy);
        QVERIFY(!player.m_timer.isActive());
    }
};
}
Q_DECLARE_METATYPE(insidious::core::DecodedImage)
QTEST_MAIN(insidious::ui::AnimationTests)
#include "animation_tests.moc"
