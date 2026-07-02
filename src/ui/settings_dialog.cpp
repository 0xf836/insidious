#include "ui/settings_dialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFontComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
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
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    return page;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    return qobject_cast<QVBoxLayout *>(page->layout());
}

QScrollArea *scrollablePage(QWidget *page)
{
    auto *area = new QScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidgetResizable(true);
    area->setWidget(page);
    return area;
}

QLabel *pageDescription(QWidget *page, const QString &text)
{
    auto *description = new QLabel(text, page);
    description->setWordWrap(true);
    description->setObjectName(QStringLiteral("pageDescription"));
    pageLayout(page)->addWidget(description);
    return description;
}

QFormLayout *sectionForm(QWidget *page, const QString &title)
{
    auto *group = new QGroupBox(title, page);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setVerticalSpacing(8);
    form->setContentsMargins(12, 10, 12, 12);
    group->setLayout(form);
    pageLayout(page)->addWidget(group);
    return form;
}

int fieldMinimumHeight(const QWidget *widget)
{
    const QFontMetrics metrics(widget->font());
    return std::max(28, metrics.height() + 10);
}

template <typename Widget>
Widget *stabilizeFieldHeight(Widget *widget)
{
    widget->setMinimumHeight(fieldMinimumHeight(widget));
    return widget;
}

QVBoxLayout *sectionLayout(QWidget *page, const QString &title)
{
    auto *group = new QGroupBox(title, page);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    pageLayout(page)->addWidget(group, 1);
    return layout;
}

void setComboToData(QComboBox *combo, int value)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

void setFontComboFamily(QFontComboBox *combo, const QString &family)
{
    const QFont font(family.isEmpty() ? QFont().family() : family);
    combo->setCurrentFont(font);
}

void addWheelActionItems(QComboBox *combo)
{
    combo->addItem(QObject::tr("Navigate images"), static_cast<int>(core::WheelAction::NavigateImages));
    combo->addItem(QObject::tr("Zoom"), static_cast<int>(core::WheelAction::Zoom));
    combo->addItem(QObject::tr("No action"), static_cast<int>(core::WheelAction::NoAction));
}

QStringList mouseShortcutSequences(const QStringList &values)
{
    QStringList result;
    for (const QString &value : values) {
        if (core::shortcutSequenceIsMouseInput(value)) {
            result.append(value);
        }
    }
    return result;
}

