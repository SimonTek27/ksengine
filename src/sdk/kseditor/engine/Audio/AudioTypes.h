#ifndef KSAUDIO_H
#define KSAUDIO_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVariantMap>
#include <QMap>
#include <QMutex>
#include <QTimer>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QFileSystemWatcher>

#include "AudioVST3Host.h"

namespace ks {
namespace audio {

// ============================================================================
// Types
// ============================================================================

enum class AttenuationModel { None, Linear, Logarithmic, Inverse };
enum class EffectType { LowPass, HighPass, BandPass, Reverb, Delay, Chorus, Flanger, Compressor, Limiter };
enum class AutomationParameter { Volume, Pan, LowPassCutoff, HighPassCutoff, ReverbMix, Pitch };

struct AutomationPoint {
    float time = 0.0f;       // time in seconds
    float value = 0.0f;      // parameter value
    float tension = 0.5f;    // curve tension (0=linear, 1=exponential)

    AutomationPoint() = default;
    AutomationPoint(float t, float v, float tn = 0.5f) : time(t), value(v), tension(tn) {}
};

struct AutomationLane {
    AutomationParameter parameter = AutomationParameter::Volume;
    QVector<AutomationPoint> points;
    bool enabled = true;

    float getValueAtTime(float timeSec) const;
    void addPoint(const AutomationPoint& point);
    void removePoint(int index);
    void updatePoint(int index, float time, float value);
};

enum class EventStatus { Ready, Playing, Stopped, Paused };

struct AudioEvent {
    QString id;
    QString name;
    QString audioFile;
    EventStatus status = EventStatus::Ready;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    AttenuationModel attenuation = AttenuationModel::Inverse;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    QVector3D position;
    bool is3D = true;
    QVariantMap parameters;

    // Automation lanes
    QVector<AutomationLane> automationLanes;

    float getParameter(const QString& name, float defaultValue = 0.0f) const {
        return parameters.contains(name) ? parameters[name].toFloat() : defaultValue;
    }
    void setParameter(const QString& name, float value) { parameters[name] = value; }

    AutomationLane* findAutomationLane(AutomationParameter param);
    AutomationLane& addAutomationLane(AutomationParameter param);
    void removeAutomationLane(AutomationParameter param);
    float getAutomatedValue(AutomationParameter param, float timeSec) const;
};

struct SoundBank {
    QString id;
    QString name;
    QString filePath;
    QVector<AudioEvent> events;
    bool isLoaded = false;

    // Pre-decoded audio samples from FMOD banks
    QMap<QString, QVector<float>> preloadedSamples;     // sound name -> PCM samples
    QMap<QString, quint32> preloadedSampleRate;          // sound name -> sample rate
    QMap<QString, quint32> preloadedChannels;            // sound name -> channel count

    AudioEvent* findEvent(const QString& eventName) {
        for (auto& e : events) if (e.name == eventName) return &e;
        return nullptr;
    }
    const AudioEvent* findEvent(const QString& eventName) const {
        for (const auto& e : events) if (e.name == eventName) return &e;
        return nullptr;
    }
};

struct BusInfo {
    QString name;
    float volume = 1.0f;
    float fader = 1.0f;
    bool enabled = true;
    bool muted = false;
    QVector<QString> effectChain;
    QStringList eventPaths;
};

struct MixerConfig {
    BusInfo masterBus{ "Master", 1.0f, 1.0f };
    QVector<BusInfo> busses;
};

struct ListenerInfo {
    QVector3D position{0,0,0}, forward{0,0,1}, up{0,1,0}, velocity{0,0,0};
};

struct PlaybackInstance {
    int id;
    QString eventPath, audioFile;
    float volume = 1.0f, pitch = 1.0f;
    bool loop = false, is3D = true;
    QVector3D position;
    QVector<float> samples;
    int sampleRate = 44100, channels = 2;
    qint64 currentSample = 0;
    bool isPlaying = false, isPaused = false;
    QVariantMap parameters;
    PlaybackInstance(int i) : id(i) {}

    float getParameter(const QString &name, float defaultValue = 0.0f) const {
        return parameters.contains(name) ? parameters[name].toFloat() : defaultValue;
    }
    void setParameter(const QString &name, float value) { parameters[name] = value; }
};

struct AttenuationSettings {
    float minDistance = 1.0f, maxDistance = 100.0f, rolloffFactor = 1.0f;
};

struct DSPEffect {
    QString name;
    EffectType type;
    bool enabled = true;
    QVector<float> parameters;
    DSPEffect(const QString& n, EffectType t) : name(n), type(t) {}
};

// ============================================================================
// KSAudioEngine - Main real-time audio engine
// ============================================================================

class KSAudioEngine : public QObject {
    Q_OBJECT
public:
    static KSAudioEngine* instance();
    explicit KSAudioEngine(QObject* parent = nullptr);
    ~KSAudioEngine();

