#include "core/app_settings.h"

#include <QColor>
#include <QKeySequence>
#include <QSettings>
#include <QVariant>

#include <algorithm>

namespace insidious::core {
namespace {

constexpr int CurrentSettingsSchemaVersion = 1;
constexpr double MinClickZoomMultiplier = 1.1;
constexpr double MaxClickZoomMultiplier = 8.0;
constexpr double MinZoomStepPercent = 5.0;
constexpr double MaxZoomStepPercent = 100.0;
constexpr double MinFullscreenTriggerRatio = 0.01;
constexpr double MaxFullscreenTriggerRatio = 0.30;
constexpr int MinFullscreenInfoLineFontPointSize = 8;
constexpr int MaxFullscreenInfoLineFontPointSize = 32;

struct MouseButtonName {
    Qt::MouseButton button;
    const char *name;
};

constexpr MouseButtonName MouseButtonNames[] = {
    { Qt::LeftButton, "MouseLeft" },
    { Qt::MiddleButton, "MouseMiddle" },
    { Qt::RightButton, "MouseRight" },
    { Qt::BackButton, "MouseBack" },
    { Qt::ForwardButton, "MouseForward" },
    { Qt::ExtraButton1, "MouseExtra1" },
    { Qt::ExtraButton2, "MouseExtra2" },
};

QString keySequenceToSettingsString(const QKeySequence &sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

double clampedDouble(const QVariant &value, double fallback, double minimum, double maximum)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

int clampedInt(const QVariant &value, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

QString validColorName(const QVariant &value, const QString &fallback)
{
    const QColor color(value.toString());
    if (!color.isValid()) {
        return fallback;
    }
    return color.name(QColor::HexRgb);
}

bool isValidShortcutSequence(const QString &value)
{
    return !value.isEmpty()
        && (shortcutSequenceIsMouseInput(value)
            || !QKeySequence::fromString(value, QKeySequence::PortableText).isEmpty());
}

QStringList validatedShortcutSequences(const QVariant &value, const QStringList &fallback, bool *valid)
{
    QStringList parsed = value.toStringList();
    if (parsed.isEmpty() && value.canConvert<QString>()) {
        const QString singleValue = value.toString();
        if (!singleValue.isEmpty()) {
            parsed = QStringList { singleValue };
        }
    }

    for (const QString &sequence : parsed) {
        if (!isValidShortcutSequence(sequence)) {
            if (valid != nullptr) {
                *valid = false;
            }
            return fallback;
        }
    }

    if (valid != nullptr) {
        *valid = true;
    }
    return parsed;
}

} // namespace

AppSettings AppSettings::defaults()
{
    AppSettings settings;
    settings.shortcuts = defaultShortcutSettings();
    return settings;
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
    result.viewer.clickZoomBehavior = clickZoomBehaviorFromSettingsString(
        settings.value(QStringLiteral("clickZoomBehavior"), clickZoomBehaviorToSettingsString(defaults.viewer.clickZoomBehavior)).toString(),
        defaults.viewer.clickZoomBehavior);
    result.viewer.exifAutorotate = settings.value(QStringLiteral("exifAutorotate"), defaults.viewer.exifAutorotate).toBool();
    result.viewer.clickZoomMultiplier = clampedDouble(
        settings.value(QStringLiteral("clickZoomMultiplier"), defaults.viewer.clickZoomMultiplier),
        defaults.viewer.clickZoomMultiplier,
        MinClickZoomMultiplier,
        MaxClickZoomMultiplier);
    result.viewer.wheelAction = wheelActionFromSettingsString(
        settings.value(QStringLiteral("wheelAction"), wheelActionToSettingsString(defaults.viewer.wheelAction)).toString(),
        defaults.viewer.wheelAction);
    result.viewer.ctrlWheelAction = wheelActionFromSettingsString(
        settings.value(QStringLiteral("ctrlWheelAction"), wheelActionToSettingsString(defaults.viewer.ctrlWheelAction)).toString(),
        defaults.viewer.ctrlWheelAction);
    result.viewer.shiftWheelAction = wheelActionFromSettingsString(
        settings.value(QStringLiteral("shiftWheelAction"), wheelActionToSettingsString(defaults.viewer.shiftWheelAction)).toString(),
        defaults.viewer.shiftWheelAction);
    result.viewer.zoomStepPercent = clampedDouble(
        settings.value(QStringLiteral("zoomStepPercent"), defaults.viewer.zoomStepPercent),
        defaults.viewer.zoomStepPercent,
        MinZoomStepPercent,
        MaxZoomStepPercent);
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
    result.fullscreen.infoLineEnabled = settings.value(QStringLiteral("infoLineEnabled"), defaults.fullscreen.infoLineEnabled).toBool();
    result.fullscreen.infoLineFontFamily = settings.value(QStringLiteral("infoLineFontFamily"), defaults.fullscreen.infoLineFontFamily).toString();
    result.fullscreen.infoLineFontPointSize = clampedInt(
        settings.value(QStringLiteral("infoLineFontPointSize"), defaults.fullscreen.infoLineFontPointSize),
        defaults.fullscreen.infoLineFontPointSize,
        MinFullscreenInfoLineFontPointSize,
        MaxFullscreenInfoLineFontPointSize);
    result.fullscreen.infoLineColor = validColorName(
        settings.value(QStringLiteral("infoLineColor"), defaults.fullscreen.infoLineColor),
        defaults.fullscreen.infoLineColor);
    settings.endGroup();

    result.shortcuts = defaultShortcutSettings();
    settings.beginGroup(QStringLiteral("shortcuts"));
    for (ShortcutCommand command : shortcutCommands()) {
        const QString id = shortcutCommandId(command);
        settings.beginGroup(id);
        const bool customized = settings.value(QStringLiteral("customized"), false).toBool();
        if (customized) {
            bool validSequences = true;
            ShortcutBinding binding;
            binding.customized = true;
            binding.sequences = validatedShortcutSequences(
                settings.value(QStringLiteral("sequences"), QStringList()),
                defaultShortcutSequences(command),
                &validSequences);
            if (!validSequences) {
                binding.customized = false;
                binding.sequences = defaultShortcutSequences(command);
            }
            result.shortcuts.bindings.insert(id, binding);
        }
        settings.endGroup();
    }
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
    settings.setValue(QStringLiteral("clickZoomBehavior"), clickZoomBehaviorToSettingsString(viewer.clickZoomBehavior));
    settings.setValue(QStringLiteral("exifAutorotate"), viewer.exifAutorotate);
    settings.setValue(QStringLiteral("clickZoomMultiplier"), std::clamp(viewer.clickZoomMultiplier, MinClickZoomMultiplier, MaxClickZoomMultiplier));
    settings.setValue(QStringLiteral("wheelAction"), wheelActionToSettingsString(viewer.wheelAction));
    settings.setValue(QStringLiteral("ctrlWheelAction"), wheelActionToSettingsString(viewer.ctrlWheelAction));
    settings.setValue(QStringLiteral("shiftWheelAction"), wheelActionToSettingsString(viewer.shiftWheelAction));
    settings.setValue(QStringLiteral("zoomStepPercent"), std::clamp(viewer.zoomStepPercent, MinZoomStepPercent, MaxZoomStepPercent));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("fullscreen"));
    settings.setValue(QStringLiteral("sideTriggerRatio"), std::clamp(fullscreen.sideTriggerRatio, MinFullscreenTriggerRatio, MaxFullscreenTriggerRatio));
    settings.setValue(QStringLiteral("verticalTriggerRatio"), std::clamp(fullscreen.verticalTriggerRatio, MinFullscreenTriggerRatio, MaxFullscreenTriggerRatio));
    settings.setValue(QStringLiteral("infoLineEnabled"), fullscreen.infoLineEnabled);
    settings.setValue(QStringLiteral("infoLineFontFamily"), fullscreen.infoLineFontFamily);
    settings.setValue(QStringLiteral("infoLineFontPointSize"), std::clamp(fullscreen.infoLineFontPointSize, MinFullscreenInfoLineFontPointSize, MaxFullscreenInfoLineFontPointSize));
    settings.setValue(QStringLiteral("infoLineColor"), validColorName(fullscreen.infoLineColor, AppSettings::defaults().fullscreen.infoLineColor));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("shortcuts"));
    for (ShortcutCommand command : shortcutCommands()) {
        const QString id = shortcutCommandId(command);
        const ShortcutBinding binding = shortcuts.bindings.value(
            id,
            ShortcutBinding { false, defaultShortcutSequences(command) });
        settings.beginGroup(id);
        settings.setValue(QStringLiteral("customized"), binding.customized);
        if (binding.customized) {
            settings.setValue(QStringLiteral("sequences"), binding.sequences);
        } else {
            settings.remove(QStringLiteral("sequences"));
        }
        settings.endGroup();
    }
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
    case SettingsCategory::Shortcuts:
        shortcuts = defaults.shortcuts;
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

QString clickZoomBehaviorToSettingsString(ClickZoomBehavior behavior)
{
    switch (behavior) {
    case ClickZoomBehavior::ToggleOnClick:
        return QStringLiteral("toggle");
    case ClickZoomBehavior::HoldToZoomPan:
        return QStringLiteral("hold-pan");
    }
    return QStringLiteral("toggle");
}

ClickZoomBehavior clickZoomBehaviorFromSettingsString(const QString &value, ClickZoomBehavior fallback)
{
    const QString normalized = value.toCaseFolded();
    if (normalized == QLatin1String("toggle")) {
        return ClickZoomBehavior::ToggleOnClick;
    }
    if (normalized == QLatin1String("hold-pan")) {
        return ClickZoomBehavior::HoldToZoomPan;
    }
    return fallback;
}

QString wheelActionToSettingsString(WheelAction action)
{
    switch (action) {
    case WheelAction::NavigateImages:
        return QStringLiteral("navigate");
    case WheelAction::Zoom:
        return QStringLiteral("zoom");
    case WheelAction::NoAction:
        return QStringLiteral("none");
    }
    return QStringLiteral("navigate");
}

WheelAction wheelActionFromSettingsString(const QString &value, WheelAction fallback)
{
    const QString normalized = value.toCaseFolded();
    if (normalized == QLatin1String("navigate")) {
        return WheelAction::NavigateImages;
    }
    if (normalized == QLatin1String("zoom")) {
        return WheelAction::Zoom;
    }
    if (normalized == QLatin1String("none")) {
        return WheelAction::NoAction;
    }
    return fallback;
}

QVector<ShortcutCommand> shortcutCommands()
{
    return {
        ShortcutCommand::OpenFile,
        ShortcutCommand::OpenFolder,
        ShortcutCommand::PreviousImage,
        ShortcutCommand::NextImage,
        ShortcutCommand::PreviousFolder,
        ShortcutCommand::NextFolder,
        ShortcutCommand::FirstImage,
        ShortcutCommand::LastImage,
        ShortcutCommand::QuickJump,
        ShortcutCommand::CopyOriginalImage,
        ShortcutCommand::Preferences,
        ShortcutCommand::ZoomIn,
        ShortcutCommand::ZoomOut,
        ShortcutCommand::FitToWindow,
        ShortcutCommand::ActualSize,
        ShortcutCommand::RotateLeft,
        ShortcutCommand::RotateRight,
        ShortcutCommand::ExifAutorotate,
        ShortcutCommand::Fullscreen,
    };
}

QString shortcutCommandId(ShortcutCommand command)
{
    switch (command) {
    case ShortcutCommand::OpenFile:
        return QStringLiteral("openFile");
    case ShortcutCommand::OpenFolder:
        return QStringLiteral("openFolder");
    case ShortcutCommand::PreviousImage:
        return QStringLiteral("previousImage");
    case ShortcutCommand::NextImage:
        return QStringLiteral("nextImage");
    case ShortcutCommand::PreviousFolder:
        return QStringLiteral("previousFolder");
    case ShortcutCommand::NextFolder:
        return QStringLiteral("nextFolder");
    case ShortcutCommand::FirstImage:
        return QStringLiteral("firstImage");
    case ShortcutCommand::LastImage:
        return QStringLiteral("lastImage");
    case ShortcutCommand::QuickJump:
        return QStringLiteral("quickJump");
    case ShortcutCommand::CopyOriginalImage:
        return QStringLiteral("copyOriginalImage");
    case ShortcutCommand::Preferences:
        return QStringLiteral("preferences");
    case ShortcutCommand::ZoomIn:
        return QStringLiteral("zoomIn");
    case ShortcutCommand::ZoomOut:
        return QStringLiteral("zoomOut");
    case ShortcutCommand::FitToWindow:
        return QStringLiteral("fitToWindow");
    case ShortcutCommand::ActualSize:
        return QStringLiteral("actualSize");
    case ShortcutCommand::RotateLeft:
        return QStringLiteral("rotateLeft");
    case ShortcutCommand::RotateRight:
        return QStringLiteral("rotateRight");
    case ShortcutCommand::ExifAutorotate:
        return QStringLiteral("exifAutorotate");
    case ShortcutCommand::Fullscreen:
        return QStringLiteral("fullscreen");
    }
    return QString();
}

QStringList defaultShortcutSequences(ShortcutCommand command)
{
    switch (command) {
    case ShortcutCommand::OpenFile:
        return { keySequenceToSettingsString(QKeySequence::Open) };
    case ShortcutCommand::OpenFolder:
        return { keySequenceToSettingsString(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)) };
    case ShortcutCommand::PreviousImage:
        return {
            keySequenceToSettingsString(QKeySequence(Qt::Key_Left)),
            keySequenceToSettingsString(QKeySequence(Qt::Key_PageUp)),
        };
    case ShortcutCommand::NextImage:
        return {
            keySequenceToSettingsString(QKeySequence(Qt::Key_Right)),
            keySequenceToSettingsString(QKeySequence(Qt::Key_PageDown)),
        };
    case ShortcutCommand::PreviousFolder:
        return { QStringLiteral("MouseBack") };
    case ShortcutCommand::NextFolder:
        return { QStringLiteral("MouseForward") };
    case ShortcutCommand::FirstImage:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_Home)) };
    case ShortcutCommand::LastImage:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_End)) };
    case ShortcutCommand::QuickJump:
        return { keySequenceToSettingsString(QKeySequence::Find) };
    case ShortcutCommand::CopyOriginalImage:
        return { keySequenceToSettingsString(QKeySequence::Copy) };
    case ShortcutCommand::Preferences:
        return { keySequenceToSettingsString(QKeySequence::Preferences) };
    case ShortcutCommand::ZoomIn:
        return { keySequenceToSettingsString(QKeySequence::ZoomIn) };
    case ShortcutCommand::ZoomOut:
        return { keySequenceToSettingsString(QKeySequence::ZoomOut) };
    case ShortcutCommand::FitToWindow:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_0)) };
    case ShortcutCommand::ActualSize:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_1)) };
    case ShortcutCommand::RotateLeft:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_BracketLeft)) };
    case ShortcutCommand::RotateRight:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_BracketRight)) };
    case ShortcutCommand::ExifAutorotate:
        return {};
    case ShortcutCommand::Fullscreen:
        return { keySequenceToSettingsString(QKeySequence(Qt::Key_F11)) };
    }
    return {};
}