QStringList keyboardShortcutSequences(const QStringList &values)
{
    QStringList result;
    for (const QString &value : values) {
        if (!core::shortcutSequenceIsMouseInput(value)) {
            result.append(value);
        }
    }
    return result;
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
    resize(840, 620);
    setMinimumSize(780, 560);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(160);
    m_categoryList->setFrameShape(QFrame::NoFrame);
    m_categoryList->setSpacing(2);
    m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(m_categoryList);
    contentLayout->addWidget(m_pages, 1);

    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::RestoreDefaults,
        this);
    m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Page Defaults"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(12);
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
    pageDescription(page, tr("Viewer defaults apply to newly opened images and future navigation actions."));

    QFormLayout *viewForm = sectionForm(page, tr("View Defaults"));

    m_defaultZoomCombo = stabilizeFieldHeight(new QComboBox(page));
    m_defaultZoomCombo->addItem(tr("Fit to window"), static_cast<int>(core::ZoomMode::FitToWindow));
    m_defaultZoomCombo->addItem(tr("Actual size"), static_cast<int>(core::ZoomMode::ActualSize));
    viewForm->addRow(tr("Default zoom:"), m_defaultZoomCombo);

    m_imageSwitchPolicyCombo = stabilizeFieldHeight(new QComboBox(page));
    m_imageSwitchPolicyCombo->addItem(tr("Reset zoom and pan"), static_cast<int>(core::ImageSwitchPolicy::ResetToBaseMode));
    m_imageSwitchPolicyCombo->addItem(tr("Preserve current view"), static_cast<int>(core::ImageSwitchPolicy::PreserveView));
    viewForm->addRow(tr("When switching images:"), m_imageSwitchPolicyCombo);

    m_exifAutorotateCheck = new QCheckBox(tr("Use EXIF orientation"), page);
    viewForm->addRow(QString(), m_exifAutorotateCheck);

    QFormLayout *zoomForm = sectionForm(page, tr("Zoom and Rendering"));

    m_clickZoomBehaviorCombo = stabilizeFieldHeight(new QComboBox(page));
    m_clickZoomBehaviorCombo->addItem(tr("Click to zoom / restore"), static_cast<int>(core::ClickZoomBehavior::ToggleOnClick));
    m_clickZoomBehaviorCombo->addItem(tr("Hold to zoom, drag to pan"), static_cast<int>(core::ClickZoomBehavior::HoldToZoomPan));
    zoomForm->addRow(tr("Left button zoom:"), m_clickZoomBehaviorCombo);

    m_clickZoomSpin = stabilizeFieldHeight(new QDoubleSpinBox(page));
    m_clickZoomSpin->setRange(1.1, 8.0);
    m_clickZoomSpin->setSingleStep(0.1);
    m_clickZoomSpin->setDecimals(1);
    m_clickZoomSpin->setSuffix(QStringLiteral("x"));
    zoomForm->addRow(tr("Click zoom multiplier:"), m_clickZoomSpin);

    m_zoomStepSpin = stabilizeFieldHeight(new QDoubleSpinBox(page));
    m_zoomStepSpin->setRange(5.0, 100.0);
    m_zoomStepSpin->setSingleStep(5.0);
    m_zoomStepSpin->setDecimals(0);
    m_zoomStepSpin->setSuffix(QStringLiteral("%"));
    zoomForm->addRow(tr("Zoom step:"), m_zoomStepSpin);

    m_scalingQualityCombo = stabilizeFieldHeight(new QComboBox(page));
    m_scalingQualityCombo->addItem(tr("Fast (linear)"), static_cast<int>(core::ScalingQuality::FastLinear));
    m_scalingQualityCombo->addItem(tr("Balanced (Mitchell)"), static_cast<int>(core::ScalingQuality::BalancedMitchell));
    m_scalingQualityCombo->addItem(tr("High quality (Lanczos3)"), static_cast<int>(core::ScalingQuality::HighLanczos3));
    m_scalingQualityCombo->addItem(tr("Sharpest (MKS2021)"), static_cast<int>(core::ScalingQuality::SharpestMks2021));
    zoomForm->addRow(tr("Scaling quality:"), m_scalingQualityCombo);

    m_colorManagementCheck = new QCheckBox(tr("Use embedded color profiles"), page);
    m_colorManagementCheck->setToolTip(tr("Convert embedded image color profiles to sRGB for display. Images without profiles are treated as sRGB."));
    zoomForm->addRow(QString(), m_colorManagementCheck);

    m_regionRenderingCheck = new QCheckBox(tr("Render only visible zoom region"), page);
    m_regionRenderingCheck->setToolTip(tr("Use less memory for zoom and actual size renders. Disable for smoother panning at higher memory cost."));
    zoomForm->addRow(QString(), m_regionRenderingCheck);

    QFormLayout *wheelForm = sectionForm(page, tr("Wheel Actions"));

    m_wheelActionCombo = stabilizeFieldHeight(new QComboBox(page));
    addWheelActionItems(m_wheelActionCombo);
    wheelForm->addRow(tr("Wheel:"), m_wheelActionCombo);

    m_ctrlWheelActionCombo = stabilizeFieldHeight(new QComboBox(page));
    addWheelActionItems(m_ctrlWheelActionCombo);
    wheelForm->addRow(tr("Ctrl+wheel:"), m_ctrlWheelActionCombo);

    m_shiftWheelActionCombo = stabilizeFieldHeight(new QComboBox(page));
    addWheelActionItems(m_shiftWheelActionCombo);
    wheelForm->addRow(tr("Shift+wheel:"), m_shiftWheelActionCombo);

    pageLayout(page)->addStretch(1);

    m_pages->addWidget(scrollablePage(page));
}

