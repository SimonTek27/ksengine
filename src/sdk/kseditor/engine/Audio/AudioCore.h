#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QUuid>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMutex>

namespace ks {
namespace audio {

struct KSAudioAutomationPoint { float position; float value; };
struct KSAudioSound {
    QString name;
    QString audioFilePath;
    QUuid audioFileGuid;
    int startPosition = 0;
    int length = 0;
    float volume = 1.0f;
    bool loop = false;
};

struct KSBankInfo { QUuid guid; QString name; QString filePath; };
struct KSEventInfo { QString name; QUuid guid; QString path; float volume = 1.0f; float pitch = 1.0f; bool is3D=false; bool loop=false; };
struct KSBusInfo { QUuid guid; QString name; };
struct KSVCAInfo { QUuid guid; QString name; float volume = 1.0f; };
struct KSEventParameter { QString name; float minValue=0.0f; float maxValue=1.0f; float defaultValue=0.0f; };

// Simple in-process audio studio using Qt Multimedia for I/O
class Studio : public QObject {
    Q_OBJECT
public:
    explicit Studio(QObject* parent = nullptr);
    ~Studio();

    bool openOutput(const QAudioFormat& fmt);
    void closeOutput();
    bool openInput(const QAudioFormat& fmt);
    void closeInput();

    QIODevice* outputDevice() { return m_outputDevice; }
    QIODevice* inputDevice() { return m_inputDevice; }

    // Real-time preview / audition
    bool previewEvent(const QString& eventPath, const QString& audioFilePath,
                      float volume = 1.0f, float pitch = 1.0f, bool loop = false);
    void stopPreview();
    bool isPreviewing() const { return m_previewing; }
    void setPreviewVolume(float volume);
    void setPreviewPitch(float pitch);

signals:
    void previewStarted(const QString& eventPath);
    void previewStopped();
    void previewError(const QString& msg);
    void error(const QString& msg);

private:
    void processPreviewOutput();

    QAudioSink* m_audioOut = nullptr;
    QAudioSource* m_audioIn = nullptr;
    QIODevice* m_outputDevice = nullptr;
    QIODevice* m_inputDevice = nullptr;
    QMutex m_mutex;

    // Preview state
    bool m_previewing = false;
    QString m_previewEventPath;
    QVector<float> m_previewSamples;
    int m_previewSampleRate = 44100;
    int m_previewChannels = 2;
    int m_previewPosition = 0;
    float m_previewVolume = 1.0f;
    float m_previewPitch = 1.0f;
    bool m_previewLoop = false;
};

class KSAudioCore : public QObject {
    Q_OBJECT

public:
    static KSAudioCore* instance();

    bool initialize(const QString& simContentPath = QString());
    void shutdown();

    bool isInitialized() const { return m_initialized; }
    QString version() const { return m_version; }
    QString simContentPath() const { return m_simContentPath; }

    float getCPUUsage() const;

    Studio* studio() { return m_studio; }

signals:
    void initialized(bool success);
    void errorOccurred(const QString& error);
    void cpuUsageUpdated(float usage);

private:
    explicit KSAudioCore(QObject* parent = nullptr);
    ~KSAudioCore();

    bool initSystem();
    void cleanupSystem();

    bool m_initialized = false;
    QString m_version;
    QString m_simContentPath;
    Studio* m_studio = nullptr;

    static KSAudioCore* s_instance;
    static QMutex s_mutex;

    Q_DISABLE_COPY(KSAudioCore)
};

// Minimal manager/containers
class Manager {};
class Bank {};
class Generator {};

} // namespace audio
} // namespace ks
