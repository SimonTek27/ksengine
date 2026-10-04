#include "RPMRecorder.h"
#include "WaveProcessor.h"
#include <QDir>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QDateTime>
#include <QDebug>
#include <QMediaDevices>

namespace ks {
namespace audio {

KSRPMRecorder::KSRPMRecorder(QObject* parent)
    : QObject(parent)
    , m_updateTimer(new QTimer(this))
{
    m_hookDLL = nullptr;
    memset(&m_hook, 0, sizeof(m_hook));

    connect(m_updateTimer, &QTimer::timeout, this, &KSRPMRecorder::onUpdateTimer);
    m_updateTimer->setInterval(50);
}

KSRPMRecorder::~KSRPMRecorder() {
    stopRecording();
    disconnect();
    unloadHookDLL();
}

bool KSRPMRecorder::loadHookDLL() {
    QString hookPath = QCoreApplication::applicationDirPath() + "/es_hook.dll";

#ifdef Q_OS_WIN
    m_hookDLL = LoadLibraryW(reinterpret_cast<LPCWSTR>(hookPath.utf16()));
#else
    m_hookDLL = nullptr;
#endif

    if (!m_hookDLL) {
        hookPath = QCoreApplication::applicationDirPath() + "/assets/es_hook.dll";
#ifdef Q_OS_WIN
        m_hookDLL = LoadLibraryW(reinterpret_cast<LPCWSTR>(hookPath.utf16()));
#endif
    }

    if (!m_hookDLL) {
        qWarning() << "KSRPMRecorder: Could not load es_hook.dll";
        return false;
    }

    auto loadFunc = [this](const char* name, void*& funcPtr) {
#ifdef Q_OS_WIN
        funcPtr = reinterpret_cast<void*>(GetProcAddress(m_hookDLL, name));
#else
        funcPtr = nullptr;
#endif
        return funcPtr != nullptr;
    };

    if (!loadFunc("EngineSimHook_Initialize", reinterpret_cast<void*&>(m_hook.Initialize)) ||
        !loadFunc("EngineSimHook_IsConnected", reinterpret_cast<void*&>(m_hook.IsConnected)) ||
        !loadFunc("EngineSimHook_GetRPM", reinterpret_cast<void*&>(m_hook.GetRPM)) ||
        !loadFunc("EngineSimHook_GetThrottle", reinterpret_cast<void*&>(m_hook.GetThrottle)) ||
        !loadFunc("EngineSimHook_GetLoad", reinterpret_cast<void*&>(m_hook.GetLoad)) ||
        !loadFunc("EngineSimHook_GetSpeed", reinterpret_cast<void*&>(m_hook.GetSpeed)) ||
        !loadFunc("EngineSimHook_SetTargetRPM", reinterpret_cast<void*&>(m_hook.SetTargetRPM)) ||
        !loadFunc("EngineSimHook_ClearTargetRPM", reinterpret_cast<void*&>(m_hook.ClearTargetRPM))) {
        qWarning() << "KSRPMRecorder: Failed to load hook functions";
        unloadHookDLL();
        return false;
    }

    return true;
}

void KSRPMRecorder::unloadHookDLL() {
    if (m_hookDLL) {
#ifdef Q_OS_WIN
        FreeLibrary(m_hookDLL);
#endif
        m_hookDLL = nullptr;
    }
}

bool KSRPMRecorder::connectToEngineSim() {
    if (m_state != RecordingState::Idle) return false;

    transitionToState(RecordingState::Connecting);

    if (!loadHookDLL()) {
        transitionToState(RecordingState::Idle);
        emit error("Failed to load EngineSim hook DLL");
        return false;
    }

    if (m_hook.Initialize) {
        m_hook.Initialize();
    }

    if (!m_hook.IsConnected || !m_hook.IsConnected()) {
        transitionToState(RecordingState::Idle);
        emit error("Could not connect to Engine Simulator");
        return false;
    }

    transitionToState(RecordingState::Connected);
    emit connected(true);
    return true;
}

void KSRPMRecorder::disconnect() {
    stopRecording();
    clearTargetRPM();
    unloadHookDLL();

    if (m_state != RecordingState::Idle) {
        transitionToState(RecordingState::Idle);
    }
}

float KSRPMRecorder::readRPM() {
    if (m_hook.GetRPM) {
        m_currentRPM = m_hook.GetRPM();
    }
    return m_currentRPM;
}

void KSRPMRecorder::setTargetRPM(float rpm) {
    if (m_hook.SetTargetRPM) {
        m_hook.SetTargetRPM(rpm);
    }
}

void KSRPMRecorder::clearTargetRPM() {
    if (m_hook.ClearTargetRPM) {
        m_hook.ClearTargetRPM();
    }
}

void KSRPMRecorder::setRPMPoints(const QVector<RPMPoint>& points) {
    m_rpmPoints = points;
    m_rpmRecorded.resize(points.size(), false);
}

void KSRPMRecorder::setOutputDirectory(const QString& dir) {
    m_outputDir = dir;
    QDir().mkpath(dir);
}

void KSRPMRecorder::setSamplePrefix(const QString& prefix) {
    m_samplePrefix = prefix;
}

void KSRPMRecorder::setSampleRate(int rate) {
    m_sampleRate = rate;
}

void KSRPMRecorder::setChannels(int channels) {
    m_channels = channels;
}

void KSRPMRecorder::setRecordingLoadType(LoadType type) {
    m_loadType = type;
}

void KSRPMRecorder::setHoldDuration(int ms) {
    m_holdDurationMs = ms;
}

bool KSRPMRecorder::startRecording() {
    if (m_state == RecordingState::Idle || m_state == RecordingState::Connecting) {
        if (!connectToEngineSim()) return false;
    }

    if (m_state != RecordingState::Connected) {
        emit error("Not connected to Engine Simulator");
        return false;
    }

    if (m_rpmPoints.isEmpty()) {
        emit error("No RPM points configured");
        return false;
    }

    QDir().mkpath(m_outputDir);

    m_currentRPMIndex = -1;
    m_progress = 0.0f;
    m_rpmRecorded.fill(false);

    transitionToState(RecordingState::RecordingRPM);
    m_updateTimer->start();

    return true;
}

void KSRPMRecorder::stopRecording() {
    m_updateTimer->stop();
    stopSampleRecording();
    clearTargetRPM();

    if (m_state != RecordingState::Idle) {
        transitionToState(RecordingState::Stopped);
        QTimer::singleShot(100, this, [this]() {
            transitionToState(RecordingState::Idle);
        });
    }
}

void KSRPMRecorder::transitionToState(RecordingState newState) {
    if (m_state == newState) return;
    m_state = newState;
    emit stateChanged(newState);
}

int KSRPMRecorder::findNextUnskippedRPM() {
    for (int i = m_currentRPMIndex + 1; i < m_rpmPoints.size(); ++i) {
        if (!m_rpmPoints[i].skip && !m_rpmRecorded[i]) {
            return i;
        }
    }
    return -1;
}

void KSRPMRecorder::onUpdateTimer() {
    readRPM();

    switch (m_state) {
    case RecordingState::RecordingRPM:
        if (m_currentRPMIndex < 0) {
            int nextRPM = findNextUnskippedRPM();
            if (nextRPM < 0) {
                stopRecording();
                emit recordingCompleted();
                return;
            }
            m_currentRPMIndex = nextRPM;
            setTargetRPM(m_rpmPoints[m_currentRPMIndex].rpm);
        } else {
            float targetRPM = m_rpmPoints[m_currentRPMIndex].rpm;
            if (std::abs(m_currentRPM - targetRPM) < 20.0f) {
                m_sampleHoldCount++;
                if (m_sampleHoldCount >= 20) {
                    transitionToState(RecordingState::RecordingSample);
                    if (!startSampleRecording()) {
                        stopRecording();
                        emit error("Failed to start sample recording");
                        return;
                    }
                }
            } else {
                m_sampleHoldCount = 0;
            }
        }
        break;

    case RecordingState::RecordingSample:
        if (m_sampleHoldCount >= m_holdDurationMs / 50) {
            finalizeCurrentRPM();
        } else {
            m_sampleHoldCount++;
        }
        break;

    default:
        break;
    }

    int recorded = 0;
    for (bool r : m_rpmRecorded) if (r) recorded++;
    m_progress = m_rpmPoints.isEmpty() ? 0.0f : static_cast<float>(recorded) / m_rpmPoints.size();
    emit recordingProgress(m_progress);
    emit rpmReached(m_currentRPM);
}

bool KSRPMRecorder::startSampleRecording() {
    stopSampleRecording();

    QString typeStr = (m_loadType == LoadType::OnLoad) ? "on" : "off";
    QString fileName = QString("%1_%2_%3.wav")
        .arg(m_samplePrefix)
        .arg(typeStr)
        .arg(static_cast<int>(m_rpmPoints[m_currentRPMIndex].rpm));

    QString filePath = m_outputDir + "/" + fileName;
    m_outputFile.setFileName(filePath);

    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(m_sampleRate);
    format.setChannelCount(m_channels);
    format.setSampleFormat(QAudioFormat::Float);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    m_audioSource = new QAudioSource(inputDevice, format, this);

    m_capturedSamples.clear();
    m_sampleStartTime = QDateTime::currentMSecsSinceEpoch();
    m_sampleHoldCount = 0;

    connect(m_audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState) {
        }
    });

