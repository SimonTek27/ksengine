#pragma once
// ============================================================================
// SequencerEditorModule.h
// Cinematic Sequencer - Timeline-based editor for camera cuts, fades,
// audio triggers, and event scripting. Enables creation of cinematic
// replays and cutscenes without external video editing software.
// ============================================================================

#include "editor/EditorModule.h"
#include <QObject>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QColor>
#include <QUuid>
#include <QVector3D>

namespace ks {

// ============================================================================
// Sequencer Track Types
// ============================================================================
enum class SequencerTrackType {
    Camera,         // Camera cuts and transitions
    Fade,           // Screen fade in/out (color + opacity)
    Text,           // On-screen text overlays
    Audio,          // Audio trigger points
    Event,          // Custom script events
    Transform       // Object transform keyframes
};

// ============================================================================
// Easing functions for transitions
// ============================================================================
enum class SequencerEasing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Step,
    Bezier
};

// ============================================================================
// Camera Cut
// ============================================================================
struct SequencerCameraCut {
    float time = 0.0f;                    // Time in seconds
    float duration = 2.0f;                // Duration in seconds
    int cameraIndex = 0;                  // Camera preset index
    QVector3D position;                   // Camera world position
    QVector3D target;                     // Look-at target
    float fov = 60.0f;                   // Field of view
    float roll = 0.0f;                   // Camera roll
    SequencerEasing easing = SequencerEasing::Linear;
    float transitionDuration = 0.5f;     // Cross-fade duration
};

// ============================================================================
// Fade Cue
// ============================================================================
struct SequencerFadeCue {
    float time = 0.0f;
    float duration = 1.0f;
    QColor color = Qt::black;
    float fromOpacity = 0.0f;            // 0 = transparent
    float toOpacity = 1.0f;              // 1 = fully opaque
    SequencerEasing easing = SequencerEasing::EaseInOut;
};

// ============================================================================
// Text Overlay
// ============================================================================
struct SequencerTextCue {
    float time = 0.0f;
    float duration = 3.0f;
    QString text;
    QString font = "Arial";
    int fontSize = 32;
    QColor color = Qt::white;
    QColor shadowColor = QColor(0, 0, 0, 180);
    int shadowOffset = 2;
    float x = 0.5f;                     // Normalized position [0,1]
    float y = 0.9f;                     // Normalized position [0,1]
    SequencerEasing easing = SequencerEasing::EaseInOut;
};

// ============================================================================
// Audio Cue
// ============================================================================
struct SequencerAudioCue {
    float time = 0.0f;
    QString audioPath;
    float volume = 1.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;
    bool loop = false;
};

// ============================================================================
// Event Cue
// ============================================================================
struct SequencerEventCue {
    float time = 0.0f;
    QString eventName;
    QJsonObject parameters;
};

// ============================================================================
// Transform Keyframe
// ============================================================================
struct SequencerTransformKey {
    float time = 0.0f;
    QVector3D position;
    QVector3D rotation;
    QVector3D scale = QVector3D(1, 1, 1);
    SequencerEasing easing = SequencerEasing::Linear;
};

// ============================================================================
// Sequencer Track (generic container)
// ============================================================================
struct SequencerTrack {
    QUuid id;
    QString name;
    SequencerTrackType type;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    QColor color = QColor(80, 160, 240);
    float volume = 1.0f;                 // For audio tracks
};

// ============================================================================
// Sequencer Timeline
// ============================================================================
struct SequencerTimeline {
    QString name;
    float duration = 30.0f;              // Total duration in seconds
    float fps = 30.0f;
    float currentTime = 0.0f;
    bool loop = false;

    QVector<SequencerTrack> tracks;
    QVector<SequencerCameraCut> cameraCuts;
    QVector<SequencerFadeCue> fadeCues;
    QVector<SequencerTextCue> textCues;
    QVector<SequencerAudioCue> audioCues;
    QVector<SequencerEventCue> eventCues;

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
};

// ============================================================================
// SequencerEditorModule - Cinematic Sequencer Editor
// ============================================================================
class SequencerEditorModule : public EditorModule
{
    Q_OBJECT
public:
    explicit SequencerEditorModule(QWidget* parent = nullptr);
    ~SequencerEditorModule() override;

    // EditorModule interface
    bool initialize() override;
    void shutdown() override;
    QString moduleName() const override { return "Cinematic Sequencer"; }
    QString moduleId() const override { return "sequencer"; }
    int getModulePriority() const override { return 38; }

    QDockWidget* getOrCreateDockWidget(QMainWindow* mainWindow) override;

    // File operations
    void exportFile(const QString& path) override;
    void importFile(const QString& path) override;
    void newProject(const QString& name, const QString& path) override;
    void saveProject(const QString& path) override;

    // Serialization
    QJsonObject serializeProject() const override;
    void deserializeProject(const QJsonObject& json) override;

    // ---- Timeline management ------------------------------------------------
    SequencerTimeline& timeline() { return m_timeline; }
    const SequencerTimeline& timeline() const { return m_timeline; }

    void setTimeline(const SequencerTimeline& tl) { m_timeline = tl; emit timelineChanged(); }

    // ---- Playback -----------------------------------------------------------
    void play();
    void pause();
    void stop();
    void seek(float timeSeconds);
    bool isPlaying() const { return m_playing; }
    float currentTime() const { return m_timeline.currentTime; }

    // ---- Track CRUD ---------------------------------------------------------
    void addTrack(const QString& name, SequencerTrackType type);
    void removeTrack(const QUuid& trackId);
    void muteTrack(const QUuid& trackId, bool muted);
    void soloTrack(const QUuid& trackId, bool solo);

    // ---- Camera cuts --------------------------------------------------------
    void addCameraCut(const SequencerCameraCut& cut);
    void removeCameraCut(int index);
    void updateCameraCut(int index, const SequencerCameraCut& cut);

    // ---- Fade cues ----------------------------------------------------------
    void addFadeCue(const SequencerFadeCue& cue);
    void removeFadeCue(int index);

    // ---- Text cues ----------------------------------------------------------
    void addTextCue(const SequencerTextCue& cue);
    void removeTextCue(int index);

    // ---- Audio cues ---------------------------------------------------------
    void addAudioCue(const SequencerAudioCue& cue);
    void removeAudioCue(int index);

    // ---- Event cues ---------------------------------------------------------
    void addEventCue(const SequencerEventCue& cue);
    void removeEventCue(int index);

    // ---- Render to video ----------------------------------------------------
    bool renderToVideo(const QString& outputPath, int width = 1920, int height = 1080,
                       float fps = 30.0f);

signals:
    void timelineChanged();
    void playbackStarted();
    void playbackStopped();
    void timeChanged(float timeSeconds);
    void cameraCutAdded(int index);
    void cueAdded(const QString& type, int index);

private slots:
    void onPlaybackTick();

private:
    void buildUI();

    SequencerTimeline m_timeline;
    QTimer m_playbackTimer;
    bool m_playing = false;
    float m_playbackSpeed = 1.0f;
};

} // namespace ks
