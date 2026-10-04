#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>

namespace ks {

namespace audio {

class AIAudioAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit AIAudioAnalyzer(QObject* parent = nullptr) : QObject(parent) {}
    ~AIAudioAnalyzer() {}

    enum AudioClass { Engine, Tire, Wind, Impact, Music, Voice, Ambient, Unknown };

    struct AnalysisResult {
        AudioClass classification;
        float confidence;
        QString description;
        QVector<float> features;
    };

    struct AudioFingerprint {
        QString id;
        QVector<float> hash;
        QString name;
        int sampleRate;
    };

    AnalysisResult analyze(const QVector<float>& audio, int sampleRate);

    void addFingerprint(const AudioFingerprint& fp) { m_fingerprints[fp.id] = fp; }
    AudioFingerprint getFingerprint(const QString& id) const { return m_fingerprints.value(id); }
    QVector<AudioFingerprint> findSimilar(const QVector<float>& audio, int limit = 5) const;

    void trainModel(const QVector<AnalysisResult>& samples);

signals:
    void analysisComplete(const AnalysisResult& result);

private:
    QMap<QString, AudioFingerprint> m_fingerprints;
    QMap<AudioClass, QVector<QVector<float>>> m_trainedFeatures;
};

class EngineSoundSynth : public QObject
{
    Q_OBJECT
public:
    explicit EngineSoundSynth(QObject* parent = nullptr) : QObject(parent) {}
    ~EngineSoundSynth() {}

    enum EngineType {
        Inline4, Inline6, V6, V8, V10, V12,
        Flat4, Flat6, Flat8,
        Rotary, Diesel, Turbocharged,
        Electric, Hybrid
    };

    struct EngineParams {
        EngineType type = Inline4;
        int cylinders = 4;
        int displacement = 2000;
        float boreStrokeRatio = 1.0f;
        int maxRPM = 7000;
        int idleRPM = 800;
        float compressionRatio = 10.0f;
        bool turbocharged = false;
        int turboSize = 0;
        int numTurbos = 1;
    };

    void setEngineParams(const EngineParams& params) { m_params = params; }
    EngineParams params() const { return m_params; }

    QVector<float> generate(int sampleRate, int durationMs, float rpm);
    QVector<float> generateAtRPM(float rpm, int sampleRate);

    void setExhaustType(const QString& type) { m_exhaustType = type; }
    void setExhaustLength(float mm) { m_exhaustLength = mm; }
    void setExhaustDiameter(float mm) { m_exhaustDiameter = mm; }

    void setTurboParams(float boost, float size, float response);
    void setIntakeParams(const QString& type);

    QVector<float> generateLoad(float loadFactor, int sampleRate);

signals:
    void generated();

private:
    float calculateFiringAngle(float rpm);
    float calculatePrimaryFreq(float rpm);
    float calculateSecondaryFreq(float rpm);

    EngineParams m_params;
    QString m_exhaustType = "single";
    float m_exhaustLength = 500.0f;
    float m_exhaustDiameter = 60.0f;

    struct TurboParams { float boost = 1.0f; float size = 0.5f; float response = 0.5f; } m_turbo;
    QString m_intakeType = "natural";
};

} // namespace audio
} // namespace ks