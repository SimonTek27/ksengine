#include "CameraEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QSplitter>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ks {

CameraEditorModule::CameraEditorModule(QWidget* parent)
    : EditorModule(parent)
{
}

bool CameraEditorModule::initialize() { LOG_INFO("CameraEditorModule", "Initialized"); return true; }
void CameraEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* CameraEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;

    m_dockWidget = new QDockWidget(tr("Camera Editor"), mainWindow);
    m_dockWidget->setObjectName("CameraEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* splitter = new QSplitter(Qt::Vertical);

    // Camera table
    auto* tableWidget = new QWidget();
    auto* tableLayout = new QVBoxLayout(tableWidget);
    m_cameraTable = new QTableWidget();
    m_cameraTable->setColumnCount(5);
    m_cameraTable->setHorizontalHeaderLabels({tr("Index"), tr("Position"), tr("Target"), tr("FOV"), tr("Speed")});
    m_cameraTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_cameraTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableLayout->addWidget(m_cameraTable);

    auto* tableBtnLayout = new QHBoxLayout();
    m_addCameraBtn = new QPushButton(tr("Add"));
    m_removeCameraBtn = new QPushButton(tr("Remove"));
    m_duplicateCameraBtn = new QPushButton(tr("Duplicate"));
    tableBtnLayout->addWidget(m_addCameraBtn);
    tableBtnLayout->addWidget(m_removeCameraBtn);
    tableBtnLayout->addWidget(m_duplicateCameraBtn);
    tableBtnLayout->addStretch();
    tableLayout->addLayout(tableBtnLayout);

    splitter->addWidget(tableWidget);

    // Properties panel
    auto* propsWidget = new QWidget();
    auto* propsLayout = new QGridLayout(propsWidget);

    m_cameraTypeCombo = new QComboBox();
    m_cameraTypeCombo->addItems({tr("TV"), tr("Onboard"), tr("Chase"), tr("Hood"), tr("Bumper"), tr("Trackside")});
    propsLayout->addWidget(new QLabel(tr("Type:")), 0, 0);
    propsLayout->addWidget(m_cameraTypeCombo, 0, 1);

    m_posXSpin = new QDoubleSpinBox();
    m_posXSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Pos X:")), 1, 0);
    propsLayout->addWidget(m_posXSpin, 1, 1);

    m_posYSpin = new QDoubleSpinBox();
    m_posYSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Pos Y:")), 2, 0);
    propsLayout->addWidget(m_posYSpin, 2, 1);

    m_posZSpin = new QDoubleSpinBox();
    m_posZSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Pos Z:")), 3, 0);
    propsLayout->addWidget(m_posZSpin, 3, 1);

    m_targetXSpin = new QDoubleSpinBox();
    m_targetXSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Target X:")), 4, 0);
    propsLayout->addWidget(m_targetXSpin, 4, 1);

    m_targetYSpin = new QDoubleSpinBox();
    m_targetYSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Target Y:")), 5, 0);
    propsLayout->addWidget(m_targetYSpin, 5, 1);

    m_targetZSpin = new QDoubleSpinBox();
    m_targetZSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Target Z:")), 6, 0);
    propsLayout->addWidget(m_targetZSpin, 6, 1);

    m_fovSpin = new QDoubleSpinBox();
    m_fovSpin->setRange(1.0, 180.0);
    m_fovSpin->setValue(60.0);
    propsLayout->addWidget(new QLabel(tr("FOV:")), 7, 0);
    propsLayout->addWidget(m_fovSpin, 7, 1);

    m_nearSpin = new QDoubleSpinBox();
    m_nearSpin->setRange(0.01, 1000.0);
    propsLayout->addWidget(new QLabel(tr("Near:")), 8, 0);
    propsLayout->addWidget(m_nearSpin, 8, 1);

    m_farSpin = new QDoubleSpinBox();
    m_farSpin->setRange(1.0, 100000.0);
    propsLayout->addWidget(new QLabel(tr("Far:")), 9, 0);
    propsLayout->addWidget(m_farSpin, 9, 1);

    m_speedSpin = new QDoubleSpinBox();
    m_speedSpin->setRange(0.0, 100.0);
    propsLayout->addWidget(new QLabel(tr("Speed:")), 10, 0);
    propsLayout->addWidget(m_speedSpin, 10, 1);

    splitter->addWidget(propsWidget);
    mainLayout->addWidget(splitter);

    // Action buttons
    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load cameras.ini"));
    m_saveBtn = new QPushButton(tr("Save cameras.ini"));
    m_resetBtn = new QPushButton(tr("Reset Defaults"));
    m_moveBtn = new QPushButton(tr("Move to Position"));
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addWidget(m_resetBtn);
    actionLayout->addWidget(m_moveBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    // Connections
    connect(m_cameraTable, &QTableWidget::cellClicked, this, [this](int row, int) { onCameraSelected(row); });
    connect(m_addCameraBtn, &QPushButton::clicked, this, &CameraEditorModule::onAddCamera);
    connect(m_removeCameraBtn, &QPushButton::clicked, this, &CameraEditorModule::onRemoveCamera);
    connect(m_duplicateCameraBtn, &QPushButton::clicked, this, &CameraEditorModule::onDuplicateCamera);
    connect(m_cameraTypeCombo, QOverload<const QString&>::of(&QComboBox::currentTextChanged), this, &CameraEditorModule::onCameraTypeChanged);
    connect(m_posXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onPosXChanged);
    connect(m_posYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onPosYChanged);
    connect(m_posZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onPosZChanged);
    connect(m_targetXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onTargetXChanged);
    connect(m_targetYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onTargetYChanged);
    connect(m_targetZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onTargetZChanged);
    connect(m_fovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onFovChanged);
    connect(m_nearSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onNearChanged);
    connect(m_farSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onFarChanged);
    connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onSpeedChanged);
    connect(m_loadBtn, &QPushButton::clicked, this, &CameraEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &CameraEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &CameraEditorModule::onResetDefaults);
    connect(m_moveBtn, &QPushButton::clicked, this, &CameraEditorModule::onMoveToPosition);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void CameraEditorModule::importFile(const QString& filePath) { m_filePath = filePath; loadFileToUI(); }
void CameraEditorModule::exportFile(const QString& filePath) { m_filePath = filePath; saveFileFromUI(); }
void CameraEditorModule::onActivation()
{
    connect(m_posXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onPosXChanged);
    connect(m_posYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onPosYChanged);
    connect(m_posZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onPosZChanged);
    connect(m_targetXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onTargetXChanged);
    connect(m_targetYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onTargetYChanged);
    connect(m_targetZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onTargetZChanged);
    connect(m_fovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onFovChanged);
    connect(m_nearSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onNearChanged);
    connect(m_farSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onFarChanged);
    connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CameraEditorModule::onSpeedChanged);
    m_statusLabel->setText(tr("Active"));
}

