#pragma once

#include "core/viewer_controller.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <Qt>
#include <QVector>

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

enum class ClickZoomBehavior {
    ToggleOnClick,
    HoldToZoomPan,
};

enum class ShortcutCommand {
    OpenFile,
    OpenFolder,
    PreviousImage,
    NextImage,
    PreviousFolder,
    NextFolder,
    FirstImage,
    LastImage,
    QuickJump,
    Preferences,
    ZoomIn,
    ZoomOut,
    FitToWindow,
    ActualSize,
    RotateLeft,
    RotateRight,
    ExifAutorotate,
    Fullscreen,
};

enum class SettingsCategory {
    Viewer,
    Fullscreen,
    Interface,
    Shortcuts,
};

struct ViewerSettings {
    ZoomMode defaultZoomMode = ZoomMode::FitToWindow;
    ImageSwitchPolicy imageSwitchPolicy = ImageSwitchPolicy::ResetToBaseMode;
    ClickZoomBehavior clickZoomBehavior = ClickZoomBehavior::ToggleOnClick;
    bool exifAutorotate = true;
    double clickZoomMultiplier = 2.0;
    WheelAction wheelAction = WheelAction::NavigateImages;
    WheelAction ctrlWheelAction = WheelAction::Zoom;
    WheelAction shiftWheelAction = WheelAction::NoAction;
    double zoomStepPercent = 25.0;
};

struct FullscreenSettings {
    double sideTriggerRatio = 0.07;
    double verticalTriggerRatio = 0.05;
    bool infoLineEnabled = true;
    QString infoLineFontFamily = QStringLiteral("DejaVu Sans");
    int infoLineFontPointSize = 10;
    QString infoLineColor = QStringLiteral("#00ff00");
};

struct InterfaceSettings {
    bool rememberLayout = true;
    QByteArray mainWindowGeometry;
    QByteArray mainWindowState;
    QByteArray fileSplitterState;
    QByteArray fullscreenFileSplitterState;
};

struct ShortcutBinding {
    bool customized = false;
    QStringList sequences;
};

struct MouseShortcut {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    Qt::MouseButton button = Qt::NoButton;
};

struct ShortcutSettings {
    QHash<QString, ShortcutBinding> bindings;
};

struct AppSettings {
    ViewerSettings viewer;
    FullscreenSettings fullscreen;
    InterfaceSettings interface;
    ShortcutSettings shortcuts;

    [[nodiscard]] static AppSettings defaults();
    [[nodiscard]] static AppSettings read(QSettings &settings);

    void write(QSettings &settings) const;
    void resetCategory(SettingsCategory category);
};

[[nodiscard]] QString zoomModeToSettingsString(ZoomMode mode);
[[nodiscard]] ZoomMode zoomModeFromSettingsString(const QString &value, ZoomMode fallback);
[[nodiscard]] QString imageSwitchPolicyToSettingsString(ImageSwitchPolicy policy);
[[nodiscard]] ImageSwitchPolicy imageSwitchPolicyFromSettingsString(const QString &value, ImageSwitchPolicy fallback);
[[nodiscard]] QString clickZoomBehaviorToSettingsString(ClickZoomBehavior behavior);
[[nodiscard]] ClickZoomBehavior clickZoomBehaviorFromSettingsString(const QString &value, ClickZoomBehavior fallback);
[[nodiscard]] QString wheelActionToSettingsString(WheelAction action);
[[nodiscard]] WheelAction wheelActionFromSettingsString(const QString &value, WheelAction fallback);
[[nodiscard]] QVector<ShortcutCommand> shortcutCommands();
[[nodiscard]] QString shortcutCommandId(ShortcutCommand command);
[[nodiscard]] QStringList defaultShortcutSequences(ShortcutCommand command);
[[nodiscard]] ShortcutSettings defaultShortcutSettings();
[[nodiscard]] QStringList shortcutSequences(const ShortcutSettings &settings, ShortcutCommand command);
[[nodiscard]] bool shortcutSequenceIsMouseInput(const QString &value);
[[nodiscard]] bool mouseShortcutFromSettingsString(const QString &value, MouseShortcut *shortcut);
[[nodiscard]] QString mouseShortcutToSettingsString(const MouseShortcut &shortcut);
[[nodiscard]] QString shortcutSequenceDisplayString(const QString &value);
void setShortcutSequences(ShortcutSettings *settings, ShortcutCommand command, const QStringList &sequences);
void resetShortcutSequences(ShortcutSettings *settings, ShortcutCommand command);

} // namespace insidious::core
