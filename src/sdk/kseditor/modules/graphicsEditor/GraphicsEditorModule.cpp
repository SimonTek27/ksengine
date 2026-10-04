#include "GraphicsEditorModule.h"
#include "sdk/kseditor/engine/Graphics/VulkanRenderer.h"
#include "sdk/kseditor/engine/Graphics/VulkanShaderLoader.h"
#include "sdk/kseditor/engine/Graphics/RenderGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QSettings>

namespace ks {

GraphicsEditorModule::GraphicsEditorModule(QWidget* parent)
    : ModuleGuiBase(parent)
    , m_tabWidget(nullptr)
    , m_sceneGraphTab(nullptr)
    , m_sceneTree(nullptr)
    , m_sceneInfoLabel(nullptr)
    , m_loadSceneBtn(nullptr)
    , m_exportScreenshotBtn(nullptr)
    , m_renderGraphTab(nullptr)
    , m_passTree(nullptr)
    , m_addPassBtn(nullptr)
    , m_removePassBtn(nullptr)
    , m_passTypeCombo(nullptr)
    , m_shadersTab(nullptr)
    , m_shaderTree(nullptr)
    , m_shaderCodeEdit(nullptr)
    , m_addShaderBtn(nullptr)
    , m_removeShaderBtn(nullptr)
    , m_shaderTypeCombo(nullptr)
    , m_shaderLanguageCombo(nullptr)
    , m_postFxTab(nullptr)
    , m_effectTree(nullptr)
    , m_gammaSpin(nullptr)
    , m_exposureSpin(nullptr)
    , m_drawDistanceSlider(nullptr)
    , m_fovSlider(nullptr)
    , m_drawDistanceLabel(nullptr)
    , m_fovLabel(nullptr)
    , m_settingsTab(nullptr)
    , m_resolutionCombo(nullptr)
    , m_vsyncCheck(nullptr)
    , m_msaaCombo(nullptr)
    , m_shadowQualityCombo(nullptr)
    , m_textureQualityCombo(nullptr)
    , m_anisotropySpin(nullptr)
    , m_resetDefaultsBtn(nullptr)
{
    setObjectName("GraphicsEditorModule");
}

bool GraphicsEditorModule::initialize() {
    if (m_uiBuilt) return true;
    ModuleGuiBase::initialize();
    return true;
}

void GraphicsEditorModule::shutdown() {
    m_uiBuilt = false;
}

void GraphicsEditorModule::importFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    if (suffix == "kn5" || suffix == "fbx" || suffix == "gltf" || suffix == "glb") {
        log(QString("Loading scene: %1").arg(filePath));
    } else {
        logError(QString("Unsupported scene format: %1").arg(suffix));
    }
}

void GraphicsEditorModule::exportFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    if (suffix == "png" || suffix == "jpg" || suffix == "bmp" || suffix == "tga") {
        log(QString("Exporting screenshot to: %1").arg(filePath));
    } else {
        logError(QString("Unsupported image format: %1").arg(suffix));
    }
}

void GraphicsEditorModule::onActivation() {}
void GraphicsEditorModule::onDeactivation() {}

void GraphicsEditorModule::buildUI() {
    m_tabWidget = new QTabWidget();

    setupSceneGraphTab();
    setupRenderGraphTab();
    setupShadersTab();
    setupPostProcessingTab();
    setupSettingsTab();

    m_mainLayout->addWidget(m_tabWidget);
    m_mainLayout->addWidget(m_logOutput);
}

void GraphicsEditorModule::setupSceneGraphTab() {
    m_sceneGraphTab = new QWidget();
    auto* layout = new QVBoxLayout(m_sceneGraphTab);

    auto* toolbar = new QHBoxLayout();
    m_loadSceneBtn = createButton("Load Scene");
    m_exportScreenshotBtn = createButton("Export Screenshot");
    toolbar->addWidget(m_loadSceneBtn);
    toolbar->addWidget(m_exportScreenshotBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);
    m_sceneTree = createTreeWidget({"Node", "Type", "Triangles", "Visible"});
    m_sceneTree->setHeaderLabels({"Node", "Type", "Triangles", "Visible"});
    splitter->addWidget(m_sceneTree);

    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);
    m_sceneInfoLabel = createLabel("Select a scene node to view properties");
    rightLayout->addWidget(m_sceneInfoLabel);
    rightLayout->addStretch();
    splitter->addWidget(rightPanel);

    layout->addWidget(splitter);

    connect(m_loadSceneBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onLoadScene);
    connect(m_exportScreenshotBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onExportScreenshot);
    connect(m_sceneTree, &QTreeWidget::itemClicked, this, &GraphicsEditorModule::onSceneNodeSelected);

    populateSceneGraph();
    m_tabWidget->addTab(m_sceneGraphTab, "Scene Graph");
}