    bool initialize(int sampleRate = 44100, int channels = 2, int bufferSize = 2048);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    QStringList loadedBanks() const;
    bool loadBank(const QString& bankPath);
    void unloadBank(const QString& bankName);
    void unloadAllBanks();

    // Hot-reload support
    void enableHotReload(bool enable);
    bool isHotReloadEnabled() const { return m_hotReloadEnabled; }
    void reloadBank(const QString& bankName);

    QStringList getEvents(const QString& bankName) const;
    AudioEvent getEventInfo(const QString& eventPath) const;

    int playEvent(const QString& eventPath, bool paused = false);
    void stopEvent(int instanceId);
    void stopAllEvents();

    bool setEventVolume(int instanceId, float volume);
    bool setEventPitch(int instanceId, float pitch);
    bool setEventPosition(int instanceId, const QVector3D& position);
    bool setEventParameter(int instanceId, const QString& param, float value);
    bool setEventParameterByPath(const QString& eventPath, const QString& param, float value);

    void set3DListenerPosition(const QVector3D& pos, const QVector3D& fwd, const QVector3D& up);
    void set3DListenerVelocity(const QVector3D& velocity);
    void setMasterVolume(float volume);
    float masterVolume() const { return m_masterVolume; }
    int activeEventCount() const;

    void update();

signals:
    void initialized();
    void error(const QString& message);
    void eventStarted(int instanceId, const QString& eventPath);
    void eventStopped(int instanceId);
    void bankLoaded(const QString& bankName);
    void bankUnloaded(const QString& bankName);
    void bankReloaded(const QString& bankName);

private:
    static KSAudioEngine* s_instance;
    static QMutex s_mutex;
    bool m_initialized = false;
    int m_sampleRate = 44100, m_channels = 2, m_nextInstanceId = 0;
    float m_masterVolume = 1.0f;
    QAudioFormat m_format;
    QAudioSink* m_audioSink = nullptr;
    QIODevice* m_outputDevice = nullptr;
    QVector<SoundBank> m_banks;
    QMap<QString, SoundBank*> m_bankMap;
    QVector<PlaybackInstance> m_activeInstances;
    ListenerInfo m_listener;
    QTimer* m_updateTimer = nullptr;
    mutable QMutex m_instancesMutex;

    // Hot-reload
    bool m_hotReloadEnabled = false;
    QFileSystemWatcher* m_bankWatcher = nullptr;
    QMap<QString, QString> m_bankPathByName;  // bank name → file path

    void onBankFileChanged(const QString& path);

    float applyAttenuation(const AudioEvent& event, const QVector3D& sourcePos) const;
    QVector3D calculatePan(const QVector3D& sourcePos) const;
    bool loadAudioFile(const QString& filePath, QVector<float>& samples, int& sampleRate, int& channels);
    void mixAudio(char* buffer, qint64 bytes);
    friend class KSAudioIODevice;
};

class KSAudioEngine;

class KSAudioIODevice : public QIODevice {
    Q_OBJECT
public:
    explicit KSAudioIODevice(KSAudioEngine* e, QObject* p = nullptr);
    ~KSAudioIODevice() override;

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;

private:
    KSAudioEngine* m_engine = nullptr;
};

// ============================================================================
// KSAudioProject - .ksaudio project format
// ============================================================================
// Supports both v1 (simple) and v2 (full FSPRO-compatible) schemas.
// v2 preserves the complete data model for round-tripping.

class KSAudioProject : public QObject {
    Q_OBJECT
public:
    explicit KSAudioProject(QObject* parent = nullptr);

    // File I/O
    bool load(const QString& filePath);
    bool save(const QString& filePath);
    bool isLoaded() const { return m_loaded; }
    QString filePath() const { return m_filePath; }

    // Format info
    QString schemaVersion() const { return m_schemaVersion; }
    QString projectName() const { return m_projectName; }
    void setProjectName(const QString& n) { m_projectName = n; }
    QString projectGuid() const { return m_projectGuid; }
    void setProjectGuid(const QString& g) { m_projectGuid = g; }

    // Banks — the runtime-focused view
    QVector<SoundBank>& banks() { return m_banks; }
    const QVector<SoundBank>& banks() const { return m_banks; }
    SoundBank* findBank(const QString& name);
    bool addBank(const QString& name);
    bool removeBank(const QString& name);
    bool addEvent(const QString& bankName, const AudioEvent& event);

    // Mixer
    MixerConfig& mixer() { return m_mixer; }
    const MixerConfig& mixer() const { return m_mixer; }

    // Full round-trip access — stores the complete JSON document so no data
    // is lost when loading a v2 (FSPRO-format) project and saving it back.
    QJsonObject rawDocument() const { return m_rawDocument; }
    void setRawDocument(const QJsonObject& doc) { m_rawDocument = doc; }

signals:
    void loaded(); void saved(); void error(const QString& message);

private:
    QString m_filePath, m_projectName = "Untitled", m_schemaVersion = "2.0.0", m_projectGuid;
    bool m_loaded = false;
    QVector<SoundBank> m_banks;
    MixerConfig m_mixer;
    QJsonObject m_rawDocument;  // full JSON for v2 round-tripping

