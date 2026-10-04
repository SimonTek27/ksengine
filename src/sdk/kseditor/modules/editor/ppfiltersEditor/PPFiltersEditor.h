#ifndef KSPPFILTERSEDITOR_H
#define KSPPFILTERSEDITOR_H

#include <QMainWindow>
#include <QPointer>
#include <QMap>
#include <QString>
#include <QVector>
#include <memory>
#include <QPlainTextEdit>

#include "Config/PPFilterPreset.h"
#include "sdk/kseditor/engine/Graphics/VulkanIntegration.h"
#include "PPFilterColorGrading.h"

#if HAS_QT3D
// Qt6 3D Core
#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DRender/QCamera>
#include <Qt3DRender/QSceneLoader>
#include <Qt3DRender/QRenderSettings>

// Qt6 3D Render (Vulkan backend)
#include <Qt3DRender/QRenderSurfaceSelector>
#include <Qt3DRender/QViewport>
#include <Qt3DRender/QCameraSelector>
#include <Qt3DRender/QClearBuffers>
#include <Qt3DRender/QMesh>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QDiffuseSpecularMaterial>
#include <Qt3DExtras/QSphereMesh>
#include <Qt3DExtras/QPlaneMesh>
#include <Qt3DExtras/QForwardRenderer>
#include <Qt3DExtras/Qt3DWindow>
#endif

// Qt Widgets
#include <QSplitter>
#include <QTreeWidget>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QVulkanWindow>

class Qt3DPreviewWidget;

struct PPFilterParameter {
    QString name;
    float value;
    float min;
    float max;
    float step;
    QString type; // "float", "color", "bool"
};

struct PPFilter {
    QString name;
    QString path;
    QString description;
    QString author;
    QMap<QString, PPFilterParameter> parameters;
};

class KSPFiltersEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit KSPFiltersEditor(QWidget *parent = nullptr);
    ~KSPFiltersEditor();

    void loadFromKsSystem(const QString& systemPath);

private slots:
    void loadFiltersList();
    void onFilterSelected(const QModelIndex &index);
    void onParameterChanged(const QString &paramName, float value);
    void onSaveFilter();
    void onExportFilter();
    void onReloadFilter();
    void onColorGradingParamChanged();
    void onColorGradingPresetChanged(const QString& preset);
    void onExportCubeLUT();
    void onImportCubeLUT();
    void updatePreview();

    // Shader editor slots
    void onShaderTextChanged();
    void onCompileShader();
    void onLoadShaderFile();
    void onReloadShaderToPreview();

private:
    void setupUI();
    void setup3DPreview();
    void setupVulkanBackend();
    void parseFilterINI(const QString &filePath);
    void saveFilterINI();
    void applyParametersTo3DScene();
    void createTestScene();
    void applyToKs(PPFilterPreset& preset);
    void syncWithKsVulkan();

    // UI Components
    QTreeWidget *m_filterTree;
    QTableWidget *m_paramTable;
    QTabWidget *m_mainTabs;
    QWidget *m_previewContainer;
    QPushButton *m_saveBtn;
    QPushButton *m_exportBtn;
    QPushButton *m_reloadBtn;
    QLabel *m_filterInfoLabel;
    QComboBox *m_scenePresetCombo;

#if HAS_QT3D
    // 3D Preview with Qt6 3D + Vulkan
    class Qt3DWindow : public QWidget
    {
    public:
        explicit Qt3DWindow(QWidget *parent = nullptr);
        ~Qt3DWindow();

        Qt3DRender::QRenderSettings* renderSettings() const { return m_renderSettings; }
        Qt3DRender::QCamera* camera();
        Qt3DCore::QEntity* rootEntity() const { return m_rootEntity; }
        void setRootEntity(Qt3DCore::QEntity *entity);

    private:
        Qt3DExtras::Qt3DWindow *m_view;
        Qt3DCore::QEntity *m_rootEntity;
        Qt3DRender::QCamera *m_camera;
        Qt3DRender::QRenderSettings *m_renderSettings;
    };

    Qt3DWindow *m_3dWindow;
    Qt3DCore::QEntity *m_rootEntity;
    Qt3DCore::QEntity *m_carEntity;
    Qt3DCore::QEntity *m_trackEntity;
    Qt3DCore::QEntity *m_skySphere;
    Qt3DRender::QCamera *m_camera;
    Qt3DRender::QRenderSettings *m_renderSettings;