void GraphicsEditorModule::setupRenderGraphTab() {
    m_renderGraphTab = new QWidget();
    auto* layout = new QVBoxLayout(m_renderGraphTab);

    auto* toolbar = new QHBoxLayout();
    m_passTypeCombo = createComboBox({"Geometry Pass", "Lighting Pass", "Shadow Pass", "Post Process", "UI Pass", "Compute Pass"});
    m_addPassBtn = createButton("Add Pass");
    m_removePassBtn = createButton("Remove Pass");
    toolbar->addWidget(m_passTypeCombo);
    toolbar->addWidget(m_addPassBtn);
    toolbar->addWidget(m_removePassBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    m_passTree = createTreeWidget({"Pass", "Type", "Resolution", "Enabled"});
    m_passTree->setHeaderLabels({"Pass", "Type", "Resolution", "Enabled"});
    layout->addWidget(m_passTree);

    connect(m_addPassBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onAddPass);
    connect(m_removePassBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onRemovePass);
    connect(m_passTree, &QTreeWidget::itemClicked, this, &GraphicsEditorModule::onPassSelected);

    populateRenderGraph();
    m_tabWidget->addTab(m_renderGraphTab, "Render Graph");
}

void GraphicsEditorModule::setupShadersTab() {
    m_shadersTab = new QWidget();
    auto* layout = new QVBoxLayout(m_shadersTab);

    auto* toolbar = new QHBoxLayout();
    m_shaderTypeCombo = createComboBox({"Vertex Shader", "Fragment Shader", "Geometry Shader", "Compute Shader", "Tessellation Shader"});
    m_shaderLanguageCombo = createComboBox({"GLSL", "HLSL", "Vulkan SPIR-V", "Metal Shading Lang"});
    m_addShaderBtn = createButton("Add Shader");
    m_removeShaderBtn = createButton("Remove Shader");
    toolbar->addWidget(m_shaderTypeCombo);
    toolbar->addWidget(m_shaderLanguageCombo);
    toolbar->addWidget(m_addShaderBtn);
    toolbar->addWidget(m_removeShaderBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);
    m_shaderTree = createTreeWidget({"Name", "Type", "Language", "Compiled"});
    splitter->addWidget(m_shaderTree);

    m_shaderCodeEdit = new QTextEdit();
    m_shaderCodeEdit->setPlaceholderText("Shader source code...");
    m_shaderCodeEdit->setStyleSheet("QTextEdit { background-color: #1e1e1e; color: #d4d4d4; font-family: 'Consolas', monospace; font-size: 12px; }");
    splitter->addWidget(m_shaderCodeEdit);

    layout->addWidget(splitter);

    connect(m_addShaderBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onAddShader);
    connect(m_removeShaderBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onRemoveShader);
    connect(m_shaderTree, &QTreeWidget::itemClicked, this, &GraphicsEditorModule::onShaderSelected);

    populateShaders();
    m_tabWidget->addTab(m_shadersTab, "Shaders");
}

void GraphicsEditorModule::setupPostProcessingTab() {
    m_postFxTab = new QWidget();
    auto* layout = new QVBoxLayout(m_postFxTab);

    m_effectTree = createTreeWidget({"Effect", "Enabled"});
    m_effectTree->setHeaderLabels({"Effect", "Enabled"});
    layout->addWidget(m_effectTree);

    auto* paramsGroup = new QGroupBox("Effect Parameters");
    auto* paramsLayout = new QFormLayout(paramsGroup);

    m_gammaSpin = createDoubleSpinBox(0.1, 5.0, 2.2, 2, "");
    m_exposureSpin = createDoubleSpinBox(0.01, 10.0, 1.0, 3, "");
    paramsLayout->addRow("Gamma:", m_gammaSpin);
    paramsLayout->addRow("Exposure:", m_exposureSpin);

    m_drawDistanceSlider = new QSlider(Qt::Horizontal);
    m_drawDistanceSlider->setRange(10, 10000);
    m_drawDistanceSlider->setValue(1000);
    m_drawDistanceLabel = createLabel("1000 m");
    paramsLayout->addRow("Draw Distance:", m_drawDistanceSlider);
    paramsLayout->addRow("", m_drawDistanceLabel);

    m_fovSlider = new QSlider(Qt::Horizontal);
    m_fovSlider->setRange(10, 160);
    m_fovSlider->setValue(75);
    m_fovLabel = createLabel("75 deg");
    paramsLayout->addRow("Field of View:", m_fovSlider);
    paramsLayout->addRow("", m_fovLabel);

    layout->addWidget(paramsGroup);
    layout->addStretch();

    connect(m_effectTree, &QTreeWidget::itemClicked, this, &GraphicsEditorModule::onEffectToggled);
    connect(m_gammaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GraphicsEditorModule::onGammaChanged);
    connect(m_exposureSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &GraphicsEditorModule::onExposureChanged);
    connect(m_drawDistanceSlider, &QSlider::valueChanged, this, &GraphicsEditorModule::onDrawDistanceChanged);
    connect(m_fovSlider, &QSlider::valueChanged, this, &GraphicsEditorModule::onFOVChanged);

    m_tabWidget->addTab(m_postFxTab, "Post-Processing");
}

void GraphicsEditorModule::setupSettingsTab() {
    m_settingsTab = new QWidget();
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    auto* container = new QWidget();
    auto* layout = new QFormLayout(container);

    m_resolutionCombo = createComboBox({"1920x1080", "2560x1440", "3840x2160", "1280x720", "Custom"});
    m_vsyncCheck = createCheckBox("Enable V-Sync", true);
    m_msaaCombo = createComboBox({"Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA"});
    m_shadowQualityCombo = createComboBox({"Low", "Medium", "High", "Ultra"});
    m_textureQualityCombo = createComboBox({"Low", "Medium", "High", "Ultra"});
    m_anisotropySpin = createSpinBox(1, 16, 4, "x");

    layout->addRow("Resolution:", m_resolutionCombo);
    layout->addRow("", m_vsyncCheck);
    layout->addRow("Anti-Aliasing:", m_msaaCombo);
    layout->addRow("Shadow Quality:", m_shadowQualityCombo);
    layout->addRow("Texture Quality:", m_textureQualityCombo);
    layout->addRow("Anisotropic Filtering:", m_anisotropySpin);

    m_resetDefaultsBtn = createButton("Reset to Defaults");
    layout->addRow("", m_resetDefaultsBtn);

    scrollArea->setWidget(container);
    auto* mainLayout = new QVBoxLayout(m_settingsTab);
    mainLayout->addWidget(scrollArea);

    connect(m_resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphicsEditorModule::onResolutionChanged);
    connect(m_vsyncCheck, &QCheckBox::toggled, this, &GraphicsEditorModule::onVSyncToggled);
    connect(m_msaaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphicsEditorModule::onMSAAChanged);
    connect(m_shadowQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphicsEditorModule::onShadowQualityChanged);
    connect(m_textureQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GraphicsEditorModule::onTextureQualityChanged);
    connect(m_anisotropySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &GraphicsEditorModule::onAnisotropyChanged);
    connect(m_resetDefaultsBtn, &QPushButton::clicked, this, &GraphicsEditorModule::onResetDefaults);

    m_tabWidget->addTab(m_settingsTab, "Settings");
}

void GraphicsEditorModule::populateSceneGraph() {
    m_sceneTree->clear();
    auto* renderer = VulkanRenderer::instance();
    if (!renderer) return;

    const auto& meshes = renderer->allMeshes();
    for (auto it = meshes.constBegin(); it != meshes.constEnd(); ++it) {
        auto* item = new QTreeWidgetItem(m_sceneTree, {
            it.key(),
            "Mesh",
            QString::number(it->vertices.size()),
            it.key().startsWith("_") ? "No" : "Yes"
        });
        item->setCheckState(3, it.key().startsWith("_") ? Qt::Unchecked : Qt::Checked);
    }
    m_sceneTree->expandAll();
}

void GraphicsEditorModule::populateRenderGraph() {
    m_passTree->clear();
    auto* renderer = VulkanRenderer::instance();
    if (!renderer || !renderer->device()) {
        m_passTree->addTopLevelItem(new QTreeWidgetItem({"No Render Graph", "N/A", "N/A", "No"}));
        return;
    }

    // Report swap chain info as the main render pass
    int scCount = renderer->swapChainImageCount();
    if (scCount > 0) {
        auto* swapItem = new QTreeWidgetItem(m_passTree, {
            "Swap Chain Pass", "Color Output",
            QString("%1x%2").arg(renderer->swapChainExtent().width).arg(renderer->swapChainExtent().height),
            "Yes"
        });
        swapItem->setCheckState(3, Qt::Checked);
    }

    // Show render pass if available
    if (renderer->renderPass()) {
        auto* rpItem = new QTreeWidgetItem(m_passTree, {
            "Main RenderPass", "Geometry",
            QString("%1x%2").arg(renderer->viewportWidth()).arg(renderer->viewportHeight()),
            "Yes"
        });
        rpItem->setCheckState(3, Qt::Checked);
    }

    // Show shader loader pipelines as compute/passes
    auto* shaderLoader = renderer->shaderLoader();
    if (shaderLoader) {
        QStringList pipelines = shaderLoader->loadedPipelineNames();
        for (const auto& pn : pipelines) {
            auto* plItem = new QTreeWidgetItem(m_passTree, {
                pn, "Shader Pipeline", "N/A", "Yes"
            });
            plItem->setCheckState(3, Qt::Checked);
        }
    }

    if (m_passTree->topLevelItemCount() == 0) {
        m_passTree->addTopLevelItem(new QTreeWidgetItem({"No active passes", "N/A", "N/A", "No"}));
    }
}

void GraphicsEditorModule::populateShaders() {
    m_shaderTree->clear();
    auto* renderer = VulkanRenderer::instance();
    if (!renderer || !renderer->shaderLoader()) {
        m_shaderTree->addTopLevelItem(new QTreeWidgetItem({"No shaders loaded", "N/A", "N/A", "No"}));
        return;
    }

    auto* shaderLoader = renderer->shaderLoader();
    QStringList shaderNames = shaderLoader->loadedShaderNames();
    for (const auto& name : shaderNames) {
        m_shaderTree->addTopLevelItem(new QTreeWidgetItem({name, "Shader", "SPIR-V", "Yes"}));
    }

    QStringList pipelineNames = shaderLoader->loadedPipelineNames();
    for (const auto& name : pipelineNames) {
        m_shaderTree->addTopLevelItem(new QTreeWidgetItem({name, "Pipeline", "SPIR-V", "Yes"}));
    }

    if (shaderNames.isEmpty() && pipelineNames.isEmpty()) {
        m_shaderTree->addTopLevelItem(new QTreeWidgetItem({"No shaders loaded", "N/A", "N/A", "No"}));
    }
}

void GraphicsEditorModule::onSceneNodeSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        m_sceneInfoLabel->setText(QString("Selected: %1 (Type: %2)").arg(item->text(0), item->text(1)));
    }
}