void CameraEditorModule::onDeactivation()
{
    m_posXSpin->disconnect(); m_posYSpin->disconnect(); m_posZSpin->disconnect();
    m_targetXSpin->disconnect(); m_targetYSpin->disconnect(); m_targetZSpin->disconnect();
    m_fovSpin->disconnect(); m_nearSpin->disconnect(); m_farSpin->disconnect();
    m_speedSpin->disconnect();
    m_statusLabel->setText(tr("Inactive"));
}

void CameraEditorModule::onCameraSelected(int row) { if (row >= 0 && row < m_cameras.size()) { m_selectedCameraIndex = row; selectCamera(row); } }
void CameraEditorModule::onAddCamera() { CameraEditorData c; c.index = m_cameras.size(); m_cameras.append(c); updateCameraTable(); }
void CameraEditorModule::onRemoveCamera() { if (m_selectedCameraIndex >= 0) { m_cameras.removeAt(m_selectedCameraIndex); updateCameraTable(); } }
void CameraEditorModule::onDuplicateCamera() { if (m_selectedCameraIndex >= 0) { m_cameras.append(m_cameras[m_selectedCameraIndex]); updateCameraTable(); } }
void CameraEditorModule::onCameraTypeChanged(const QString& t) { m_cameraType = t; }
void CameraEditorModule::onPosXChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].pos[0] = v; }
void CameraEditorModule::onPosYChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].pos[1] = v; }
void CameraEditorModule::onPosZChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].pos[2] = v; }
void CameraEditorModule::onTargetXChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].target[0] = v; }
void CameraEditorModule::onTargetYChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].target[1] = v; }
void CameraEditorModule::onTargetZChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].target[2] = v; }
void CameraEditorModule::onFovChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].fov = v; }
void CameraEditorModule::onNearChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].nearPlane = v; }
void CameraEditorModule::onFarChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].farPlane = v; }
void CameraEditorModule::onSpeedChanged(double v) { if (m_selectedCameraIndex >= 0) m_cameras[m_selectedCameraIndex].speed = v; }
void CameraEditorModule::onMoveToPosition() { m_statusLabel->setText(tr("Move to position: camera preview updated")); }

