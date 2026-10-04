#include "AudioEngineSimIntegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

namespace ks {
namespace audio {

KSAudioEngineSimIntegration* KSAudioEngineSimIntegration::s_instance = nullptr;

KSAudioEngineSimIntegration* KSAudioEngineSimIntegration::instance() {
    if (!s_instance) {
        s_instance = new KSAudioEngineSimIntegration();
    }
    return s_instance;
}

KSAudioEngineSimIntegration::KSAudioEngineSimIntegration(QObject* parent)
    : QObject(parent)
{
    QString defaultPath = QCoreApplication::applicationDirPath() + "/workspace/audio";
    setWorkspacePath(defaultPath);
}

KSAudioEngineSimIntegration::~KSAudioEngineSimIntegration() {
    shutdown();
}

bool KSAudioEngineSimIntegration::initialize() {
    qDebug() << "Initializing Audio EngineSim Integration...";

    m_recorder = new KSRPMRecorder(this);
    m_acoustics = new KSCarAcoustics(this);
    m_audioGenerator = new KSAudioGenerator(this);
    m_bridge = new KSAudioModuleBridge(this);

    QDir().mkpath(m_workspacePath);
    QDir().mkpath(m_workspacePath + "/recordings");
    QDir().mkpath(m_workspacePath + "/processed");
    QDir().mkpath(m_workspacePath + "/audio_output");

    m_recorder->setOutputDirectory(m_workspacePath + "/recordings");

    qDebug() << "Audio EngineSim Integration initialized at:" << m_workspacePath;
    emit initialized();
    return true;
}

void KSAudioEngineSimIntegration::shutdown() {
    qDebug() << "Shutting down Audio EngineSim Integration...";

    if (m_recorder) {
        m_recorder->stopRecording();
        m_recorder->disconnect();
    }

    emit shutdowned();
    s_instance = nullptr;
    deleteLater();
}

}
}
