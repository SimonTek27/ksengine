#include "TrackCameraEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QSplitter>
#include <QTextStream>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>

namespace ks {

TrackCameraEditorModule::TrackCameraEditorModule(QWidget* parent) : EditorModule(parent) {}
bool TrackCameraEditorModule::initialize() { LOG_INFO("TrackCameraEditorModule", "Initialized"); return true; }
void TrackCameraEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText("Shut down"); }

QDockWidget* TrackCameraEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget("Camera Editor", mainWindow);
    m_dockWidget->setObjectName("CameraTrackEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);
    auto* splitter = new QSplitter(Qt::Vertical);

    // Table
    auto* tableWidget = new QWidget(); auto* tableLayout = new QVBoxLayout(tableWidget);
    m_cameraTable = new QTableWidget(); m_cameraTable->setColumnCount(3);
    m_cameraTable->setHorizontalHeaderLabels({"ID", "Name", "Type"});
    m_cameraTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_cameraTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableLayout->addWidget(m_cameraTable);
    auto* tableBtns = new QHBoxLayout();
    m_addBtn = new QPushButton("Add"); m_removeBtn = new QPushButton("Remove");
    tableBtns->addWidget(m_addBtn); tableBtns->addWidget(m_removeBtn); tableBtns->addStretch();
    tableLayout->addLayout(tableBtns);
    splitter->addWidget(tableWidget);

    // Props
    auto* propsWidget = new QWidget();

    auto* typeGroup = new QGroupBox("Type & Name");
    auto* typeLayout = new QFormLayout(typeGroup);
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({"TV", "Onboard", "Helicopter", "Chase", "Cockpit"});
    typeLayout->addRow("Type:", m_typeCombo);
    m_activeCheck = new QCheckBox("Active"); m_activeCheck->setChecked(true);
    typeLayout->addRow("", m_activeCheck);
    mainLayout->addWidget(typeGroup);

    auto* propsLayout = new QGridLayout(propsWidget);

    auto* posGroup = new QGroupBox("Position");
    auto* posLayout = new QGridLayout(posGroup);
    m_posXSpin = new QDoubleSpinBox(); m_posXSpin->setRange(-100000, 100000); m_posXSpin->setDecimals(2);
    posLayout->addWidget(new QLabel("X:"), 0, 0); posLayout->addWidget(m_posXSpin, 0, 1);
    m_posYSpin = new QDoubleSpinBox(); m_posYSpin->setRange(-100000, 100000); m_posYSpin->setDecimals(2);
    posLayout->addWidget(new QLabel("Y:"), 1, 0); posLayout->addWidget(m_posYSpin, 1, 1);
    m_posZSpin = new QDoubleSpinBox(); m_posZSpin->setRange(-100000, 100000); m_posZSpin->setDecimals(2);
    posLayout->addWidget(new QLabel("Z:"), 2, 0); posLayout->addWidget(m_posZSpin, 2, 1);

    auto* lookGroup = new QGroupBox("Look At");
    auto* lookLayout = new QGridLayout(lookGroup);
    m_lookXSpin = new QDoubleSpinBox(); m_lookXSpin->setRange(-100000, 100000); m_lookXSpin->setDecimals(2);
    lookLayout->addWidget(new QLabel("X:"), 0, 0); lookLayout->addWidget(m_lookXSpin, 0, 1);
    m_lookYSpin = new QDoubleSpinBox(); m_lookYSpin->setRange(-100000, 100000); m_lookYSpin->setDecimals(2);
    lookLayout->addWidget(new QLabel("Y:"), 1, 0); lookLayout->addWidget(m_lookYSpin, 1, 1);
    m_lookZSpin = new QDoubleSpinBox(); m_lookZSpin->setRange(-100000, 100000); m_lookZSpin->setDecimals(2);
    lookLayout->addWidget(new QLabel("Z:"), 2, 0); lookLayout->addWidget(m_lookZSpin, 2, 1);

    auto* lensGroup = new QGroupBox("Lens");
    auto* lensLayout = new QFormLayout(lensGroup);
    m_fovSpin = new QDoubleSpinBox(); m_fovSpin->setRange(0.1, 180.0); m_fovSpin->setValue(45.0); m_fovSpin->setSuffix(" deg");
    lensLayout->addRow("FOV:", m_fovSpin);
    m_nearSpin = new QDoubleSpinBox(); m_nearSpin->setRange(0.001, 100.0); m_nearSpin->setValue(0.1);
    lensLayout->addRow("Near Clip:", m_nearSpin);
    m_farSpin = new QDoubleSpinBox(); m_farSpin->setRange(1.0, 100000.0); m_farSpin->setValue(5000.0);
    lensLayout->addRow("Far Clip:", m_farSpin);

    auto* orientGroup = new QGroupBox("Orientation");
    auto* orientLayout = new QFormLayout(orientGroup);
    m_pitchSpin = new QDoubleSpinBox(); m_pitchSpin->setRange(-180, 180);
    orientLayout->addRow("Pitch:", m_pitchSpin);
    m_yawSpin = new QDoubleSpinBox(); m_yawSpin->setRange(-180, 180);
    orientLayout->addRow("Yaw:", m_yawSpin);
    m_rollSpin = new QDoubleSpinBox(); m_rollSpin->setRange(-180, 180);
    orientLayout->addRow("Roll:", m_rollSpin);

    auto* motionGroup = new QGroupBox("Motion");
    auto* motionLayout = new QFormLayout(motionGroup);
    m_forwardSpin = new QDoubleSpinBox(); m_forwardSpin->setRange(-100, 100); m_forwardSpin->setDecimals(2);
    motionLayout->addRow("Forward Offset:", m_forwardSpin);
    m_upSpin = new QDoubleSpinBox(); m_upSpin->setRange(-100, 100); m_upSpin->setDecimals(2);
    motionLayout->addRow("Up Offset:", m_upSpin);
    m_maxSpeedSpin = new QDoubleSpinBox(); m_maxSpeedSpin->setRange(0, 1000); m_maxSpeedSpin->setDecimals(1); m_maxSpeedSpin->setSuffix(" km/h");
    motionLayout->addRow("Max Speed:", m_maxSpeedSpin);
    m_shakeCombo = new QComboBox();
    m_shakeCombo->addItems({"None", "Low", "Medium", "High", "Full"});
    motionLayout->addRow("Shake:", m_shakeCombo);

    auto* grid = new QGridLayout();
    grid->addWidget(posGroup, 0, 0); grid->addWidget(lookGroup, 0, 1);
    grid->addWidget(lensGroup, 1, 0); grid->addWidget(orientGroup, 1, 1);
    grid->addWidget(motionGroup, 2, 0); grid->addWidget(typeGroup, 2, 1);
    propsWidget->setLayout(grid);
    splitter->addWidget(propsWidget);

    mainLayout->addWidget(splitter);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton("Load cameras.ini"); m_saveBtn = new QPushButton("Save cameras.ini"); m_resetBtn = new QPushButton("Reset");
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel("Ready"); mainLayout->addWidget(m_statusLabel);

    connect(m_cameraTable, &QTableWidget::cellClicked, this, [this](int r, int) { onCameraSelected(r); });
    connect(m_addBtn, &QPushButton::clicked, this, &TrackCameraEditorModule::onAddCamera);
    connect(m_removeBtn, &QPushButton::clicked, this, &TrackCameraEditorModule::onRemoveCamera);
    connect(m_loadBtn, &QPushButton::clicked, this, &TrackCameraEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &TrackCameraEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &TrackCameraEditorModule::onResetDefaults);

    // Wire property slot connections
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TrackCameraEditorModule::onTypeChanged);
    connect(m_posXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onPosXChanged);
    connect(m_posYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onPosYChanged);
    connect(m_posZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onPosZChanged);
    connect(m_lookXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onLookXChanged);
    connect(m_lookYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onLookYChanged);
    connect(m_lookZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onLookZChanged);
    connect(m_fovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onFovChanged);
    connect(m_nearSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onNearClipChanged);
    connect(m_farSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onFarClipChanged);
    connect(m_pitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onPitchChanged);
    connect(m_yawSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onYawChanged);
    connect(m_rollSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onRollChanged);
    connect(m_forwardSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onForwardOffsetChanged);
    connect(m_upSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onUpOffsetChanged);
    connect(m_maxSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackCameraEditorModule::onMaxSpeedChanged);
    connect(m_shakeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TrackCameraEditorModule::onShakeChanged);
    connect(m_activeCheck, &QCheckBox::toggled, this, &TrackCameraEditorModule::onActiveChanged);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void TrackCameraEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void TrackCameraEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }

