#pragma once

#include "core/app_settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QListWidget;
class QStackedWidget;
class QTableWidget;

namespace insidious::ui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setSettings(const core::AppSettings &settings);
    [[nodiscard]] core::AppSettings settings() const;

signals:
    void settingsApplied(const core::AppSettings &settings);

private:
    void setupUi();
    void setupViewerPage();
    void setupShortcutsPage();
    void setupFullscreenPage();
    void setupInterfacePage();
    void updateControlsFromSettings();
    void updateSettingsFromControls();
    void restoreCurrentPageDefaults();
    [[nodiscard]] core::SettingsCategory currentCategory() const;
    [[nodiscard]] QString shortcutCommandLabel(core::ShortcutCommand command) const;
    [[nodiscard]] QString shortcutDisplayText(core::ShortcutCommand command) const;
    [[nodiscard]] bool validateShortcutConflicts();
    [[nodiscard]] bool selectedShortcutCommand(core::ShortcutCommand *command) const;
    void refreshShortcutsTable();
    void changeKeyboardShortcut(core::ShortcutCommand command);
    void changeMouseShortcut(core::ShortcutCommand command);
    void clearShortcut(core::ShortcutCommand command);
    void resetShortcut(core::ShortcutCommand command);

    core::AppSettings m_settings;
    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pages = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    QComboBox *m_defaultZoomCombo = nullptr;
    QComboBox *m_imageSwitchPolicyCombo = nullptr;
    QComboBox *m_clickZoomBehaviorCombo = nullptr;
    QCheckBox *m_exifAutorotateCheck = nullptr;
    QDoubleSpinBox *m_clickZoomSpin = nullptr;
    QComboBox *m_wheelActionCombo = nullptr;
    QComboBox *m_ctrlWheelActionCombo = nullptr;
    QComboBox *m_shiftWheelActionCombo = nullptr;
    QDoubleSpinBox *m_zoomStepSpin = nullptr;
    QTableWidget *m_shortcutTable = nullptr;
    QVector<core::ShortcutCommand> m_shortcutCommands;
    QDoubleSpinBox *m_fullscreenSideTriggerSpin = nullptr;
    QDoubleSpinBox *m_fullscreenVerticalTriggerSpin = nullptr;
    QCheckBox *m_rememberLayoutCheck = nullptr;
};

} // namespace insidious::ui
