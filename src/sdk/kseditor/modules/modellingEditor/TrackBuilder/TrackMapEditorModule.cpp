#include "TrackMapEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QTextStream>
#include <QFileInfo>
#include <QPixmap>

namespace ks {

TrackMapEditorModule::TrackMapEditorModule(QWidget* parent) : EditorModule(parent) {}
bool TrackMapEditorModule::initialize() { LOG_INFO("TrackMapEditorModule", "Initialized"); return true; }
void TrackMapEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* TrackMapEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Track Map Editor"), mainWindow);
    m_dockWidget->setObjectName("TrackMapEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* propsGroup = new QGroupBox(tr("Map Settings"));
    auto* propsLayout = new QGridLayout(propsGroup);

    m_centerXSpin = new QDoubleSpinBox(); m_centerXSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Center X:")), 0, 0); propsLayout->addWidget(m_centerXSpin, 0, 1);

    m_centerYSpin = new QDoubleSpinBox(); m_centerYSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Center Y:")), 1, 0); propsLayout->addWidget(m_centerYSpin, 1, 1);

    m_zoomSpin = new QDoubleSpinBox(); m_zoomSpin->setRange(0.001, 1000); m_zoomSpin->setValue(1.0);
    propsLayout->addWidget(new QLabel(tr("Zoom:")), 2, 0); propsLayout->addWidget(m_zoomSpin, 2, 1);

    m_mapSizeSpin = new QDoubleSpinBox(); m_mapSizeSpin->setRange(0.001, 10000); m_mapSizeSpin->setValue(1.0);
    propsLayout->addWidget(new QLabel(tr("Map Size:")), 3, 0); propsLayout->addWidget(m_mapSizeSpin, 3, 1);

    m_mapImageEdit = new QLineEdit(); m_mapImageEdit->setText(tr("map.png"));
    propsLayout->addWidget(new QLabel(tr("Image:")), 4, 0); propsLayout->addWidget(m_mapImageEdit, 4, 1);

    m_flipXCheck = new QCheckBox();
    propsLayout->addWidget(new QLabel(tr("Flip X:")), 5, 0); propsLayout->addWidget(m_flipXCheck, 5, 1);

    m_flipYCheck = new QCheckBox();
    propsLayout->addWidget(new QLabel(tr("Flip Y:")), 6, 0); propsLayout->addWidget(m_flipYCheck, 6, 1);

    mainLayout->addWidget(propsGroup);

    m_previewView = new QGraphicsView();
    m_previewScene = new QGraphicsScene();
    m_previewView->setScene(m_previewScene);
    m_previewView->setMinimumHeight(200);
    mainLayout->addWidget(m_previewView);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load map.ini"));
    m_saveBtn = new QPushButton(tr("Save map.ini"));
    m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &TrackMapEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &TrackMapEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &TrackMapEditorModule::onResetDefaults);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void TrackMapEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void TrackMapEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void TrackMapEditorModule::onActivation()
{
    connect(m_centerXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackMapEditorModule::onCenterXChanged);
    connect(m_centerYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackMapEditorModule::onCenterYChanged);
    connect(m_zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackMapEditorModule::onZoomChanged);
    connect(m_mapSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackMapEditorModule::onMapSizeChanged);
    connect(m_mapImageEdit, &QLineEdit::textChanged, this, &TrackMapEditorModule::onMapImageChanged);
    connect(m_flipXCheck, &QCheckBox::toggled, this, &TrackMapEditorModule::onFlipXChanged);
    connect(m_flipYCheck, &QCheckBox::toggled, this, &TrackMapEditorModule::onFlipYChanged);
    m_statusLabel->setText(tr("Active"));
}

void TrackMapEditorModule::onDeactivation()
{
    // Connections are permanent (set up in getOrCreateDockWidget); no need to disconnect.
    m_statusLabel->setText(tr("Inactive"));
}
void TrackMapEditorModule::onCenterXChanged(double v) { m_centerX = v; }
void TrackMapEditorModule::onCenterYChanged(double v) { m_centerY = v; }
void TrackMapEditorModule::onZoomChanged(double v) { m_zoom = v; }
void TrackMapEditorModule::onMapSizeChanged(double v) { m_mapSize = v; }
void TrackMapEditorModule::onMapImageChanged(const QString& text) {
    m_statusLabel->setText(tr("Modified (Map Image: %1)").arg(text));
    // Try to load the image into the preview
    if (!m_previewScene || !m_previewView) return;
    m_previewScene->clear();
    QImage img(text);
    if (img.isNull()) {
        // Try relative to the INI file directory
        if (!m_filePath.isEmpty()) {
            QString dir = QFileInfo(m_filePath).absolutePath();
            img.load(dir + "/" + text);
        }
    }
    if (!img.isNull()) {
        // Scale to fit the view
        QPixmap pix = QPixmap::fromImage(img);
        int vw = m_previewView->viewport()->width();
        int vh = m_previewView->viewport()->height();
        pix = pix.scaled(vw, vh, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_previewScene->addPixmap(pix);
        m_previewScene->setSceneRect(0, 0, pix.width(), pix.height());
    } else {
        // Draw placeholder text
        m_previewScene->addText(tr("Map image not found:\n%1").arg(text));
    }
}
void TrackMapEditorModule::onFlipXChanged(bool c) { m_flipX = c; }
void TrackMapEditorModule::onFlipYChanged(bool c) { m_flipY = c; }

void TrackMapEditorModule::onLoadFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open map.ini"), QString(), tr("Map INI (*.ini)"));
    if (!path.isEmpty()) { m_filePath = path; loadFileToUI(); m_statusLabel->setText(tr("Loaded: %1").arg(path)); }
}

void TrackMapEditorModule::onSaveFile()
{
    QString path = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save map.ini"), QString(), tr("Map INI (*.ini)")) : m_filePath;
    if (!path.isEmpty()) { m_filePath = path; saveFileFromUI(); m_statusLabel->setText(tr("Saved: %1").arg(path)); }
}

void TrackMapEditorModule::onResetDefaults()
{
    m_centerX = 0; m_centerY = 0; m_zoom = 1.0; m_mapSize = 1.0; m_flipX = false; m_flipY = false;
    m_centerXSpin->setValue(0); m_centerYSpin->setValue(0); m_zoomSpin->setValue(1.0); m_mapSizeSpin->setValue(1.0);
    m_flipXCheck->setChecked(false); m_flipYCheck->setChecked(false);
    m_statusLabel->setText(tr("Reset to defaults"));
}

void TrackMapEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void TrackMapEditorModule::loadFileToUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = file.readAll(); file.close();
    QStringList lines = content.split("\n", Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString l = line.trimmed();
        if (l.startsWith("CENTER_X=")) m_centerX = l.mid(9).toFloat();
        else if (l.startsWith("CENTER_Y=")) m_centerY = l.mid(9).toFloat();
        else if (l.startsWith("ZOOM=")) m_zoom = l.mid(5).toFloat();
        else if (l.startsWith("MAP_SIZE=")) m_mapSize = l.mid(9).toFloat();
        else if (l.startsWith("IMAGE=")) m_mapImageEdit->setText(l.mid(6));
        else if (l.startsWith("FLIP_X=")) m_flipX = (l.mid(7) == "1");
        else if (l.startsWith("FLIP_Y=")) m_flipY = (l.mid(7) == "1");
    }
    m_centerXSpin->setValue(m_centerX); m_centerYSpin->setValue(m_centerY);
    m_zoomSpin->setValue(m_zoom); m_mapSizeSpin->setValue(m_mapSize);
    m_flipXCheck->setChecked(m_flipX); m_flipYCheck->setChecked(m_flipY);
}

