#include "ShowroomEditorModule.h"
#include "ShowroomEditorQmlBridge.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include "sdk/kseditor/engine/FileFormat/INIParser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace ks {

// ============================================================================
// ShowroomPreviewWidget — 2D top-down preview of showroom layout
// ============================================================================
class ShowroomPreviewWidget : public QWidget {
public:
    explicit ShowroomPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(300, 300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void updateData(const ShowroomSystem::ShowroomConfig& config,
                    const QVector<ShowroomSystem::ShowroomCamera>& cameras,
                    const QVector<ShowroomSystem::ShowroomLight>& lights) {
        m_config = config;
        m_cameras = cameras;
        m_lights = lights;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const int w = width();
        const int h = height();
        const float centerX = w / 2.0f;
        const float groundY = h * 0.6f;
        const float scale = w / 20.0f;

        // Background
        painter.fillRect(rect(), QColor(25, 25, 30));

        // Grid floor
        QPen gridPen(QColor(50, 50, 55), 1);
        painter.setPen(gridPen);
        int gridSpacing = qMax(10, h / 25);
        for (int y = static_cast<int>(groundY); y <= h; y += gridSpacing)
            painter.drawLine(0, y, w, y);
        for (int x = 0; x <= w; x += gridSpacing)
            painter.drawLine(x, static_cast<int>(groundY), x, h);

        // Horizon
        painter.setPen(QPen(QColor(80, 80, 90), 2));
        painter.drawLine(0, static_cast<int>(groundY), w, static_cast<int>(groundY));

        // Lights
        for (const auto& light : m_lights) {
            if (!light.isActive) continue;
            float lx = centerX + light.position[0] * scale;
            float ly = groundY - light.position[1] * (scale * 0.5f);
            int radius = qBound(4, static_cast<int>(light.intensity * 8), 16);

            painter.setPen(Qt::NoPen);
            QColor glow = light.color; glow.setAlpha(40);
            painter.setBrush(glow);
            painter.drawEllipse(QPointF(lx, ly), radius * 2.5f, radius * 2.5f);
            painter.setBrush(light.color);
            painter.drawEllipse(QPointF(lx, ly), radius, radius);

            painter.setPen(QColor(160, 160, 160));
            painter.setFont(QFont("Segoe UI", 8));
            painter.drawText(QPointF(lx + radius + 4, ly + 3), light.name);
        }

        // Cameras
        for (int i = 0; i < m_cameras.size(); ++i) {
            const auto& cam = m_cameras[i];
            if (!cam.isActive) continue;
            float cx = centerX + cam.position[0] * scale;
            float cy = groundY - cam.position[1] * (scale * 0.5f);
            float tx = centerX + cam.target[0] * scale;
            float ty = groundY - cam.target[1] * (scale * 0.5f);

            // Frustum
            float dirX = tx - cx, dirY = ty - cy;
            float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
            if (dirLen > 0.001f) { dirX /= dirLen; dirY /= dirLen; }
            float perpX = -dirY, perpY = dirX;
            float fovRad = cam.fov * 3.14159265f / 180.0f;
            float frustumLen = scale * 2.0f;

            QPointF leftTip(cx + dirX * frustumLen + perpX * frustumLen * std::tan(fovRad * 0.5f),
                            cy + dirY * frustumLen + perpY * frustumLen * std::tan(fovRad * 0.5f));
            QPointF rightTip(cx + dirX * frustumLen - perpX * frustumLen * std::tan(fovRad * 0.5f),
                             cy + dirY * frustumLen - perpY * frustumLen * std::tan(fovRad * 0.5f));

            QPen camPen(Qt::white, 2);
            painter.setPen(camPen);
            painter.setBrush(QColor(255, 255, 255, 20));
            QPolygonF frustum;
            frustum << QPointF(cx, cy) << leftTip << rightTip;
            painter.drawPolygon(frustum);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 200, 200));
            painter.drawEllipse(QPointF(cx, cy), 5, 5);

            painter.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(cx, cy), QPointF(tx, ty));

