#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QFileInfo>

namespace ks { namespace audio {

class AIAudioStemSeparator : public QObject
{
    Q_OBJECT
public:
    explicit AIAudioStemSeparator(QObject* parent = nullptr) : QObject(parent) {}
    ~AIAudioStemSeparator() {}

    static AIAudioStemSeparator* instance(QObject* parent = nullptr) {
        if (!s_instance) s_instance = new AIAudioStemSeparator(parent);
        return s_instance;
    }

    static void releaseInstance() {
        delete s_instance;
        s_instance = nullptr;
    }

    enum SeparationModel { ModelDemucs, ModelSpleeter, ModelHtdemucs, ModelFft };
    enum SeparationMethod { MethodDemucs, MethodSpleeter, MethodFft, MethodAuto };

    struct StemInfo {
        QString name;
        QString filepath;
        float duration;
        int channels;
        int sampleRate;
    };

    struct SeparationResult {
        bool success = false;
        QString inputFile;
        QString outputDirectory;
        QVector<StemInfo> stems;
        QString message;
        QString error;
        int sampleRate = 44100;
    };

    void setModel(SeparationModel model) { m_model = model; }
    SeparationModel model() const { return m_model; }

    void setMethod(SeparationMethod method) { m_method = method; }
    SeparationMethod method() const { return m_method; }

private:
    static AIAudioStemSeparator* s_instance;

    private slots:
    void separate(const QString& inputFilePath, const QString& outputDir);

signals:
    void separationStarted(const QString& inputFile, const QString& outputDir);
    void separationProgress(const QString& status, int percent);
    void separationCompleted(const SeparationResult& result);
    void separationError(const QString& error);

private:
    SeparationModel m_model = ModelFft;
    SeparationMethod m_method = MethodAuto;

    SeparationResult runPythonSeparation(const QString& inputFilePath, const QString& outputDir);
    QStringList findStemsInOutput(const QString& outputDir, const QString& inputStem);
};

}} // namespace ks::audio