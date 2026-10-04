#ifndef KSAUDIOBANKGENERATOR_H
#define KSAUDIOBANKGENERATOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

namespace ks {
namespace audio {

class KSAudioBankGenerator : public QObject {
    Q_OBJECT

public:
    enum class GenerationMode {
        UseExistingTemplate,
        FromScratch
    };

    enum class EventType {
        EngineInterior,
        EngineExterior,
        Clutch,
        Brake,
        Accelerator,
        Engine,
        Exhaust,
        GearShiftInterior,
        GearShiftExterior,
        Transmission,
        Turbo,
        Wastegate,
        Blowoff,
        TireRoll,
        TireSlip,
        Wind,
    };

    struct RPMSample {
        int     rpm           = 0;
        QString onLoadFile;
        QString offLoadFile;
        float   volumeOffset  = 0.0f;
        float   pitchOffset   = 0.0f;
        int     loopStart     = 0;
        int     offLoopStart  = 0;

        RPMSample() = default;
        RPMSample(int r) : rpm(r) {}
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

    explicit KSAudioBankGenerator(QObject* parent = nullptr);
    ~KSAudioBankGenerator() = default;

    void setAudioProjectPath(const QString& path);
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
    bool copySamplesToAssets(const QString& assetsDir);
    bool createDirectoryStructure();
    QString getEventName(EventType type) const;

    int calculateRPMParameter(float rpm, int minRPM, int maxRPM) const;
    float calculatePitch(float rpm, int baseRPM) const;
    float calculateVolume(float rpm, int idleRPM, int redlineRPM) const;

    QString m_audioProjectPath;
    QString m_carName;
    QString m_templateCarName;
    QString m_sampleDirectory;
    QString m_lastError;
    QString m_lastGeneratedPath;
    GenerationMode m_mode = GenerationMode::FromScratch;

    EngineConfig m_engineConfig;
    QVector<RPMSample> m_samples;
    QMap<EventType, QVector<RPMSample>> m_eventSamples;

    int  detectLoopPoint(const QString& wavPath) const;
};

}
}

#endif
