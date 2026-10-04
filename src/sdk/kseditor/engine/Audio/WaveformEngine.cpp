#include "WaveformEngine.h"
#include <QDebug>
#include <QCoreApplication>
#include <QtMath>
#include <cstring>

WaveformEngine* WaveformEngine::s_instance = nullptr;

WaveformEngine::WaveformEngine(QObject *parent)
    : QObject(parent)
    , m_audioOutput(nullptr)
    , m_outputDevice(nullptr)
    , m_positionTimer(new QTimer(this))
    , m_channels(2)
    , m_sampleRate(44100)
    , m_positionMs(0)
    , m_durationMs(0)
    , m_isPlaying(false)
    , m_isPaused(false)
    , m_loopEnabled(false)
    , m_loopStartMs(0)
    , m_loopEndMs(0)
{
    s_instance = this;

    m_format.setChannelCount(2);
    m_format.setSampleRate(44100);
    m_format.setSampleFormat(QAudioFormat::Float);

    m_audioOutput = new QAudioSink(m_format, this);

    connect(m_positionTimer, &QTimer::timeout, this, &WaveformEngine::updatePosition);

    qDebug() << "WaveformEngine: Initialized";
}

WaveformEngine::~WaveformEngine()
{
    stop();
    if (m_audioOutput) {
        m_audioOutput->stop();
        m_audioOutput->deleteLater();
    }
    s_instance = nullptr;
}

WaveformEngine* WaveformEngine::instance()
{
    if (!s_instance) {
        s_instance = new WaveformEngine();
    }
    return s_instance;
}

void WaveformEngine::setSamples(const QVector<float> &samples, int channels, int sampleRate)
{
    m_samples = samples;
    m_channels = channels;
    m_sampleRate = sampleRate;

    m_format.setChannelCount(channels);
    m_format.setSampleRate(sampleRate);

    if (!m_samples.isEmpty()) {
        m_durationMs = (m_samples.size() / channels * 1000) / sampleRate;
    } else {
        m_durationMs = 0;
    }

    if (m_loopEndMs == 0 || m_loopEndMs > m_durationMs) {
        m_loopEndMs = m_durationMs;
    }

    qDebug() << "WaveformEngine: Loaded" << samples.size() << "samples,"
             << channels << "channels," << sampleRate << "Hz," << m_durationMs << "ms";
}

void WaveformEngine::clear()
{
    stop();
    m_samples.clear();
    m_positionMs = 0;
    m_durationMs = 0;
}

void WaveformEngine::play()
{
    if (m_samples.isEmpty()) return;

    if (m_isPaused && m_audioOutput->state() == QAudio::SuspendedState) {
        m_audioOutput->resume();
        m_isPaused = false;
        m_isPlaying = true;
        m_positionTimer->start(16);
        emit playbackStarted();
        return;
    }

    if (m_isPlaying) return;

    if (m_audioOutput->state() == QAudio::StoppedState) {
        m_audioOutput->stop();
    }

    WaveformIODevice *device = new WaveformIODevice(this);
    device->setData(m_samples, m_channels, m_sampleRate);
    device->setReadPosition(m_positionMs);

    if (m_loopEnabled) {
        device->m_loopEnabled = true;
        device->m_startPosition = m_loopStartMs * m_sampleRate * m_channels / 1000;
        device->m_endPosition = m_loopEndMs * m_sampleRate * m_channels / 1000;
    }

    if (m_audioOutput) {
        m_audioOutput->start(device);
    }
    m_outputDevice = device;

    m_isPlaying = true;
    m_isPaused = false;
    m_positionTimer->start(16);

    emit playbackStarted();
    qDebug() << "WaveformEngine: Playback started at" << m_positionMs << "ms";
}

void WaveformEngine::stop()
{
    if (!m_isPlaying && !m_isPaused) return;

    m_positionTimer->stop();

    if (m_audioOutput) {
        m_audioOutput->stop();
    }

    if (m_outputDevice) {
        m_outputDevice->deleteLater();
        m_outputDevice = nullptr;
    }

    m_positionMs = m_loopEnabled ? m_loopStartMs : 0;
    m_isPlaying = false;
    m_isPaused = false;

    emit playbackStopped();
    emit positionChanged(m_positionMs);

    qDebug() << "WaveformEngine: Playback stopped";
}

void WaveformEngine::pause()
{
    if (!m_isPlaying) return;

    m_positionTimer->stop();

    if (m_audioOutput) {
        m_audioOutput->suspend();
    }

    m_isPlaying = false;
    m_isPaused = true;

    emit playbackPaused();
    qDebug() << "WaveformEngine: Playback paused at" << m_positionMs << "ms";
}