            painter.setPen(QColor(200, 200, 200));
            painter.setFont(QFont("Segoe UI", 8));
            painter.drawText(QPointF(cx + 8, cy - 4), tr("Cam %1").arg(i + 1));
        }

        // Info overlay
        painter.setPen(QColor(180, 180, 180));
        painter.setFont(QFont("Segoe UI", 9));
        painter.drawText(10, 16, tr("Cameras: %1 | Lights: %2").arg(m_cameras.size()).arg(m_lights.size()));
        painter.drawText(10, 30, tr("FOV: %1 | Dist: %2 | Height: %3")
            .arg(m_config.cameraFov, 0, 'f', 1)
            .arg(m_config.cameraDistance, 0, 'f', 1)
            .arg(m_config.cameraHeight, 0, 'f', 1));
    }

private:
    ShowroomSystem::ShowroomConfig m_config;
    QVector<ShowroomSystem::ShowroomCamera> m_cameras;
    QVector<ShowroomSystem::ShowroomLight> m_lights;
};

// ============================================================================
// ShowroomEditorModule
// ============================================================================

ShowroomEditorModule::ShowroomEditorModule(QWidget* parent)
    : EditorModule(parent)
{
    m_config = ShowroomSystem::getDefaultConfig();
    m_cameras.append(ShowroomSystem::getDefaultCamera());
    m_lights = ShowroomSystem::getDefaultLights();
}

bool ShowroomEditorModule::initialize()
{
    LOG_INFO("ShowroomEditorModule", "Initializing Showroom Editor module");
    return true;
}

void ShowroomEditorModule::shutdown()
{
    LOG_INFO("ShowroomEditorModule", "Shutting down Showroom Editor module");
}

QDockWidget* ShowroomEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (!m_dockWidget) {
        m_dockWidget = new QDockWidget(tr("Showroom Editor"), mainWindow);
        m_dockWidget->setAllowedAreas(Qt::AllDockWidgetAreas);
        m_centralWidget = new QWidget(m_dockWidget);
        setupUi();
        m_dockWidget->setWidget(m_centralWidget);
    }
    return m_dockWidget;
}

