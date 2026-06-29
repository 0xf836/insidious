#include "ui/settings_dialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
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
    resize(760, 460);
    setMinimumSize(700, 420);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(150);
    m_categoryList->addItem(tr("Viewer"));
    m_categoryList->addItem(tr("Shortcuts"));
    m_categoryList->addItem(tr("Fullscreen"));
    m_categoryList->addItem(tr("Interface"));
    m_categoryList->setCurrentRow(0);

    m_pages = new QStackedWidget(this);
    setupViewerPage();
    setupShortcutsPage();
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
        if (!validateShortcutConflicts()) {
            return;
        }
        emit settingsApplied(m_settings);
    });
    connect(m_buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this]() {
        restoreCurrentPageDefaults();
    });
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        updateSettingsFromControls();
        if (!validateShortcutConflicts()) {
            return;
        }
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

    m_exifAutorotateCheck = new QCheckBox(tr("Use EXIF orientation"), page);
    form->addRow(QString(), m_exifAutorotateCheck);

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

void SettingsDialog::setupShortcutsPage()
{
    QWidget *page = createPage();
    auto *description = new QLabel(tr("Customize core viewer shortcuts. Empty shortcuts leave commands available from menus and toolbars."), page);
    description->setWordWrap(true);
    auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
    layout->addWidget(description);

    m_shortcutCommands = core::shortcutCommands();
    m_shortcutTable = new QTableWidget(page);
    m_shortcutTable->setColumnCount(2);
    m_shortcutTable->setHorizontalHeaderLabels({ tr("Command"), tr("Shortcut") });
    m_shortcutTable->verticalHeader()->hide();
    m_shortcutTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_shortcutTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_shortcutTable->setAlternatingRowColors(true);
    m_shortcutTable->setWordWrap(false);
    m_shortcutTable->horizontalHeader()->setStretchLastSection(false);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_shortcutTable, 1);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);

    auto *changeButton = new QPushButton(tr("Set"), page);
    connect(changeButton, &QPushButton::clicked, this, [this]() {
        core::ShortcutCommand command;
        if (selectedShortcutCommand(&command)) {
            changeShortcut(command);
        }
    });
    buttonLayout->addWidget(changeButton);

    auto *clearButton = new QPushButton(tr("Clear"), page);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        core::ShortcutCommand command;
        if (selectedShortcutCommand(&command)) {
            clearShortcut(command);
        }
    });
    buttonLayout->addWidget(clearButton);

    auto *resetButton = new QPushButton(tr("Reset"), page);
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        core::ShortcutCommand command;
        if (selectedShortcutCommand(&command)) {
            resetShortcut(command);
        }
    });
    buttonLayout->addWidget(resetButton);

    layout->addLayout(buttonLayout);

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
    m_exifAutorotateCheck->setChecked(m_settings.viewer.exifAutorotate);
    m_clickZoomSpin->setValue(m_settings.viewer.clickZoomMultiplier);
    m_zoomStepSpin->setValue(m_settings.viewer.zoomStepPercent);
    setComboToData(m_wheelActionCombo, static_cast<int>(m_settings.viewer.wheelAction));
    setComboToData(m_ctrlWheelActionCombo, static_cast<int>(m_settings.viewer.ctrlWheelAction));
    setComboToData(m_shiftWheelActionCombo, static_cast<int>(m_settings.viewer.shiftWheelAction));
    refreshShortcutsTable();
    m_fullscreenSideTriggerSpin->setValue(m_settings.fullscreen.sideTriggerRatio * 100.0);
    m_fullscreenVerticalTriggerSpin->setValue(m_settings.fullscreen.verticalTriggerRatio * 100.0);
    m_rememberLayoutCheck->setChecked(m_settings.interface.rememberLayout);
}

void SettingsDialog::updateSettingsFromControls()
{
    m_settings.viewer.defaultZoomMode = static_cast<core::ZoomMode>(m_defaultZoomCombo->currentData().toInt());
    m_settings.viewer.imageSwitchPolicy = static_cast<core::ImageSwitchPolicy>(m_imageSwitchPolicyCombo->currentData().toInt());
    m_settings.viewer.clickZoomBehavior = static_cast<core::ClickZoomBehavior>(m_clickZoomBehaviorCombo->currentData().toInt());
    m_settings.viewer.exifAutorotate = m_exifAutorotateCheck->isChecked();
    m_settings.viewer.clickZoomMultiplier = m_clickZoomSpin->value();
    m_settings.viewer.zoomStepPercent = m_zoomStepSpin->value();
    m_settings.viewer.wheelAction = static_cast<core::WheelAction>(m_wheelActionCombo->currentData().toInt());
    m_settings.viewer.ctrlWheelAction = static_cast<core::WheelAction>(m_ctrlWheelActionCombo->currentData().toInt());
    m_settings.viewer.shiftWheelAction = static_cast<core::WheelAction>(m_shiftWheelActionCombo->currentData().toInt());
    m_settings.fullscreen.sideTriggerRatio = m_fullscreenSideTriggerSpin->value() / 100.0;
    m_settings.fullscreen.verticalTriggerRatio = m_fullscreenVerticalTriggerSpin->value() / 100.0;
    m_settings.interface.rememberLayout = m_rememberLayoutCheck->isChecked();
}

