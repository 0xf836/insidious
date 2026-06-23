#include "core/app_settings.h"

#include <QSettings>
#include <QVariant>

#include <algorithm>

namespace insidious::core {
namespace {

constexpr int CurrentSettingsSchemaVersion = 1;
constexpr double MinClickZoomMultiplier = 1.1;
constexpr double MaxClickZoomMultiplier = 8.0;
constexpr double MinFullscreenTriggerRatio = 0.01;
constexpr double MaxFullscreenTriggerRatio = 0.30;

double clampedDouble(const QVariant &value, double fallback, double minimum, double maximum)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

} // namespace

AppSettings AppSettings::defaults()
{
    return {};
}

AppSettings AppSettings::read(QSettings &settings)
{
    const AppSettings defaults = AppSettings::defaults();
    AppSettings result = defaults;

    settings.beginGroup(QStringLiteral("viewer"));
    result.viewer.defaultZoomMode = zoomModeFromSettingsString(
        settings.value(QStringLiteral("defaultZoomMode"), zoomModeToSettingsString(defaults.viewer.defaultZoomMode)).toString(),
        defaults.viewer.defaultZoomMode);
    result.viewer.imageSwitchPolicy = imageSwitchPolicyFromSettingsString(
        settings.value(QStringLiteral("imageSwitchPolicy"), imageSwitchPolicyToSettingsString(defaults.viewer.imageSwitchPolicy)).toString(),
        defaults.viewer.imageSwitchPolicy);
    result.viewer.clickZoomMultiplier = clampedDouble(
        settings.value(QStringLiteral("clickZoomMultiplier"), defaults.viewer.clickZoomMultiplier),
        defaults.viewer.clickZoomMultiplier,
        MinClickZoomMultiplier,
        MaxClickZoomMultiplier);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("fullscreen"));
    result.fullscreen.sideTriggerRatio = clampedDouble(
        settings.value(QStringLiteral("sideTriggerRatio"), defaults.fullscreen.sideTriggerRatio),
        defaults.fullscreen.sideTriggerRatio,
        MinFullscreenTriggerRatio,
        MaxFullscreenTriggerRatio);
    result.fullscreen.verticalTriggerRatio = clampedDouble(
        settings.value(QStringLiteral("verticalTriggerRatio"), defaults.fullscreen.verticalTriggerRatio),
        defaults.fullscreen.verticalTriggerRatio,
        MinFullscreenTriggerRatio,
        MaxFullscreenTriggerRatio);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("interface"));
    result.interface.rememberLayout = settings.value(QStringLiteral("rememberLayout"), defaults.interface.rememberLayout).toBool();
    result.interface.mainWindowGeometry = settings.value(QStringLiteral("mainWindowGeometry")).toByteArray();
    result.interface.mainWindowState = settings.value(QStringLiteral("mainWindowState")).toByteArray();
    result.interface.fileSplitterState = settings.value(QStringLiteral("fileSplitterState")).toByteArray();
    result.interface.fullscreenFileSplitterState = settings.value(QStringLiteral("fullscreenFileSplitterState")).toByteArray();
    settings.endGroup();

    return result;
}

void AppSettings::write(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("settings"));
    settings.setValue(QStringLiteral("schemaVersion"), CurrentSettingsSchemaVersion);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("viewer"));
    settings.setValue(QStringLiteral("defaultZoomMode"), zoomModeToSettingsString(viewer.defaultZoomMode));
    settings.setValue(QStringLiteral("imageSwitchPolicy"), imageSwitchPolicyToSettingsString(viewer.imageSwitchPolicy));
    settings.setValue(QStringLiteral("clickZoomMultiplier"), std::clamp(viewer.clickZoomMultiplier, MinClickZoomMultiplier, MaxClickZoomMultiplier));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("fullscreen"));
    settings.setValue(QStringLiteral("sideTriggerRatio"), std::clamp(fullscreen.sideTriggerRatio, MinFullscreenTriggerRatio, MaxFullscreenTriggerRatio));
    settings.setValue(QStringLiteral("verticalTriggerRatio"), std::clamp(fullscreen.verticalTriggerRatio, MinFullscreenTriggerRatio, MaxFullscreenTriggerRatio));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("interface"));
    settings.setValue(QStringLiteral("rememberLayout"), interface.rememberLayout);
    if (interface.rememberLayout) {
        settings.setValue(QStringLiteral("mainWindowGeometry"), interface.mainWindowGeometry);
        settings.setValue(QStringLiteral("mainWindowState"), interface.mainWindowState);
        settings.setValue(QStringLiteral("fileSplitterState"), interface.fileSplitterState);
        settings.setValue(QStringLiteral("fullscreenFileSplitterState"), interface.fullscreenFileSplitterState);
    } else {
        settings.remove(QStringLiteral("mainWindowGeometry"));
        settings.remove(QStringLiteral("mainWindowState"));
        settings.remove(QStringLiteral("fileSplitterState"));
        settings.remove(QStringLiteral("fullscreenFileSplitterState"));
    }
    settings.endGroup();
}

void AppSettings::resetCategory(SettingsCategory category)
{
    const AppSettings defaults = AppSettings::defaults();
    switch (category) {
    case SettingsCategory::Viewer:
        viewer = defaults.viewer;
        break;
    case SettingsCategory::Fullscreen:
        fullscreen = defaults.fullscreen;
        break;
    case SettingsCategory::Interface:
        interface = defaults.interface;
        break;
    }
}

QString zoomModeToSettingsString(ZoomMode mode)
{
    switch (mode) {
    case ZoomMode::FitToWindow:
        return QStringLiteral("fit");
    case ZoomMode::ActualSize:
        return QStringLiteral("actual");
    case ZoomMode::Custom:
        return QStringLiteral("fit");
    }
    return QStringLiteral("fit");
}

ZoomMode zoomModeFromSettingsString(const QString &value, ZoomMode fallback)
{
    const QString normalized = value.toCaseFolded();
    if (normalized == QLatin1String("fit")) {
        return ZoomMode::FitToWindow;
    }
    if (normalized == QLatin1String("actual")) {
        return ZoomMode::ActualSize;
    }
    return fallback;
}

QString imageSwitchPolicyToSettingsString(ImageSwitchPolicy policy)
{
    switch (policy) {
    case ImageSwitchPolicy::ResetToBaseMode:
        return QStringLiteral("reset");
    case ImageSwitchPolicy::PreserveView:
        return QStringLiteral("preserve");
    }
    return QStringLiteral("reset");
}

ImageSwitchPolicy imageSwitchPolicyFromSettingsString(const QString &value, ImageSwitchPolicy fallback)
{
    const QString normalized = value.toCaseFolded();
    if (normalized == QLatin1String("reset")) {
        return ImageSwitchPolicy::ResetToBaseMode;
    }
    if (normalized == QLatin1String("preserve")) {
        return ImageSwitchPolicy::PreserveView;
    }
    return fallback;
}

} // namespace insidious::core