void ShowroomEditorModule::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    QSplitter* splitter = new QSplitter(Qt::Vertical, m_centralWidget);
    mainLayout->addWidget(splitter);

    // --- Top: Preview + Settings side by side ---
    QWidget* topWidget = new QWidget();
    QHBoxLayout* topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);

    // Preview Tabs (2D Layout + 3D Viewport)
    m_previewTabs = new QTabWidget();
    m_previewTabs->setMinimumWidth(300);

    m_previewWidget = new ShowroomPreviewWidget();
    m_previewTabs->addTab(m_previewWidget, tr("2D Layout"));

    m_viewport3D = new ShowroomViewport3D();
    m_previewTabs->addTab(m_viewport3D, tr("3D Preview"));
    m_previewTabs->setCurrentIndex(1); // default to 3D

    topLayout->addWidget(m_previewTabs, 2);

    // Settings panel
    QScrollArea* settingsScroll = new QScrollArea();
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setMaximumWidth(320);
    QWidget* settingsWidget = new QWidget();
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsWidget);
    settingsLayout->setContentsMargins(4, 4, 4, 4);

    // Camera group
    QGroupBox* cameraGroup = new QGroupBox(tr("Camera"));
    QFormLayout* cameraForm = new QFormLayout(cameraGroup);

    m_cameraDistanceSpin = new QDoubleSpinBox();
    m_cameraDistanceSpin->setRange(0.5, 50.0);
    m_cameraDistanceSpin->setSingleStep(0.5);
    m_cameraDistanceSpin->setValue(m_config.cameraDistance);
    cameraForm->addRow(tr("Distance:"), m_cameraDistanceSpin);

    m_cameraHeightSpin = new QDoubleSpinBox();
    m_cameraHeightSpin->setRange(0.0, 20.0);
    m_cameraHeightSpin->setSingleStep(0.25);
    m_cameraHeightSpin->setValue(m_config.cameraHeight);
    cameraForm->addRow(tr("Height:"), m_cameraHeightSpin);

    m_cameraAngleSpin = new QDoubleSpinBox();
    m_cameraAngleSpin->setRange(0.0, 360.0);
    m_cameraAngleSpin->setSingleStep(5.0);
    m_cameraAngleSpin->setValue(m_config.cameraAngle);
    cameraForm->addRow(tr("Angle:"), m_cameraAngleSpin);

    m_cameraFovSpin = new QDoubleSpinBox();
    m_cameraFovSpin->setRange(10.0, 170.0);
    m_cameraFovSpin->setSingleStep(5.0);
    m_cameraFovSpin->setValue(m_config.cameraFov);
    cameraForm->addRow(tr("FOV:"), m_cameraFovSpin);

    m_rotateSpeedSpin = new QDoubleSpinBox();
    m_rotateSpeedSpin->setRange(0.0, 5.0);
    m_rotateSpeedSpin->setSingleStep(0.1);
    m_rotateSpeedSpin->setValue(m_config.rotateSpeed);
    cameraForm->addRow(tr("Rotate Speed:"), m_rotateSpeedSpin);

    m_autoRotateCheck = new QCheckBox(tr("Auto Rotate"));
    m_autoRotateCheck->setChecked(m_config.autoRotate);
    cameraForm->addRow("", m_autoRotateCheck);

    settingsLayout->addWidget(cameraGroup);

    // Lighting group
    QGroupBox* lightGroup = new QGroupBox(tr("Lighting"));
    QFormLayout* lightForm = new QFormLayout(lightGroup);

    QHBoxLayout* sunColorLayout = new QHBoxLayout();
    m_sunColorBtn = new QPushButton();
    m_sunColorBtn->setFixedSize(50, 24);
    m_sunColorBtn->setStyleSheet("background-color: " + m_config.sunColor.name() + "; border: 1px solid #555;");
    sunColorLayout->addWidget(m_sunColorBtn);
    sunColorLayout->addStretch();
    lightForm->addRow(tr("Sun Color:"), sunColorLayout);

    m_sunIntensitySpin = new QDoubleSpinBox();
    m_sunIntensitySpin->setRange(0.0, 5.0);
    m_sunIntensitySpin->setSingleStep(0.1);
    m_sunIntensitySpin->setValue(m_config.sunIntensity);
    lightForm->addRow(tr("Sun Intensity:"), m_sunIntensitySpin);

    QHBoxLayout* ambientColorLayout = new QHBoxLayout();
    m_ambientColorBtn = new QPushButton();
    m_ambientColorBtn->setFixedSize(50, 24);
    m_ambientColorBtn->setStyleSheet("background-color: " + m_config.ambientColor.name() + "; border: 1px solid #555;");
    ambientColorLayout->addWidget(m_ambientColorBtn);
    ambientColorLayout->addStretch();
    lightForm->addRow(tr("Ambient Color:"), ambientColorLayout);

    m_ambientIntensitySpin = new QDoubleSpinBox();
    m_ambientIntensitySpin->setRange(0.0, 5.0);
    m_ambientIntensitySpin->setSingleStep(0.1);
    m_ambientIntensitySpin->setValue(m_config.ambientIntensity);
    lightForm->addRow(tr("Ambient Intensity:"), m_ambientIntensitySpin);

    settingsLayout->addWidget(lightGroup);

    // Cameras list
    QGroupBox* camListGroup = new QGroupBox(tr("Cameras"));
    QVBoxLayout* camListLayout = new QVBoxLayout(camListGroup);

    m_cameraList = new QListWidget();
    m_cameraList->setMaximumHeight(100);
    camListLayout->addWidget(m_cameraList);

    QHBoxLayout* camBtnLayout = new QHBoxLayout();
    m_addCameraBtn = new QPushButton("+");
    m_addCameraBtn->setFixedSize(28, 28);
    m_removeCameraBtn = new QPushButton("-");
    m_removeCameraBtn->setFixedSize(28, 28);
    camBtnLayout->addWidget(m_addCameraBtn);
    camBtnLayout->addWidget(m_removeCameraBtn);
    camBtnLayout->addStretch();
    camListLayout->addLayout(camBtnLayout);

    settingsLayout->addWidget(camListGroup);

    // Lights list
    QGroupBox* lightListGroup = new QGroupBox(tr("Lights"));
    QVBoxLayout* lightListLayout = new QVBoxLayout(lightListGroup);

    m_lightList = new QListWidget();
    m_lightList->setMaximumHeight(100);
    lightListLayout->addWidget(m_lightList);

    QHBoxLayout* lightBtnLayout = new QHBoxLayout();
    m_addLightBtn = new QPushButton("+");
    m_addLightBtn->setFixedSize(28, 28);
    m_removeLightBtn = new QPushButton("-");
    m_removeLightBtn->setFixedSize(28, 28);
    lightBtnLayout->addWidget(m_addLightBtn);
    lightBtnLayout->addWidget(m_removeLightBtn);
    lightBtnLayout->addStretch();
    lightListLayout->addLayout(lightBtnLayout);

    settingsLayout->addWidget(lightListGroup);

    // Car model loading
    QGroupBox* carGroup = new QGroupBox(tr("Car Model"));
    QVBoxLayout* carLayout = new QVBoxLayout(carGroup);

    QHBoxLayout* carPathLayout = new QHBoxLayout();
    m_loadCarBtn = new QPushButton(tr("Load Car..."));
    carPathLayout->addWidget(m_loadCarBtn);
    carPathLayout->addStretch();
    carLayout->addLayout(carPathLayout);

    settingsLayout->addWidget(carGroup);

    // Action buttons
    QGroupBox* actionGroup = new QGroupBox(tr("Actions"));
    QVBoxLayout* actionLayout = new QVBoxLayout(actionGroup);

    m_loadBtn = new QPushButton(tr("Load Config"));
    m_saveBtn = new QPushButton(tr("Save Config"));
    m_previewBtn = new QPushButton(tr("Generate Preview"));
    m_resetBtn = new QPushButton(tr("Reset Defaults"));
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addWidget(m_previewBtn);
    actionLayout->addWidget(m_resetBtn);

    settingsLayout->addWidget(actionGroup);

    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setStyleSheet("color: #aaa; padding: 4px;");
    settingsLayout->addWidget(m_statusLabel);

    settingsLayout->addStretch();
    settingsScroll->setWidget(settingsWidget);
    topLayout->addWidget(settingsScroll, 1);

    splitter->addWidget(topWidget);

    // Connections
    connect(m_cameraDistanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onCameraDistanceChanged);
    connect(m_cameraHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onCameraHeightChanged);
    connect(m_cameraAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onCameraAngleChanged);
    connect(m_cameraFovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onCameraFovChanged);
    connect(m_rotateSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onRotateSpeedChanged);
    connect(m_autoRotateCheck, &QCheckBox::toggled, this, &ShowroomEditorModule::onAutoRotateToggled);
    connect(m_sunColorBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onSunColorClicked);
    connect(m_ambientColorBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onAmbientColorClicked);
    connect(m_sunIntensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onSunIntensityChanged);
    connect(m_ambientIntensitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomEditorModule::onAmbientIntensityChanged);
    connect(m_cameraList, &QListWidget::currentRowChanged, this, &ShowroomEditorModule::onCameraSelected);
    connect(m_addCameraBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onAddCamera);
    connect(m_removeCameraBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onRemoveCamera);
    connect(m_lightList, &QListWidget::currentRowChanged, this, &ShowroomEditorModule::onLightSelected);
    connect(m_addLightBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onAddLight);
    connect(m_removeLightBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onRemoveLight);
    connect(m_loadCarBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onLoadCarModel);
    connect(m_loadBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onLoadConfig);
    connect(m_saveBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onSaveConfig);
    connect(m_previewBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onGeneratePreview);
    connect(m_resetBtn, &QPushButton::clicked, this, &ShowroomEditorModule::onResetDefaults);

    // ── 3D Viewport live camera sync ──
    connect(m_cameraDistanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { if (m_viewport3D) m_viewport3D->syncCamera(v, m_cameraHeightSpin->value(), m_cameraAngleSpin->value(), m_cameraFovSpin->value()); });
    connect(m_cameraHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { if (m_viewport3D) m_viewport3D->syncCamera(m_cameraDistanceSpin->value(), v, m_cameraAngleSpin->value(), m_cameraFovSpin->value()); });
    connect(m_cameraAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { if (m_viewport3D) m_viewport3D->syncCamera(m_cameraDistanceSpin->value(), m_cameraHeightSpin->value(), v, m_cameraFovSpin->value()); });
    connect(m_cameraFovSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { if (m_viewport3D) m_viewport3D->syncCamera(m_cameraDistanceSpin->value(), m_cameraHeightSpin->value(), m_cameraAngleSpin->value(), v); });

    // Populate lists
    for (const auto& cam : m_cameras)
        m_cameraList->addItem(cam.name);
    for (const auto& light : m_lights)
        m_lightList->addItem(light.name);

    updatePreview();
}

