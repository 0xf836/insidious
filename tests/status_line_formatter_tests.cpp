#include "ui/status_line_formatter.h"

#include <QDateTime>
#include <QLocale>
#include <QTimeZone>
#include <QtTest>

using namespace insidious;

class StatusLineFormatterTests : public QObject {
    Q_OBJECT

private slots:
    void formatsDecodedStatusLine();
    void formatsFitScaleAsPercentage();
    void formatsFullscreenInfoLine();
    void escapesFullscreenInfoLineName();
    void prefersDateTakenOverGenericExifDate();
    void marksMissingExifDateAsUnavailable();
    void messageBypassesFieldFormatting();
};

namespace {

core::ImageEntry sampleEntry()
{
    core::ImageEntry entry;
    entry.displayName = QStringLiteral("image & one.jpg");
    entry.sourceRelativePath = QStringLiteral("folder/image & one.jpg");
    entry.absolutePath = QStringLiteral("/tmp/folder/image & one.jpg");
    entry.fileSize = 1536;
    entry.modifiedTime = QDateTime(QDate(2026, 6, 29), QTime(12, 34));
    entry.formatHint = QStringLiteral("JPEG");
    return entry;
}

core::DecodedImage sampleDecoded()
{
    core::DecodedImage decoded;
    decoded.image = QImage(20, 10, QImage::Format_RGB32);
    decoded.sourceSize = QSize(4000, 3000);
    decoded.displaySize = QSize(1000, 750);
    decoded.effectiveScale = 0.25;
    return decoded;
}

ui::StatusLineContext sampleContext(const core::ImageEntry &entry, const core::DecodedImage &decoded)
{
    ui::StatusLineContext context;
    context.entry = &entry;
    context.decoded = &decoded;
    context.zoomMode = core::ZoomMode::Custom;
    context.zoomFactor = 0.42;
    context.currentIndex = 4;
    context.totalCount = 12;
    return context;
}

ui::StatusLineContext sampleContext(const core::ImageEntry &entry, const core::DecodedImage &decoded, core::ZoomMode zoomMode, double zoomFactor)
{
    ui::StatusLineContext context = sampleContext(entry, decoded);
    context.zoomMode = zoomMode;
    context.zoomFactor = zoomFactor;
    return context;
}

} // namespace

void StatusLineFormatterTests::formatsDecodedStatusLine()
{
    const core::ImageEntry entry = sampleEntry();
    core::DecodedImage decoded = sampleDecoded();
    decoded.metadataFields.append({ QStringLiteral("Date taken"), QStringLiteral("2024:05:04 10:20:30+03:00 some extra metadata") });

    const QString line = ui::formatStatusLine(sampleContext(entry, decoded));

    QVERIFY(line.contains(QStringLiteral("image:")));
    QVERIFY(line.contains(QStringLiteral("4000 x 3000 = 12.0 MP")));
    QVERIFY(line.contains(QStringLiteral("size:")));
    QVERIFY(line.contains(QLocale::system().formattedDataSize(entry.fileSize)));
    QVERIFY(line.contains(QStringLiteral("modified:")));
    QVERIFY(line.contains(QStringLiteral("2026.6.29 12:34:00")));
    QVERIFY(line.contains(QStringLiteral("taken:")));
    QVERIFY(line.contains(QStringLiteral("2024.5.4 10:20:30")));
    QVERIFY(!line.contains(QStringLiteral("+03:00")));
    QVERIFY(line.contains(QStringLiteral("name:")));
    QVERIFY(line.contains(QStringLiteral("folder/image &amp; one.jpg")));
    QVERIFY(line.contains(QStringLiteral("zoom:")));
    QVERIFY(line.contains(QStringLiteral("42%")));
    QVERIFY(line.contains(QStringLiteral("pos:")));
    QVERIFY(line.contains(QStringLiteral("5 / 12")));
    QVERIFY(line.contains(QStringLiteral("|")));
}

void StatusLineFormatterTests::formatsFitScaleAsPercentage()
{
    const core::ImageEntry entry = sampleEntry();
    core::DecodedImage decoded = sampleDecoded();
    decoded.effectiveScale = 0.25;

    const QString line = ui::formatStatusLine(sampleContext(entry, decoded, core::ZoomMode::FitToWindow, 0.42));

    QVERIFY(line.contains(QStringLiteral("25%")));
    QVERIFY(!line.contains(QStringLiteral("fit")));
}

void StatusLineFormatterTests::formatsFullscreenInfoLine()
{
    const core::ImageEntry entry = sampleEntry();
    core::DecodedImage decoded = sampleDecoded();
    decoded.effectiveScale = 0.352;

    const QString line = ui::formatFullscreenInfoLine(sampleContext(entry, decoded, core::ZoomMode::FitToWindow, 0.42));

    QVERIFY(line.contains(QStringLiteral("folder/image &amp; one.jpg")));
    QVERIFY(line.contains(QStringLiteral("(4000 x 3000 |")));
    QVERIFY(line.contains(QLocale::system().formattedDataSize(entry.fileSize)));
    QVERIFY(line.contains(QStringLiteral("[5 / 12]")));
    QVERIFY(line.contains(QLocale::system().toString(35.2, 'f', 1) + QStringLiteral("%")));
    QVERIFY(!line.contains(QStringLiteral("image:")));
}

void StatusLineFormatterTests::escapesFullscreenInfoLineName()
{
    core::ImageEntry entry = sampleEntry();
    entry.sourceRelativePath = QStringLiteral("folder/<script>.jpg");
    const core::DecodedImage decoded = sampleDecoded();

    const QString line = ui::formatFullscreenInfoLine(sampleContext(entry, decoded));

    QVERIFY(line.contains(QStringLiteral("folder/&lt;script&gt;.jpg")));
    QVERIFY(!line.contains(QStringLiteral("folder/<script>.jpg")));
}

void StatusLineFormatterTests::prefersDateTakenOverGenericExifDate()
{
    const core::ImageEntry entry = sampleEntry();
    core::DecodedImage decoded = sampleDecoded();
    decoded.metadataFields.append({ QStringLiteral("Date"), QStringLiteral("generic-date") });
    decoded.metadataFields.append({ QStringLiteral("Date taken"), QStringLiteral("taken-date") });

    const QString line = ui::formatStatusLine(sampleContext(entry, decoded));

    QVERIFY(line.contains(QStringLiteral("taken-date")));
    QVERIFY(!line.contains(QStringLiteral("generic-date")));
}

void StatusLineFormatterTests::marksMissingExifDateAsUnavailable()
{
    const core::ImageEntry entry = sampleEntry();
    const core::DecodedImage decoded = sampleDecoded();

    const QString line = ui::formatStatusLine(sampleContext(entry, decoded));

    QVERIFY(line.contains(QStringLiteral("taken:")));
    QVERIFY(line.contains(QStringLiteral("N/A")));
}

void StatusLineFormatterTests::messageBypassesFieldFormatting()
{
    ui::StatusLineContext context;
    context.message = QStringLiteral("Loading <image>...");

    const QString line = ui::formatStatusLine(context);

    QCOMPARE(line, QStringLiteral("Loading &lt;image&gt;..."));
}

QTEST_MAIN(StatusLineFormatterTests)

#include "status_line_formatter_tests.moc"
