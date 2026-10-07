#ifndef KSENGINE_QT_FREE
#include "DeviceSettingsWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QProgressBar>
#include <QHeaderView>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

namespace ks::ui {

// ============================================================================
// MonitorSettingsPanel
// ============================================================================

MonitorSettingsPanel::MonitorSettingsPanel(ks::device::DeviceManager* dm, QWidget* parent)
    : QWidget(parent), m_dm(dm)
{
    buildUI();
    refresh();
}

void MonitorSettingsPanel::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Monitor list
    auto* listGroup = new QGroupBox("Detected Monitors");
    auto* listLayout = new QVBoxLayout(listGroup);

    m_centerCombo = new QComboBox;
    listLayout->addWidget(new QLabel("Center Monitor:"));
    listLayout->addWidget(m_centerCombo);

    m_arrangementCombo = new QComboBox;
    m_arrangementCombo->addItems({"Horizontal", "Landscape", "Portrait Top", "Vertical Stack", "Custom"});
    listLayout->addWidget(new QLabel("Arrangement:"));
    listLayout->addWidget(m_arrangementCombo);

    mainLayout->addWidget(listGroup);

    // Physical settings
    auto* physGroup = new QGroupBox("Physical Settings");
    auto* physLayout = new QGridLayout(physGroup);

    physLayout->addWidget(new QLabel("Bezel Compensation (mm):"), 0, 0);
    m_bezelSpin = new QDoubleSpinBox;
    m_bezelSpin->setRange(0, 100);
    m_bezelSpin->setValue(20);
    physLayout->addWidget(m_bezelSpin, 0, 1);

    physLayout->addWidget(new QLabel("Eye Distance (m):"), 1, 0);
    m_eyeDistSpin = new QDoubleSpinBox;
    m_eyeDistSpin->setRange(0.3, 1.5);
    m_eyeDistSpin->setValue(0.63);
    m_eyeDistSpin->setSingleStep(0.01);
    physLayout->addWidget(m_eyeDistSpin, 1, 1);

    physLayout->addWidget(new QLabel("Horizontal FOV (deg):"), 2, 0);
    m_fovSpin = new QDoubleSpinBox;
    m_fovSpin->setRange(60, 180);
    m_fovSpin->setValue(108);
    physLayout->addWidget(m_fovSpin, 2, 1);

    mainLayout->addWidget(physGroup);

    // Display settings
    auto* displayGroup = new QGroupBox("Display Settings");
    auto* displayLayout = new QGridLayout(displayGroup);

    m_vsyncCheck = new QCheckBox("Vertical Sync");
    m_vsyncCheck->setChecked(true);
    displayLayout->addWidget(m_vsyncCheck, 0, 0);

    displayLayout->addWidget(new QLabel("Target FPS:"), 1, 0);
    m_fpsSpin = new QSpinBox;
    m_fpsSpin->setRange(30, 240);
    m_fpsSpin->setValue(60);
    displayLayout->addWidget(m_fpsSpin, 1, 1);

    mainLayout->addWidget(displayGroup);

    // Preview
    auto* previewGroup = new QGroupBox("Preview");
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_previewWidget = new QWidget;
    m_previewWidget->setMinimumHeight(100);
    m_previewWidget->setMaximumHeight(150);
    previewLayout->addWidget(m_previewWidget);
    mainLayout->addWidget(previewGroup);

    mainLayout->addStretch();

    // Apply/Reset buttons
    auto* btnLayout = new QHBoxLayout;
    auto* applyBtn = new QPushButton("Apply");
    auto* resetBtn = new QPushButton("Reset");
    btnLayout->addStretch();
    btnLayout->addWidget(resetBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    connect(applyBtn, &QPushButton::clicked, this, &MonitorSettingsPanel::apply);
    connect(resetBtn, &QPushButton::clicked, this, &MonitorSettingsPanel::refresh);

    connect(m_centerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        updatePreview();
        emit settingsChanged();
    });
    connect(m_bezelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
        updatePreview();
        emit settingsChanged();
    });
    connect(m_fovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() {
        updatePreview();
        emit settingsChanged();
    });
}

void MonitorSettingsPanel::refresh()
{
    auto* tm = m_dm->tripleMonitor();
    tm->detectMonitors();
    updateMonitorList();

    auto cfg = tm->config();
    m_bezelSpin->setValue(cfg.bezelCompensationMm);
    m_fovSpin->setValue(cfg.fovHorizontal);
    m_eyeDistSpin->setValue(cfg.eyeDistance);
    m_vsyncCheck->setChecked(cfg.verticalSync);
    m_fpsSpin->setValue(cfg.targetFps);
    m_arrangementCombo->setCurrentIndex(static_cast<int>(cfg.arrangement));

    updatePreview();
}