void GraphicsEditorModule::onPassSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        log(QString("Selected pass: %1").arg(item->text(0)));
    }
}

void GraphicsEditorModule::onShaderSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        m_shaderCodeEdit->setText(QString("// %1 (%2)\n\nvoid main() {\n    // Shader code here\n}\n").arg(item->text(0), item->text(1)));
    }
}

void GraphicsEditorModule::onEffectToggled(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        bool enabled = (item->text(1) == "Yes");
        item->setText(1, enabled ? "No" : "Yes");
        log(QString("%1 %2").arg(item->text(0), enabled ? "disabled" : "enabled"));
    }
}

void GraphicsEditorModule::onResolutionChanged(int index) {
    QSettings s; s.setValue("Graphics/Resolution", m_resolutionCombo->currentText());
    log(QString("Resolution changed to: %1").arg(m_resolutionCombo->currentText()));
}

void GraphicsEditorModule::onVSyncToggled(bool checked) {
    QSettings s; s.setValue("Graphics/VSync", checked);
    log(QString("V-Sync %1").arg(checked ? "enabled" : "disabled"));
}

void GraphicsEditorModule::onMSAAChanged(int index) {
    QSettings s; s.setValue("Graphics/MSAA", m_msaaCombo->currentText());
    log(QString("MSAA set to: %1").arg(m_msaaCombo->currentText()));
}

