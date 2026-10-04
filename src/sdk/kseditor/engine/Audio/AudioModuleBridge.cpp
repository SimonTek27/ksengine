#include "AudioModuleBridge.h"
#include "RPMRecorder.h"
#include "CarAcoustics.h"
#include "AudioGenerator.h"
#include "RPMProfile.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

namespace ks {
namespace audio {

KSAudioModuleBridge::KSAudioModuleBridge(QObject* parent)
    : QObject(parent)
{
}

KSAudioModuleBridge::~KSAudioModuleBridge() {
    shutdown();
}

void KSAudioModuleBridge::initialize() {
    m_recorder = new KSRPMRecorder(this);
    m_acoustics = new KSCarAcoustics(this);
    m_audioGenerator = new KSAudioGenerator(this);
    m_currentProfile = new KSRPMProfile(this);

    connectSignals();

    QObject::connect(m_recorder, &KSRPMRecorder::sampleRecorded, this, [this](const QString& path, float rpm, KSRPMRecorder::LoadType type) {
        m_samplesRecorded++;
        emit sampleRecorded(path, rpm, static_cast<int>(type));
    });
}

void KSAudioModuleBridge::shutdown() {
    if (m_recorder) {
        m_recorder->stopRecording();
        m_recorder->disconnect();
    }

    // Objects are Qt-parented to this, so they will be deleted automatically.
    // Just null the pointers to prevent use-after-shutdown.
    m_recorder = nullptr;
    m_acoustics = nullptr;
    m_audioGenerator = nullptr;
    m_currentProfile = nullptr;
}

void KSAudioModuleBridge::connectSignals() {
    if (!m_recorder) return;

    QObject::connect(m_recorder, &KSRPMRecorder::connected, this, &KSAudioModuleBridge::connectedChanged);
    QObject::connect(m_recorder, &KSRPMRecorder::rpmReached, this, &KSAudioModuleBridge::rpmChanged);
    QObject::connect(m_recorder, SIGNAL(recordingProgress(float)), this, SLOT(progressChanged(float)));
    QObject::connect(m_recorder, &KSRPMRecorder::stateChanged, this, [this](KSRPMRecorder::RecordingState state) {
        bool recording = (state != KSRPMRecorder::RecordingState::Idle &&
                          state != KSRPMRecorder::RecordingState::Stopped);
        emit recordingStateChanged(recording);
    });
    QObject::connect(m_recorder, &KSRPMRecorder::recordingCompleted, this, &KSAudioModuleBridge::recordingCompleted);
    QObject::connect(m_recorder, &KSRPMRecorder::error, this, &KSAudioModuleBridge::error);

    QObject::connect(m_acoustics, SIGNAL(modeChanged(int)), this, SLOT(acousticsModeChanged(int)));
    QObject::connect(m_acoustics, SIGNAL(carTypeChanged(int)), this, SLOT(carTypeChanged(int)));

    QObject::connect(m_audioGenerator, &KSAudioGenerator::generationStarted, this, &KSAudioModuleBridge::audioGenerationStarted);
    QObject::connect(m_audioGenerator, &KSAudioGenerator::generationProgress, this, &KSAudioModuleBridge::audioGenerationProgress);
    QObject::connect(m_audioGenerator, &KSAudioGenerator::generationCompleted, this, &KSAudioModuleBridge::audioGenerationCompleted);
    QObject::connect(m_audioGenerator, &KSAudioGenerator::generationFailed, this, &KSAudioModuleBridge::audioGenerationFailed);
}

bool KSAudioModuleBridge::isConnected() const {
    return m_recorder && m_recorder->state() == KSRPMRecorder::RecordingState::Connected;
}

float KSAudioModuleBridge::currentRPM() const {
    return m_recorder ? m_recorder->currentRPM() : 0.0f;
}

float KSAudioModuleBridge::recordingProgress() const {
    return m_recorder ? m_recorder->recordingProgress() : 0.0f;
}

bool KSAudioModuleBridge::isRecording() const {
    if (!m_recorder) return false;
    auto state = m_recorder->state();
    return state == KSRPMRecorder::RecordingState::RecordingRPM ||
           state == KSRPMRecorder::RecordingState::RecordingSample;
}

QString KSAudioModuleBridge::outputDirectory() const {
    return m_outputDirectory;
}

void KSAudioModuleBridge::setOutputDirectory(const QString& dir) {
    m_outputDirectory = dir;
    if (m_recorder) {
        m_recorder->setOutputDirectory(dir);
    }
}

QString KSAudioModuleBridge::samplePrefix() const {
    return m_samplePrefix;
}

void KSAudioModuleBridge::setSamplePrefix(const QString& prefix) {
    m_samplePrefix = prefix;
    if (m_recorder) {
        m_recorder->setSamplePrefix(prefix);
    }
}

int KSAudioModuleBridge::currentRPMIndex() const {
    return m_recorder ? m_recorder->currentRPMIndex() : -1;
}

int KSAudioModuleBridge::totalRPMPoints() const {
    return m_recorder ? m_recorder->currentRPMIndex() : 0;
}

int KSAudioModuleBridge::samplesRecorded() const {
    return m_samplesRecorded;
}

bool KSAudioModuleBridge::connectToEngineSim() {
    if (!m_recorder) return false;
    return m_recorder->connectToEngineSim();
}

void KSAudioModuleBridge::disconnect() {
    if (m_recorder) {
        m_recorder->disconnect();
    }
}

bool KSAudioModuleBridge::startRecording() {
    if (!m_recorder) return false;
    return m_recorder->startRecording();
}

void KSAudioModuleBridge::stopRecording() {
    if (m_recorder) {
        m_recorder->stopRecording();
    }
}

void KSAudioModuleBridge::emergencyStop() {
    if (m_recorder) {
        m_recorder->stopRecording();
        m_recorder->disconnect();
    }
}

void KSAudioModuleBridge::loadProfile(int index) {
    QVector<KSRPMProfile*> presets = KSRPMProfile::createAllPresets();
    if (index >= 0 && index < presets.size()) {
        delete m_currentProfile;
        m_currentProfile = presets[index];
        updateRPMPoints();
    }
}

void KSAudioModuleBridge::saveCurrentProfile() {
    if (m_currentProfile && !m_outputDirectory.isEmpty()) {
        QString filePath = m_outputDirectory + "/profile_" + m_currentProfile->name() + ".json";
        m_currentProfile->save(filePath);
    }
}

void KSAudioModuleBridge::createProfile(const QString& name, const QString& engineType) {
    delete m_currentProfile;
    m_currentProfile = new KSRPMProfile(this);
    m_currentProfile->setName(name);
    m_currentProfile->setEngineType(KSRPMProfile::engineTypeFromString(engineType));
    m_currentProfile->generateEngineRange(m_currentProfile->engineType());
    updateRPMPoints();
}

void KSAudioModuleBridge::setAcousticsMode(int mode) {
    if (m_acoustics) {
        m_acoustics->setMode(static_cast<KSCarAcoustics::AcousticMode>(mode));
    }
}

void KSAudioModuleBridge::setCarType(int type) {
    if (m_acoustics) {
        m_acoustics->setCarType(static_cast<KSCarAcoustics::CarType>(type));
    }
}

void KSAudioModuleBridge::setExteriorPreset(int preset) {
    if (m_acoustics) {
        m_acoustics->setExteriorPreset(static_cast<KSCarAcoustics::ExteriorPreset>(preset));
    }
}

void KSAudioModuleBridge::setInteriorPreset(int preset) {
    if (m_acoustics) {
        m_acoustics->setInteriorPreset(static_cast<KSCarAcoustics::InteriorPreset>(preset));
    }
}

void KSAudioModuleBridge::processSelectedSamples() {
    qDebug() << "Processing selected samples with car acoustics...";
}

void KSAudioModuleBridge::setProjectPath(const QString& path) {
    if (m_audioGenerator) {
        m_audioGenerator->setProjectPath(path);
    }
}

void KSAudioModuleBridge::setCarName(const QString& name) {
    if (m_audioGenerator) {
        m_audioGenerator->setCarName(name);
    }
}

void KSAudioModuleBridge::setGenerationMode(int mode) {
    if (m_audioGenerator) {
        m_audioGenerator->setGenerationMode(static_cast<KSAudioGenerator::GenerationMode>(mode));
    }
}

void KSAudioModuleBridge::setTemplateCarName(const QString& name) {
    if (m_audioGenerator) {
        m_audioGenerator->setTemplateCarName(name);
    }
}

bool KSAudioModuleBridge::generateScript() {
    if (!m_audioGenerator) return false;

    if (m_recorder) {
        m_audioGenerator->setSampleDirectory(m_recorder->outputDirectory());
    }

    return m_audioGenerator->generate();
}

bool KSAudioModuleBridge::validateProject() {
    if (!m_audioGenerator) return false;
    QString path = m_audioGenerator->lastGeneratedProjectPath();
    return QFileInfo::exists(path);
}

void KSAudioModuleBridge::openProject() {
    QString path = m_audioGenerator ? m_audioGenerator->lastGeneratedProjectPath() : QString();
    if (!path.isEmpty()) {
        qDebug() << "Opening ksaudio project:" << path;
    }
}

QVector<int> KSAudioModuleBridge::getRPMPoints() const {
    QVector<int> points;
    if (m_currentProfile) {
        for (const auto& pt : m_currentProfile->points()) {
            if (!pt.skip) {
                points.append(pt.rpm);
            }
        }
    }
    return points;
}

void KSAudioModuleBridge::setRPMPoints(const QVector<int>& points) {
    if (!m_recorder || !m_currentProfile) return;

    QVector<KSRPMRecorder::RPMPoint> recorderPoints;
    for (int rpm : points) {
        recorderPoints.append(KSRPMRecorder::RPMPoint{static_cast<float>(rpm), 3000.0f, false});
    }
    m_recorder->setRPMPoints(recorderPoints);
}

void KSAudioModuleBridge::updateRPMPoints() {
    if (!m_recorder || !m_currentProfile) return;

    QVector<KSRPMRecorder::RPMPoint> points;
    for (const auto& pt : m_currentProfile->points()) {
        points.append(KSRPMRecorder::RPMPoint{
            static_cast<float>(pt.rpm),
            static_cast<float>(pt.durationMs),
            pt.skip
        });
    }
    m_recorder->setRPMPoints(points);
}

}
}
