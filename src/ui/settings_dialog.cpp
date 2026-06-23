#include "ui/settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace insidious::ui {
namespace {

QWidget *createPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    return page;
}

QFormLayout *pageForm(QWidget *page)
{
    auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setVerticalSpacing(8);
    layout->addLayout(form);
    layout->addStretch(1);
    return form;
}

void setComboToData(QComboBox *combo, int value)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

void addWheelActionItems(QComboBox *combo)
{
    combo->addItem(QObject::tr("Navigate images"), static_cast<int>(core::WheelAction::NavigateImages));
    combo->addItem(QObject::tr("Zoom"), static_cast<int>(core::WheelAction::Zoom));
    combo->addItem(QObject::tr("No action"), static_cast<int>(core::WheelAction::NoAction));
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setSettings(core::AppSettings::defaults());
}

void SettingsDialog::setSettings(const core::AppSettings &settings)
{
    m_settings = settings;
    updateControlsFromSettings();
}

core::AppSettings SettingsDialog::settings() const
{
    return m_settings;
}

void SettingsDialog::setupUi()
{
    setWindowTitle(tr("Preferences"));
    resize(680, 430);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(150);
    m_categoryList->addItem(tr("Viewer"));
    m_categoryList->addItem(tr("Fullscreen"));
    m_categoryList->addItem(tr("Interface"));
    m_categoryList->setCurrentRow(0);

    m_pages = new QStackedWidget(this);
    setupViewerPage();
    setupFullscreenPage();
    setupInterfacePage();

    auto *contentLayout = new QHBoxLayout;
    contentLayout->addWidget(m_categoryList);
    contentLayout->addWidget(m_pages, 1);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
        this);
    m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Page Defaults"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(contentLayout, 1);
    mainLayout->addWidget(m_buttonBox);

    connect(m_categoryList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        updateSettingsFromControls();
        emit settingsApplied(m_settings);
    });
    connect(m_buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        restoreCurrentPageDefaults();
    });
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        updateSettingsFromControls();
        emit settingsApplied(m_settings);
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::setupViewerPage()
{
    QWidget *page = createPage();
    auto *description = new QLabel(tr("Viewer defaults apply to newly opened images and future navigation actions."), page);
    description->setWordWrap(true);
    qobject_cast<QVBoxLayout *>(page->layout())->addWidget(description);

    QFormLayout *form = pageForm(page);

    m_defaultZoomCombo = new QComboBox(page);
    m_defaultZoomCombo->addItem(tr("Fit to window"), static_cast<int>(core::ZoomMode::FitToWindow));
    m_defaultZoomCombo->addItem(tr("Actual size"), static_cast<int>(core::ZoomMode::ActualSize));
    form->addRow(tr("Default zoom:"), m_defaultZoomCombo);

    m_imageSwitchPolicyCombo = new QComboBox(page);
    m_imageSwitchPolicyCombo->addItem(tr("Reset zoom and pan"), static_cast<int>(core::ImageSwitchPolicy::ResetToBaseMode));
    m_imageSwitchPolicyCombo->addItem(tr("Preserve current view"), static_cast<int>(core::ImageSwitchPolicy::PreserveView));
    form->addRow(tr("When switching images:"), m_imageSwitchPolicyCombo);

    m_clickZoomBehaviorCombo = new QComboBox(page);
    m_clickZoomBehaviorCombo->addItem(tr("Click to zoom / restore"), static_cast<int>(core::ClickZoomBehavior::ToggleOnClick));
    m_clickZoomBehaviorCombo->addItem(tr("Hold to zoom, drag to pan"), static_cast<int>(core::ClickZoomBehavior::HoldToZoomPan));
    form->addRow(tr("Left button zoom:"), m_clickZoomBehaviorCombo);

    m_clickZoomSpin = new QDoubleSpinBox(page);
    m_clickZoomSpin->setRange(1.1, 8.0);
    m_clickZoomSpin->setSingleStep(0.1);
    m_clickZoomSpin->setDecimals(1);
    m_clickZoomSpin->setSuffix(QStringLiteral("x"));
    form->addRow(tr("Click zoom multiplier:"), m_clickZoomSpin);

    m_zoomStepSpin = new QDoubleSpinBox(page);
    m_zoomStepSpin->setRange(5.0, 100.0);
    m_zoomStepSpin->setSingleStep(5.0);
    m_zoomStepSpin->setDecimals(0);
    m_zoomStepSpin->setSuffix(QStringLiteral("%"));
    form->addRow(tr("Zoom step:"), m_zoomStepSpin);

    m_wheelActionCombo = new QComboBox(page);
    addWheelActionItems(m_wheelActionCombo);
    form->addRow(tr("Wheel:"), m_wheelActionCombo);

    m_ctrlWheelActionCombo = new QComboBox(page);
    addWheelActionItems(m_ctrlWheelActionCombo);
    form->addRow(tr("Ctrl+wheel:"), m_ctrlWheelActionCombo);

    m_shiftWheelActionCombo = new QComboBox(page);
    addWheelActionItems(m_shiftWheelActionCombo);
    form->addRow(tr("Shift+wheel:"), m_shiftWheelActionCombo);

    m_pages->addWidget(page);
}

