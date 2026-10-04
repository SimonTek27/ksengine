#include "AudioMixer.h"
#include <QTimer>
#include <QDebug>
#include <QMutexLocker>

namespace ks { namespace audio {

// Simple ring-buffer IO device for Qt6 push-mode audio
class AudioIODevice : public QIODevice {
public:
    explicit AudioIODevice(QObject* parent = nullptr) : QIODevice(parent) {}
    qint64 readData(char* data, qint64 maxSize) override {
        QMutexLocker lock(&m_mutex);
        qint64 avail = m_buffer.size();
        qint64 toCopy = qMin(maxSize, avail);
        if (toCopy > 0) {
            memcpy(data, m_buffer.constData(), toCopy);
            m_buffer.remove(0, toCopy);
        }
        return toCopy;
    }
    qint64 writeData(const char* data, qint64 size) override {
        QMutexLocker lock(&m_mutex);
        m_buffer.append(data, size);
        // Keep buffer from growing unbounded
        if (m_buffer.size() > m_maxSize * 4)
            m_buffer.remove(0, m_buffer.size() - m_maxSize * 2);
        return size;
    }
    void setMaxSize(qint64 bytes) { m_maxSize = bytes; }
    qint64 bytesAvailable() const override {
        QMutexLocker lock(&m_mutex);
        return m_buffer.size();
    }
private:
    mutable QMutex m_mutex;
    QByteArray m_buffer;
    qint64 m_maxSize = 65536;
};

Mixer::Mixer(KSAudioVSTManager* manager, QObject* parent) : QObject(parent), m_manager(manager) {
    m_audioDevice = new AudioIODevice(this);
}

Mixer::~Mixer() { stop(); }

void Mixer::start(QAudioSink* output, const QAudioFormat& fmt, int blockSize) {
    if (!output) return;
    m_output = output;
    m_format = fmt;
    m_blockSize = blockSize;
    m_outBuffer.resize(m_blockSize * m_format.channelCount() * sizeof(float));
    m_running = true;

    // Open device and start Qt6 QAudioSink
    m_audioDevice->setMaxSize(m_blockSize * m_format.channelCount() * sizeof(float) * 8);
    m_audioDevice->open(QIODevice::ReadWrite);
    m_output->start(m_audioDevice);

    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &Mixer::onTimer);
        m_timer->start( (int)((double)m_blockSize / m_format.sampleRate() * 1000.0) );
    }
}

void Mixer::stop() {
    m_running = false;
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    if (m_output) {
        m_output->stop();
        m_output = nullptr;
    }
    m_audioDevice->close();
}

void Mixer::onTimer() {
    if (!m_running || !m_manager || !m_output) return;
    int frames = m_blockSize;
    int channels = m_format.channelCount();

    // allocate per-plugin buffers
    QVector<QVector<float>> pluginOutputs;
    pluginOutputs.resize(m_manager->pluginCount());
    for (int i = 0; i < m_manager->pluginCount(); ++i) {
        pluginOutputs[i].resize(frames * channels);
    }

    for (int i = 0; i < m_manager->pluginCount(); ++i) {
        KSAudioVSTHost* plugin = m_manager->getPlugin(i);
        if (!plugin || !plugin->isLoaded()) continue;

        QVector<float*> outs(channels);
        for (int ch = 0; ch < channels; ++ch) outs[ch] = pluginOutputs[i].data() + ch;

        plugin->startProcessing();
        plugin->process(nullptr, outs.data(), frames);
        plugin->stopProcessing();
    }

    // mix plugins into final buffer (simple sum)
    int samples = frames * channels;
    float* mixBuf = reinterpret_cast<float*>(m_outBuffer.data());
    memset(mixBuf, 0, samples * sizeof(float));

    for (int i = 0; i < m_manager->pluginCount(); ++i) {
        if (pluginOutputs[i].isEmpty()) continue;
        for (int s = 0; s < samples; ++s) mixBuf[s] += pluginOutputs[i][s];
    }

    // Clamp and write to output device
    for (int s = 0; s < samples; ++s) {
        mixBuf[s] = qBound(-1.0f, mixBuf[s], 1.0f);
    }

    QByteArray writeBytes(reinterpret_cast<const char*>(mixBuf), samples * sizeof(float));
    m_audioDevice->write(writeBytes);
}

}}
