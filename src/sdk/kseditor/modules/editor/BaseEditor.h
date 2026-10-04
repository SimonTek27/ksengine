#pragma once

#include <QObject>
#include <QString>
#include <QWidget>
#include <QMainWindow>
#include <QDockWidget>
#include <QSplitter>
#include <QTreeView>
#include <QTreeWidget>
#include <QListWidget>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QMenu>
#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QPushButton>
#include <QButtonGroup>
#include <QInputDialog>
#include <QMimeData>
#include <QStackedWidget>

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cmath>
#include <algorithm>

#include "Math/MathCore.h"

namespace ks {
class SceneGraph;
class SceneObject;
} // namespace ks

using ks::SceneGraph;
using ks::SceneObject;

namespace ks {

class core_BaseEditor : public QObject {
    Q_OBJECT

public:
    static core_BaseEditor* instance();
    
    virtual void initialize();
    virtual void shutdown();
    virtual QString getName() const { return "BaseEditor"; }
    virtual QString getTool() const { return m_currentTool; }
    virtual void setTool(const QString& tool) { m_currentTool = tool; }
    
    virtual bool loadProject(const QString& path);
    virtual bool saveProject(const QString& path);
    virtual bool importFile(const QString& path);
    virtual bool exportFile(const QString& path);

signals:
    void toolChanged(const QString& tool);
    void projectLoaded(const QString& path);
    void projectSaved(const QString& path);

protected:
    explicit core_BaseEditor(QObject* parent = nullptr);
    static core_BaseEditor* s_instance;
    
    QString m_currentTool;
    QString m_currentProject;
};

class core_BaseEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit core_BaseEditorWidget(QWidget* parent = nullptr);
    virtual ~core_BaseEditorWidget();
    
    virtual void setEditor(void* editor);
    virtual void updateUI();
    virtual QString getWidgetName() const { return "BaseEditorWidget"; }

signals:
    void widgetReady();

protected:
    void* m_editor;
};

enum class ToolType {
    Select, Move, Rotate, Scale,
    Extrude, Bevel, Inset, LoopCut,
    Knife, Paint, Sculpt, UV
};

struct TransformSettings {
    bool snapToGrid = true;
    float gridSize = 1.0f;
    bool snapToVertex = false;
    float snapDistance = 0.1f;
    bool absoluteTransform = false;
    bool affectPivotOnly = false;
};

struct ExtrudeSettings {
    float distance = 0.5f;
    bool individual = false;
    bool offsetEven = true;
    float thickness = 0.1f;
};

struct BevelSettings {
    float amount = 0.1f;
    int segments = 1;
    float profile = 0.5f;
    bool clampOverlap = true;
};

struct PaintSettings {
    float radius = 0.1f;
    float strength = 0.5f;
    float hardness = 0.5f;
    int falloffType = 0;
    bool additive = true;
};

class EditorSceneGraphWidget : public QTreeView {
    Q_OBJECT
public:
    explicit EditorSceneGraphWidget(QWidget* parent = nullptr);
    ~EditorSceneGraphWidget();

    void setScene(SceneGraph* scene);
    SceneGraph* scene() const { return m_scene; }
    void refresh();
    void clear();
    void selectObject(SceneObject* obj);
    SceneObject* selectedObject() const;
    QList<SceneObject*> selectedObjects() const;
    void toggleVisibility(SceneObject* obj);
    void isolateSelection();
    void setFilter(const QString& filter);

signals:
    void objectSelected(SceneObject* obj);
    void objectDoubleClicked(SceneObject* obj);
    void objectRenamed(SceneObject* obj, const QString& newName);
    void objectVisibilityChanged(SceneObject* obj, bool visible);
    void objectsDeleted(const QList<SceneObject*>& objects);
    void objectAdded(SceneObject* obj);
    void visibilityChanged();
    void focusRequested(SceneObject* obj);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onItemChanged(QStandardItem* item);
    void onRename();
    void onDelete();
    void onDuplicate();
    void onHide();
    void onShow();
    void onIsolate();
    void onFocus();

private:
    void updateItem(SceneObject* obj, QStandardItem* item);
    void addObjectToModel(QStandardItem* parent, SceneObject* obj);