void ShowroomEditorModule::loadConfigToUI()
{
    m_cameraDistanceSpin->setValue(m_config.cameraDistance);
    m_cameraHeightSpin->setValue(m_config.cameraHeight);
    m_cameraAngleSpin->setValue(m_config.cameraAngle);
    m_cameraFovSpin->setValue(m_config.cameraFov);
    m_rotateSpeedSpin->setValue(m_config.rotateSpeed);
    m_autoRotateCheck->setChecked(m_config.autoRotate);
    m_sunColorBtn->setStyleSheet("background-color: " + m_config.sunColor.name() + "; border: 1px solid #555;");
    m_ambientColorBtn->setStyleSheet("background-color: " + m_config.ambientColor.name() + "; border: 1px solid #555;");
    m_sunIntensitySpin->setValue(m_config.sunIntensity);
    m_ambientIntensitySpin->setValue(m_config.ambientIntensity);

    m_cameraList->clear();
    for (const auto& cam : m_cameras)
        m_cameraList->addItem(cam.name);

    m_lightList->clear();
    for (const auto& light : m_lights)
        m_lightList->addItem(light.name);

    updatePreview();
}

void ShowroomEditorModule::saveConfigFromUI()
{
    m_config.cameraDistance = static_cast<float>(m_cameraDistanceSpin->value());
    m_config.cameraHeight = static_cast<float>(m_cameraHeightSpin->value());
    m_config.cameraAngle = static_cast<float>(m_cameraAngleSpin->value());
    m_config.cameraFov = static_cast<float>(m_cameraFovSpin->value());
    m_config.rotateSpeed = static_cast<float>(m_rotateSpeedSpin->value());
    m_config.autoRotate = m_autoRotateCheck->isChecked();
    m_config.sunIntensity = static_cast<float>(m_sunIntensitySpin->value());
    m_config.ambientIntensity = static_cast<float>(m_ambientIntensitySpin->value());
}

