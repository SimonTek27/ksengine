#include "AudioRecording.h"
#include <QFile>
#include <QDateTime>
#include <cmath>

namespace ks { namespace audio {

void AudioRecorder::start()
{
    if (m_state == Recording) return;

    m_state = Recording;
    m_recordedData.clear();
    m_recordedDuration = 0;

    if (m_preBufferSeconds > 0 && !m_recordedData.isEmpty()) {
        int preBufSize = m_preBufferSeconds * m_format.sampleRate() * m_format.channelCount();
        if (preBufSize < m_recordedData.size()) {
            m_recordedData = m_recordedData.mid(m_recordedData.size() - preBufSize);
        }
    }

    emit stateChanged(Recording);
}

void AudioRecorder::stop()
{
    if (m_state == Stopped) return;

    m_state = Stopped;

    if (!m_outputPath.isEmpty()) {
        QFile file(m_outputPath);
        if (file.open(QIODevice::WriteOnly)) {
            int sampleRate = m_format.sampleRate();
            int channels = m_format.channelCount();
            int bitsPerSample = 16;
            quint32 dataSize = m_recordedData.size() * 2;
            quint32 fileSize = dataSize + 36;

            QDataStream out(&file);
            out.setByteOrder(QDataStream::LittleEndian);

            out.writeRawData("RIFF", 4);
            out << fileSize;
            out.writeRawData("WAVE", 4);
            out.writeRawData("fmt ", 4);
            out << static_cast<quint32>(16);
            out << static_cast<quint16>(1);
            out << static_cast<quint16>(channels);
            out << static_cast<quint32>(sampleRate);
            out << static_cast<quint32>(sampleRate * channels * bitsPerSample / 8);
            out << static_cast<quint16>(channels * bitsPerSample / 8);
            out << static_cast<quint16>(bitsPerSample);
            out.writeRawData("data", 4);
            out << dataSize;

            for (float sample : m_recordedData) {
                qint16 val = static_cast<qint16>(qBound(-1.0f, sample * m_inputLevel, 1.0f) * 32767.0f);
                out << val;
            }

            file.close();
        }
    }

    emit recordingComplete(m_outputPath);
    emit stateChanged(Stopped);
}

void AudioRecorder::pause()
{
    if (m_state != Recording) return;
    m_state = Paused;
    emit stateChanged(Paused);
}

void AudioRecorder::resume()
{
    if (m_state != Paused) return;
    m_state = Recording;
    emit stateChanged(Recording);
}

void AudioRecorder::appendData(const QVector<float>& samples)
{
    if (m_state != Recording) return;

    for (float sample : samples) {
        m_recordedData.append(sample * m_inputLevel);
    }

    m_recordedDuration += static_cast<qint64>(samples.size() * 1000.0 / m_format.sampleRate());

    float peak = 0.0f;
    for (float s : samples) {
        float absS = std::abs(s);
        if (absS > peak) peak = absS;
    }
    emit levelChanged(peak);
    emit dataAppended();
}

} } // namespace ks::audio