QString SettingsDialog::shortcutCommandLabel(core::ShortcutCommand command) const
{
    switch (command) {
    case core::ShortcutCommand::OpenFile:
        return tr("Open File");
    case core::ShortcutCommand::OpenFolder:
        return tr("Open Folder");
    case core::ShortcutCommand::PreviousImage:
        return tr("Previous");
    case core::ShortcutCommand::NextImage:
        return tr("Next");
    case core::ShortcutCommand::FirstImage:
        return tr("First");
    case core::ShortcutCommand::LastImage:
        return tr("Last");
    case core::ShortcutCommand::QuickJump:
        return tr("Quick Jump");
    case core::ShortcutCommand::Preferences:
        return tr("Preferences");
    case core::ShortcutCommand::ZoomIn:
        return tr("Zoom In");
    case core::ShortcutCommand::ZoomOut:
        return tr("Zoom Out");
    case core::ShortcutCommand::FitToWindow:
        return tr("Fit");
    case core::ShortcutCommand::ActualSize:
        return tr("Actual Size");
    case core::ShortcutCommand::RotateLeft:
        return tr("Rotate Left");
    case core::ShortcutCommand::RotateRight:
        return tr("Rotate Right");
    case core::ShortcutCommand::ExifAutorotate:
        return tr("Use EXIF Orientation");
    case core::ShortcutCommand::Fullscreen:
        return tr("Fullscreen");
    }
    return QString();
}

QString SettingsDialog::shortcutDisplayText(core::ShortcutCommand command) const
{
    const QStringList values = core::shortcutSequences(m_settings.shortcuts, command);
    if (values.isEmpty()) {
        return tr("None");
    }

    QStringList displayValues;
    for (const QString &value : values) {
        const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
        displayValues.append(sequence.isEmpty() ? value : sequence.toString(QKeySequence::NativeText));
    }
    return displayValues.join(tr(", "));
}

bool SettingsDialog::validateShortcutConflicts()
{
    QHash<QString, QString> commandBySequence;
    for (core::ShortcutCommand command : m_shortcutCommands) {
        const QString commandLabel = shortcutCommandLabel(command);
        for (const QString &value : core::shortcutSequences(m_settings.shortcuts, command)) {
            const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
            if (sequence.isEmpty()) {
                continue;
            }
            const QString portable = sequence.toString(QKeySequence::PortableText);
            const auto existing = commandBySequence.constFind(portable);
            if (existing != commandBySequence.constEnd()) {
                QMessageBox::warning(
                    this,
                    tr("Shortcut conflict"),
                    tr("%1 is assigned to both %2 and %3. Choose a unique shortcut or clear one of them.")
                        .arg(sequence.toString(QKeySequence::NativeText), existing.value(), commandLabel));
                return false;
            }
            commandBySequence.insert(portable, commandLabel);
        }
    }
    return true;
}

bool SettingsDialog::selectedShortcutCommand(core::ShortcutCommand *command) const
{
    if (m_shortcutTable == nullptr || command == nullptr) {
        return false;
    }

    const int row = m_shortcutTable->currentRow();
    if (row < 0 || row >= m_shortcutCommands.size()) {
        return false;
    }

    *command = m_shortcutCommands.at(row);
    return true;
}

void SettingsDialog::refreshShortcutsTable()
{
    if (m_shortcutTable == nullptr) {
        return;
    }

    const int previousRow = m_shortcutTable->currentRow();
    m_shortcutTable->setRowCount(m_shortcutCommands.size());
    for (int row = 0; row < m_shortcutCommands.size(); ++row) {
        const core::ShortcutCommand command = m_shortcutCommands.at(row);

        auto *commandItem = new QTableWidgetItem(shortcutCommandLabel(command));
        commandItem->setFlags(commandItem->flags() & ~Qt::ItemIsEditable);
        m_shortcutTable->setItem(row, 0, commandItem);

        auto *shortcutItem = new QTableWidgetItem(shortcutDisplayText(command));
        shortcutItem->setFlags(shortcutItem->flags() & ~Qt::ItemIsEditable);
        m_shortcutTable->setItem(row, 1, shortcutItem);
    }

    if (!m_shortcutCommands.isEmpty()) {
        const int rowToSelect = previousRow >= 0 && previousRow < m_shortcutCommands.size()
            ? previousRow
            : 0;
        m_shortcutTable->setCurrentCell(rowToSelect, 0);
    }
}

void SettingsDialog::changeShortcut(core::ShortcutCommand command)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Change Shortcut"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Press a new shortcut for %1.").arg(shortcutCommandLabel(command)), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *editor = new QKeySequenceEdit(&dialog);
    const QStringList currentSequences = core::shortcutSequences(m_settings.shortcuts, command);
    if (!currentSequences.isEmpty()) {
        editor->setKeySequence(QKeySequence::fromString(currentSequences.first(), QKeySequence::PortableText));
    }
    layout->addWidget(editor);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString sequence = editor->keySequence().toString(QKeySequence::PortableText);
    if (sequence.isEmpty()) {
        core::setShortcutSequences(&m_settings.shortcuts, command, QStringList());
    } else {
        core::setShortcutSequences(&m_settings.shortcuts, command, QStringList { sequence });
    }
    refreshShortcutsTable();
}

void SettingsDialog::clearShortcut(core::ShortcutCommand command)
{
    core::setShortcutSequences(&m_settings.shortcuts, command, QStringList());
    refreshShortcutsTable();
}

void SettingsDialog::resetShortcut(core::ShortcutCommand command)
{
    core::resetShortcutSequences(&m_settings.shortcuts, command);
    refreshShortcutsTable();
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
        return core::SettingsCategory::Shortcuts;
    case 2:
        return core::SettingsCategory::Fullscreen;
    case 3:
        return core::SettingsCategory::Interface;
    default:
        return core::SettingsCategory::Viewer;
    }
}

} // namespace insidious::ui