void ShowroomEditorModule::updatePreview()
{
    if (m_previewWidget) {
        m_previewWidget->updateData(m_config, m_cameras, m_lights);
    }
    if (m_viewport3D) {
        m_viewport3D->syncConfig(m_config);
    }
}

// ── Slots ────────────────────────────────────────────────────────

void ShowroomEditorModule::onCameraDistanceChanged(double v) { m_config.cameraDistance = static_cast<float>(v); updatePreview(); }
void ShowroomEditorModule::onCameraHeightChanged(double v) { m_config.cameraHeight = static_cast<float>(v); updatePreview(); }
void ShowroomEditorModule::onCameraAngleChanged(double v) { m_config.cameraAngle = static_cast<float>(v); updatePreview(); }
void ShowroomEditorModule::onCameraFovChanged(double v) { m_config.cameraFov = static_cast<float>(v); updatePreview(); }
void ShowroomEditorModule::onRotateSpeedChanged(double v) { m_config.rotateSpeed = static_cast<float>(v); updatePreview(); }
void ShowroomEditorModule::onAutoRotateToggled(bool checked) { m_config.autoRotate = checked; updatePreview(); }

void ShowroomEditorModule::onSunIntensityChanged(double v) { m_config.sunIntensity = static_cast<float>(v); updatePreview(); }
void ShowroomEditorModule::onAmbientIntensityChanged(double v) { m_config.ambientIntensity = static_cast<float>(v); updatePreview(); }

void ShowroomEditorModule::onSunColorClicked()
{
    QColor color = QColorDialog::getColor(m_config.sunColor, m_centralWidget, tr("Sun Color"));
    if (color.isValid()) {
        m_config.sunColor = color;
        m_sunColorBtn->setStyleSheet("background-color: " + color.name() + "; border: 1px solid #555;");
        updatePreview();
    }
}