void TrackCameraEditorModule::onActivation()
{
    m_statusLabel->setText("Active");
}

void TrackCameraEditorModule::onDeactivation()
{
    m_statusLabel->setText("Inactive");
}

void TrackCameraEditorModule::onCameraSelected(int r)
{
    if (r >= 0 && r < m_cameras.size()) {
        m_selectedIndex = r;
        selectCamera(r);
    }
}

void TrackCameraEditorModule::onAddCamera()
{
    TrackCamera c;
    c.id = m_cameras.size();
    c.name = QString("Camera_%1").arg(c.id + 1);
    m_cameras.append(c);
    updateTable();
}

void TrackCameraEditorModule::onRemoveCamera()
{
    if (m_selectedIndex >= 0) {
        m_cameras.removeAt(m_selectedIndex);
        m_selectedIndex = -1;
        updateTable();
    }
}

void TrackCameraEditorModule::onTypeChanged(int index)
{
    if (m_selectedIndex >= 0) {
        m_cameras[m_selectedIndex].type = static_cast<TrackCamera::Type>(index);
        updatePropEditableState();
        updateTable();
    }
}

void TrackCameraEditorModule::onPosXChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].position[0] = v; }
void TrackCameraEditorModule::onPosYChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].position[1] = v; }
void TrackCameraEditorModule::onPosZChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].position[2] = v; }
void TrackCameraEditorModule::onLookXChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].lookAt[0] = v; }
void TrackCameraEditorModule::onLookYChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].lookAt[1] = v; }
void TrackCameraEditorModule::onLookZChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].lookAt[2] = v; }
void TrackCameraEditorModule::onFovChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].fov = v; }
void TrackCameraEditorModule::onNearClipChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].nearClip = v; }
void TrackCameraEditorModule::onFarClipChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].farClip = v; }
void TrackCameraEditorModule::onPitchChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].pitch = v; }
void TrackCameraEditorModule::onYawChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].yaw = v; }
void TrackCameraEditorModule::onRollChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].roll = v; }
void TrackCameraEditorModule::onForwardOffsetChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].forwardOffset = v; }
void TrackCameraEditorModule::onUpOffsetChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].upOffset = v; }
void TrackCameraEditorModule::onMaxSpeedChanged(double v) { if (m_selectedIndex >= 0) m_cameras[m_selectedIndex].maxSpeed = v; }