    void loadFromJson(const QJsonObject& root);
    void loadV2FromJson(const QJsonObject& root);
    QJsonObject toJson() const;
};

// ============================================================================
// KSAudioMixer - Mixer with buses and effects
// ============================================================================

class KSAudioMixer : public QObject {
    Q_OBJECT
public:
    explicit KSAudioMixer(QObject* parent = nullptr);
    float masterVolume() const { return m_masterVolume; }
    void setMasterVolume(float volume);
    int addBus(const QString& name, float volume = 1.0f);
    bool removeBus(int busIndex);
    void clearBuses();
    int busCount() const { return m_buses.size(); }
    QString busName(int i) const;
    float busVolume(int i) const;
    void setBusVolume(int i, float volume);
    void addEffectToBus(int busIndex, const QString& effectName);
    void removeEffectFromBus(int busIndex, int effectIndex);
    void processMix(float* output, int sampleCount, int channels);

signals:
    void masterVolumeChanged(float);
    void busAdded(int, const QString&);
    void busRemoved(int);
    void busVolumeChanged(int, float);

private:
    float m_masterVolume = 1.0f;
    QVector<BusInfo> m_buses;
    QMap<QString, int> m_busNameMap;
};

// ============================================================================
// KSAudio3D - 3D audio positioning
// ============================================================================

class KSAudio3D : public QObject {
    Q_OBJECT
public:
    explicit KSAudio3D(QObject* parent = nullptr);
    void setListenerPosition(const QVector3D& p);
    void setListenerOrientation(const QVector3D& fwd, const QVector3D& up);
    void setListenerVelocity(const QVector3D& v);
    QVector3D listenerPosition() const { return m_listenerPos; }
    float calculateAttenuation(const QVector3D& src, AttenuationModel model, const AttenuationSettings& s) const;
    QVector3D calculatePan(const QVector3D& src) const;
    float calculateDopplerPitch(const QVector3D& src, const QVector3D& vel, float basePitch) const;
    void setDopplerFactor(float f) { m_dopplerFactor = f; }
    float dopplerFactor() const { return m_dopplerFactor; }

signals:
    void listenerPositionChanged(const QVector3D&);
    void listenerOrientationChanged(const QVector3D&, const QVector3D&);

private:
    QVector3D m_listenerPos{0,0,0}, m_listenerForward{0,0,1}, m_listenerUp{0,1,0}, m_listenerVelocity{0,0,0};
    float m_dopplerFactor = 1.0f, m_distanceFactor = 1.0f, m_speedOfSound = 343.3f;
};

// ============================================================================
// KSAudioDSP - Real-time DSP effects
// ============================================================================

class KSAudioDSP : public QObject {
    Q_OBJECT
public:
    explicit KSAudioDSP(QObject* parent = nullptr);
    int addEffect(EffectType type, const QString& name);
    bool removeEffect(int index);
    void clearEffects();
    DSPEffect* getEffect(int index);
    int effectCount() const { return m_effects.size(); }
    void process(float* samples, int sampleCount, int channels, int sampleRate);
    void setEffectParameter(int effectIndex, int paramIndex, float value);
    void enableEffect(int index, bool enable);

signals:
    void effectAdded(int, const QString&);
    void effectRemoved(int);
    void effectEnabledChanged(int, bool);
    void parameterChanged(int, int, float);

private:
    QVector<DSPEffect> m_effects;
    void processLowPass(float*, int, int, float, float, int);
    void processHighPass(float*, int, int, float, float, int);
    void processReverb(float*, int, int, float, float, float);
    void processDelay(float*, int, int, float, float, float, int);
    void processCompressor(float*, int, int, float, float, float, float, float);
};

// ============================================================================
// KSAudioBankExporter - Export .ksaudio bank files
// ============================================================================

class KSAudioBankExporter : public QObject {
    Q_OBJECT
public:
    explicit KSAudioBankExporter(QObject* parent = nullptr);
    bool exportBank(const SoundBank& bank, const QString& outputPath);
    bool exportProject(const KSAudioProject& project, const QString& outputDir);
    void setCompressionEnabled(bool e) { m_compressionEnabled = e; }

signals:
    void exportStarted(const QString&);
    void exportProgress(int);
    void exportCompleted(const QString&);
    void exportFailed(const QString&);

private:
    bool m_compressionEnabled = false;
    bool writeBankHeader(QFile& file, const SoundBank& bank);
    bool writeEventData(QFile& file, const AudioEvent& event);
};

} // namespace audio
} // namespace ks

#endif // KSAUDIO_H