void ShowroomEditorModule::onAmbientColorClicked()
{
    QColor color = QColorDialog::getColor(m_config.ambientColor, m_centralWidget, tr("Ambient Color"));
    if (color.isValid()) {
        m_config.ambientColor = color;
        m_ambientColorBtn->setStyleSheet("background-color: " + color.name() + "; border: 1px solid #555;");
        updatePreview();
    }
}

void ShowroomEditorModule::onCameraSelected(int index)
{
    if (index >= 0 && index < m_cameras.size()) {
        const auto& cam = m_cameras[index];
        m_cameraDistanceSpin->setValue(cam.position[2]);
        m_cameraHeightSpin->setValue(cam.position[1]);
        m_cameraAngleSpin->setValue(0);
        m_cameraFovSpin->setValue(cam.fov);
    }
}

void ShowroomEditorModule::onAddCamera()
{
    ShowroomSystem::ShowroomCamera cam;
    cam.name = tr("Camera %1").arg(m_cameras.size() + 1);
    m_cameras.append(cam);
    m_cameraList->addItem(cam.name);
    updatePreview();
    m_statusLabel->setText(tr("Added camera: %1").arg(cam.name));
}

void ShowroomEditorModule::onRemoveCamera()
{
    int row = m_cameraList->currentRow();
    if (row < 0 || row >= m_cameras.size()) return;
    QString name = m_cameras[row].name;
    m_cameras.removeAt(row);
    delete m_cameraList->takeItem(row);
    updatePreview();
    m_statusLabel->setText(tr("Removed camera: %1").arg(name));
}

void ShowroomEditorModule::onLightSelected(int index)
{
    if (index < 0 || index >= m_lights.size()) return;
    m_statusLabel->setText(tr("Selected light: %1").arg(m_lights[index].name));
}

void ShowroomEditorModule::onAddLight()
{
    ShowroomSystem::ShowroomLight light;
    light.name = tr("Light %1").arg(m_lights.size() + 1);
    light.type = "point";
    light.position[0] = 0; light.position[1] = 3; light.position[2] = 0;
    light.intensity = 1.0f;
    light.range = 10.0f;
    light.isActive = true;
    m_lights.append(light);
    m_lightList->addItem(light.name);
    updatePreview();
    m_statusLabel->setText(tr("Added light: %1").arg(light.name));
}

void ShowroomEditorModule::onRemoveLight()
{
    int row = m_lightList->currentRow();
    if (row < 0 || row >= m_lights.size()) return;
    QString name = m_lights[row].name;
    m_lights.removeAt(row);
    delete m_lightList->takeItem(row);
    updatePreview();
    m_statusLabel->setText(tr("Removed light: %1").arg(name));
}

void ShowroomEditorModule::onLoadConfig()
{
    QString path = QFileDialog::getOpenFileName(m_centralWidget, tr("Load Showroom Config"),
        QString(), tr("INI Files (*.ini);;All Files (*)"));
    if (path.isEmpty()) return;

    m_configPath = path;
    m_config = ShowroomSystem::loadConfig(path);
    m_cameras = ShowroomSystem::loadCameras(path);
    m_lights = ShowroomSystem::loadLights(path);
    loadConfigToUI();
    m_statusLabel->setText(tr("Loaded: %1").arg(QFileInfo(path).fileName()));
}

void ShowroomEditorModule::onSaveConfig()
{
    QString path = m_configPath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(m_centralWidget, tr("Save Showroom Config"),
            QString(), tr("INI Files (*.ini)"));
        if (path.isEmpty()) return;
        m_configPath = path;
    }

    saveConfigFromUI();
    bool ok = ShowroomSystem::saveConfig(m_config, path);
    ok &= ShowroomSystem::saveCameras(m_cameras, path);
    ok &= ShowroomSystem::saveLights(m_lights, path);
    m_statusLabel->setText(ok ? tr("Saved: %1").arg(QFileInfo(path).fileName()) : tr("Save failed!"));
}

