#ifndef KSRPMRECORDER_H
#define KSRPMRECORDER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>
#include <QAudioSource>
#include <QFile>
#include <QCoreApplication>
#include <windows.h>

namespace ks {
namespace audio {

class KSRPMRecorder : public QObject {
    Q_OBJECT

public:
    enum class RecordingState {
        Idle,
        Connecting,
        Connected,
        RecordingRPM,
        RecordingSample,
        Stopped
    };

    enum class LoadType {
        OnLoad,
        OffLoad
    };

    struct RPMPoint {
        float rpm;
        float durationMs;
        bool skip = false;
    };

    struct EngineSimConnection {
        void* hookDLL;
        bool(*Initialize)(void);
        bool(*IsConnected)(void);
        float(*GetRPM)(void);
        float(*GetThrottle)(void);
        float(*GetLoad)(void);
        float(*GetSpeed)(void);
        void(*SetTargetRPM)(float rpm);
        void(*ClearTargetRPM)(void);
    };

    explicit KSRPMRecorder(QObject* parent = nullptr);
    ~KSRPMRecorder();

    bool connectToEngineSim();
    void disconnect();

    void setRPMPoints(const QVector<RPMPoint>& points);
    void setOutputDirectory(const QString& dir);
    QString outputDirectory() const { return m_outputDir; }
    void setSamplePrefix(const QString& prefix);
    void setSampleRate(int rate);
    void setChannels(int channels);
    void setRecordingLoadType(LoadType type);
    void setHoldDuration(int ms);

    bool startRecording();
    void stopRecording();

    RecordingState state() const { return m_state; }
    float currentRPM() const { return m_currentRPM; }
    float recordingProgress() const { return m_progress; }
    int currentRPMIndex() const { return m_currentRPMIndex; }

signals:
    void connected(bool success);
    void stateChanged(RecordingState state);
    void rpmReached(float rpm);
    void sampleRecorded(const QString& filePath, float rpm, LoadType type);
    void recordingProgress(float progress);
    void recordingCompleted();
    void error(const QString& message);

private slots:
    void onUpdateTimer();

private:
    bool loadHookDLL();
    void unloadHookDLL();
    float readRPM();
    void setTargetRPM(float rpm);
    void clearTargetRPM();

    bool startSampleRecording();
    void stopSampleRecording();
    void saveSample(LoadType type);
    void finalizeCurrentRPM();

    void transitionToState(RecordingState newState);
    int findNextUnskippedRPM();

    RecordingState m_state = RecordingState::Idle;
    QString m_outputDir;
    QString m_samplePrefix = "engine";
    int m_sampleRate = 44100;
    int m_channels = 2;
    LoadType m_loadType = LoadType::OnLoad;
    int m_holdDurationMs = 3000;
    int m_currentRPMIndex = -1;
    float m_currentRPM = 0.0f;
    float m_progress = 0.0f;

    QVector<RPMPoint> m_rpmPoints;
    QVector<bool> m_rpmRecorded;

    QTimer* m_updateTimer = nullptr;
    QAudioSource* m_audioSource = nullptr;
    QFile m_outputFile;
    qint64 m_sampleStartTime = 0;
    int m_sampleHoldCount = 0;
    QVector<float> m_capturedSamples;

    HMODULE m_hookDLL = nullptr;
    EngineSimConnection m_hook;
};

}
}

#endif