void TrackCameraEditorModule::onShakeChanged(int index)
{
    if (m_selectedIndex >= 0) {
        m_cameras[m_selectedIndex].shake = static_cast<TrackCamera::Shake>(index);
    }
}

void TrackCameraEditorModule::onActiveChanged(bool checked)
{
    if (m_selectedIndex >= 0) {
        m_cameras[m_selectedIndex].isActive = checked;
    }
}

void TrackCameraEditorModule::onLoadFile()
{
    QString p = QFileDialog::getOpenFileName(this, "Open cameras.ini", QString(), "Camera INI (*.ini)");
    if (!p.isEmpty()) { m_filePath = p; loadFileToUI(); m_statusLabel->setText("Loaded: " + p); }
}

void TrackCameraEditorModule::onSaveFile()
{
    QString p = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, "Save cameras.ini", QString(), "Camera INI (*.ini)") : m_filePath;
    if (!p.isEmpty()) { m_filePath = p; saveFileFromUI(); m_statusLabel->setText("Saved: " + p); }
}

void TrackCameraEditorModule::onResetDefaults()
{
    m_cameras.clear();

    TrackCamera tv1; tv1.name = "TV1"; tv1.type = TrackCamera::Type::TV; tv1.position[0] = 0; tv1.position[1] = 5; tv1.position[2] = -30; tv1.lookAt[0] = 0; tv1.fov = 45;
    m_cameras.append(tv1);
    TrackCamera tv2; tv2.name = "TV2"; tv2.type = TrackCamera::Type::TV; tv2.position[0] = 50; tv2.position[1] = 8; tv2.position[2] = -20; tv2.lookAt[0] = 50; tv2.fov = 55;
    m_cameras.append(tv2);
    TrackCamera heli; heli.name = "Helicopter"; heli.type = TrackCamera::Type::Heli; heli.position[0] = 0; heli.position[1] = 80; heli.position[2] = -60; heli.fov = 70;
    m_cameras.append(heli);
    TrackCamera onboard; onboard.name = "Onboard"; onboard.type = TrackCamera::Type::Onboard; onboard.forwardOffset = 1.5f; onboard.upOffset = 0.3f; onboard.fov = 55;
    m_cameras.append(onboard);

    m_selectedIndex = -1;
    updateTable();
    m_statusLabel->setText("Reset to defaults");
}

void TrackCameraEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText("UI Ready"); }

void TrackCameraEditorModule::loadFileToUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString c = file.readAll(); file.close();
    m_cameras.clear();

    QStringList sections = c.split("[", Qt::SkipEmptyParts);
    for (const QString& sec : sections) {
        if (!sec.startsWith("CAMERA_")) continue;
        TrackCamera cam;
        for (const QString& line : sec.split("\n")) {
            QString l = line.trimmed();
            if (l.startsWith("NAME=")) cam.name = l.mid(5);
            else if (l.startsWith("TYPE=")) cam.type = typeFromString(l.mid(5));
            else if (l.startsWith("POSITION=")) {
                QStringList v = l.mid(9).split(",");
                if (v.size() >= 3) { cam.position[0] = v[0].toFloat(); cam.position[1] = v[1].toFloat(); cam.position[2] = v[2].toFloat(); }
            }
            else if (l.startsWith("LOOK_AT=")) {
                QStringList v = l.mid(8).split(",");
                if (v.size() >= 3) { cam.lookAt[0] = v[0].toFloat(); cam.lookAt[1] = v[1].toFloat(); cam.lookAt[2] = v[2].toFloat(); }
            }
            else if (l.startsWith("FOV=")) cam.fov = l.mid(4).toFloat();
            else if (l.startsWith("NEAR_CLIP=")) cam.nearClip = l.mid(10).toFloat();
            else if (l.startsWith("FAR_CLIP=")) cam.farClip = l.mid(9).toFloat();
            else if (l.startsWith("PITCH=")) cam.pitch = l.mid(6).toFloat();
            else if (l.startsWith("YAW=")) cam.yaw = l.mid(4).toFloat();
            else if (l.startsWith("ROLL=")) cam.roll = l.mid(5).toFloat();
            else if (l.startsWith("FORWARD_OFFSET=")) cam.forwardOffset = l.mid(15).toFloat();
            else if (l.startsWith("UP_OFFSET=")) cam.upOffset = l.mid(10).toFloat();
            else if (l.startsWith("MAX_SPEED=")) cam.maxSpeed = l.mid(10).toFloat();
            else if (l.startsWith("SHAKE=")) cam.shake = shakeFromString(l.mid(6));
            else if (l.startsWith("IS_ACTIVE=")) cam.isActive = (l.mid(10).toInt() != 0);
        }
        m_cameras.append(cam);
    }

    for (int i = 0; i < m_cameras.size(); ++i) m_cameras[i].id = i;
    m_selectedIndex = -1;
    updateTable();
}

void TrackCameraEditorModule::saveFileFromUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&file);
    o << "; Track Camera Definitions\n";
    o << "; Generated by ksEditor\n\n";

    for (int i = 0; i < m_cameras.size(); ++i) {
        const auto& c = m_cameras[i];
        o << "[CAMERA_" << i << "]\n";
        o << "NAME=" << c.name << "\n";
        o << "TYPE=" << typeToString(c.type) << "\n";
        o << "POSITION=" << c.position[0] << "," << c.position[1] << "," << c.position[2] << "\n";
        o << "LOOK_AT=" << c.lookAt[0] << "," << c.lookAt[1] << "," << c.lookAt[2] << "\n";
        o << "FOV=" << c.fov << "\n";
        o << "NEAR_CLIP=" << c.nearClip << "\n";
        o << "FAR_CLIP=" << c.farClip << "\n";
        o << "PITCH=" << c.pitch << "\n";
        o << "YAW=" << c.yaw << "\n";
        o << "ROLL=" << c.roll << "\n";
        o << "FORWARD_OFFSET=" << c.forwardOffset << "\n";
        o << "UP_OFFSET=" << c.upOffset << "\n";
        o << "MAX_SPEED=" << c.maxSpeed << "\n";
        o << "SHAKE=" << shakeToString(c.shake) << "\n";
        o << "IS_ACTIVE=" << (c.isActive ? 1 : 0) << "\n\n";
    }
    file.close();
}