#else
    // Stub for when Qt3D is not available
    QWidget *m_3dWindow;
    void *m_rootEntity;
    void *m_carEntity;
    void *m_trackEntity;
    void *m_skySphere;
    void *m_camera;
    void *m_renderSettings;
#endif

    // Filter Data
    PPFilter m_currentFilter;
    QString  m_currentFilterPath;
    QString  m_acSystemPath;
    PPFilterPreset* m_currentPreset = nullptr;  // owned, deleted in dtor
    QMap<QString, QPointer<QDoubleSpinBox>> m_paramSpinBoxes;

    // Vulkan specific
    QVulkanInstance *m_vulkanInstance;

    // ── Color Grading ────────────────────────────────────────────────────
public:
    void setColorGradingPreset(const QString& name);
    void exportCubeLUT(const QString& path);
    void importCubeLUT(const QString& path);
    void applyColorGradingToScene();

private:
    void setupColorGradingUI(QWidget* parent);
    void updateColorGradingParams();

    ks::PPFilterColorGrading m_colorGrading;
    QWidget* m_colorGradingTab = nullptr;
    QDoubleSpinBox* m_cgExposure = nullptr;
    QDoubleSpinBox* m_cgGamma = nullptr;
    QDoubleSpinBox* m_cgContrast = nullptr;
    QDoubleSpinBox* m_cgBrightness = nullptr;
    QDoubleSpinBox* m_cgSaturation = nullptr;
    QDoubleSpinBox* m_cgVibrance = nullptr;
    QDoubleSpinBox* m_cgTemp = nullptr;
    QDoubleSpinBox* m_cgTint = nullptr;
    QDoubleSpinBox* m_cgLiftR = nullptr, *m_cgLiftG = nullptr, *m_cgLiftB = nullptr;
    QDoubleSpinBox* m_cgGammaR = nullptr, *m_cgGammaG = nullptr, *m_cgGammaB = nullptr;
    QDoubleSpinBox* m_cgGainR = nullptr, *m_cgGainG = nullptr, *m_cgGainB = nullptr;
    QDoubleSpinBox* m_cgShadowsSat = nullptr, *m_cgHighlightsSat = nullptr;
    QDoubleSpinBox* m_cgBalance = nullptr;
    QDoubleSpinBox* m_cgLutIntensity = nullptr;
    QComboBox* m_cgPresetCombo = nullptr;
    QPushButton* m_cgExportLUTBtn = nullptr;
    QPushButton* m_cgImportLUTBtn = nullptr;

    // ── Custom Shader Support ────────────────────────────────────────────
public:
    bool loadCustomShader(const QString& shaderPath);
    bool compileShader(const QString& source, const QString& entryPoint, const QString& shaderModel);
    bool previewCustomShader(const QString& shaderSource);
    QStringList getShaderErrors() const { return m_shaderErrors; }
    void clearShaderErrors() { m_shaderErrors.clear(); }

    // Shader editor UI
    void setupShaderEditorTab();
    QString defaultVertexShader() const;
    QString defaultFragmentShader() const;

private:
    QString compileGLSLToSPIRV(const QString& glslSource, const QString& entryPoint, bool isFragment);
    bool validateShaderSource(const QString& source);
    QStringList m_shaderErrors;
    QString m_currentShaderSource;

    // Shader editor widgets
    class QPlainTextEdit* m_shaderEditor = nullptr;
    class QPlainTextEdit* m_shaderOutput = nullptr;
    class QPushButton* m_compileBtn = nullptr;
    class QPushButton* m_loadShaderBtn = nullptr;
    class QPushButton* m_reloadShaderBtn = nullptr;
    class QTabWidget* m_shaderTabWidget = nullptr;
    QString m_currentShaderPath;
    bool m_shaderDirty = false;
};

#endif // KSPPFILTERSEDITOR_H