void GraphicsEditorModule::onShadowQualityChanged(int index) {
    QSettings s; s.setValue("Graphics/ShadowQuality", m_shadowQualityCombo->currentText());
    log(QString("Shadow quality set to: %1").arg(m_shadowQualityCombo->currentText()));
}

void GraphicsEditorModule::onTextureQualityChanged(int index) {
    QSettings s; s.setValue("Graphics/TextureQuality", m_textureQualityCombo->currentText());
    log(QString("Texture quality set to: %1").arg(m_textureQualityCombo->currentText()));
}

void GraphicsEditorModule::onAnisotropyChanged(int value) {
    QSettings s; s.setValue("Graphics/Anisotropy", value);
    log(QString("Anisotropic filtering set to: %1x").arg(value));
}

void GraphicsEditorModule::onGammaChanged(double value) {
    QSettings s; s.setValue("Graphics/Gamma", value);
    log(QString("Gamma set to: %1").arg(value, 0, 'f', 2));
}

void GraphicsEditorModule::onExposureChanged(double value) {
    QSettings s; s.setValue("Graphics/Exposure", value);
    log(QString("Exposure set to: %1").arg(value, 0, 'f', 3));
}

void GraphicsEditorModule::onBloomToggled(bool checked) {
    QSettings s; s.setValue("Graphics/Bloom", checked);
    log(QString("Bloom %1").arg(checked ? "enabled" : "disabled"));
}