void TrackMapEditorModule::saveFileFromUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    out << "CENTER_X=" << m_centerXSpin->value() << "\n";
    out << "CENTER_Y=" << m_centerYSpin->value() << "\n";
    out << "ZOOM=" << m_zoomSpin->value() << "\n";
    out << "MAP_SIZE=" << m_mapSizeSpin->value() << "\n";
    out << "IMAGE=" << m_mapImageEdit->text() << "\n";
    out << "FLIP_X=" << (m_flipXCheck->isChecked() ? "1" : "0") << "\n";
    out << "FLIP_Y=" << (m_flipYCheck->isChecked() ? "1" : "0") << "\n";
    file.close();
}

QJsonObject TrackMapEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    data["centerX"] = static_cast<double>(m_centerX);
    data["centerY"] = static_cast<double>(m_centerY);
    data["zoom"] = static_cast<double>(m_zoom);
    data["mapSize"] = static_cast<double>(m_mapSize);
    data["flipX"] = m_flipX;
    data["flipY"] = m_flipY;
    return data;
}

void TrackMapEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_centerX = static_cast<float>(data["centerX"].toDouble());
    m_centerY = static_cast<float>(data["centerY"].toDouble());
    m_zoom = static_cast<float>(data["zoom"].toDouble(1.0));
    m_mapSize = static_cast<float>(data["mapSize"].toDouble(1.0));
    m_flipX = data["flipX"].toBool();
    m_flipY = data["flipY"].toBool();
}

} // namespace ks