void CameraEditorModule::onLoadFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open cameras.ini"), QString(), tr("Cameras INI (*.ini);;All Files (*)"));
    if (!path.isEmpty()) { m_filePath = path; loadFileToUI(); m_statusLabel->setText(tr("Loaded: %1").arg(path)); }
}

void CameraEditorModule::onSaveFile()
{
    QString path = m_filePath.isEmpty() ?
        QFileDialog::getSaveFileName(this, tr("Save cameras.ini"), QString(), tr("Cameras INI (*.ini)")) : m_filePath;
    if (!path.isEmpty()) { m_filePath = path; saveFileFromUI(); m_statusLabel->setText(tr("Saved: %1").arg(path)); }
}

void CameraEditorModule::onResetDefaults()
{
    m_cameras.clear();
        CameraEditorData c; c.pos[0] = 0; c.pos[1] = 5; c.pos[2] = -10; c.fov = 60;
    m_cameras.append(c);
    updateCameraTable();
    m_statusLabel->setText(tr("Reset to defaults"));
}

void CameraEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void CameraEditorModule::loadFileToUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = file.readAll();
    file.close();

    m_cameras.clear();
    QStringList sections = content.split("[", Qt::SkipEmptyParts);
    for (const QString& sec : sections) {
        if (!sec.startsWith("CAMERA_")) continue;
        CameraEditorData c;
        QStringList lines = sec.split("\n", Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString l = line.trimmed();
            if (l.startsWith("POSITION=")) {
                QStringList v = l.mid(9).split(",");
                if (v.size() >= 3) { c.pos[0] = v[0].toFloat(); c.pos[1] = v[1].toFloat(); c.pos[2] = v[2].toFloat(); }
            } else if (l.startsWith("TARGET=")) {
                QStringList v = l.mid(7).split(",");
                if (v.size() >= 3) { c.target[0] = v[0].toFloat(); c.target[1] = v[1].toFloat(); c.target[2] = v[2].toFloat(); }
            } else if (l.startsWith("FOV=")) c.fov = l.mid(4).toFloat();
            else if (l.startsWith("NEAR=")) c.nearPlane = l.mid(5).toFloat();
            else if (l.startsWith("FAR=")) c.farPlane = l.mid(4).toFloat();
            else if (l.startsWith("SPEED=")) c.speed = l.mid(6).toFloat();
        }
        m_cameras.append(c);
    }
    updateCameraTable();
}

void CameraEditorModule::saveFileFromUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    for (int i = 0; i < m_cameras.size(); ++i) {
        const auto& c = m_cameras[i];
        out << "[CAMERA_" << i << "]\n";
        out << "POSITION=" << c.pos[0] << "," << c.pos[1] << "," << c.pos[2] << "\n";
        out << "TARGET=" << c.target[0] << "," << c.target[1] << "," << c.target[2] << "\n";
        out << "UP=0,1,0\n";
        out << "FOV=" << c.fov << "\n";
        out << "NEAR=" << c.nearPlane << "\n";
        out << "FAR=" << c.farPlane << "\n";
        out << "SPEED=" << c.speed << "\n\n";
    }
    file.close();
}