void WaveformEngine::setPosition(qint64 ms)
{
    m_positionMs = qBound(0LL, ms, m_durationMs);

    if (m_outputDevice) {
        seekToPosition(ms);
    }

    emit positionChanged(m_positionMs);
}

void WaveformEngine::setLoopEnabled(bool enabled)
{
    m_loopEnabled = enabled;
    emit loopToggled(enabled);
    qDebug() << "WaveformEngine: Loop" << (enabled ? "enabled" : "disabled");
}

void WaveformEngine::setLoopRegion(qint64 startMs, qint64 endMs)
{
    m_loopStartMs = qBound(0LL, startMs, m_durationMs);
    m_loopEndMs = qBound(m_loopStartMs, endMs, m_durationMs);
    qDebug() << "WaveformEngine: Loop region set to" << m_loopStartMs << "-" << m_loopEndMs << "ms";
}

void WaveformEngine::updatePosition()
{
    if (!m_isPlaying || !m_audioOutput) return;

    qint64 bytesProcessed = m_audioOutput->processedUSecs() / 1000 * m_sampleRate * m_channels / 1000;
    qint64 newPositionMs = (m_positionMs * m_sampleRate * m_channels / 1000 + bytesProcessed) * 1000 / (m_sampleRate * m_channels);

    if (newPositionMs > m_durationMs) {
        if (m_loopEnabled) {
            newPositionMs = m_loopStartMs;
        } else {
            stop();
            emit playbackFinished();
            return;
        }
    }

    m_positionMs = newPositionMs;
    emit positionChanged(m_positionMs);
}

void WaveformEngine::seekToPosition(qint64 ms)
{
    if (ms < 0 || ms >= m_durationMs) return;

    m_positionMs = ms;
    emit positionChanged(m_positionMs);
}

WaveformIODevice::WaveformIODevice(QObject *parent)
    : QIODevice(parent)
    , m_channels(2)
    , m_sampleRate(44100)
    , m_readPosition(0)
    , m_dataSize(0)
    , m_startPosition(0)
    , m_endPosition(0)
    , m_loopEnabled(false)
{
}

WaveformIODevice::~WaveformIODevice() = default;

void WaveformIODevice::setData(const QVector<float> &samples, int channels, int sampleRate)
{
    m_samples = samples;
    m_channels = channels;
    m_sampleRate = sampleRate;
    m_readPosition = 0;
    m_dataSize = samples.size() * sizeof(float);
    m_endPosition = m_dataSize;
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void WaveformIODevice::clear()
{
    m_samples.clear();
    m_readPosition = 0;
    m_dataSize = 0;
}

void WaveformIODevice::setReadPosition(qint64 ms)
{
    qint64 sampleIndex = ms * m_sampleRate * m_channels / 1000;
    qint64 byteIndex = sampleIndex * qint64(sizeof(float));
    m_readPosition = qBound<qint64>(0, byteIndex, m_dataSize);
}


qint64 WaveformIODevice::readData(char *data, qint64 maxlen)
{
    if (m_readPosition >= m_dataSize) {
        return 0;
    }

    qint64 bytesRemaining = m_dataSize - m_readPosition;
    qint64 bytesToRead = qMin(maxlen, bytesRemaining);

    if (m_loopEnabled && m_endPosition > 0) {
        qint64 loopLength = m_endPosition - m_startPosition;
        if (loopLength > 0) {
            qint64 loopPos = (m_readPosition - m_startPosition) % loopLength;
            bytesToRead = qMin(maxlen, loopLength - loopPos);
        }
    }

    std::memcpy(data, reinterpret_cast<const char*>(m_samples.constData()) + m_readPosition, bytesToRead);
    m_readPosition += bytesToRead;

    if (m_loopEnabled && m_readPosition >= m_endPosition && m_endPosition > m_startPosition) {
        m_readPosition = m_startPosition;
    }

    return bytesToRead;
}

qint64 WaveformIODevice::writeData(const char *data, qint64 len)
{
    if (!isWritable() || len <= 0) return 0;

    int samplesToWrite = len / sizeof(float);
    int writePos = m_readPosition / sizeof(float);

    for (int i = 0; i < samplesToWrite && writePos + i < m_samples.size(); ++i) {
        m_samples[writePos + i] = reinterpret_cast<const float*>(data)[i];
    }

    return samplesToWrite * sizeof(float);
}