#pragma once

#include <QWidget>
#include <QString>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>

#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "sdk/kseditor/engine/mesh/MeshRenderer.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/engine/Graphics/VulkanRenderer.h"
#include "ShowroomSystem.h"

namespace ks {

class ShowroomViewport3D : public QWidget {
    Q_OBJECT
public:
    explicit ShowroomViewport3D(QWidget* parent = nullptr);
    ~ShowroomViewport3D() override;

    void loadCarMesh(const QString& filePath);
    void loadCarFromFolder(const QString& carFolder);
    void clearCar();

    void syncConfig(const ShowroomSystem::ShowroomConfig& config);
    void syncCamera(double distance, double height, double angle, double fov);
    void syncLight(const QColor& sunColor, double sunIntensity,
                   const QColor& ambientColor, double ambientIntensity);

    // Real PBR preview generation using Vulkan
    bool generatePBRPreview(const QString& outputPath, int width = 1920, int height = 1080);
    QImage renderToImage(int width, int height);
    
    // Custom Preview Updater - generates high-quality previews with post-processing
    bool generateCustomPreview(const QString& outputPath, int width, int height,
                               bool useSSLR = false, bool useSSAO = false, bool usePCSS = false,
                               float ssaoRadius = 0.5f, float pcssK = 0.5f);
    
    // Post-processing control
    void setSSLREnabled(bool enabled);
    void setSSAOEnabled(bool enabled);
    void setPCSSEnabled(bool enabled);
    void setSSAORadius(float radius);
    void setPCSSK(float k);
    
    Viewport3DWidget* viewportWidget() const { return m_viewport; }

signals:
    void carLoaded(const QString& name, int vertexCount, int faceCount);
    void loadError(const QString& error);

private slots:
    void onOpenFile();
    void onResetView();
    void onRenderModeChanged(int index);
    void onCameraModeChanged(int index);
    void onMeshLoaded(const QString& name, int vertexCount, int faceCount);
    void onMeshLoadError(const QString& error);

private:
    void buildUI();
    void buildControls();
    void convertToScene();
    void applyColorsFromConfig();
    SceneObject* createSceneNode(const QString& name, SceneObject::Type type,
                                  SceneObject* parent = nullptr);

    Viewport3DWidget* m_viewport = nullptr;
    MeshRenderer* m_meshRenderer = nullptr;
    SceneObject* m_sceneRoot = nullptr;

    QPushButton* m_openBtn = nullptr;
    QPushButton* m_resetViewBtn = nullptr;
    QComboBox* m_renderModeCombo = nullptr;
    QComboBox* m_cameraModeCombo = nullptr;
    QLabel* m_infoLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QCheckBox* m_showGridCheck = nullptr;
    QCheckBox* m_showAxesCheck = nullptr;

    ShowroomSystem::ShowroomConfig m_config;
    QVector<ShowroomSystem::ShowroomCamera> m_cameras;
    QVector<ShowroomSystem::ShowroomLight> m_lights;
    int m_currentVertexCount = 0;
    int m_currentFaceCount = 0;
};

} // namespace ks
