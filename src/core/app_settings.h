#pragma once

#include "core/viewer_controller.h"

#include <QByteArray>
#include <QString>

class QSettings;

namespace insidious::core {

enum class ImageSwitchPolicy {
    ResetToBaseMode,
    PreserveView,
};

enum class WheelAction {
    NavigateImages,
    Zoom,
    NoAction,
};

enum class SettingsCategory {
    Viewer,
    Fullscreen,
    Interface,
};

struct ViewerSettings {
    ZoomMode defaultZoomMode = ZoomMode::FitToWindow;
    ImageSwitchPolicy imageSwitchPolicy = ImageSwitchPolicy::ResetToBaseMode;
    double clickZoomMultiplier = 2.0;
    WheelAction wheelAction = WheelAction::NavigateImages;
    WheelAction ctrlWheelAction = WheelAction::Zoom;
    WheelAction shiftWheelAction = WheelAction::NoAction;
    double zoomStepPercent = 25.0;
};

struct FullscreenSettings {
    double sideTriggerRatio = 0.07;
    double verticalTriggerRatio = 0.05;
};

struct InterfaceSettings {
    bool rememberLayout = true;
    QByteArray mainWindowGeometry;
    QByteArray mainWindowState;
    QByteArray fileSplitterState;
    QByteArray fullscreenFileSplitterState;
};

struct AppSettings {
    ViewerSettings viewer;
    FullscreenSettings fullscreen;
    InterfaceSettings interface;

    [[nodiscard]] static AppSettings defaults();
    [[nodiscard]] static AppSettings read(QSettings &settings);

    void write(QSettings &settings) const;
    void resetCategory(SettingsCategory category);
};

[[nodiscard]] QString zoomModeToSettingsString(ZoomMode mode);
[[nodiscard]] ZoomMode zoomModeFromSettingsString(const QString &value, ZoomMode fallback);
[[nodiscard]] QString imageSwitchPolicyToSettingsString(ImageSwitchPolicy policy);
[[nodiscard]] ImageSwitchPolicy imageSwitchPolicyFromSettingsString(const QString &value, ImageSwitchPolicy fallback);
[[nodiscard]] QString wheelActionToSettingsString(WheelAction action);
[[nodiscard]] WheelAction wheelActionFromSettingsString(const QString &value, WheelAction fallback);

} // namespace insidious::core
