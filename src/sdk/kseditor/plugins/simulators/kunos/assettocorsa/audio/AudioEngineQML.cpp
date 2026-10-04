#include "AudioEngineQML.h"
#include "sdk/kseditor/engine/Audio/AudioTypes.h"
#include "CarAudioEngine.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/ksAssettocorsasndeventdefs.h"
#include "sdk/kseditor/engine/FileFormat/ACGuidsParser.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>

namespace ks { namespace audio {

static AudioEngineQML* s_audioEngine = nullptr;

AudioEngineQML::AudioEngineQML(QObject* parent)
    : QObject(parent)
{
    s_audioEngine = this;
}

AudioEngineQML::~AudioEngineQML() {
    shutdown();
    s_audioEngine = nullptr;
}

AudioEngineQML* AudioEngineQML::instance() {
    if (!s_audioEngine) {
        s_audioEngine = new AudioEngineQML();
    }
    return s_audioEngine;
}

bool AudioEngineQML::initialize(const QString& simRootPath) {
    m_simRootPath = simRootPath;

    auto* engine = audio::KSAudioEngine::instance();
    if (!engine->isInitialized()) {
        engine->initialize(44100, 2, 2048);
    }

    auto* carAudio = KsCarAudioEngine::instance();
    if (!carAudio->isInitialized()) {
        carAudio->initialize(simRootPath);
    }

    m_initialized = true;
    emit initializedChanged(true);
    qInfo() << "AudioEngine: Initialized with kseditor software audio engine";
    return true;
}

void AudioEngineQML::shutdown() {
    if (!m_initialized) return;

    auto* engine = audio::KSAudioEngine::instance();
    engine->unloadAllBanks();
    engine->stopAllEvents();

    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->shutdown();

    m_initialized = false;
    emit initializedChanged(false);
    qInfo() << "AudioEngine: Shutdown complete";
}

bool AudioEngineQML::loadBank(const QString& bankPath) {
    auto* engine = audio::KSAudioEngine::instance();
    bool result = engine->loadBank(bankPath);
    if (result) {
        m_loadedBanks.append(bankPath);
        m_bankEvents[bankPath] = engine->getEvents(QFileInfo(bankPath).completeBaseName());
        emit banksChanged();
        emit bankLoaded(bankPath);
    }
    return result;
}

void AudioEngineQML::unloadBank(const QString& bankName) {
    auto* engine = audio::KSAudioEngine::instance();
    engine->unloadBank(bankName);
    m_loadedBanks.removeAll(bankName);
    m_bankEvents.remove(bankName);
    emit banksChanged();
    emit bankUnloaded(bankName);
}

void AudioEngineQML::unloadAllBanks() {
    auto* engine = audio::KSAudioEngine::instance();
    engine->unloadAllBanks();
    m_loadedBanks.clear();
    m_bankEvents.clear();
    emit banksChanged();
}

QStringList AudioEngineQML::getEvents(const QString& bankName) const {
    if (m_bankEvents.contains(bankName)) {
        return m_bankEvents[bankName];
    }
    auto* engine = audio::KSAudioEngine::instance();
    return engine->getEvents(bankName);
}

QVariantMap AudioEngineQML::getEventInfo(const QString& eventPath) const {
    auto* engine = audio::KSAudioEngine::instance();
    audio::AudioEvent event = engine->getEventInfo(eventPath);

    QVariantMap info;
    info["path"] = eventPath;
    info["name"] = event.name;
    info["volume"] = event.volume;
    info["pitch"] = event.pitch;
    info["loop"] = event.loop;
    info["is3D"] = event.is3D;
    info["minDistance"] = event.minDistance;
    info["maxDistance"] = event.maxDistance;
    return info;
}

void AudioEngineQML::playEvent(const QString& eventPath) {
    if (!m_initialized) {
        m_lastError = "Audio engine not initialized";
        emit error(m_lastError);
        return;
    }

    auto* engine = audio::KSAudioEngine::instance();
    int instanceId = engine->playEvent(eventPath);
    if (instanceId >= 0) {
        m_currentEvent = eventPath;
        m_isPlaying = true;
        emit eventStarted(eventPath);
        emit playbackChanged();
    }
}

void AudioEngineQML::stopEvent(const QString& eventPath) {
    auto* engine = audio::KSAudioEngine::instance();
    engine->stopAllEvents();

    if (m_currentEvent == eventPath) {
        m_currentEvent.clear();
        m_isPlaying = false;
    }

    emit eventStopped(eventPath);
    emit playbackChanged();
}

void AudioEngineQML::stopAllEvents() {
    auto* engine = audio::KSAudioEngine::instance();
    engine->stopAllEvents();
    m_currentEvent.clear();
    m_isPlaying = false;
    emit playbackChanged();
}

void AudioEngineQML::setRpm(float value) {
    m_rpm = qBound(0.0f, value, 12000.0f);
    emit rpmChanged(m_rpm);

    auto* engine = audio::KSAudioEngine::instance();
    engine->setEventParameterByPath(m_currentEvent, "rpms", m_rpm);

    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setEngineParameters(m_rpm, m_throttle / 100.0f);
}

void AudioEngineQML::setThrottle(float value) {
    m_throttle = qBound(0.0f, value, 100.0f);
    emit throttleChanged(m_throttle);

    auto* engine = audio::KSAudioEngine::instance();
    engine->setEventParameterByPath(m_currentEvent, "throttle", m_throttle / 100.0f);

    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setEngineParameters(m_rpm, m_throttle / 100.0f);
}

void AudioEngineQML::setTurboBoost(float value) {
    m_turboBoost = qBound(0.0f, value, 3.0f);
    emit turboChanged(m_turboBoost);

    auto* engine = audio::KSAudioEngine::instance();
    engine->setEventParameterByPath(m_currentEvent, "boost", m_turboBoost);

    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setTurbo(m_turboBoost);
}

void AudioEngineQML::setLimiterRPM(int value) {
    m_limiterRPM = qBound(1000, value, 15000);
    emit limiterChanged(m_limiterRPM);
}

void AudioEngineQML::set3DListenerPosition(float x, float y, float z, float fx, float fy, float fz) {
    auto* engine = audio::KSAudioEngine::instance();
    engine->set3DListenerPosition(QVector3D(x, y, z), QVector3D(fx, fy, fz), QVector3D(0, 1, 0));

    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setListenerPosition(x, y, z, fx, fy, fz, 0, 1, 0);
}

static QMap<QString, float>& busVolumes() {
    static QMap<QString, float> vols;
    if (vols.isEmpty()) vols["Master"] = 1.0f;
    return vols;
}
static QMap<QString, bool>& busMutes() {
    static QMap<QString, bool> mutes;
    return mutes;
}
static QMap<QString, float>& vcaVolumes() {
    static QMap<QString, float> vols;
    return vols;
}

QStringList AudioEngineQML::getBuses() const {
    return busVolumes().keys();
}

float AudioEngineQML::getBusVolume(const QString& busPath) const {
    return busVolumes().value(busPath, 1.0f);
}

void AudioEngineQML::setBusVolume(const QString& busPath, float volume) {
    busVolumes()[busPath] = qBound(0.0f, volume, 1.0f);
    emit busVolumeChanged(busPath, busVolumes()[busPath]);
}

void AudioEngineQML::setBusMute(const QString& busPath, bool mute) {
    busMutes()[busPath] = mute;
    emit busMuteChanged(busPath, mute);
}

QStringList AudioEngineQML::getVCAs() const {
    return vcaVolumes().keys();
}

float AudioEngineQML::getVCAVolume(const QString& vcaPath) const {
    return vcaVolumes().value(vcaPath, 1.0f);
}

void AudioEngineQML::setVCAVolume(const QString& vcaPath, float volume) {
    vcaVolumes()[vcaPath] = qBound(0.0f, volume, 1.0f);
    emit vcaVolumeChanged(vcaPath, vcaVolumes()[vcaPath]);
}

bool AudioEngineQML::loadSoundbank(const QString& carDirectory, const QString& carId) {
    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->loadCarSoundBank(carDirectory, carId);
    return true;
}

void AudioEngineQML::setCameraExternal(bool external) {
    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setCameraMode(external);
}

void AudioEngineQML::setTurboValue(float boost) {
    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setTurbo(boost);
}

void AudioEngineQML::setLimiterValue(float decay) {
    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setLimiter(decay);
}

void AudioEngineQML::setDoorState(bool open) {
    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setDoorOpen(open);
}

void AudioEngineQML::setHornState(bool active) {
    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->setHorn(active);
}

void AudioEngineQML::update() {
    auto* engine = audio::KSAudioEngine::instance();
    engine->update();

    auto* carAudio = KsCarAudioEngine::instance();
    carAudio->update();
}

} } // namespace ks::audio
