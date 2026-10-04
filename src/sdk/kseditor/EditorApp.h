#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QTimer>
#include <QSettings>

#include "sdk/kseditor/engine/Audio/AudioStudioTypes.h"
#include "sdk/kseditor/modules/modellingEditor/TrackBuilder/TrackSurfaceEditorModule.h"
#include "sdk/kseditor/modules/modellingEditor/CarBuilder/cameracarEditor/CameraEditorModule.h"
#include "sdk/kseditor/modules/modellingEditor/TrackBuilder/TrackMapEditorModule.h"
#include "sdk/kseditor/modules/modellingEditor/CharacterBuilder/DriverEditorModule.h"
#include "sdk/kseditor/modules/PaintEditor/SkinIniEditorModule.h"
#include "sdk/kseditor/modules/ShowroomEditor/ShowroomPPEditorModule.h"
#include "sdk/kseditor/modules/modellingEditor/TrackBuilder/TrackLightingEditorModule.h"
#include "sdk/kseditor/modules/modellingEditor/TrackBuilder/DRSZoneEditorModule.h"
#include "sdk/kseditor/modules/modellingEditor/TrackBuilder/cameratrackEditor/TrackCameraEditorModule.h"
#include "sdk/kseditor/modules/editor/eventEditor/raceConfigEditor/RaceConfigEditorModule.h"
#include "sdk/kseditor/modules/editor/eventEditor/specialEventsEditor/SpecialEventsEditorModule.h"
#include "sdk/kseditor/modules/editor/eventEditor/careerEditor/CareerEditorModule.h"
#include "sdk/kseditor/modules/PaintEditor/GUISkinEditorModule.h"
#include "sdk/kseditor/modules/luaScriptEditor/LuaScriptEditorModule.h"
#include "Scripting/python/PythonScriptEngine.h"
#include "sdk/kseditor/modules/vrEditor/VREditorModule.h"

namespace ks {
namespace graphics { class Scene3D; }
namespace graphics { class RenderEngine; class Camera3D; }
namespace io { class ImportExport3D; }
namespace ac { class TrackModeler; class CarModeler; }
namespace physics {
class PhysicsWorld; class VehicleSimulator; class TrackSimulator; class WeatherModule;
}
namespace ui { class EditorWindow; class DockPanel; class Timeline; class Viewport; }
namespace audio { class AudioStudio; class AudioProject; class AudioManager; }

using physics::WeatherModule;

class ksEditor : public QObject
{
    Q_OBJECT
public:
    explicit ksEditor(QObject* parent = nullptr);
    ~ksEditor();

    bool initialize();
    void shutdown();

    enum Mode { Audio, Model, Physics, Setup, Telemetry, Workshop,
                Weather, TrackSurface, Camera, TrackMap, Driver, SkinIni,
                ShowroomPP, TrackLighting, DRSZone, CameraTrack, RaceConfig,
                SpecialEvents, Career, GUISkin, LuaScript, VR };
    void setMode(Mode mode);
    Mode currentMode() const { return m_mode; }

    // Audio methods
    audio::AudioStudio* audioStudio() { return m_audioStudio; }

    // 3D methods
    graphics::Scene3D* scene() { return m_scene; }
    graphics::RenderEngine* renderer() { return m_renderer; }

    // Asset methods
    void createAudioProject(const QString& name);
    void importAudio(const QString& path);
    void exportAudioBank(const QString& path);
    void import3DModel(const QString& path);
    void export3DModel(const QString& path, const QString& format);
    void exportToAC(const QString& outputDir);

    // Physics methods
    physics::PhysicsWorld* physicsWorld() { return m_physicsWorld; }
    physics::VehicleSimulator* carPhysics() { return m_carPhysics; }
    physics::TrackSimulator* trackSimulator() { return m_trackSimulator; }
    void simulate(float dt);
    void loadTrack(const QString& path);
    void loadCar(const QString& path);

    // Driving input controls
    void setThrottle(double v);
    void setBrake(double v);
    void setSteering(double v);

    // Assetto Corsa methods
    ac::TrackModeler* trackModeler() { return m_trackModeler; }
    ac::CarModeler* carModeler() { return m_carModeler; }

    // New editor modules
    WeatherModule* weatherEditor() { return m_weatherEditor; }
    TrackSurfaceEditorModule* trackSurfaceEditor() { return m_trackSurfaceEditor; }
    CameraEditorModule* cameraEditor() { return m_cameraEditor; }
    TrackMapEditorModule* trackMapEditor() { return m_trackMapEditor; }
    DriverEditorModule* driverEditor() { return m_driverEditor; }
    SkinIniEditorModule* skinIniEditor() { return m_skinIniEditor; }
    ShowroomPPEditorModule* showroomPPEditor() { return m_showroomPPEditor; }
    TrackLightingEditorModule* trackLightingEditor() { return m_trackLightingEditor; }
    DRSZoneEditorModule* drsZoneEditor() { return m_drsZoneEditor; }
    TrackCameraEditorModule* trackCameraEditor() { return m_trackCameraEditor; }
    RaceConfigEditorModule* raceConfigEditor() { return m_raceConfigEditor; }
    SpecialEventsEditorModule* specialEventsEditor() { return m_specialEventsEditor; }
    CareerEditorModule* careerEditor() { return m_careerEditor; }
    GUISkinEditorModule* guiSkinEditor() { return m_guiSkinEditor; }
    LuaScriptEditorModule* luaScriptEditor() { return m_luaScriptEditor; }
    PythonScriptEngine* pythonScriptEngine() { return m_pythonScriptEngine; }
    VREditorModule* vrEditor() { return m_vrEditor; }

