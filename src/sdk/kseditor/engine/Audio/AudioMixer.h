#pragma once

#include <QObject>
#include <QThread>
#include <QAudioFormat>
#include <QByteArray>
#include <QTimer>
#include <atomic>
#include <QIODevice>
#include <QAudioSink>

#include "AudioVSTHost.h"
#include <QAudioDevice>

namespace ks { namespace audio {

class AudioIODevice;

class Mixer : public QObject {
    Q_OBJECT
public:
    explicit Mixer(KSAudioVSTManager* manager, QObject* parent = nullptr);
    ~Mixer();

    void start(QAudioSink* output, const QAudioFormat& fmt, int blockSize = 512);
    void stop();

private slots:
    void onTimer();

private:
    KSAudioVSTManager* m_manager = nullptr;
    QAudioSink* m_output = nullptr;
    QAudioFormat m_format;
    int m_blockSize = 512;
    std::atomic<bool> m_running{false};
    QByteArray m_outBuffer;
    QTimer* m_timer = nullptr;
    AudioIODevice* m_audioDevice = nullptr;
};

}} // namespace ks::audio