void ShowroomEditorModule::onGeneratePreview()
{
    saveConfigFromUI();
    ShowroomSystem::PreviewConfig previewConfig;
    previewConfig.cameraDistance = m_config.cameraDistance;
    previewConfig.cameraHeight = m_config.cameraHeight;
    previewConfig.cameraAngle = m_config.cameraAngle;
    previewConfig.fov = m_config.cameraFov;

    QString outputPath = QFileDialog::getSaveFileName(m_centralWidget, tr("Save Preview"),
        QString(), tr("PNG Image (*.png)"));
    if (outputPath.isEmpty()) return;

    previewConfig.outputPath = outputPath;
    bool ok = ShowroomSystem::generatePreview(m_carPath, previewConfig);
    m_statusLabel->setText(ok ? tr("Preview saved: %1").arg(QFileInfo(outputPath).fileName()) : tr("Preview generation failed"));
}

void ShowroomEditorModule::onLoadCarModel()
{
    QString filter = tr("3D Models (*.obj *.kn5 *.gltf *.glb);;All Files (*)");
    QString path = QFileDialog::getOpenFileName(m_centralWidget, tr("Load Car Model"),
        m_carPath.isEmpty() ? QString() : QFileInfo(m_carPath).absolutePath(), filter);
    if (path.isEmpty()) return;

    m_carPath = path;
    if (m_viewport3D) {
        m_viewport3D->loadCarMesh(path);
    }
    m_statusLabel->setText(tr("Loaded: %1").arg(QFileInfo(path).fileName()));
}

void ShowroomEditorModule::onCarPathChanged(const QString& path)
{
    m_carPath = path;
    if (m_viewport3D && !path.isEmpty()) {
        if (QFileInfo(path).isDir())
            m_viewport3D->loadCarFromFolder(path);
        else
            m_viewport3D->loadCarMesh(path);
    }
}

void ShowroomEditorModule::onResetDefaults()
{
    m_config = ShowroomSystem::getDefaultConfig();
    m_cameras.clear();
    m_cameras.append(ShowroomSystem::getDefaultCamera());
    m_lights = ShowroomSystem::getDefaultLights();
    loadConfigToUI();
    m_statusLabel->setText(tr("Reset to defaults"));
}

void ShowroomEditorModule::importFile(const QString& filePath)
{
    if (QFile::exists(filePath)) {
        m_configPath = filePath;
        m_config = ShowroomSystem::loadConfig(filePath);
        m_cameras = ShowroomSystem::loadCameras(filePath);
        m_lights = ShowroomSystem::loadLights(filePath);
        loadConfigToUI();
        LOG_INFO("ShowroomEditorModule", QString("Imported config: %1").arg(filePath));
    }
}

void ShowroomEditorModule::exportFile(const QString& filePath)
{
    saveConfigFromUI();
    bool ok = ShowroomSystem::saveConfig(m_config, filePath);
    ok &= ShowroomSystem::saveCameras(m_cameras, filePath);
    ok &= ShowroomSystem::saveLights(m_lights, filePath);
    LOG_INFO("ShowroomEditorModule", QString("Exported config: %1 (success=%2)").arg(filePath).arg(ok));
}

void ShowroomEditorModule::onActivation()
{
    LOG_INFO("ShowroomEditorModule", "Showroom Editor activated");
}

void ShowroomEditorModule::onDeactivation()
{
    LOG_INFO("ShowroomEditorModule", "Showroom Editor deactivated");
}

QJsonObject ShowroomEditorModule::serializeProject() const
{
    QJsonObject data;

    QJsonObject config;
    config["cameraDistance"] = static_cast<double>(m_config.cameraDistance);
    config["cameraHeight"] = static_cast<double>(m_config.cameraHeight);
    config["cameraAngle"] = static_cast<double>(m_config.cameraAngle);
    config["cameraFov"] = static_cast<double>(m_config.cameraFov);
    config["rotateSpeed"] = static_cast<double>(m_config.rotateSpeed);
    config["autoRotate"] = m_config.autoRotate;
    config["sunColor"] = m_config.sunColor.name();
    config["ambientColor"] = m_config.ambientColor.name();
    config["sunIntensity"] = static_cast<double>(m_config.sunIntensity);
    config["ambientIntensity"] = static_cast<double>(m_config.ambientIntensity);
    data["config"] = config;

    QJsonArray camerasArray;
    for (const auto& cam : m_cameras) {
        QJsonObject camObj;
        camObj["name"] = cam.name;
        camObj["fov"] = static_cast<double>(cam.fov);
        camObj["isActive"] = cam.isActive;
        QJsonArray pos;
        for (int i = 0; i < 3; ++i) pos.append(static_cast<double>(cam.position[i]));
        camObj["position"] = pos;
        QJsonArray target;
        for (int i = 0; i < 3; ++i) target.append(static_cast<double>(cam.target[i]));
        camObj["target"] = target;
        camerasArray.append(camObj);
    }
    data["cameras"] = camerasArray;

    QJsonArray lightsArray;
    for (const auto& light : m_lights) {
        QJsonObject lightObj;
        lightObj["name"] = light.name;
        lightObj["type"] = light.type;
        lightObj["color"] = light.color.name();
        lightObj["intensity"] = static_cast<double>(light.intensity);
        lightObj["range"] = static_cast<double>(light.range);
        lightObj["isActive"] = light.isActive;
        QJsonArray pos;
        for (int i = 0; i < 3; ++i) pos.append(static_cast<double>(light.position[i]));
        lightObj["position"] = pos;
        QJsonArray dir;
        for (int i = 0; i < 3; ++i) dir.append(static_cast<double>(light.direction[i]));
        lightObj["direction"] = dir;
        lightsArray.append(lightObj);
    }
    data["lights"] = lightsArray;

    return data;
}