void SettingsDialog::setupShortcutsPage()
{
    QWidget *page = createPage();
    pageDescription(page, tr("Customize core viewer shortcuts. Empty shortcuts leave commands available from menus and toolbars."));

    m_shortcutCommands = core::shortcutCommands();
    QVBoxLayout *shortcutLayout = sectionLayout(page, tr("Assignments"));
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
    shortcutLayout->addWidget(m_shortcutTable, 1);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);

    auto *changeButton = new QPushButton(tr("Set Keyboard"), page);
    connect(changeButton, &QPushButton::clicked, this, [this]() {
        core::ShortcutCommand command;
        if (selectedShortcutCommand(&command)) {
            changeKeyboardShortcut(command);
        }
    });
    buttonLayout->addWidget(changeButton);

    auto *mouseButton = new QPushButton(tr("Set Mouse"), page);
    connect(mouseButton, &QPushButton::clicked, this, [this]() {
        core::ShortcutCommand command;
        if (selectedShortcutCommand(&command)) {
            changeMouseShortcut(command);
        }
    });
    buttonLayout->addWidget(mouseButton);

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

    shortcutLayout->addLayout(buttonLayout);

    m_pages->addWidget(scrollablePage(page));
}

void SettingsDialog::setupFullscreenPage()
{
    QWidget *page = createPage();
    pageDescription(page, tr("Fullscreen settings control overlay panels and the pinned presentation info line."));

    QFormLayout *edgeForm = sectionForm(page, tr("Edge Overlays"));

    m_fullscreenSideTriggerSpin = stabilizeFieldHeight(new QDoubleSpinBox(page));
    m_fullscreenSideTriggerSpin->setRange(1.0, 30.0);
    m_fullscreenSideTriggerSpin->setSingleStep(0.5);
    m_fullscreenSideTriggerSpin->setDecimals(1);
    m_fullscreenSideTriggerSpin->setSuffix(QStringLiteral("%"));
    edgeForm->addRow(tr("Side edge trigger:"), m_fullscreenSideTriggerSpin);

    m_fullscreenVerticalTriggerSpin = stabilizeFieldHeight(new QDoubleSpinBox(page));
    m_fullscreenVerticalTriggerSpin->setRange(1.0, 30.0);
    m_fullscreenVerticalTriggerSpin->setSingleStep(0.5);
    m_fullscreenVerticalTriggerSpin->setDecimals(1);
    m_fullscreenVerticalTriggerSpin->setSuffix(QStringLiteral("%"));
    edgeForm->addRow(tr("Top/bottom trigger:"), m_fullscreenVerticalTriggerSpin);

    QFormLayout *infoLineForm = sectionForm(page, tr("Pinned Info Line"));

    m_fullscreenInfoLineCheck = new QCheckBox(tr("Show pinned info line"), page);
    infoLineForm->addRow(QString(), m_fullscreenInfoLineCheck);

    m_fullscreenInfoLineFontCombo = stabilizeFieldHeight(new QFontComboBox(page));
    infoLineForm->addRow(tr("Font:"), m_fullscreenInfoLineFontCombo);

    m_fullscreenInfoLineSizeSpin = stabilizeFieldHeight(new QSpinBox(page));
    m_fullscreenInfoLineSizeSpin->setRange(8, 32);
    m_fullscreenInfoLineSizeSpin->setSingleStep(1);
    m_fullscreenInfoLineSizeSpin->setSuffix(tr(" pt"));
    infoLineForm->addRow(tr("Size:"), m_fullscreenInfoLineSizeSpin);

    m_fullscreenInfoLineColorButton = new QPushButton(page);
    connect(m_fullscreenInfoLineColorButton, &QPushButton::clicked, this, &SettingsDialog::chooseFullscreenInfoLineColor);
    infoLineForm->addRow(tr("Color:"), m_fullscreenInfoLineColorButton);

    pageLayout(page)->addStretch(1);

    m_pages->addWidget(scrollablePage(page));
}