ShortcutSettings defaultShortcutSettings()
{
    ShortcutSettings settings;
    for (ShortcutCommand command : shortcutCommands()) {
        settings.bindings.insert(shortcutCommandId(command), ShortcutBinding { false, defaultShortcutSequences(command) });
    }
    return settings;
}

QStringList shortcutSequences(const ShortcutSettings &settings, ShortcutCommand command)
{
    const QString id = shortcutCommandId(command);
    const ShortcutBinding binding = settings.bindings.value(
        id,
        ShortcutBinding { false, defaultShortcutSequences(command) });
    if (!binding.customized) {
        return defaultShortcutSequences(command);
    }
    return binding.sequences;
}

bool shortcutSequenceIsMouseInput(const QString &value)
{
    MouseShortcut shortcut;
    return mouseShortcutFromSettingsString(value, &shortcut);
}

bool mouseShortcutFromSettingsString(const QString &value, MouseShortcut *shortcut)
{
    if (shortcut == nullptr) {
        return false;
    }

    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QString mouseButtonName;
    const QStringList parts = value.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }

    for (const QString &part : parts) {
        const QString normalized = part.trimmed().toCaseFolded();
        if (normalized == QLatin1String("ctrl") || normalized == QLatin1String("control")) {
            modifiers |= Qt::ControlModifier;
        } else if (normalized == QLatin1String("shift")) {
            modifiers |= Qt::ShiftModifier;
        } else if (normalized == QLatin1String("alt")) {
            modifiers |= Qt::AltModifier;
        } else if (normalized == QLatin1String("meta")) {
            modifiers |= Qt::MetaModifier;
        } else if (mouseButtonName.isEmpty()) {
            mouseButtonName = part.trimmed();
        } else {
            return false;
        }
    }

    if (mouseButtonName.isEmpty()) {
        return false;
    }

    for (const MouseButtonName &buttonName : MouseButtonNames) {
        if (mouseButtonName.compare(QLatin1String(buttonName.name), Qt::CaseInsensitive) == 0) {
            shortcut->modifiers = modifiers;
            shortcut->button = buttonName.button;
            return true;
        }
    }

    return false;
}

