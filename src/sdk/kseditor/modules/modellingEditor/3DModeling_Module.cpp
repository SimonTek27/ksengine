#include "3DModeling_Module.h"
#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "CarBuilder/CarEditorWidget.h"
#include "TrackBuilder/TrackEditorWidget.h"
#include "CharacterBuilder/CharacterEditorWidget.h"
#include "3DModeling_panels.h"
#include "3DModelingQmlBridge.h"
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QSplitter>
#include <QPushButton>

namespace ks {

KSModelerModule::KSModelerModule(QWidget* parent)
    : EditorModule(parent)
    , m_centralWidget(nullptr)
    , m_dockWidget(nullptr)
    , m_editorCombo(nullptr)
    , m_editorStack(nullptr)
    , m_carEditor(nullptr)
    , m_carWidget(nullptr)
    , m_trackEditor(nullptr)
    , m_trackWidget(nullptr)
    , m_characterEditor(nullptr)
    , m_characterWidget(nullptr)
    , m_scene(new SceneGraph())
{}

KSModelerModule::~KSModelerModule() {
    delete m_scene;
}

QDockWidget* KSModelerModule::getOrCreateDockWidget(QMainWindow* mainWindow) {
    if (!m_dockWidget) {
        m_dockWidget = new QDockWidget("Modeler Tools", mainWindow);
        m_dockWidget->setWidget(m_centralWidget ? m_centralWidget : new QWidget());
    }
    return m_dockWidget;
}

void KSModelerModule::onActivation() {
    qDebug() << "KSModelerModule activated";
    if (!m_centralWidget) {
        setupUI();
        setupEditors();
    }
}

void KSModelerModule::onDeactivation() {
    qDebug() << "KSModelerModule deactivated";
}

void KSModelerModule::setupUI() {
    m_centralWidget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(m_centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Editor type selector bar
    QWidget* selectorBar = new QWidget(m_centralWidget);
    selectorBar->setFixedHeight(32);
    selectorBar->setStyleSheet("background-color: #1a1a1a;");
    QHBoxLayout* selectorLayout = new QHBoxLayout(selectorBar);
    selectorLayout->setContentsMargins(6, 2, 6, 2);

    QLabel* label = new QLabel("Editor:", selectorBar);
    label->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    selectorLayout->addWidget(label);

    m_editorCombo = new QComboBox(selectorBar);
    m_editorCombo->addItem("Car Editor");
    m_editorCombo->addItem("Track Editor");
    m_editorCombo->addItem("Character Editor");
    m_editorCombo->setStyleSheet(
        "QComboBox { background: #2d2d2d; color: #ffffff; border: 1px solid #444; "
        "padding: 2px 6px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
    );
    selectorLayout->addWidget(m_editorCombo);
    selectorLayout->addStretch();

    // Viewport controls
    QWidget* viewportControls = new QWidget(selectorBar);
    viewportControls->setStyleSheet("background: transparent;");
    QHBoxLayout* vcLayout = new QHBoxLayout(viewportControls);
    vcLayout->setContentsMargins(0, 0, 0, 0);
    vcLayout->setSpacing(8);

    QLabel* viewLabel = new QLabel("View:", viewportControls);
    viewLabel->setStyleSheet("color: #888888; font-size: 11px;");
    vcLayout->addWidget(viewLabel);

    QComboBox* viewModeCombo = new QComboBox(viewportControls);
    viewModeCombo->addItems({"Perspective", "Top", "Front", "Right"});
    viewModeCombo->setFixedWidth(100);
    viewModeCombo->setStyleSheet(
        "QComboBox { background: #2d2d2d; color: #ffffff; border: 1px solid #444; "
        "padding: 2px 4px; font-size: 11px; }"
    );
    vcLayout->addWidget(viewModeCombo);

    QPushButton* resetBtn = new QPushButton("Reset", viewportControls);
    resetBtn->setFixedSize(50, 22);
    resetBtn->setStyleSheet(
        "QPushButton { background: #3a3a3a; color: #cccccc; border: 1px solid #555; "
        "padding: 2px; font-size: 10px; }"
        "QPushButton:hover { background: #4a4a4a; }"
    );
    vcLayout->addWidget(resetBtn);

    selectorLayout->addWidget(viewportControls);
    selectorLayout->addSpacing(20);

    // Render mode toggle
    QLabel* renderLabel = new QLabel("Render:", selectorBar);
    renderLabel->setStyleSheet("color: #888888; font-size: 11px;");
    selectorLayout->addWidget(renderLabel);

    QComboBox* renderModeCombo = new QComboBox(selectorBar);
    renderModeCombo->addItems({"Solid", "Wireframe", "Textured", "Lit"});
    renderModeCombo->setFixedWidth(90);
    renderModeCombo->setStyleSheet(
        "QComboBox { background: #2d2d2d; color: #ffffff; border: 1px solid #444; "
        "padding: 2px 4px; font-size: 11px; }"
    );
    selectorLayout->addWidget(renderModeCombo);

    layout->addWidget(selectorBar);

    // Main content area with split view
    QWidget* contentArea = new QWidget(m_centralWidget);
    QHBoxLayout* contentLayout = new QHBoxLayout(contentArea);
    contentLayout->setContentsMargins(2, 2, 2, 2);
    contentLayout->setSpacing(2);

    // Left side: 3D Viewport (takes 70% width)
    QWidget* viewportContainer = new QWidget(contentArea);
    viewportContainer->setStyleSheet("background-color: #1a1a1a; border: 1px solid #3a3a3a;");
    QVBoxLayout* viewportLayout = new QVBoxLayout(viewportContainer);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);

    // Viewport toolbar
    QWidget* viewportToolbar = new QWidget(viewportContainer);
    viewportToolbar->setFixedHeight(28);
    viewportToolbar->setStyleSheet("background-color: #252525; border-bottom: 1px solid #3a3a3a;");
    QHBoxLayout* vtLayout = new QHBoxLayout(viewportToolbar);
    vtLayout->setContentsMargins(4, 2, 4, 2);

    QLabel* vpLabel = new QLabel("3D Viewport", viewportToolbar);
    vpLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    vtLayout->addWidget(vpLabel);
    vtLayout->addStretch();

    QLabel* coordLabel = new QLabel("X: 0.00  Y: 0.00  Z: 0.00", viewportToolbar);
    coordLabel->setStyleSheet("color: #666666; font-size: 10px;");
    vtLayout->addWidget(coordLabel);

    viewportLayout->addWidget(viewportToolbar);

    // Main viewport area with 3D widget
    m_viewport3D = new Viewport3DWidget(viewportContainer);
    if (m_scene) m_viewport3D->setScene(m_scene->root());
    viewportLayout->addWidget(m_viewport3D);

    // Viewport info bar
    QWidget* infoBar = new QWidget(viewportContainer);
    infoBar->setFixedHeight(24);
    infoBar->setStyleSheet("background-color: #252525; border-top: 1px solid #3a3a3a;");
    QHBoxLayout* ibLayout = new QHBoxLayout(infoBar);
    ibLayout->setContentsMargins(8, 2, 8, 2);

    m_fpsLabel = new QLabel("FPS: --", infoBar);
    m_fpsLabel->setStyleSheet("color: #666666; font-size: 10px;");
    ibLayout->addWidget(m_fpsLabel);

    m_triLabel = new QLabel("Triangles: 0", infoBar);
    m_triLabel->setStyleSheet("color: #666666; font-size: 10px;");
    ibLayout->addWidget(m_triLabel);

    m_vertLabel = new QLabel("Vertices: 0", infoBar);
    m_vertLabel->setStyleSheet("color: #666666; font-size: 10px;");
    ibLayout->addWidget(m_vertLabel);

    ibLayout->addStretch();

    QLabel* statusLabel = new QLabel("Ready", infoBar);
    statusLabel->setStyleSheet("color: #558855; font-size: 10px;");
    ibLayout->addWidget(statusLabel);

    viewportLayout->addWidget(infoBar);

    // Right side: List/Panel area (takes 30% width)
    QWidget* listContainer = new QWidget(contentArea);
    listContainer->setStyleSheet("background-color: #1e1e1e; border: 1px solid #3a3a3a;");
    QVBoxLayout* listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(4, 4, 4, 4);
    listLayout->setSpacing(4);

    // Object List panel
    QGroupBox* objectListGroup = new QGroupBox("Objects", listContainer);
    objectListGroup->setStyleSheet(
        "QGroupBox { color: #aaaaaa; border: 1px solid #444; font-size: 11px; "
        "margin-top: 4px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    QVBoxLayout* objectListLayout = new QVBoxLayout(objectListGroup);
    objectListLayout->setContentsMargins(4, 8, 4, 4);

    auto m_objectListWidget = new ObjectListWidget(objectListGroup);
    m_objectListWidget->setObjectName("ObjectList");
    objectListLayout->addWidget(m_objectListWidget);

    // Layer Panel
    QGroupBox* layerGroup = new QGroupBox("Layers", listContainer);
    layerGroup->setStyleSheet(
        "QGroupBox { color: #aaaaaa; border: 1px solid #444; font-size: 11px; "
        "margin-top: 4px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    QVBoxLayout* layerLayout = new QVBoxLayout(layerGroup);
    layerLayout->setContentsMargins(4, 8, 4, 4);

    auto m_layerPanelWidget = new LayerPanelWidget(layerGroup);
    m_layerPanelWidget->setObjectName("LayerPanel");
    layerLayout->addWidget(m_layerPanelWidget);

    // Tool Palette
    QGroupBox* toolGroup = new QGroupBox("Tools", listContainer);
    toolGroup->setStyleSheet(
        "QGroupBox { color: #aaaaaa; border: 1px solid #444; font-size: 11px; "
        "margin-top: 4px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    QVBoxLayout* toolLayout = new QVBoxLayout(toolGroup);
    toolLayout->setContentsMargins(4, 8, 4, 4);

    auto m_toolPaletteWidget = new ToolPaletteWidget(toolGroup);
    m_toolPaletteWidget->setObjectName("ToolPalette");
    toolLayout->addWidget(m_toolPaletteWidget);

    // Properties Panel
    QGroupBox* propsGroup = new QGroupBox("Properties", listContainer);
    propsGroup->setStyleSheet(
        "QGroupBox { color: #aaaaaa; border: 1px solid #444; font-size: 11px; "
        "margin-top: 4px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    QVBoxLayout* propsLayout = new QVBoxLayout(propsGroup);
    propsLayout->setContentsMargins(4, 8, 4, 4);

    PropertiesPanel* propsPanel = new PropertiesPanel(propsGroup);
    propsPanel->setObjectName("PropertiesPanel");
    propsLayout->addWidget(propsPanel);

    // Material Editor Panel
    QGroupBox* materialGroup = new QGroupBox("Material", listContainer);
    materialGroup->setStyleSheet(
        "QGroupBox { color: #aaaaaa; border: 1px solid #444; font-size: 11px; "
        "margin-top: 4px; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
    );
    QVBoxLayout* materialLayout = new QVBoxLayout(materialGroup);
    materialLayout->setContentsMargins(4, 8, 4, 4);

    MaterialEditorPanel* materialPanel = new MaterialEditorPanel(materialGroup);
    materialPanel->setObjectName("MaterialEditorPanel");
    materialLayout->addWidget(materialPanel);

    // Add panels to list container
    listLayout->addWidget(objectListGroup);
    listLayout->addWidget(layerGroup);
    listLayout->addWidget(toolGroup);
    listLayout->addWidget(propsGroup);
    listLayout->addWidget(materialGroup);

    // Use splitter for resizable panels
    m_splitter = new QSplitter(Qt::Horizontal, contentArea);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #3a3a3a; width: 4px; }");
    m_splitter->addWidget(viewportContainer);
    m_splitter->addWidget(listContainer);
    m_splitter->setStretchFactor(0, 7);
    m_splitter->setStretchFactor(1, 3);

    contentLayout->addWidget(m_splitter);

    // Editor stack below the viewport/panel split
    m_editorStack = new QStackedWidget(contentArea);
    m_editorStack->setStyleSheet("background: #1e1e1e; border: 1px solid #3a3a3a;");
    m_editorStack->setMinimumHeight(200);
    contentLayout->addWidget(m_editorStack);

    layout->addWidget(contentArea);

    m_centralWidget->setLayout(layout);

    // Add to this widget
    QVBoxLayout* selfLayout = new QVBoxLayout(this);
    selfLayout->setContentsMargins(0, 0, 0, 0);
    selfLayout->addWidget(m_centralWidget);
    setLayout(selfLayout);

    connect(m_editorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &KSModelerModule::onEditorTypeChanged);

    connect(viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        switch (index) {
            case 0: m_viewport3D->setCameraMode(Viewport3DWidget::Perspective); break;
            case 1: m_viewport3D->setCameraMode(Viewport3DWidget::Top); break;
            case 2: m_viewport3D->setCameraMode(Viewport3DWidget::Front); break;
            case 3: m_viewport3D->setCameraMode(Viewport3DWidget::Right); break;
        }
    });

    connect(renderModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        switch (index) {
            case 0: m_viewport3D->setRenderMode(Viewport3DWidget::Solid); break;
            case 1: m_viewport3D->setRenderMode(Viewport3DWidget::Wireframe); break;
            case 2: m_viewport3D->setRenderMode(Viewport3DWidget::Solid); break;
            case 3: m_viewport3D->setRenderMode(Viewport3DWidget::Solid); break;
        }
    });

    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_viewport3D->resetCamera();
    });

    connect(m_viewport3D, &Viewport3DWidget::renderStatsUpdated, this, [this](int triangles, int vertices, float fps) {
        if (m_fpsLabel) m_fpsLabel->setText(QString("FPS: %1").arg(fps, 0, 'f', 1));
        if (m_triLabel) m_triLabel->setText(QString("Triangles: %1").arg(triangles));
        if (m_vertLabel) m_vertLabel->setText(QString("Vertices: %1").arg(vertices));
    });
}

void KSModelerModule::setupEditors() {
    if (!m_editorStack) return;

    m_carWidget = new CarEditorWidget(m_editorStack);
    m_editorStack->addWidget(m_carWidget);

    m_trackWidget = new TrackEditorWidget(m_editorStack);
    m_editorStack->addWidget(m_trackWidget);

    m_characterWidget = new CharacterEditorWidget(m_editorStack);
    m_editorStack->addWidget(m_characterWidget);

    m_editorStack->setCurrentIndex(0);
}

void KSModelerModule::onEditorTypeChanged(int index) {
    if (m_editorStack) {
        m_editorStack->setCurrentIndex(index);
    }
    qDebug() << "KSModelerModule: editor type changed to" << index;
}

bool KSModelerModule::initialize() {
    qDebug() << "KSModelerModule::initialize()";
    if (!m_scene) {
        m_scene = new SceneGraph();
    }
    KSModelerQml::instance().setScene(m_scene);
    return true;
}

void KSModelerModule::shutdown() {
    qDebug() << "KSModelerModule::shutdown()";
    KSModelerQml::instance().setScene(nullptr);
    delete m_scene;
    m_scene = nullptr;
}

void KSModelerModule::onSceneChanged()
{
    if (m_viewport3D && m_scene)
        m_viewport3D->setScene(m_scene->root());
}

} // namespace ks