    SceneGraph* m_scene = nullptr;
    QStandardItemModel* m_model = nullptr;
    QMap<SceneObject*, QStandardItem*> m_itemMap;
};

class ToolSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolSettingsWidget(QWidget* parent = nullptr);
    ~ToolSettingsWidget();

    void setToolType(ToolType type);
    ToolType toolType() const { return m_currentTool; }
    TransformSettings transformSettings() const { return m_transformSettings; }
    ExtrudeSettings extrudeSettings() const { return m_extrudeSettings; }
    BevelSettings bevelSettings() const { return m_bevelSettings; }
    PaintSettings paintSettings() const { return m_paintSettings; }

signals:
    void toolChanged(ToolType type);
    void settingsChanged();
    void uvUnwrapRequested(const QString& methodInfo);
    void uvPackChartsRequested(float margin, bool shareUV);

private slots:
    void onToolSelected(int toolIndex);
    void onTransformSnapToggled(bool enabled);
    void onGridSizeChanged(double size);
    void onExtrudeDistanceChanged(double dist);
    void onExtrudeIndividualToggled(bool enabled);
    void onBevelAmountChanged(double amount);
    void onBevelSegmentsChanged(int segments);
    void onPaintRadiusChanged(double radius);
    void onPaintStrengthChanged(double strength);
    void onPaintHardnessChanged(double hardness);
    void onPaintFalloffChanged(int type);
    void onUnwrapClicked();
    void onPackChartsClicked();

private:
    void setupUI();
    void updateVisibility();
    void createTransformPanel();
    void createExtrudePanel();
    void createBevelPanel();
    void createInsetPanel();
    void createLoopCutPanel();
    void createKnifePanel();
    void createPaintPanel();
    void createSculptPanel();
    void createUVPanel();

    ToolType m_currentTool = ToolType::Select;
    TransformSettings m_transformSettings;
    ExtrudeSettings m_extrudeSettings;
    BevelSettings m_bevelSettings;
    PaintSettings m_paintSettings;

    QVBoxLayout* m_mainLayout = nullptr;
    QHBoxLayout* m_toolBarLayout = nullptr;
    QButtonGroup* m_toolButtonGroup = nullptr;
    QStackedWidget* m_settingsStack = nullptr;

    QWidget* m_transformPanel = nullptr;
    QCheckBox* m_snapToGridCheck = nullptr;
    QDoubleSpinBox* m_gridSizeSpin = nullptr;
    QWidget* m_extrudePanel = nullptr;
    QDoubleSpinBox* m_extrudeDistanceSpin = nullptr;
    QCheckBox* m_extrudeIndividualCheck = nullptr;
    QWidget* m_bevelPanel = nullptr;
    QDoubleSpinBox* m_bevelAmountSpin = nullptr;
    QSpinBox* m_bevelSegmentsSpin = nullptr;
    QWidget* m_insetPanel = nullptr;
    QDoubleSpinBox* m_insetAmountSpin = nullptr;
    QWidget* m_loopCutPanel = nullptr;
    QSpinBox* m_loopCutCountSpin = nullptr;
    QWidget* m_knifePanel = nullptr;
    QCheckBox* m_knifeMidpointCheck = nullptr;
    QCheckBox* m_knifeIgnoreSnapCheck = nullptr;
    QWidget* m_paintPanel = nullptr;
    QDoubleSpinBox* m_paintRadiusSpin = nullptr;
    QDoubleSpinBox* m_paintStrengthSpin = nullptr;
    QWidget* m_sculptPanel = nullptr;
    QComboBox* m_sculptBrushCombo = nullptr;
    QWidget* m_uvPanel = nullptr;
    QComboBox* m_uvProjectionCombo = nullptr;
    QComboBox* m_uvUnwrapMethodCombo = nullptr;
    QDoubleSpinBox* m_seamAngleSpin = nullptr;
    QDoubleSpinBox* m_packMarginSpin = nullptr;
    QCheckBox* m_shareUVCheck = nullptr;
    QPushButton* m_unwrapBtn = nullptr;
    QPushButton* m_packChartsBtn = nullptr;
    QLabel* m_uvPreviewLabel = nullptr;
};

class ToolBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolBarWidget(QWidget* parent = nullptr);
    ~ToolBarWidget();

    void setActiveTool(ToolType type);
    ToolType activeTool() const { return m_activeTool; }

signals:
    void toolSelected(ToolType type);

private slots:
    void onButtonClicked(int id);

private:
    void setupUI();

    ToolType m_activeTool = ToolType::Select;
    QButtonGroup* m_buttonGroup = nullptr;
    QList<QPushButton*> m_buttons;
};

struct ExtrudeResult {
    std::vector<int> newFaces;
    std::vector<int> newVertices;
    bool success = false;
};

struct BevelResult {
    std::vector<int> newFaces;
    std::vector<int> newEdges;
    bool success = false;
};

struct SubdivideResult {
    std::vector<int> newFaces;
    std::vector<int> newVertices;
    bool success = false;
};

inline Vec3 computeCentroid(const std::vector<Vec3>& points) {
    if (points.empty()) return Vec3(0, 0, 0);
    Vec3 sum(0, 0, 0);
    for (const auto& p : points) sum = sum + p;
    return sum / (float)points.size();
}

inline float computeArea(const Vec3& v1, const Vec3& v2, const Vec3& v3) {
    Vec3 ab = v2 - v1;
    Vec3 ac = v3 - v1;
    return length(cross(ab, ac)) * 0.5f;
}

inline bool isDegenerate(const Vec3& v1, const Vec3& v2, const Vec3& v3, float epsilon = 0.0001f) {
    return computeArea(v1, v2, v3) < epsilon;
}

struct BaseSkinWeight {
    int boneId;
    float weight;
};

struct SkinningResult {
    bool success = false;
    std::vector<std::vector<BaseSkinWeight>> vertexWeights;
};

struct WeightPaintOptions {
    float radius = 0.1f;
    float strength = 0.5f;
    bool additive = true;
    bool normalize = true;
    int falloffType = 0;
};

class ModelerPropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ModelerPropertiesPanel(QWidget* parent = nullptr) : QWidget(parent) {}
};

class BaseEditor {
public:
    BaseEditor();
    virtual ~BaseEditor();
    virtual void initialize() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
protected:
    bool isInitialized;
    float editorTime;
};

class UnifiedEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class EditorMode { Car, Track, Character, Font, Physics, Sound, License, Display };
    explicit UnifiedEditorWindow(QWidget* parent = nullptr);
    ~UnifiedEditorWindow();
    EditorMode currentMode() const { return m_currentMode; }
    void setMode(EditorMode mode);
    QWidget* currentEditorWidget() const;

signals:
    void modeChanged(EditorMode mode);
    void editorChanged(QWidget* editor);
    void sceneModified();
    void fileOpened(const QString& path);
    void fileSaved(const QString& path);
    void gridToggled(bool visible);
    void viewReset();
    void focusOnSelection();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onNewFile();
    void onOpenFile();
    void onSaveFile();
    void onModeCar();
    void onModeTrack();
    void onModeCharacter();
    void onToggleGrid();
    void onResetView();
    void onFocusSelection();

private:
    void setupUI();
    void setupRibbon();
    void setupEditorStack();
    void updateWindowTitle();

    EditorMode m_currentMode = EditorMode::Car;
    QString m_currentFilePath;
    QString m_currentFileName;
    bool m_sceneModified = false;
    bool m_gridVisible = true;
    QWidget* m_centralWidget = nullptr;
    QStackedWidget* m_editorStack = nullptr;
    QDockWidget* m_sceneDock = nullptr;
    QDockWidget* m_propertiesDock = nullptr;
    QTreeWidget* m_sceneTree = nullptr;
    QToolBar* m_ribbon = nullptr;
};

class Editor3DModule {
public:
    explicit Editor3DModule(QWidget* parent = nullptr);
    ~Editor3DModule();
    QString getModuleName() const { return "3D Editor"; }
    int getModulePriority() const { return 10; }
    QDockWidget* getOrCreateDockWidget(QMainWindow* mainWindow);

protected:
    void onActivation();
    void onDeactivation();

private:
    UnifiedEditorWindow* m_editorWindow = nullptr;
    QDockWidget* m_dockWidget = nullptr;
};

}