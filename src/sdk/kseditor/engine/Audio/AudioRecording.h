#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QAudioFormat>

namespace ks { namespace audio {

class AudioRecorder : public QObject
{
    Q_OBJECT
public:
    explicit AudioRecorder(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioRecorder() {}

    enum State { Stopped, Recording, Paused };

    void setFormat(const QAudioFormat& format) { m_format = format; }
    QAudioFormat format() const { return m_format; }

    void setDevice(const QString& deviceName) { m_device = deviceName; }
    QString device() const { return m_device; }

    void setOutputPath(const QString& path) { m_outputPath = path; }
    QString outputPath() const { return m_outputPath; }

    void start();
    void stop();
    void pause();
    void resume();

    State state() const { return m_state; }

    void setPreRecordingBuffer(int seconds) { m_preBufferSeconds = seconds; }
    int preRecordingBuffer() const { return m_preBufferSeconds; }

    void appendData(const QVector<float>& samples);

    void setLevel(float level) { m_inputLevel = level; }
    float level() const { return m_inputLevel; }

    qint64 recordedDuration() const { return m_recordedDuration; }

signals:
    void stateChanged(State state);
    void levelChanged(float level);
    void dataAppended();
    void recordingComplete(const QString& path);

private:
    State m_state = Stopped;
    QAudioFormat m_format;
    QString m_device;
    QString m_outputPath;
    int m_preBufferSeconds = 0;
    float m_inputLevel = 1.0f;
    qint64 m_recordedDuration = 0;
    QVector<float> m_recordedData;
};

} } // namespace ks::audio