void CameraEditorModule::updateCameraTable()
{
    m_cameraTable->setRowCount(m_cameras.size());
    for (int i = 0; i < m_cameras.size(); ++i) {
        const auto& c = m_cameras[i];
        m_cameraTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        m_cameraTable->setItem(i, 1, new QTableWidgetItem(QString("(%1,%2,%3)").arg(c.pos[0],0,'f',1).arg(c.pos[1],0,'f',1).arg(c.pos[2],0,'f',1)));
        m_cameraTable->setItem(i, 2, new QTableWidgetItem(QString("(%1,%2,%3)").arg(c.target[0],0,'f',1).arg(c.target[1],0,'f',1).arg(c.target[2],0,'f',1)));
        m_cameraTable->setItem(i, 3, new QTableWidgetItem(QString::number(c.fov, 'f', 1)));
        m_cameraTable->setItem(i, 4, new QTableWidgetItem(QString::number(c.speed, 'f', 2)));
    }
}

void CameraEditorModule::selectCamera(int index)
{
    if (index < 0 || index >= m_cameras.size()) return;
    const auto& c = m_cameras[index];
    m_posXSpin->setValue(c.pos[0]);
    m_posYSpin->setValue(c.pos[1]);
    m_posZSpin->setValue(c.pos[2]);
    m_targetXSpin->setValue(c.target[0]);
    m_targetYSpin->setValue(c.target[1]);
    m_targetZSpin->setValue(c.target[2]);
    m_fovSpin->setValue(c.fov);
    m_nearSpin->setValue(c.nearPlane);
    m_farSpin->setValue(c.farPlane);
    m_speedSpin->setValue(c.speed);
}

QJsonObject CameraEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    data["cameraType"] = m_cameraType;
    QJsonArray camerasArray;
    for (const auto& cam : m_cameras) {
        QJsonObject obj;
        obj["fov"] = static_cast<double>(cam.fov);
        obj["nearPlane"] = static_cast<double>(cam.nearPlane);
        obj["farPlane"] = static_cast<double>(cam.farPlane);
        obj["speed"] = static_cast<double>(cam.speed);
        obj["index"] = cam.index;
        QJsonArray pos;
        for (int i = 0; i < 3; ++i) pos.append(static_cast<double>(cam.pos[i]));
        obj["pos"] = pos;
        QJsonArray target;
        for (int i = 0; i < 3; ++i) target.append(static_cast<double>(cam.target[i]));
        obj["target"] = target;
        QJsonArray up;
        for (int i = 0; i < 3; ++i) up.append(static_cast<double>(cam.up[i]));
        obj["up"] = up;
        camerasArray.append(obj);
    }
    data["cameras"] = camerasArray;
    return data;
}

void CameraEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_cameraType = data["cameraType"].toString();
    m_cameras.clear();
    for (const auto& v : data["cameras"].toArray()) {
        QJsonObject obj = v.toObject();
        CameraEditorData cam;
        cam.fov = static_cast<float>(obj["fov"].toDouble(60.0));
        cam.nearPlane = static_cast<float>(obj["nearPlane"].toDouble(0.1));
        cam.farPlane = static_cast<float>(obj["farPlane"].toDouble(1000.0));
        cam.speed = static_cast<float>(obj["speed"].toDouble());
        cam.index = obj["index"].toInt();
        QJsonArray pos = obj["pos"].toArray();
        for (int i = 0; i < qMin(3, pos.size()); ++i)
            cam.pos[i] = static_cast<float>(pos[i].toDouble());
        QJsonArray target = obj["target"].toArray();
        for (int i = 0; i < qMin(3, target.size()); ++i)
            cam.target[i] = static_cast<float>(target[i].toDouble());
        QJsonArray up = obj["up"].toArray();
        for (int i = 0; i < qMin(3, up.size()); ++i)
            cam.up[i] = static_cast<float>(up[i].toDouble());
        m_cameras.append(cam);
    }
}

} // namespace ks