    QIODevice* device = m_audioSource->start();
    connect(device, &QIODevice::readyRead, this, [this, device]() {
        QByteArray data = device->readAll();
        const float* samples = reinterpret_cast<const float*>(data.constData());
        int count = data.size() / sizeof(float);
        for (int i = 0; i < count; ++i) {
            m_capturedSamples.append(samples[i]);
        }
    });

    return true;
}

void KSRPMRecorder::stopSampleRecording() {
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }

    if (m_outputFile.isOpen()) {
        m_outputFile.close();
    }
}

void KSRPMRecorder::saveSample(LoadType type) {
    if (m_capturedSamples.isEmpty()) return;

    QString typeStr = (type == LoadType::OnLoad) ? "on" : "off";
    QString fileName = QString("%1_%2_%3.wav")
        .arg(m_samplePrefix)
        .arg(typeStr)
        .arg(static_cast<int>(m_rpmPoints[m_currentRPMIndex].rpm));

    QString filePath = m_outputDir + "/" + fileName;

    WaveProcessor wp;
    wp.setSamples(m_capturedSamples);
    wp.saveWav(filePath);

    emit sampleRecorded(filePath, m_rpmPoints[m_currentRPMIndex].rpm, type);
}

void KSRPMRecorder::finalizeCurrentRPM() {
    stopSampleRecording();

    float rpm = m_rpmPoints[m_currentRPMIndex].rpm;
    saveSample(m_loadType);
    m_rpmRecorded[m_currentRPMIndex] = true;

    clearTargetRPM();
    transitionToState(RecordingState::RecordingRPM);
    m_sampleHoldCount = 0;
    m_currentRPMIndex = -1;

    int nextRPM = findNextUnskippedRPM();
    if (nextRPM < 0) {
        stopRecording();
        emit recordingCompleted();
    }
}

}
}
