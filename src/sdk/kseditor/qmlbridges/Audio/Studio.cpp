#include "sdk/kseditor/engine/Audio/AudioCore.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QAudioDecoder>
#include <QMediaDevices>
#include <QBuffer>
#include <QtEndian>
#include <cmath>

namespace ks { namespace audio {

// ── Studio ─────────────────────────────────────────────────────────────────

Studio::Studio(QObject* parent)
    : QObject(parent)
{
}

Studio::~Studio()
{
    closeOutput();
    closeInput();
    stopPreview();
}

bool Studio::openOutput(const QAudioFormat& fmt)
{
    QMutexLocker locker(&m_mutex);
    closeOutput();

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        emit error(QStringLiteral("No default audio output device"));
        return false;
    }

    QAudioFormat supported = fmt;
    if (!device.isFormatSupported(supported))
        supported = device.preferredFormat();

    m_audioOut = new QAudioSink(device, supported, this);
    m_outputDevice = m_audioOut->start();
    if (!m_outputDevice) {
        emit error(QStringLiteral("Failed to start audio output"));
        delete m_audioOut;
        m_audioOut = nullptr;
        return false;
    }
    return true;
}

void Studio::closeOutput()
{
    QMutexLocker locker(&m_mutex);
    if (m_audioOut) {
        m_audioOut->stop();
        delete m_audioOut;
        m_audioOut = nullptr;
    }
    m_outputDevice = nullptr;
}

bool Studio::openInput(const QAudioFormat& fmt)
{
    QMutexLocker locker(&m_mutex);
    closeInput();

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        emit error(QStringLiteral("No default audio input device"));
        return false;
    }

    QAudioFormat supported = fmt;
    if (!device.isFormatSupported(supported))
        supported = device.preferredFormat();

    m_audioIn = new QAudioSource(device, supported, this);
    m_inputDevice = m_audioIn->start();
    if (!m_inputDevice) {
        emit error(QStringLiteral("Failed to start audio input"));
        delete m_audioIn;
        m_audioIn = nullptr;
        return false;
    }
    return true;
}

void Studio::closeInput()
{
    QMutexLocker locker(&m_mutex);
    if (m_audioIn) {
        m_audioIn->stop();
        delete m_audioIn;
        m_audioIn = nullptr;
    }
    m_inputDevice = nullptr;
}

bool Studio::previewEvent(const QString& eventPath, const QString& audioFilePath,
                          float volume, float pitch, bool loop)
{
    stopPreview();

    QFile file(audioFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit previewError(QStringLiteral("Cannot open %1").arg(audioFilePath));
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    // Decode WAV (PCM16) into float samples; other formats are rejected.
    if (data.size() < 44 || data.mid(0, 4) != "RIFF" || data.mid(8, 4) != "WAVE") {
        emit previewError(QStringLiteral("Only WAV previews are supported: %1").arg(audioFilePath));
        return false;
    }

    const quint16 channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 22);
    const quint32 sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()) + 24);
    const quint16 bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 34);
    if (bits != 16 || channels == 0) {
        emit previewError(QStringLiteral("Unsupported WAV encoding"));
        return false;
    }

    const int dataOffset = 44;
    const int bytesPerSample = 2;
    const int frameCount = (data.size() - dataOffset) / (bytesPerSample * channels);
    if (frameCount <= 0) {
        emit previewError(QStringLiteral("Empty audio file"));
        return false;
    }

    m_previewSamples.resize(frameCount * channels);
    const auto* pcm = reinterpret_cast<const qint16*>(data.constData() + dataOffset);
    for (int i = 0; i < frameCount * channels; ++i)
        m_previewSamples[i] = float(pcm[i]) / 32768.0f;

    m_previewEventPath = eventPath;
    m_previewSampleRate = int(sampleRate);
    m_previewChannels = int(channels);
    m_previewPosition = 0;
    m_previewVolume = volume;
    m_previewPitch = pitch;
    m_previewLoop = loop;
    m_previewing = true;

    if (!m_audioOut || !m_outputDevice) {
        QAudioFormat fmt;
        fmt.setSampleRate(m_previewSampleRate);
        fmt.setChannelCount(m_previewChannels);
        fmt.setSampleFormat(QAudioFormat::Int16);
        if (!openOutput(fmt)) {
            m_previewing = false;
            emit previewError(QStringLiteral("No audio output for preview"));
            return false;
        }
    }

    processPreviewOutput();
    emit previewStarted(eventPath);
    return true;
}

void Studio::stopPreview()
{
    if (!m_previewing) return;
    m_previewing = false;
    m_previewPosition = 0;
    m_previewSamples.clear();
    emit previewStopped();
}

void Studio::setPreviewVolume(float volume)
{
    m_previewVolume = volume;
}

void Studio::setPreviewPitch(float pitch)
{
    m_previewPitch = pitch;
}

void Studio::processPreviewOutput()
{
    if (!m_previewing || !m_outputDevice) return;

    // Write a short block each call; callers may invoke repeatedly for streaming.
    const int channels = qMax(1, m_previewChannels);
    const int frames = qMin(1024, (m_previewSamples.size() / channels) - m_previewPosition);
    if (frames <= 0) {
        if (m_previewLoop) {
            m_previewPosition = 0;
        } else {
            stopPreview();
        }
        return;
    }

    QByteArray block;
    block.resize(frames * channels * int(sizeof(qint16)));
    auto* out = reinterpret_cast<qint16*>(block.data());
    for (int i = 0; i < frames * channels; ++i) {
        const float s = m_previewSamples[m_previewPosition * channels + i] * m_previewVolume;
        out[i] = qint16(qBound(-1.0f, s, 1.0f) * 32767.0f);
    }
    m_previewPosition += frames;
    m_outputDevice->write(block);

    if (m_previewPosition >= m_previewSamples.size() / channels) {
        if (m_previewLoop)
            m_previewPosition = 0;
        else
            stopPreview();
    }
}

}} // namespace ks::audio