void MonitorSettingsPanel::updateMonitorList()
{
    m_centerCombo->clear();
    auto* tm = m_dm->tripleMonitor();
    for (int i = 0; i < tm->monitorCount(); ++i) {
        auto& m = tm->monitor(i);
        m_centerCombo->addItem(QString("%1: %2 (%3x%4)")
            .arg(i)
            .arg(m.name)
            .arg(m.geometry.width())
            .arg(m.geometry.height()));
    }

    if (tm->config().centerMonitorIndex >= 0) {
        m_centerCombo->setCurrentIndex(tm->config().centerMonitorIndex);
    }
}

void MonitorSettingsPanel::updatePreview()
{
    m_previewWidget->update();
}

void MonitorSettingsPanel::apply()
{
    ks::device::TripleMonitorConfig cfg;
    cfg.enabled = true;
    cfg.centerMonitorIndex = m_centerCombo->currentIndex();
    cfg.bezelCompensationMm = static_cast<float>(m_bezelSpin->value());
    cfg.fovHorizontal = static_cast<float>(m_fovSpin->value());
    cfg.eyeDistance = static_cast<float>(m_eyeDistSpin->value());
    cfg.verticalSync = m_vsyncCheck->isChecked();
    cfg.targetFps = m_fpsSpin->value();
    cfg.arrangement = static_cast<ks::device::TripleMonitorConfig::Arrangement>(
        m_arrangementCombo->currentIndex());

    m_dm->tripleMonitor()->setConfig(cfg);

    qInfo() << "MonitorSettings: Applied - FOV:" << cfg.fovHorizontal
            << "Bezel:" << cfg.bezelCompensationMm << "mm"
            << "Center:" << cfg.centerMonitorIndex;

    emit settingsChanged();
}

// ============================================================================
// InputSettingsPanel
// ============================================================================

InputSettingsPanel::InputSettingsPanel(ks::device::DeviceManager* dm, QWidget* parent)
    : QWidget(parent), m_dm(dm)
{
    buildUI();
    refresh();

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &InputSettingsPanel::updateAxisBars);
    m_pollTimer->start(50);
}

void InputSettingsPanel::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Device selection
    auto* deviceGroup = new QGroupBox("Input Device");
    auto* deviceLayout = new QVBoxLayout(deviceGroup);

    m_deviceCombo = new QComboBox;
    deviceLayout->addWidget(m_deviceCombo);

    auto* calibrateBtn = new QPushButton("Calibrate...");
    deviceLayout->addWidget(calibrateBtn);

    mainLayout->addWidget(deviceGroup);

    // Steering settings
    auto* steerGroup = new QGroupBox("Steering");
    auto* steerLayout = new QGridLayout(steerGroup);

    steerLayout->addWidget(new QLabel("Steering Range (deg):"), 0, 0);
    m_steerRangeSpin = new QDoubleSpinBox;
    m_steerRangeSpin->setRange(90, 1080);
    m_steerRangeSpin->setValue(900);
    m_steerRangeSpin->setSingleStep(10);
    steerLayout->addWidget(m_steerRangeSpin, 0, 1);

    mainLayout->addWidget(steerGroup);

    // Force Feedback
    auto* ffbGroup = new QGroupBox("Force Feedback");
    auto* ffbLayout = new QGridLayout(ffbGroup);

    m_ffbEnabledCheck = new QCheckBox("Enable FFB");
    m_ffbEnabledCheck->setChecked(true);
    ffbLayout->addWidget(m_ffbEnabledCheck, 0, 0);

    ffbLayout->addWidget(new QLabel("Strength:"), 1, 0);
    m_ffbStrengthSpin = new QDoubleSpinBox;
    m_ffbStrengthSpin->setRange(0, 1);
    m_ffbStrengthSpin->setValue(0.75);
    m_ffbStrengthSpin->setSingleStep(0.05);
    ffbLayout->addWidget(m_ffbStrengthSpin, 1, 1);

    mainLayout->addWidget(ffbGroup);

    // Axis visualization
    auto* axisGroup = new QGroupBox("Axis Visualization");
    auto* axisLayout = new QGridLayout(axisGroup);

    const char* axisNames[] = {"Steer", "Throttle", "Brake", "Clutch", "Handbrake", "FFB X", "FFB Y", "POV"};
    for (int i = 0; i < 8; ++i) {
        axisLayout->addWidget(new QLabel(axisNames[i]), i, 0);
        m_axisBars[i] = new QProgressBar;
        m_axisBars[i]->setRange(-100, 100);
        m_axisBars[i]->setValue(0);
        m_axisBars[i]->setTextVisible(false);
        axisLayout->addWidget(m_axisBars[i], i, 1);
    }

    mainLayout->addWidget(axisGroup);

    // Button mapping
    auto* buttonGroup = new QGroupBox("Button Mapping");
    auto* buttonLayout = new QVBoxLayout(buttonGroup);

    m_buttonTable = new QTableWidget(10, 3);
    m_buttonTable->setHorizontalHeaderLabels({"Button", "Function", "State"});
    m_buttonTable->horizontalHeader()->setStretchLastSection(true);
    buttonLayout->addWidget(m_buttonTable);

    mainLayout->addWidget(buttonGroup);

    mainLayout->addStretch();

    // Apply/Reset buttons
    auto* btnLayout = new QHBoxLayout;
    auto* applyBtn = new QPushButton("Apply");
    auto* resetBtn = new QPushButton("Reset");
    btnLayout->addStretch();
    btnLayout->addWidget(resetBtn);
    btnLayout->addWidget(applyBtn);
    mainLayout->addLayout(btnLayout);

    connect(applyBtn, &QPushButton::clicked, this, &InputSettingsPanel::apply);
    connect(resetBtn, &QPushButton::clicked, this, &InputSettingsPanel::refresh);
    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InputSettingsPanel::onDeviceSelected);
}

