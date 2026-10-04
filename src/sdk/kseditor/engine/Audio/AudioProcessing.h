#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QDir>
#include <QFile>

namespace ks {

namespace audio {

class BatchAudioProcessor : public QObject
{
    Q_OBJECT
public:
    explicit BatchAudioProcessor(QObject* parent = nullptr) : QObject(parent) {}
    ~BatchAudioProcessor() {}

    struct ProcessingJob {
        QString inputPath;
        QString outputPath;
        QStringList effects;
        QMap<QString, float> params;
    };

    void addJob(const ProcessingJob& job);
    void clearJobs();
    QVector<ProcessingJob> jobs() const { return m_jobs; }

    void setOutputFormat(const QString& format) { m_outputFormat = format; }
    QString outputFormat() const { return m_outputFormat; }

    void process();
    bool processFile(const QString& input, const QString& output);

signals:
    void progress(int percent);
    void status(const QString& msg);
    void finished();

private:
    QVector<ProcessingJob> m_jobs;
    QString m_outputFormat = "wav";
    int m_currentJob = 0;
};

class WaveformMouseEditor : public QObject
{
    Q_OBJECT
public:
    explicit WaveformMouseEditor(QObject* parent = nullptr) : QObject(parent) {}
    ~WaveformMouseEditor() {}

    void setSampleData(const QVector<float>& samples) { m_samples = samples; }
    QVector<float> sampleData() const { return m_samples; }

    enum EditMode { Draw, Erase, Smooth, FadeIn, FadeOut, Normalize };
    void setEditMode(EditMode mode) { m_editMode = mode; }
    EditMode editMode() const { return m_editMode; }

    void setBrushSize(int samples) { m_brushSize = samples; }
    int brushSize() const { return m_brushSize; }

    void setBrushStrength(float strength) { m_brushStrength = strength; }
    float brushStrength() const { return m_brushStrength; }

    void drawAt(int sampleIndex, float value);
    void eraseAt(int sampleIndex);
    void smoothAt(int sampleIndex);
    void fadeInAt(int sampleIndex);
    void fadeOutAt(int sampleIndex);

    void undo() { if (!m_undoStack.isEmpty()) { m_redoStack.append(m_samples); m_samples = m_undoStack.takeLast(); } }
    void redo() { if (!m_redoStack.isEmpty()) { m_undoStack.append(m_samples); m_samples = m_redoStack.takeLast(); } }
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

signals:
    void dataChanged();

private:
    void pushUndo() { m_undoStack.append(m_samples); if (m_undoStack.size() > 100) m_undoStack.removeFirst(); m_redoStack.clear(); }

    QVector<float> m_samples;
    EditMode m_editMode = Draw;
    int m_brushSize = 100;
    float m_brushStrength = 1.0f;
    QVector<QVector<float>> m_undoStack;
    QVector<QVector<float>> m_redoStack;
};

class SoundGenerator : public QObject
{
    Q_OBJECT
public:
    explicit SoundGenerator(QObject* parent = nullptr) : QObject(parent) {}
    ~SoundGenerator() {}

    enum WaveformType { Sine, Square, Sawtooth, Triangle, Noise, Tone };

    void setWaveform(WaveformType type) { m_waveform = type; }
    WaveformType waveform() const { return m_waveform; }

    void setFrequency(float freq) { m_frequency = qBound(20.0f, freq, 20000.0f); }
    float frequency() const { return m_frequency; }

    void setDuration(float ms) { m_duration = ms; }
    float duration() const { return m_duration; }

    void setVolume(float vol) { m_volume = qBound(0.0f, vol, 1.0f); }
    float volume() const { return m_volume; }

    QVector<float> generate(int sampleRate = 44100);

signals:
    void generated();

private:
    WaveformType m_waveform = Sine;
    float m_frequency = 440.0f;
    float m_duration = 1000.0f;
    float m_volume = 0.8f;
};

class AudioGenerator : public QObject
{
    Q_OBJECT
public:
    explicit AudioGenerator(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioGenerator() {}

    enum GeneratorType { Tone, Noise, Sweep, Click, Silence, Custom };

    void setType(GeneratorType type) { m_type = type; }
    GeneratorType type() const { return m_type; }

    void setParameters(const QMap<QString, float>& params) { m_params = params; }
    QMap<QString, float> parameters() const { return m_params; }

    QVector<float> generate(int sampleRate, int channels, int durationMs);

    void setExpression(const QString& expr) { m_expression = expr; }
    QString expression() const { return m_expression; }

signals:
    void generated();

private:
    GeneratorType m_type = Tone;
    QMap<QString, float> m_params;
    QString m_expression;
};

} // namespace audio
} // namespace ks