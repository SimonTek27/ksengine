#include "TrackLightingEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QTextStream>
#include <QtMath>

namespace ks {

TrackLightingEditorModule::TrackLightingEditorModule(QWidget* parent) : EditorModule(parent) {}
bool TrackLightingEditorModule::initialize() { LOG_INFO("TrackLightingEditorModule", "Initialized"); return true; }
void TrackLightingEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* TrackLightingEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Track Lighting Editor"), mainWindow);
    m_dockWidget->setObjectName("TrackLightingEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* propsGroup = new QGroupBox(tr("Sun Position"));
    auto* propsLayout = new QGridLayout(propsGroup);
    m_sunPitchSpin = new QDoubleSpinBox(); m_sunPitchSpin->setRange(-90, 90); m_sunPitchSpin->setValue(45);
    propsLayout->addWidget(new QLabel(tr("Pitch:")), 0, 0); propsLayout->addWidget(m_sunPitchSpin, 0, 1);
    m_sunHeadingSpin = new QDoubleSpinBox(); m_sunHeadingSpin->setRange(0, 360); m_sunHeadingSpin->setValue(180);
    propsLayout->addWidget(new QLabel(tr("Heading:")), 1, 0); propsLayout->addWidget(m_sunHeadingSpin, 1, 1);
    mainLayout->addWidget(propsGroup);

    m_previewView = new QGraphicsView(); m_previewScene = new QGraphicsScene(); m_previewView->setScene(m_previewScene);
    m_previewView->setMinimumHeight(150);
    mainLayout->addWidget(m_previewView);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load lighting.ini")); m_saveBtn = new QPushButton(tr("Save lighting.ini")); m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready")); mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &TrackLightingEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &TrackLightingEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &TrackLightingEditorModule::onResetDefaults);
    connect(m_sunPitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackLightingEditorModule::onSunPitchChanged);
    connect(m_sunHeadingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackLightingEditorModule::onSunHeadingChanged);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void TrackLightingEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void TrackLightingEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void TrackLightingEditorModule::onActivation()
{
    // Signals are already connected in getOrCreateDockWidget(); no need to reconnect
    onSunPitchChanged(m_sunPitch);
    m_statusLabel->setText(tr("Active"));
}

void TrackLightingEditorModule::onDeactivation()
{
    // Connections are permanent (set up in getOrCreateDockWidget); no need to disconnect.
    m_statusLabel->setText(tr("Inactive"));
}
void TrackLightingEditorModule::onSunPitchChanged(double v) {
    m_sunPitch = v;
    m_previewScene->clear();
    int w = m_previewView->viewport()->width();
    int h = m_previewView->viewport()->height();
    // Draw sky gradient background
    for (int y = 0; y < h; ++y) {
        float t = (float)y / h;
        int r = (int)(135 * (1 - t) + 50 * t);
        int g = (int)(206 * (1 - t) + 80 * t);
        int b = (int)(235 * (1 - t) + 120 * t);
        m_previewScene->addLine(0, y, w, y, QPen(QColor(r, g, b)));
    }
    // Draw sun position
    float rad = qDegreesToRadians(m_sunHeading);
    float elev = qDegreesToRadians(m_sunPitch);
    float sunX = w * 0.5f + (w * 0.3f) * qCos(rad) * qCos(elev);
    float sunY = h * 0.5f - (h * 0.3f) * qSin(elev);
    m_previewScene->addEllipse(sunX - 12, sunY - 12, 24, 24, QPen(Qt::yellow), QBrush(QColor(255, 220, 80)));
    // Draw horizon line
    m_previewScene->addLine(0, h * 0.7f, w, h * 0.7f, QPen(QColor(100, 160, 80), 2));
}

void TrackLightingEditorModule::onSunHeadingChanged(double v) {
    m_sunHeading = v;
    onSunPitchChanged(m_sunPitch);
}

void TrackLightingEditorModule::onLoadFile()
{
    QString p = QFileDialog::getOpenFileName(this, tr("Open lighting.ini"), QString(), tr("Lighting INI (*.ini)"));
    if (!p.isEmpty()) { m_filePath = p; loadFileToUI(); m_statusLabel->setText(tr("Loaded: %1").arg(p)); }
}

void TrackLightingEditorModule::onSaveFile()
{
    QString p = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save lighting.ini"), QString(), tr("Lighting INI (*.ini)")) : m_filePath;
    if (!p.isEmpty()) { m_filePath = p; saveFileFromUI(); m_statusLabel->setText(tr("Saved: %1").arg(p)); }
}

void TrackLightingEditorModule::onResetDefaults()
{
    m_sunPitch = 45.0f; m_sunHeading = 180.0f;
    m_sunPitchSpin->setValue(45); m_sunHeadingSpin->setValue(180);
    m_statusLabel->setText(tr("Reset to defaults"));
}

void TrackLightingEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void TrackLightingEditorModule::loadFileToUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString c = file.readAll(); file.close();
    for (const QString& line : c.split("\n")) {
        QString l = line.trimmed();
        if (l.startsWith("SUN_PITCH=")) m_sunPitch = l.mid(10).toFloat();
        else if (l.startsWith("SUN_HEADING=")) m_sunHeading = l.mid(12).toFloat();
    }
    m_sunPitchSpin->setValue(m_sunPitch); m_sunHeadingSpin->setValue(m_sunHeading);
}

void TrackLightingEditorModule::saveFileFromUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&file);
    o << "SUN_PITCH=" << m_sunPitchSpin->value() << "\n";
    o << "SUN_HEADING=" << m_sunHeadingSpin->value() << "\n";
    file.close();
}

QJsonObject TrackLightingEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    data["sunPitch"] = static_cast<double>(m_sunPitch);
    data["sunHeading"] = static_cast<double>(m_sunHeading);
    return data;
}

void TrackLightingEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_sunPitch = static_cast<float>(data["sunPitch"].toDouble(45.0));
    m_sunHeading = static_cast<float>(data["sunHeading"].toDouble(180.0));
}

} // namespace ks