void InputSettingsPanel::refresh()
{
    auto* ri = m_dm->racingInput();
    ri->scanDevices();
    updateDeviceList();
}

void InputSettingsPanel::updateDeviceList()
{
    m_deviceCombo->clear();
    auto* ri = m_dm->racingInput();
    for (int i = 0; i < ri->deviceCount(); ++i) {
        m_deviceCombo->addItem(ri->device(i).name);
    }

    if (ri->selectedDevice() >= 0) {
        m_deviceCombo->setCurrentIndex(ri->selectedDevice());
    }
}

void InputSettingsPanel::onDeviceSelected(int index)
{
    if (index >= 0) {
        m_dm->racingInput()->selectDevice(index);
        updateAxisBars();
    }
}

void InputSettingsPanel::updateAxisBars()
{
    auto* ri = m_dm->racingInput();
    if (ri->selectedDevice() < 0) return;

    using AxisType = ks::device::AxisType;
    AxisType axes[] = {
        AxisType::SteeringWheel, AxisType::Throttle, AxisType::Brake, AxisType::Clutch,
        AxisType::Handbrake, AxisType::ForceFeedbackX, AxisType::ForceFeedbackY, AxisType::POV
    };

    for (int i = 0; i < 8; ++i) {
        float val = ri->getAxis(axes[i]);
        m_axisBars[i]->setValue(static_cast<int>(val * 100));
    }
}

void InputSettingsPanel::apply()
{
    auto* ri = m_dm->racingInput();

    ri->setSteeringRange(static_cast<float>(m_steerRangeSpin->value()));
    ri->setFFBEnabled(m_ffbEnabledCheck->isChecked());
    ri->setFFBStrength(static_cast<float>(m_ffbStrengthSpin->value()));

    qInfo() << "InputSettings: Applied - Range:" << m_steerRangeSpin->value()
            << "FFB:" << m_ffbEnabledCheck->isChecked()
            << "Strength:" << m_ffbStrengthSpin->value();

    emit settingsChanged();
}

// ============================================================================
// DeviceSettingsWidget
// ============================================================================

DeviceSettingsWidget::DeviceSettingsWidget(ks::device::DeviceManager* dm, QWidget* parent)
    : QWidget(parent), m_dm(dm)
{
    setWindowTitle("Device Settings");
    setMinimumSize(500, 600);
    buildUI();
    refresh();
}

void DeviceSettingsWidget::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget;

    m_monitorPanel = new MonitorSettingsPanel(m_dm);
    m_inputPanel = new InputSettingsPanel(m_dm);

    m_tabs->addTab(m_monitorPanel, "Triple Monitor");
    m_tabs->addTab(m_inputPanel, "Racing Input");

    mainLayout->addWidget(m_tabs);

    auto* bottomLayout = new QHBoxLayout;
    auto* saveBtn = new QPushButton("Save Profiles");
    auto* loadBtn = new QPushButton("Load Profiles");
    auto* closeBtn = new QPushButton("Close");

    bottomLayout->addStretch();
    bottomLayout->addWidget(saveBtn);
    bottomLayout->addWidget(loadBtn);
    bottomLayout->addWidget(closeBtn);

    mainLayout->addLayout(bottomLayout);

    connect(saveBtn, &QPushButton::clicked, this, &DeviceSettingsWidget::save);
    connect(loadBtn, &QPushButton::clicked, this, &DeviceSettingsWidget::load);
    connect(closeBtn, &QPushButton::clicked, this, &DeviceSettingsWidget::closed);
}

void DeviceSettingsWidget::refresh()
{
    m_monitorPanel->refresh();
    m_inputPanel->refresh();
}

void DeviceSettingsWidget::save()
{
    m_monitorPanel->apply();
    m_inputPanel->apply();

    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_dm->saveProfiles(path);
    qInfo() << "DeviceSettings: Profiles saved to" << path;
}

void DeviceSettingsWidget::load()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_dm->loadProfiles(path);
    refresh();
    qInfo() << "DeviceSettings: Profiles loaded from" << path;
}

} // namespace ks::ui
#endif // !KSENGINE_QT_FREE
