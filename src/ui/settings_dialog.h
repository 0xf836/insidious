#pragma once

#include "core/app_settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QListWidget;
class QStackedWidget;

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
    void setupFullscreenPage();
    void setupInterfacePage();
    void updateControlsFromSettings();
    void updateSettingsFromControls();
    void restoreCurrentPageDefaults();
    [[nodiscard]] core::SettingsCategory currentCategory() const;

    core::AppSettings m_settings;
    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pages = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    QComboBox *m_defaultZoomCombo = nullptr;
    QComboBox *m_imageSwitchPolicyCombo = nullptr;
    QDoubleSpinBox *m_clickZoomSpin = nullptr;
    QComboBox *m_wheelActionCombo = nullptr;
    QComboBox *m_ctrlWheelActionCombo = nullptr;
    QComboBox *m_shiftWheelActionCombo = nullptr;
    QDoubleSpinBox *m_zoomStepSpin = nullptr;
    QDoubleSpinBox *m_fullscreenSideTriggerSpin = nullptr;
    QDoubleSpinBox *m_fullscreenVerticalTriggerSpin = nullptr;
    QCheckBox *m_rememberLayoutCheck = nullptr;
};

} // namespace insidious::ui
