#pragma once

#include "engine/sys/ModuleManager.h"
#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "CarBuilder/CarEditorWidget.h"
#include "TrackBuilder/TrackEditorWidget.h"
#include "CharacterBuilder/CharacterEditorWidget.h"
#include <QMainWindow>
#include <QDockWidget>
#include <QComboBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QLabel>

class CarEditor;
class TrackEditor;
class CharacterEditor;

namespace ks {

class KSModelerModule : public EditorModule {
    Q_OBJECT
public:
    explicit KSModelerModule(QWidget* parent = nullptr);
    ~KSModelerModule() override;
    bool initialize() override;
    void shutdown() override;

    QString moduleName() const override { return "3D Modeler"; }
    QString moduleId() const override { return "modeler"; }

    SceneGraph* sceneGraph() const { return m_scene; }
    CarEditor* carEditor() const { return m_carEditor; }
    CharacterEditor* characterEditor() const { return m_characterEditor; }

    QDockWidget* getOrCreateDockWidget(QMainWindow* mainWindow);

public slots:
    void onSceneChanged();

protected:
    void onActivation() override;
    void onDeactivation() override;

private:
    void setupUI();
    void setupEditors();
    void onEditorTypeChanged(int index);

    static KSModelerModule* s_instance;

    QWidget* m_centralWidget = nullptr;
    QDockWidget* m_dockWidget = nullptr;
    QComboBox* m_editorCombo = nullptr;
    QStackedWidget* m_editorStack = nullptr;
    QSplitter* m_splitter = nullptr;
    Viewport3DWidget* m_viewport3D = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_triLabel = nullptr;
    QLabel* m_vertLabel = nullptr;
    SceneGraph* m_scene = nullptr;

    CarEditorWidget* m_carWidget = nullptr;
    TrackEditorWidget* m_trackWidget = nullptr;
    CharacterEditorWidget* m_characterWidget = nullptr;
    TrackEditor* m_trackEditor = nullptr;
    QMainWindow* m_mainWindow = nullptr;
    CarEditor* m_carEditor = nullptr;
    CharacterEditor* m_characterEditor = nullptr;
};

}