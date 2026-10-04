#ifndef AUDIO_CLIP_H
#define AUDIO_CLIP_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QAudioFormat>
#include <QColor>
#include "WaveProcessor.h"

class AudioClip : public QObject
{
    Q_OBJECT

public:
    explicit AudioClip(QObject *parent = nullptr);
    AudioClip(const QVector<float> &samples, int channels, int sampleRate, qint64 startMs, QObject *parent = nullptr);
    ~AudioClip();

    struct EnvelopePoint {
        qint64 timeMs;    // time relative to clip start
        float gain;       // 0.0 to 1.0 (or more for boost)
        EnvelopePoint(qint64 t = 0, float g = 1.0f) : timeMs(t), gain(g) {}
    };

    QVector<float> samples;
    int channels;
    int sampleRate;
    qint64 startMs;          // offset from track start in ms
    QString name;
    QColor color;
    QVector<EnvelopePoint> envelope;  // gain envelope points

    qint64 endMs() const { return startMs + durationMs(); }
    qint64 durationMs() const { return (qint64)samples.size() / qMax(1, channels) * 1000 / sampleRate; }
    int frameCount() const { return samples.size() / qMax(1, channels); }

    WaveProcessor *processor() const;
    QVector<float> applyEnvelope() const;  // returns samples with envelope applied

    void setSamples(const QVector<float> &s, int ch, int sr) { samples = s; channels = ch; sampleRate = sr; }
    void setStartMs(qint64 ms) { startMs = ms; }
    void addEnvelopePoint(qint64 timeMs, float gain);
    void removeEnvelopePoint(int index);
    void setEnvelopePoint(int index, qint64 timeMs, float gain);

    bool containsMs(qint64 ms) const { return ms >= startMs && ms < endMs(); }

private:
    mutable WaveProcessor *m_processor = nullptr;
};

class TrackModel : public QObject
{
    Q_OBJECT

public:
    explicit TrackModel(QObject *parent = nullptr);
    ~TrackModel();

    QString name;
    float gain = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;

    QVector<AudioClip*> clips;  // sorted by startMs
    QVector<QVector<AudioClip*>> undoStack;
    QVector<QVector<AudioClip*>> redoStack;

    qint64 durationMs() const;
    int maxChannels() const;
    int maxSampleRate() const;

    AudioClip* clipAt(qint64 ms) const;
    int clipIndexAt(qint64 ms) const;

    void addClip(AudioClip *clip);
    void removeClip(int index);
    void removeClip(AudioClip *clip);
    void splitClipAt(qint64 ms);
    void trimClip(int index, qint64 newStartMs, qint64 newEndMs);
    void moveClip(int index, qint64 newStartMs);
    void clearClips();

    void pushUndoState();
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

signals:
    void clipsChanged();

private:
    void sortClips();
};

#endif // AUDIO_CLIP_H