void SettingsDialog::setupFullscreenPage()
{
    QWidget *page = createPage();
    auto *description = new QLabel(tr("Edge trigger zones control when fullscreen overlay panels appear."), page);
    description->setWordWrap(true);
    qobject_cast<QVBoxLayout *>(page->layout())->addWidget(description);

    QFormLayout *form = pageForm(page);

    m_fullscreenSideTriggerSpin = new QDoubleSpinBox(page);
    m_fullscreenSideTriggerSpin->setRange(1.0, 30.0);
    m_fullscreenSideTriggerSpin->setSingleStep(0.5);
    m_fullscreenSideTriggerSpin->setDecimals(1);
    m_fullscreenSideTriggerSpin->setSuffix(QStringLiteral("%"));
    form->addRow(tr("Side edge trigger:"), m_fullscreenSideTriggerSpin);

    m_fullscreenVerticalTriggerSpin = new QDoubleSpinBox(page);
    m_fullscreenVerticalTriggerSpin->setRange(1.0, 30.0);
    m_fullscreenVerticalTriggerSpin->setSingleStep(0.5);
    m_fullscreenVerticalTriggerSpin->setDecimals(1);
    m_fullscreenVerticalTriggerSpin->setSuffix(QStringLiteral("%"));
    form->addRow(tr("Top/bottom trigger:"), m_fullscreenVerticalTriggerSpin);

    m_pages->addWidget(page);
}

void SettingsDialog::setupInterfacePage()
{
    QWidget *page = createPage();
    auto *description = new QLabel(tr("Layout settings store window geometry, panel visibility, and Files panel splitter sizes. They never store opened paths."), page);
    description->setWordWrap(true);
    qobject_cast<QVBoxLayout *>(page->layout())->addWidget(description);

    QFormLayout *form = pageForm(page);

    m_rememberLayoutCheck = new QCheckBox(tr("Remember window layout"), page);
    form->addRow(QString(), m_rememberLayoutCheck);

    m_pages->addWidget(page);
}

void SettingsDialog::updateControlsFromSettings()
{
    setComboToData(m_defaultZoomCombo, static_cast<int>(m_settings.viewer.defaultZoomMode));
    setComboToData(m_imageSwitchPolicyCombo, static_cast<int>(m_settings.viewer.imageSwitchPolicy));
    setComboToData(m_clickZoomBehaviorCombo, static_cast<int>(m_settings.viewer.clickZoomBehavior));
    m_clickZoomSpin->setValue(m_settings.viewer.clickZoomMultiplier);
    m_zoomStepSpin->setValue(m_settings.viewer.zoomStepPercent);
    setComboToData(m_wheelActionCombo, static_cast<int>(m_settings.viewer.wheelAction));
    setComboToData(m_ctrlWheelActionCombo, static_cast<int>(m_settings.viewer.ctrlWheelAction));
    setComboToData(m_shiftWheelActionCombo, static_cast<int>(m_settings.viewer.shiftWheelAction));
    m_fullscreenSideTriggerSpin->setValue(m_settings.fullscreen.sideTriggerRatio * 100.0);
    m_fullscreenVerticalTriggerSpin->setValue(m_settings.fullscreen.verticalTriggerRatio * 100.0);
    m_rememberLayoutCheck->setChecked(m_settings.interface.rememberLayout);
}

void SettingsDialog::updateSettingsFromControls()
{
    m_settings.viewer.defaultZoomMode = static_cast<core::ZoomMode>(m_defaultZoomCombo->currentData().toInt());
    m_settings.viewer.imageSwitchPolicy = static_cast<core::ImageSwitchPolicy>(m_imageSwitchPolicyCombo->currentData().toInt());
    m_settings.viewer.clickZoomBehavior = static_cast<core::ClickZoomBehavior>(m_clickZoomBehaviorCombo->currentData().toInt());
    m_settings.viewer.clickZoomMultiplier = m_clickZoomSpin->value();
    m_settings.viewer.zoomStepPercent = m_zoomStepSpin->value();
    m_settings.viewer.wheelAction = static_cast<core::WheelAction>(m_wheelActionCombo->currentData().toInt());
    m_settings.viewer.ctrlWheelAction = static_cast<core::WheelAction>(m_ctrlWheelActionCombo->currentData().toInt());
    m_settings.viewer.shiftWheelAction = static_cast<core::WheelAction>(m_shiftWheelActionCombo->currentData().toInt());
    m_settings.fullscreen.sideTriggerRatio = m_fullscreenSideTriggerSpin->value() / 100.0;
    m_settings.fullscreen.verticalTriggerRatio = m_fullscreenVerticalTriggerSpin->value() / 100.0;
    m_settings.interface.rememberLayout = m_rememberLayoutCheck->isChecked();
}

void SettingsDialog::restoreCurrentPageDefaults()
{
    updateSettingsFromControls();
    m_settings.resetCategory(currentCategory());
    updateControlsFromSettings();
}

core::SettingsCategory SettingsDialog::currentCategory() const
{
    switch (m_categoryList->currentRow()) {
    case 1:
        return core::SettingsCategory::Fullscreen;
    case 2:
        return core::SettingsCategory::Interface;
    default:
        return core::SettingsCategory::Viewer;
    }
}

} // namespace insidious::ui