void ShowroomEditorModule::deserializeProject(const QJsonObject& data)
{
    if (data.contains("config")) {
        QJsonObject config = data["config"].toObject();
        m_config.cameraDistance = static_cast<float>(config["cameraDistance"].toDouble(5.0));
        m_config.cameraHeight = static_cast<float>(config["cameraHeight"].toDouble(2.0));
        m_config.cameraAngle = static_cast<float>(config["cameraAngle"].toDouble(30.0));
        m_config.cameraFov = static_cast<float>(config["cameraFov"].toDouble(60.0));
        m_config.rotateSpeed = static_cast<float>(config["rotateSpeed"].toDouble(0.5));
        m_config.autoRotate = config["autoRotate"].toBool(true);
        m_config.sunColor = QColor(config["sunColor"].toString("#FFF5F0"));
        m_config.ambientColor = QColor(config["ambientColor"].toString("#C8C8C8"));
        m_config.sunIntensity = static_cast<float>(config["sunIntensity"].toDouble(1.0));
        m_config.ambientIntensity = static_cast<float>(config["ambientIntensity"].toDouble(0.3));
    }

    if (data.contains("cameras")) {
        m_cameras.clear();
        for (const auto& v : data["cameras"].toArray()) {
            QJsonObject camObj = v.toObject();
            ShowroomSystem::ShowroomCamera cam;
            cam.name = camObj["name"].toString();
            cam.fov = static_cast<float>(camObj["fov"].toDouble(60.0));
            cam.isActive = camObj["isActive"].toBool(true);
            QJsonArray pos = camObj["position"].toArray();
            for (int i = 0; i < qMin(3, (int)pos.size()); ++i)
                cam.position[i] = static_cast<float>(pos[i].toDouble());
            QJsonArray target = camObj["target"].toArray();
            for (int i = 0; i < qMin(3, (int)target.size()); ++i)
                cam.target[i] = static_cast<float>(target[i].toDouble());
            m_cameras.append(cam);
        }
    }

    if (data.contains("lights")) {
        m_lights.clear();
        for (const auto& v : data["lights"].toArray()) {
            QJsonObject lightObj = v.toObject();
            ShowroomSystem::ShowroomLight light;
            light.name = lightObj["name"].toString();
            light.type = lightObj["type"].toString("directional");
            light.color = QColor(lightObj["color"].toString("#FFFFFF"));
            light.intensity = static_cast<float>(lightObj["intensity"].toDouble(1.0));
            light.range = static_cast<float>(lightObj["range"].toDouble(10.0));
            light.isActive = lightObj["isActive"].toBool(true);
            QJsonArray pos = lightObj["position"].toArray();
            for (int i = 0; i < qMin(3, (int)pos.size()); ++i)
                light.position[i] = static_cast<float>(pos[i].toDouble());
            QJsonArray dir = lightObj["direction"].toArray();
            for (int i = 0; i < qMin(3, (int)dir.size()); ++i)
                light.direction[i] = static_cast<float>(dir[i].toDouble());
            m_lights.append(light);
        }
    }

    loadConfigToUI();
}

}