QString mouseShortcutToSettingsString(const MouseShortcut &shortcut)
{
    if (shortcut.button == Qt::NoButton) {
        return QString();
    }

    QStringList parts;
    if (shortcut.modifiers.testFlag(Qt::ControlModifier)) {
        parts.append(QStringLiteral("Ctrl"));
    }
    if (shortcut.modifiers.testFlag(Qt::ShiftModifier)) {
        parts.append(QStringLiteral("Shift"));
    }
    if (shortcut.modifiers.testFlag(Qt::AltModifier)) {
        parts.append(QStringLiteral("Alt"));
    }
    if (shortcut.modifiers.testFlag(Qt::MetaModifier)) {
        parts.append(QStringLiteral("Meta"));
    }

    for (const MouseButtonName &buttonName : MouseButtonNames) {
        if (shortcut.button == buttonName.button) {
            parts.append(QLatin1String(buttonName.name));
            return parts.join(QLatin1Char('+'));
        }
    }

    return QString();
}

QString shortcutSequenceDisplayString(const QString &value)
{
    MouseShortcut mouseShortcut;
    if (mouseShortcutFromSettingsString(value, &mouseShortcut)) {
        QString display = mouseShortcutToSettingsString(mouseShortcut);
        display.replace(QStringLiteral("Mouse"), QStringLiteral("Mouse "));
        return display;
    }

    const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
    return sequence.isEmpty() ? value : sequence.toString(QKeySequence::NativeText);
}

void setShortcutSequences(ShortcutSettings *settings, ShortcutCommand command, const QStringList &sequences)
{
    if (settings == nullptr) {
        return;
    }
    settings->bindings.insert(shortcutCommandId(command), ShortcutBinding { true, sequences });
}

void resetShortcutSequences(ShortcutSettings *settings, ShortcutCommand command)
{
    if (settings == nullptr) {
        return;
    }
    settings->bindings.insert(shortcutCommandId(command), ShortcutBinding { false, defaultShortcutSequences(command) });
}

} // namespace insidious::core
