#ifndef WAVE_PROCESSOR_H
#define WAVE_PROCESSOR_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QAudioFormat>
#include <QFile>

class WaveProcessor : public QObject
{
    Q_OBJECT

public:
    explicit WaveProcessor(QObject *parent = nullptr);
    ~WaveProcessor();

    bool load(const QString &filePath) { return loadWav(filePath); }
    bool load(const QString &filePath, const QString & /*options*/) { return loadWav(filePath); }
    bool loadWav(const QString &filePath);
    bool save(const QString &filePath) { return saveWav(filePath); }
    bool saveWav(const QString &filePath);
    void clear();

    const QVector<float>& getSamples() const { return m_samples; }
    int getSampleCount() const { return m_samples.size(); }
    int getChannelCount() const { return m_format.channelCount(); }
    int getSampleRate() const { return m_format.sampleRate(); }
    int getBitDepth() const { return m_format.bytesPerSample() * 8; }
    qint64 getDurationMs() const;

    void setSamples(const QVector<float> &samples) { m_samples = samples; }
    void setSamples(const QVector<float> &samples, int channels, int sampleRate) { m_samples = samples; m_format.setChannelCount(channels); m_format.setSampleRate(sampleRate); }
    void setFormat(const QAudioFormat &format) { m_format = format; }

    void reverse();
    void fadeIn(int startMs, int durationMs);
    void fadeOut(int endMs, int durationMs);
    void normalize(float level = 1.0f);
    void amplify(float factor);
    void invert();
    void silence(int startMs, int endMs);

    void insertSilence(int positionMs, int durationMs);
    void deleteRegion(int startMs, int endMs);
    void copyRegion(int startMs, int endMs);
    void pasteRegion(int positionMs);

    void applyLowPassFilter(float cutoffFreq, float resonance);
    void applyHighPassFilter(float cutoffFreq, float resonance);
    void applyBandPassFilter(float lowFreq, float highFreq);
    void applyNotchFilter(float freq, float bandwidth);

    void applyDelay(float delayMs, float feedback, float mix);
    void applyReverb(float roomSize, float damping, float wetDry);
    void applyEcho(float delayMs, float feedback, float mix);
    void applyChorus(float depth, float rate, float mix);
    void applyFlanger(float depth, float rate, float mix);

    void applyCompressor(float threshold, float ratio, float attack, float release, float makeupGain);
    void applyLimiter(float threshold, float release);

    void resample(int newSampleRate);
    void convertChannels(int channels);
    void convertBitDepth(int bits);

    QVector<float> getRegion(int startMs, int endMs);
    void setRegion(int startMs, const QVector<float> &samples);

    void undo() { if (m_undoStack.size() > 0) { m_samples = m_undoStack.takeLast(); emit samplesModified(); } }
    void redo() { if (m_redoStack.size() > 0) { m_samples = m_redoStack.takeLast(); emit samplesModified(); } }
    bool hasUndo() const { return m_undoStack.size() > 0; }
    bool hasRedo() const { return m_redoStack.size() > 0; }

    void pushUndoState() {
        m_undoStack.append(m_samples);
        m_redoStack.clear();
        if (m_undoStack.size() > 100) m_undoStack.removeFirst();
    }

    void applyFadeIn(int durationMs) { fadeIn(0, durationMs); }
    void applyFadeOut(int durationMs) { fadeOut(getDurationMs(), durationMs); }

signals:
    void samplesModified();
    void loadComplete();
    void saveComplete();
    void error(const QString &message);

private:
    struct WavHeader {
        char riff[4];
        quint32 fileSize;
        char wave[4];
        char fmt[4];
        quint32 fmtSize;
        quint16 audioFormat;
        quint16 channels;
        quint32 sampleRate;
        quint32 byteRate;
        quint16 blockAlign;
        quint16 bitsPerSample;
        char data[4];
        quint32 dataSize;
    };

    QVector<float> m_samples;
    QAudioFormat m_format;
    QVector<QVector<float>> m_undoStack;
    QVector<QVector<float>> m_redoStack;
    QVector<float> m_clipboard;

    float samplesToFloat(const char *bytes, int size);
    void floatToSamples(float value, char *bytes, int size);

    void processSamples(std::function<void(float&, float&)> processor);
    void processSamplesMono(std::function<void(float&)> processor);

    float m_sampleHold;
};

#endif // WAVE_PROCESSOR_H