void GraphicsEditorModule::onSSAOToggled(bool checked) {
    QSettings s; s.setValue("Graphics/SSAO", checked);
    log(QString("SSAO %1").arg(checked ? "enabled" : "disabled"));
}

void GraphicsEditorModule::onSSRToggled(bool checked) {
    QSettings s; s.setValue("Graphics/SSR", checked);
    log(QString("SSR %1").arg(checked ? "enabled" : "disabled"));
}

void GraphicsEditorModule::onDOFToggled(bool checked) {
    QSettings s; s.setValue("Graphics/DOF", checked);
    log(QString("Depth of Field %1").arg(checked ? "enabled" : "disabled"));
}

void GraphicsEditorModule::onDrawDistanceChanged(int value) {
    m_drawDistanceLabel->setText(QString("%1 m").arg(value));
    QSettings s; s.setValue("Graphics/DrawDistance", value);
}

void GraphicsEditorModule::onFOVChanged(int value) {
    m_fovLabel->setText(QString("%1 deg").arg(value));
    QSettings s; s.setValue("Graphics/FOV", value);
}

void GraphicsEditorModule::onAddShader() {
    QString name = QString("New %1").arg(m_shaderTypeCombo->currentText());
    auto* item = new QTreeWidgetItem({name, m_shaderTypeCombo->currentText(), m_shaderLanguageCombo->currentText(), "No"});
    m_shaderTree->addTopLevelItem(item);
    log(QString("Added shader: %1").arg(name));
}

void GraphicsEditorModule::onRemoveShader() {
    auto* item = m_shaderTree->currentItem();
    if (item) {
        log(QString("Removed shader: %1").arg(item->text(0)));
        delete item;
    }
}

void GraphicsEditorModule::onAddPass() {
    QString name = QString("New %1").arg(m_passTypeCombo->currentText());
    auto* item = new QTreeWidgetItem({name, m_passTypeCombo->currentText(), "1920x1080", "Yes"});
    m_passTree->addTopLevelItem(item);
    log(QString("Added pass: %1").arg(name));
}

void GraphicsEditorModule::onRemovePass() {
    auto* item = m_passTree->currentItem();
    if (item) {
        log(QString("Removed pass: %1").arg(item->text(0)));
        delete item;
    }
}

void GraphicsEditorModule::onLoadScene() {
    QString path = selectFile("Load Scene", "Scene Files (*.kn5 *.fbx *.gltf *.glb);;All Files (*)");
    if (!path.isEmpty()) {
        QSettings s; s.setValue("Graphics/LastScenePath", path);
        log(QString("Loading scene: %1").arg(path));
    }
}

void GraphicsEditorModule::onExportScreenshot() {
    QString path = selectFile("Export Screenshot", "Images (*.png *.jpg *.bmp *.tga)");
    if (!path.isEmpty()) {
        QSettings s; s.setValue("Graphics/LastScreenshotPath", path);
        log(QString("Exporting screenshot to: %1").arg(path));
    }
}

void GraphicsEditorModule::onResetDefaults() {
    if (confirmAction("Reset Settings", "Reset all graphics settings to defaults?")) {
        m_resolutionCombo->setCurrentIndex(0);
        m_vsyncCheck->setChecked(true);
        m_msaaCombo->setCurrentIndex(0);
        m_shadowQualityCombo->setCurrentIndex(1);
        m_textureQualityCombo->setCurrentIndex(2);
        m_anisotropySpin->setValue(4);
        m_gammaSpin->setValue(2.2);
        m_exposureSpin->setValue(1.0);
        m_drawDistanceSlider->setValue(1000);
        m_fovSlider->setValue(75);
        QSettings s; s.remove("Graphics");
        logSuccess("Graphics settings reset to defaults");
    }
}

} // namespace ks