    // Project management
    void newProject(const QString& name);
    bool loadProject(const QString& path);
    bool saveProject(const QString& path);
    QString currentProject() const { return m_projectPath; }
    bool isModified() const { return m_modified; }

    void setRecentFiles(const QStringList& files);
    QStringList recentFiles() const { return m_recentFiles; }

    // Preferences
    void setPreference(const QString& key, const QVariant& value);
    QVariant getPreference(const QString& key, const QVariant& defaultValue = QVariant()) const;

signals:
    void modeChanged(Mode mode);
    void projectLoaded(const QString& path);
    void projectSaved(const QString& path);
    void modificationChanged(bool modified);
    void statusMessage(const QString& message);
    void error(const QString& error);
    void errorMessage(const QString& msg);
    void audioProjectCreated(const QString& name);
    void audioImported(const QString& path);
    void audioBankExported(const QString& path);
    void modelImported(const QString& path);
    void modelExported(const QString& path);
    void simulationUpdated(float dt);
    void acExported(const QString& path);

public slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onImportAsset();
    void onExportAsset();
    void onUndo();
    void onRedo();
    void onPreferences();

    void pushUndoAction(const QString& description, const QJsonObject& state = QJsonObject());

    void onModeAudio();
    void onModeModel();
    void onModePhysics();
    void onModeSetup();
    void onModeTelemetry();
    void onModeWorkshop();
    void onModeWeather();
    void onModeTrackSurface();
    void onModeCamera();
    void onModeTrackMap();
    void onModeDriver();
    void onModeSkinIni();
    void onModeShowroomPP();
    void onModeTrackLighting();
    void onModeDRSZone();
    void onModeCameraTrack();
    void onModeRaceConfig();
    void onModeSpecialEvents();
    void onModeCareer();
    void onModeGUISkin();
    void onModeLuaScript();
    void onModeVR();

private:
    void setupConnections();
    void loadPreferences();
    void savePreferences();

    Mode m_mode = Audio;
    QString m_projectPath;
    bool m_modified = false;

    // Audio
    audio::AudioStudio* m_audioStudio = nullptr;

    // 3D
    graphics::Scene3D* m_scene = nullptr;
    graphics::RenderEngine* m_renderer = nullptr;
    graphics::Camera3D* m_camera = nullptr;
    io::ImportExport3D* m_importer = nullptr;
    ac::TrackModeler* m_trackModeler = nullptr;
    ac::CarModeler* m_carModeler = nullptr;

    // Physics
    physics::PhysicsWorld* m_physicsWorld = nullptr;
    physics::VehicleSimulator* m_carPhysics = nullptr;
    physics::TrackSimulator* m_trackSimulator = nullptr;

    // UI
    ui::EditorWindow* m_mainWindow = nullptr;
    ui::DockPanel* m_leftPanel = nullptr;
    ui::DockPanel* m_rightPanel = nullptr;
    ui::Timeline* m_timeline = nullptr;
    ui::Viewport* m_viewport = nullptr;

    // Settings
    QSettings m_settings;
    QStringList m_recentFiles;

    // Audio project
    audio::AudioProject* m_currentAudioProject = nullptr;

    // Physics & Animation
    bool m_physicsEnabled = true;
    bool m_animationEnabled = true;

    // New editor modules
    WeatherModule* m_weatherEditor = nullptr;
    TrackSurfaceEditorModule* m_trackSurfaceEditor = nullptr;
    CameraEditorModule* m_cameraEditor = nullptr;
    TrackMapEditorModule* m_trackMapEditor = nullptr;
    DriverEditorModule* m_driverEditor = nullptr;
    SkinIniEditorModule* m_skinIniEditor = nullptr;
    ShowroomPPEditorModule* m_showroomPPEditor = nullptr;
    TrackLightingEditorModule* m_trackLightingEditor = nullptr;
    DRSZoneEditorModule* m_drsZoneEditor = nullptr;
    TrackCameraEditorModule* m_trackCameraEditor = nullptr;
    RaceConfigEditorModule* m_raceConfigEditor = nullptr;
    SpecialEventsEditorModule* m_specialEventsEditor = nullptr;
    CareerEditorModule* m_careerEditor = nullptr;
    GUISkinEditorModule* m_guiSkinEditor = nullptr;
    LuaScriptEditorModule* m_luaScriptEditor = nullptr;
    PythonScriptEngine* m_pythonScriptEngine = nullptr;
    VREditorModule* m_vrEditor = nullptr;

    // Undo/Redo
    struct Action {
        QString description;
        QJsonObject state;
    };
    QVector<Action> m_undoStack;
    QVector<Action> m_redoStack;
};
} // namespace ks