void TrackCameraEditorModule::updateTable()
{
    m_cameraTable->setRowCount(m_cameras.size());
    for (int i = 0; i < m_cameras.size(); ++i) {
        m_cameraTable->setItem(i, 0, new QTableWidgetItem(QString::number(m_cameras[i].id)));
        m_cameraTable->setItem(i, 1, new QTableWidgetItem(m_cameras[i].name));
        m_cameraTable->setItem(i, 2, new QTableWidgetItem(typeToString(m_cameras[i].type)));
    }
}

void TrackCameraEditorModule::selectCamera(int idx)
{
    if (idx < 0 || idx >= m_cameras.size()) return;
    const auto& c = m_cameras[idx];

    m_typeCombo->setCurrentIndex(static_cast<int>(c.type));
    m_posXSpin->setValue(c.position[0]); m_posYSpin->setValue(c.position[1]); m_posZSpin->setValue(c.position[2]);
    m_lookXSpin->setValue(c.lookAt[0]); m_lookYSpin->setValue(c.lookAt[1]); m_lookZSpin->setValue(c.lookAt[2]);
    m_fovSpin->setValue(c.fov);
    m_nearSpin->setValue(c.nearClip); m_farSpin->setValue(c.farClip);
    m_pitchSpin->setValue(c.pitch); m_yawSpin->setValue(c.yaw); m_rollSpin->setValue(c.roll);
    m_forwardSpin->setValue(c.forwardOffset); m_upSpin->setValue(c.upOffset);
    m_maxSpeedSpin->setValue(c.maxSpeed);
    m_shakeCombo->setCurrentIndex(static_cast<int>(c.shake));
    m_activeCheck->setChecked(c.isActive);
    updatePropEditableState();
}

void TrackCameraEditorModule::updatePropEditableState()
{
    bool isDynamic = (m_selectedIndex >= 0 && m_cameras[m_selectedIndex].type == TrackCamera::Type::Onboard);
    m_forwardSpin->setEnabled(isDynamic);
    m_upSpin->setEnabled(isDynamic);
    m_maxSpeedSpin->setEnabled(isDynamic);
    m_shakeCombo->setEnabled(isDynamic);

    bool isStatic = (m_selectedIndex >= 0 && (m_cameras[m_selectedIndex].type == TrackCamera::Type::TV || m_cameras[m_selectedIndex].type == TrackCamera::Type::Heli));
    m_posXSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_posYSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_posZSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_lookXSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_lookYSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_lookZSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_pitchSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_yawSpin->setEnabled(isStatic || m_selectedIndex < 0);
    m_rollSpin->setEnabled(isStatic || m_selectedIndex < 0);
}

TrackCamera::Type TrackCameraEditorModule::typeFromString(const QString& s)
{
    QString u = s.toUpper();
    if (u == "TV") return TrackCamera::Type::TV;
    if (u == "ONBOARD") return TrackCamera::Type::Onboard;
    if (u == "HELICOPTER") return TrackCamera::Type::Heli;
    if (u == "CHASE") return TrackCamera::Type::Chase;
    if (u == "COCKPIT") return TrackCamera::Type::Cockpit;
    return TrackCamera::Type::TV;
}

QString TrackCameraEditorModule::typeToString(TrackCamera::Type t)
{
    switch (t) {
        case TrackCamera::Type::TV: return "TV";
        case TrackCamera::Type::Onboard: return "Onboard";
        case TrackCamera::Type::Heli: return "Helicopter";
        case TrackCamera::Type::Chase: return "Chase";
        case TrackCamera::Type::Cockpit: return "Cockpit";
    }
    return "TV";
}