void SettingsDialog::setupInterfacePage()
{
    QWidget *page = createPage();
    pageDescription(page, tr("Interface settings change appearance and privacy-safe layout persistence."));

    QFormLayout *appearanceForm = sectionForm(page, tr("Appearance"));

    m_themeCombo = stabilizeFieldHeight(new QComboBox(page));
    m_themeCombo->addItem(tr("Light"), static_cast<int>(core::UiTheme::Light));
    m_themeCombo->addItem(tr("Dark"), static_cast<int>(core::UiTheme::Dark));
    appearanceForm->addRow(tr("Theme:"), m_themeCombo);

    QFormLayout *layoutForm = sectionForm(page, tr("Layout"));

    m_rememberLayoutCheck = new QCheckBox(tr("Remember window layout"), page);
    m_rememberLayoutCheck->setToolTip(tr("Stores window geometry, panel visibility, and Files panel splitters. It never stores opened paths."));
    layoutForm->addRow(QString(), m_rememberLayoutCheck);

    pageLayout(page)->addStretch(1);

    m_pages->addWidget(scrollablePage(page));
}

void SettingsDialog::updateControlsFromSettings()
{
    setComboToData(m_defaultZoomCombo, static_cast<int>(m_settings.viewer.defaultZoomMode));
    setComboToData(m_imageSwitchPolicyCombo, static_cast<int>(m_settings.viewer.imageSwitchPolicy));
    setComboToData(m_clickZoomBehaviorCombo, static_cast<int>(m_settings.viewer.clickZoomBehavior));
    m_exifAutorotateCheck->setChecked(m_settings.viewer.exifAutorotate);
    m_clickZoomSpin->setValue(m_settings.viewer.clickZoomMultiplier);
    m_zoomStepSpin->setValue(m_settings.viewer.zoomStepPercent);
    setComboToData(m_scalingQualityCombo, static_cast<int>(m_settings.viewer.scalingQuality));
    m_colorManagementCheck->setChecked(m_settings.viewer.colorManagementEnabled);
    m_regionRenderingCheck->setChecked(m_settings.viewer.regionRenderingEnabled);
    setComboToData(m_wheelActionCombo, static_cast<int>(m_settings.viewer.wheelAction));
    setComboToData(m_ctrlWheelActionCombo, static_cast<int>(m_settings.viewer.ctrlWheelAction));
    setComboToData(m_shiftWheelActionCombo, static_cast<int>(m_settings.viewer.shiftWheelAction));
    refreshShortcutsTable();
    m_fullscreenSideTriggerSpin->setValue(m_settings.fullscreen.sideTriggerRatio * 100.0);
    m_fullscreenVerticalTriggerSpin->setValue(m_settings.fullscreen.verticalTriggerRatio * 100.0);
    m_fullscreenInfoLineCheck->setChecked(m_settings.fullscreen.infoLineEnabled);
    setFontComboFamily(m_fullscreenInfoLineFontCombo, m_settings.fullscreen.infoLineFontFamily);
    m_fullscreenInfoLineSizeSpin->setValue(m_settings.fullscreen.infoLineFontPointSize);
    updateFullscreenInfoLineColorButton();
    setComboToData(m_themeCombo, static_cast<int>(m_settings.interface.theme));
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
    m_settings.viewer.scalingQuality = static_cast<core::ScalingQuality>(m_scalingQualityCombo->currentData().toInt());
    m_settings.viewer.colorManagementEnabled = m_colorManagementCheck->isChecked();
    m_settings.viewer.regionRenderingEnabled = m_regionRenderingCheck->isChecked();
    m_settings.viewer.wheelAction = static_cast<core::WheelAction>(m_wheelActionCombo->currentData().toInt());
    m_settings.viewer.ctrlWheelAction = static_cast<core::WheelAction>(m_ctrlWheelActionCombo->currentData().toInt());
    m_settings.viewer.shiftWheelAction = static_cast<core::WheelAction>(m_shiftWheelActionCombo->currentData().toInt());
    m_settings.fullscreen.sideTriggerRatio = m_fullscreenSideTriggerSpin->value() / 100.0;
    m_settings.fullscreen.verticalTriggerRatio = m_fullscreenVerticalTriggerSpin->value() / 100.0;
    m_settings.fullscreen.infoLineEnabled = m_fullscreenInfoLineCheck->isChecked();
    m_settings.fullscreen.infoLineFontFamily = m_fullscreenInfoLineFontCombo->currentFont().family();
    m_settings.fullscreen.infoLineFontPointSize = m_fullscreenInfoLineSizeSpin->value();
    m_settings.fullscreen.infoLineColor = m_fullscreenInfoLineColorButton->property("colorName").toString();
    m_settings.interface.theme = static_cast<core::UiTheme>(m_themeCombo->currentData().toInt());
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
    case core::ShortcutCommand::PreviousFolder:
        return tr("Previous Folder");
    case core::ShortcutCommand::NextFolder:
        return tr("Next Folder");
    case core::ShortcutCommand::FirstImage:
        return tr("First");
    case core::ShortcutCommand::LastImage:
        return tr("Last");
    case core::ShortcutCommand::QuickJump:
        return tr("Quick Jump");
    case core::ShortcutCommand::CopyOriginalImage:
        return tr("Copy Original Image");
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
        displayValues.append(core::shortcutSequenceDisplayString(value));
    }
    return displayValues.join(tr(", "));
}

