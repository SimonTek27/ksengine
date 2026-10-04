#ifndef KSAUDIOGENERATOR_H
#define KSAUDIOGENERATOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

namespace ks {
namespace audio {

class KSAudioGenerator : public QObject {
    Q_OBJECT

public:
    enum class GenerationMode {
        UseExistingTemplate,
        FromScratch
    };

    enum class EventType {
        EngineInterior,
        EngineExterior,
        Turbo,
        Wastegate,
        Blowoff,
        TireRoll,
        TireSlip,
        Wind,
        Brake
    };

    struct RPMSample {
        int rpm;
        QString onLoadFile;
        QString offLoadFile;
        float volumeOffset;
        float pitchOffset;

        RPMSample(int r = 0) : rpm(r), volumeOffset(0.0f), pitchOffset(0.0f) {}
    };

    struct EngineConfig {
        int minRPM;
        int maxRPM;
        int idleRPM;
        int redlineRPM;
        int limiterRPM;
        float limiterVolume;
        float displacement;
        int cylinders;
        QString engineType;
        QString carName;
    };

    explicit KSAudioGenerator(QObject* parent = nullptr);
    ~KSAudioGenerator() = default;

    void setProjectPath(const QString& path);
    void setCarName(const QString& name);
    void setGenerationMode(GenerationMode mode);
    void setTemplateCarName(const QString& name);
    void setSampleDirectory(const QString& dir);

    void setEngineConfig(const EngineConfig& config);
    EngineConfig engineConfig() const { return m_engineConfig; }

    void addRPMSample(int rpm, const QString& onLoadFile, const QString& offLoadFile = QString());
    void clearRPMSamples();

    bool generate();
    QString lastGeneratedProjectPath() const { return m_lastGeneratedPath; }
    QString lastError() const { return m_lastError; }

    QString generateEventPath(EventType type) const;
    static EventType eventTypeFromString(const QString& str);
    static QString eventTypeToString(EventType type);

signals:
    void generationStarted();
    void generationProgress(int percent);
    void generationCompleted(const QString& projectPath);
    void generationFailed(const QString& error);

private:
    QString generateKSAudioProject();

    bool copySamplesToAssets(const QString& assetsDir);
    bool createDirectoryStructure();
    QString getEventName(EventType type) const;

    int calculateRPMParameter(float rpm, int minRPM, int maxRPM) const;
    float calculatePitch(float rpm, int baseRPM) const;
    float calculateVolume(float rpm, int idleRPM, int redlineRPM) const;

    QString m_projectPath;
    QString m_carName;
    QString m_templateCarName;
    QString m_sampleDirectory;
    QString m_lastError;
    QString m_lastGeneratedPath;
    GenerationMode m_mode = GenerationMode::FromScratch;

    EngineConfig m_engineConfig;
    QVector<RPMSample> m_samples;
    QMap<EventType, QVector<RPMSample>> m_eventSamples;
};

}
}

#endif
