#ifndef KSAUDIOMODULEBRIDGE_H
#define KSAUDIOMODULEBRIDGE_H

#include <QObject>
#include <QString>
#include <QVector>

namespace ks {
namespace audio {

class KSRPMRecorder;
class KSCarAcoustics;
class KSAudioGenerator;
class KSRPMProfile;

class KSAudioModuleBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(float currentRPM READ currentRPM NOTIFY rpmChanged)
    Q_PROPERTY(float recordingProgress READ recordingProgress NOTIFY progressChanged)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory)
    Q_PROPERTY(QString samplePrefix READ samplePrefix WRITE setSamplePrefix)
    Q_PROPERTY(int currentRPMIndex READ currentRPMIndex NOTIFY rpmIndexChanged)

public:
    explicit KSAudioModuleBridge(QObject* parent = nullptr);
    ~KSAudioModuleBridge();

    void initialize();
    void shutdown();

    bool isConnected() const;
    float currentRPM() const;
    float recordingProgress() const;
    bool isRecording() const;
    QString outputDirectory() const;
    void setOutputDirectory(const QString& dir);
    QString samplePrefix() const;
    void setSamplePrefix(const QString& prefix);
    int currentRPMIndex() const;
    int totalRPMPoints() const;
    int samplesRecorded() const;

    Q_INVOKABLE bool connectToEngineSim();
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void emergencyStop();

    Q_INVOKABLE void loadProfile(int index);
    Q_INVOKABLE void saveCurrentProfile();
    Q_INVOKABLE void createProfile(const QString& name, const QString& engineType);

    Q_INVOKABLE void setAcousticsMode(int mode);
    Q_INVOKABLE void setCarType(int type);
    Q_INVOKABLE void setExteriorPreset(int preset);
    Q_INVOKABLE void setInteriorPreset(int preset);
    Q_INVOKABLE void processSelectedSamples();

    Q_INVOKABLE void setProjectPath(const QString& path);
    Q_INVOKABLE void setCarName(const QString& name);
    Q_INVOKABLE void setGenerationMode(int mode);
    Q_INVOKABLE void setTemplateCarName(const QString& name);
    Q_INVOKABLE bool generateScript();
    Q_INVOKABLE bool validateProject();
    Q_INVOKABLE void openProject();

    Q_INVOKABLE QVector<int> getRPMPoints() const;
    Q_INVOKABLE void setRPMPoints(const QVector<int>& points);

signals:
    void connectedChanged(bool connected);
    void rpmChanged(float rpm);
    void progressChanged(float progress);
    void recordingStateChanged(bool recording);
    void rpmIndexChanged(int index);
    void sampleRecorded(const QString& filePath, int rpm, int loadType);
    void recordingCompleted();
    void error(const QString& message);

    void acousticsModeChanged(int mode);
    void carTypeChanged(int type);

    void audioGenerationStarted();
    void audioGenerationProgress(int percent);
    void audioGenerationCompleted(const QString& projectPath);
    void audioGenerationFailed(const QString& error);

private:
    void connectSignals();
    void updateRPMPoints();

    KSRPMRecorder* m_recorder = nullptr;
    KSCarAcoustics* m_acoustics = nullptr;
    KSAudioGenerator* m_audioGenerator = nullptr;
    KSRPMProfile* m_currentProfile = nullptr;

    QString m_outputDirectory;
    QString m_samplePrefix = "engine";
    int m_samplesRecorded = 0;
};

}
}

#endif