TrackCamera::Shake TrackCameraEditorModule::shakeFromString(const QString& s)
{
    QString u = s.toUpper();
    if (u == "NONE") return TrackCamera::Shake::None;
    if (u == "LOW") return TrackCamera::Shake::Low;
    if (u == "MEDIUM") return TrackCamera::Shake::Medium;
    if (u == "HIGH") return TrackCamera::Shake::High;
    if (u == "FULL") return TrackCamera::Shake::Full;
    return TrackCamera::Shake::Medium;
}

QString TrackCameraEditorModule::shakeToString(TrackCamera::Shake s)
{
    switch (s) {
        case TrackCamera::Shake::None: return "None";
        case TrackCamera::Shake::Low: return "Low";
        case TrackCamera::Shake::Medium: return "Medium";
        case TrackCamera::Shake::High: return "High";
        case TrackCamera::Shake::Full: return "Full";
    }
    return "Medium";
}

QJsonObject TrackCameraEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    QJsonArray camerasArray;
    for (const auto& cam : m_cameras) {
        QJsonObject obj;
        obj["id"] = cam.id;
        obj["type"] = static_cast<int>(cam.type);
        obj["name"] = cam.name;
        obj["fov"] = static_cast<double>(cam.fov);
        obj["nearClip"] = static_cast<double>(cam.nearClip);
        obj["farClip"] = static_cast<double>(cam.farClip);
        obj["pitch"] = static_cast<double>(cam.pitch);
        obj["yaw"] = static_cast<double>(cam.yaw);
        obj["roll"] = static_cast<double>(cam.roll);
        obj["forwardOffset"] = static_cast<double>(cam.forwardOffset);
        obj["upOffset"] = static_cast<double>(cam.upOffset);
        obj["maxSpeed"] = static_cast<double>(cam.maxSpeed);
        obj["shake"] = static_cast<double>(cam.shake);
        obj["isActive"] = cam.isActive;
        QJsonArray pos;
        for (int i = 0; i < 3; ++i) pos.append(static_cast<double>(cam.position[i]));
        obj["position"] = pos;
        QJsonArray lookAt;
        for (int i = 0; i < 3; ++i) lookAt.append(static_cast<double>(cam.lookAt[i]));
        obj["lookAt"] = lookAt;
        camerasArray.append(obj);
    }
    data["cameras"] = camerasArray;
    return data;
}

void TrackCameraEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_cameras.clear();
    for (const auto& v : data["cameras"].toArray()) {
        QJsonObject obj = v.toObject();
        TrackCamera cam;
        cam.id = obj["id"].toInt();
        cam.type = static_cast<TrackCamera::Type>(obj["type"].toInt());
        cam.name = obj["name"].toString();
        cam.fov = static_cast<float>(obj["fov"].toDouble(60.0));
        cam.nearClip = static_cast<float>(obj["nearClip"].toDouble(0.1));
        cam.farClip = static_cast<float>(obj["farClip"].toDouble(1000.0));
        cam.pitch = static_cast<float>(obj["pitch"].toDouble());
        cam.yaw = static_cast<float>(obj["yaw"].toDouble());
        cam.roll = static_cast<float>(obj["roll"].toDouble());
        cam.forwardOffset = static_cast<float>(obj["forwardOffset"].toDouble());
        cam.upOffset = static_cast<float>(obj["upOffset"].toDouble());
        cam.maxSpeed = static_cast<float>(obj["maxSpeed"].toDouble());
        cam.shake = static_cast<TrackCamera::Shake>(obj["shake"].toInt());
        cam.isActive = obj["isActive"].toBool(true);
        QJsonArray pos = obj["position"].toArray();
        for (int i = 0; i < qMin(3, pos.size()); ++i)
            cam.position[i] = static_cast<float>(pos[i].toDouble());
        QJsonArray lookAt = obj["lookAt"].toArray();
        for (int i = 0; i < qMin(3, lookAt.size()); ++i)
            cam.lookAt[i] = static_cast<float>(lookAt[i].toDouble());
        m_cameras.append(cam);
    }
}

} // namespace ks
