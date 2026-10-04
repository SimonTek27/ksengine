#pragma once

#include <QObject>
#include <QVector>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>
#include <QMutex>

class WaveformIODevice : public QIODevice {
    Q_OBJECT
public:
    explicit WaveformIODevice(QObject* parent = nullptr);
    ~WaveformIODevice() override;

    void setData(const QVector<float>& samples, int channels, int sampleRate);
    void clear();
    void setReadPosition(qint64 ms);

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;

private:
    QVector<float> m_samples;
    int m_channels;
    int m_sampleRate;
    qint64 m_readPosition;
    qint64 m_dataSize;
    qint64 m_startPosition;
    qint64 m_endPosition;
    bool m_loopEnabled;

    friend class WaveformEngine;
};

class WaveformEngine : public QObject {
    Q_OBJECT
public:
    static WaveformEngine* instance();

    void setSamples(const QVector<float>& samples, int channels, int sampleRate);
    void clear();

    void play();
    void stop();
    void pause();

    void setPosition(qint64 ms);
    void setLoopEnabled(bool enabled);
    void setLoopRegion(qint64 startMs, qint64 endMs);

    qint64 positionMs() const { return m_positionMs; }
    qint64 durationMs() const { return m_durationMs; }
    bool isPlaying() const { return m_isPlaying; }
    bool isPaused() const { return m_isPaused; }
    bool loopEnabled() const { return m_loopEnabled; }
    qint64 loopStartMs() const { return m_loopStartMs; }
    qint64 loopEndMs() const { return m_loopEndMs; }

signals:
    void playbackStarted();
    void playbackStopped();
    void playbackPaused();
    void playbackFinished();
    void positionChanged(qint64 ms);
    void loopToggled(bool enabled);

private slots:
    void updatePosition();

private:
    explicit WaveformEngine(QObject* parent = nullptr);
    ~WaveformEngine();
    void seekToPosition(qint64 ms);

    static WaveformEngine* s_instance;

    QAudioSink* m_audioOutput;
    QIODevice* m_outputDevice;
    QTimer* m_positionTimer;

    QAudioFormat m_format;
    QVector<float> m_samples;

    int m_channels;
    int m_sampleRate;
    qint64 m_positionMs;
    qint64 m_durationMs;

    bool m_isPlaying;
    bool m_isPaused;

    bool m_loopEnabled;
    qint64 m_loopStartMs;
    qint64 m_loopEndMs;
};