bool SettingsDialog::validateShortcutConflicts()
{
    QHash<QString, QString> commandBySequence;
    for (core::ShortcutCommand command : m_shortcutCommands) {
        const QString commandLabel = shortcutCommandLabel(command);
        for (const QString &value : core::shortcutSequences(m_settings.shortcuts, command)) {
            QString portable;
            QString display;
            core::MouseShortcut mouseShortcut;
            if (core::mouseShortcutFromSettingsString(value, &mouseShortcut)) {
                portable = core::mouseShortcutToSettingsString(mouseShortcut);
                display = core::shortcutSequenceDisplayString(portable);
            } else {
                const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
                if (sequence.isEmpty()) {
                    continue;
                }
                portable = sequence.toString(QKeySequence::PortableText);
                display = sequence.toString(QKeySequence::NativeText);
            }
            const auto existing = commandBySequence.constFind(portable);
            if (existing != commandBySequence.constEnd()) {
                QMessageBox::warning(
                    this,
                    tr("Shortcut conflict"),
                    tr("%1 is assigned to both %2 and %3. Choose a unique shortcut or clear one of them.")
                        .arg(display, existing.value(), commandLabel));
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

void SettingsDialog::changeKeyboardShortcut(core::ShortcutCommand command)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Change Keyboard Shortcut"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Press a new keyboard shortcut for %1.").arg(shortcutCommandLabel(command)), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *editor = new QKeySequenceEdit(&dialog);
    const QStringList currentSequences = core::shortcutSequences(m_settings.shortcuts, command);
    if (!currentSequences.isEmpty()) {
        for (const QString &sequenceValue : currentSequences) {
            if (!core::shortcutSequenceIsMouseInput(sequenceValue)) {
                editor->setKeySequence(QKeySequence::fromString(sequenceValue, QKeySequence::PortableText));
                break;
            }
        }
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
    QStringList sequences = mouseShortcutSequences(core::shortcutSequences(m_settings.shortcuts, command));
    if (sequence.isEmpty()) {
        core::setShortcutSequences(&m_settings.shortcuts, command, sequences);
    } else {
        sequences.prepend(sequence);
        core::setShortcutSequences(&m_settings.shortcuts, command, sequences);
    }
    refreshShortcutsTable();
}

void SettingsDialog::changeMouseShortcut(core::ShortcutCommand command)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Change Mouse Shortcut"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(tr("Choose a mouse button for %1.").arg(shortcutCommandLabel(command)), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *form = new QFormLayout;
    auto *buttonCombo = new QComboBox(&dialog);
    buttonCombo->addItem(tr("Left button"), static_cast<int>(Qt::LeftButton));
    buttonCombo->addItem(tr("Middle button"), static_cast<int>(Qt::MiddleButton));
    buttonCombo->addItem(tr("Right button"), static_cast<int>(Qt::RightButton));
    buttonCombo->addItem(tr("Back button"), static_cast<int>(Qt::BackButton));
    buttonCombo->addItem(tr("Forward button"), static_cast<int>(Qt::ForwardButton));
    buttonCombo->addItem(tr("Extra button 1"), static_cast<int>(Qt::ExtraButton1));
    buttonCombo->addItem(tr("Extra button 2"), static_cast<int>(Qt::ExtraButton2));
    form->addRow(tr("Mouse button:"), buttonCombo);
    layout->addLayout(form);

    auto *modifiersLayout = new QHBoxLayout;
    auto *ctrlCheck = new QCheckBox(tr("Ctrl"), &dialog);
    auto *shiftCheck = new QCheckBox(tr("Shift"), &dialog);
    auto *altCheck = new QCheckBox(tr("Alt"), &dialog);
    auto *metaCheck = new QCheckBox(tr("Meta"), &dialog);
    modifiersLayout->addWidget(ctrlCheck);
    modifiersLayout->addWidget(shiftCheck);
    modifiersLayout->addWidget(altCheck);
    modifiersLayout->addWidget(metaCheck);
    modifiersLayout->addStretch(1);
    form->addRow(tr("Modifiers:"), modifiersLayout);

    for (const QString &sequenceValue : core::shortcutSequences(m_settings.shortcuts, command)) {
        core::MouseShortcut shortcut;
        if (!core::mouseShortcutFromSettingsString(sequenceValue, &shortcut)) {
            continue;
        }
        setComboToData(buttonCombo, static_cast<int>(shortcut.button));
        ctrlCheck->setChecked(shortcut.modifiers.testFlag(Qt::ControlModifier));
        shiftCheck->setChecked(shortcut.modifiers.testFlag(Qt::ShiftModifier));
        altCheck->setChecked(shortcut.modifiers.testFlag(Qt::AltModifier));
        metaCheck->setChecked(shortcut.modifiers.testFlag(Qt::MetaModifier));
        break;
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    if (ctrlCheck->isChecked()) {
        modifiers |= Qt::ControlModifier;
    }
    if (shiftCheck->isChecked()) {
        modifiers |= Qt::ShiftModifier;
    }
    if (altCheck->isChecked()) {
        modifiers |= Qt::AltModifier;
    }
    if (metaCheck->isChecked()) {
        modifiers |= Qt::MetaModifier;
    }

    const core::MouseShortcut shortcut {
        modifiers,
        static_cast<Qt::MouseButton>(buttonCombo->currentData().toInt()),
    };
    QStringList sequences = keyboardShortcutSequences(core::shortcutSequences(m_settings.shortcuts, command));
    sequences.append(core::mouseShortcutToSettingsString(shortcut));
    core::setShortcutSequences(&m_settings.shortcuts, command, sequences);
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

void SettingsDialog::chooseFullscreenInfoLineColor()
{
    const QColor currentColor(m_fullscreenInfoLineColorButton->property("colorName").toString());
    const QColor selectedColor = QColorDialog::getColor(
        currentColor.isValid() ? currentColor : QColor(core::AppSettings::defaults().fullscreen.infoLineColor),
        this,
        tr("Choose Info Line Color"));
    if (!selectedColor.isValid()) {
        return;
    }

    m_settings.fullscreen.infoLineColor = selectedColor.name(QColor::HexRgb);
    m_fullscreenInfoLineColorButton->setProperty("colorName", m_settings.fullscreen.infoLineColor);
    updateFullscreenInfoLineColorButton();
}

void SettingsDialog::updateFullscreenInfoLineColorButton()
{
    const QColor color(m_settings.fullscreen.infoLineColor);
    const QString colorName = color.isValid()
        ? color.name(QColor::HexRgb)
        : core::AppSettings::defaults().fullscreen.infoLineColor;
    m_fullscreenInfoLineColorButton->setProperty("colorName", colorName);
    m_fullscreenInfoLineColorButton->setText(colorName);
    m_fullscreenInfoLineColorButton->setStyleSheet(
        QStringLiteral("QPushButton { color: %1; background-color: #202020; border: 1px solid #6a6a6a; padding: 3px 8px; }")
            .arg(colorName));
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
