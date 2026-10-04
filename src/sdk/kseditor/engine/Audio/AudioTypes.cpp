#include "AudioTypes.h"
#include "AudioFormatConverter.h"
#include "../FileFormat/BankWriter.h"
#include "../FileFormat/BankParser.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QDataStream>
#include <QMediaDevices>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ks {
namespace audio {

// ============================================================================
// AutomationLane implementation
// ============================================================================

float AutomationLane::getValueAtTime(float timeSec) const
{
    if (points.isEmpty()) return 0.0f;
    if (points.size() == 1) return points.first().value;

    // Find surrounding points
    for (int i = 0; i < points.size() - 1; ++i) {
        if (timeSec >= points[i].time && timeSec < points[i + 1].time) {
            float dt = points[i + 1].time - points[i].time;
            if (dt <= 0.0f) return points[i].value;

            float t = (timeSec - points[i].time) / dt;
            // Apply tension curve
            float curvedT = t;
            float tension = points[i].tension;
            if (tension != 0.5f) {
                // Tension: 0 = ease-in, 0.5 = linear, 1 = ease-out
                float curve = 2.0f * (tension - 0.5f);
                if (curve > 0.0f) {
                    curvedT = std::pow(t, 1.0f + curve * 3.0f);
                } else {
                    curvedT = 1.0f - std::pow(1.0f - t, 1.0f - curve * 3.0f);
                }
            }
            return points[i].value + curvedT * (points[i + 1].value - points[i].value);
        }
    }

    // Before first or after last point
    if (timeSec < points.first().time) return points.first().value;
    return points.last().value;
}

void AutomationLane::addPoint(const AutomationPoint& point)
{
    points.append(point);
    std::sort(points.begin(), points.end(),
              [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
}

void AutomationLane::removePoint(int index)
{
    if (index >= 0 && index < points.size()) {
        points.removeAt(index);
    }
}

void AutomationLane::updatePoint(int index, float time, float value)
{
    if (index >= 0 && index < points.size()) {
        points[index].time = time;
        points[index].value = value;
        std::sort(points.begin(), points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) { return a.time < b.time; });
    }
}

// ============================================================================
// AudioEvent automation methods
// ============================================================================

AutomationLane* AudioEvent::findAutomationLane(AutomationParameter param)
{
    for (auto& lane : automationLanes) {
        if (lane.parameter == param) return &lane;
    }
    return nullptr;
}

AutomationLane& AudioEvent::addAutomationLane(AutomationParameter param)
{
    auto* existing = findAutomationLane(param);
    if (existing) return *existing;

    AutomationLane lane;
    lane.parameter = param;
    automationLanes.append(lane);
    return automationLanes.last();
}

void AudioEvent::removeAutomationLane(AutomationParameter param)
{
    for (int i = automationLanes.size() - 1; i >= 0; --i) {
        if (automationLanes[i].parameter == param) {
            automationLanes.removeAt(i);
            return;
        }
    }
}

float AudioEvent::getAutomatedValue(AutomationParameter param, float timeSec) const
{
    for (const auto& lane : automationLanes) {
        if (lane.parameter == param && lane.enabled) {
            return lane.getValueAtTime(timeSec);
        }
    }
    // Return default value if no automation lane exists
    switch (param) {
    case AutomationParameter::Volume: return volume;
    case AutomationParameter::Pan: return 0.0f;
    case AutomationParameter::LowPassCutoff: return 20000.0f;
    case AutomationParameter::HighPassCutoff: return 0.0f;
    case AutomationParameter::ReverbMix: return 0.0f;
    case AutomationParameter::Pitch: return 1.0f;
    }
    return 0.0f;
}

// ============================================================================
// KSAudioEngine Implementation
// ============================================================================

KSAudioEngine* KSAudioEngine::s_instance = nullptr;
QMutex KSAudioEngine::s_mutex;

KSAudioEngine* KSAudioEngine::instance() {
    QMutexLocker locker(&s_mutex);
    if (!s_instance) s_instance = new KSAudioEngine();
    return s_instance;
}

KSAudioEngine::KSAudioEngine(QObject* parent)
    : QObject(parent), m_initialized(false), m_sampleRate(44100), m_channels(2),
      m_masterVolume(1.0f), m_audioSink(nullptr), m_outputDevice(nullptr),
      m_nextInstanceId(0), m_updateTimer(nullptr) {}

KSAudioEngine::~KSAudioEngine() { shutdown(); }

bool KSAudioEngine::initialize(int sampleRate, int channels, int bufferSize) {
    if (m_initialized) return true;
    m_sampleRate = sampleRate; m_channels = channels;

    m_format.setSampleRate(sampleRate);
    m_format.setChannelCount(channels);
    m_format.setSampleFormat(QAudioFormat::Float);
    QAudioDevice d = QMediaDevices::defaultAudioOutput();
    if (!d.isFormatSupported(m_format)) {
        qWarning() << "KSAudio: Format unsupported, trying fallback";
        m_format = d.preferredFormat();
        m_format.setSampleFormat(QAudioFormat::Float);
    }
    m_audioSink = new QAudioSink(d, m_format, this);
    m_audioSink->setBufferSize(bufferSize * channels * sizeof(float));
    m_outputDevice = new KSAudioIODevice(this, this);
    m_audioSink->start(m_outputDevice);
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &KSAudioEngine::update);
    m_updateTimer->start(10);
    m_initialized = true;
    emit initialized();
    qInfo() << "KSAudioEngine: Initialized with Qt Multimedia" << sampleRate << "Hz," << channels << "ch";
    return true;
}

void KSAudioEngine::shutdown() {
    if (!m_initialized) return;
    if (m_updateTimer) m_updateTimer->stop();

    if (m_audioSink) { m_audioSink->stop(); delete m_audioSink; m_audioSink = nullptr; }
    m_outputDevice = nullptr;
    QMutexLocker l(&m_instancesMutex);
    m_activeInstances.clear(); m_banks.clear(); m_bankMap.clear();
    m_initialized = false;
    qInfo() << "KSAudioEngine: Shutdown";
}

QStringList KSAudioEngine::loadedBanks() const {
    QStringList r; for (const auto& b : m_banks) r.append(b.name); return r;
}

bool KSAudioEngine::loadBank(const QString& bankPath) {
    QFileInfo i(bankPath);
    if (!i.exists()) { emit error("Bank not found: " + bankPath); return false; }

    SoundBank bank;
    bank.id = QUuid::createUuid().toString();
    bank.name = i.baseName();
    bank.filePath = bankPath;
    bank.isLoaded = true;

    if (bankPath.endsWith(".bank", Qt::CaseInsensitive)) {
        // FMOD FEV2 format — use KSBankParser
        fileformat::KSBankParser parser;
        fileformat::ParsedBankData data = parser.parse(bankPath);
        if (data.isValid) {
            // Pre-load decoded audio samples from FSB5 data
            QMap<QString, QVector<float>> soundSamples;
            QMap<QString, quint32> soundSampleRate;
            QMap<QString, quint32> soundChannels;
            for (const auto& snd : data.sounds) {
                if (snd.hasAudioData && !snd.samples.isEmpty()) {
                    soundSamples[snd.name] = snd.samples;
                    soundSampleRate[snd.name] = snd.sampleRate;
                    soundChannels[snd.name] = snd.channels;
                }
            }

            for (const auto& ev : data.events) {
                AudioEvent e;
                e.id = ev.guid;
                e.name = ev.name;
                e.volume = 1.0f;
                e.pitch = 1.0f;
                e.loop = false;

                // Try to find matching sound asset by name
                for (const auto& snd : data.sounds) {
                    if (ev.name.contains(snd.name, Qt::CaseInsensitive) ||
                        snd.name.contains(ev.name, Qt::CaseInsensitive)) {
                        if (soundSamples.contains(snd.name)) {
                            e.audioFile = snd.name;
                            // Pre-load samples so playback is instant
                            bank.preloadedSamples[snd.name] = soundSamples[snd.name];
                            bank.preloadedSampleRate[snd.name] = soundSampleRate[snd.name];
                            bank.preloadedChannels[snd.name] = soundChannels[snd.name];
                        }
                        break;
                    }
                }

                bank.events.append(e);
            }
            qInfo() << "Loaded FMOD bank:" << bankPath << "with"
                    << data.events.size() << "events,"
                    << soundSamples.size() << "audio assets";
        } else {
            emit error("Failed to parse FMOD bank: " + bankPath);
            return false;
        }
    } else if (bankPath.endsWith(".ksaudio", Qt::CaseInsensitive)) {
        // JSON format — read events directly
        QFile f(bankPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject root = doc.object();
                // Try v2 format (eventGroups)
                QJsonArray events = root["events"].toArray();
                if (events.isEmpty()) {
                    for (const auto& gv : root["eventGroups"].toArray()) {
                        for (const auto& ev : gv.toObject()["events"].toArray())
                            events.append(ev);
                    }
                }
                for (const auto& ev : events) {
                    QJsonObject o = ev.toObject();
                    AudioEvent e;
                    e.id = o["guid"].toString(o["name"].toString());
                    e.name = o["name"].toString();
                    e.audioFile = o["audioFile"].toString();
                    e.volume = static_cast<float>(o["volume"].toDouble(1.0));
                    e.pitch  = static_cast<float>(o["pitch"].toDouble(1.0));
                    e.loop   = o["loop"].toBool(false);
                    bank.events.append(e);
                }
            }
        }
    }
    if (bank.events.isEmpty()) {
        AudioEvent e;
        e.id = QUuid::createUuid().toString();
        e.name = "engine";
        e.audioFile = "engine.wav";
        e.loop = true;
        bank.events.append(e);
    }
    m_banks.append(bank);
    m_bankMap[bank.name] = &m_banks.last();
    m_bankPathByName[bank.name] = bankPath;

    // Add to file watcher if hot-reload is enabled
    if (m_hotReloadEnabled && m_bankWatcher && !bankPath.isEmpty()) {
        if (!m_bankWatcher->files().contains(bankPath)) {
            m_bankWatcher->addPath(bankPath);
        }
    }

    emit bankLoaded(bank.name);
    qInfo() << "KSAudioEngine: Loaded bank" << bank.name;
    return true;
}

void KSAudioEngine::unloadBank(const QString& name) {
    for (int i = 0; i < m_banks.size(); ++i) {
        if (m_banks[i].name == name) {
            // Remove from file watcher
            if (m_bankWatcher && !m_banks[i].filePath.isEmpty()) {
                m_bankWatcher->removePath(m_banks[i].filePath);
            }
            m_bankPathByName.remove(name);
            m_banks.removeAt(i);
            m_bankMap.remove(name);
            emit bankUnloaded(name);
            return;
        }
    }
}

void KSAudioEngine::unloadAllBanks() {
    QStringList b = loadedBanks();
    for (const auto& n : b) unloadBank(n);
}

void KSAudioEngine::enableHotReload(bool enable) {
    if (enable == m_hotReloadEnabled) return;

    if (enable) {
        if (!m_bankWatcher) {
            m_bankWatcher = new QFileSystemWatcher(this);
            connect(m_bankWatcher, &QFileSystemWatcher::fileChanged,
                    this, &KSAudioEngine::onBankFileChanged);
        }
        // Watch all loaded bank files
        for (const auto& bank : m_banks) {
            if (!bank.filePath.isEmpty() && !m_bankWatcher->files().contains(bank.filePath)) {
                m_bankWatcher->addPath(bank.filePath);
            }
        }
    } else {
        if (m_bankWatcher) {
            m_bankWatcher->removePaths(m_bankWatcher->files());
        }
    }

    m_hotReloadEnabled = enable;
}

void KSAudioEngine::reloadBank(const QString& bankName) {
    QString filePath;
    if (m_bankPathByName.contains(bankName)) {
        filePath = m_bankPathByName[bankName];
    } else {
        // Find from loaded banks
        for (const auto& b : m_banks) {
            if (b.name == bankName) {
                filePath = b.filePath;
                break;
            }
        }
    }

    if (filePath.isEmpty()) {
        emit error("Cannot reload bank: no file path for " + bankName);
        return;
    }

    qInfo() << "KSAudioEngine: Hot-reloading bank" << bankName << "from" << filePath;

    // Stop any playing events from this bank
    QMutexLocker l(&m_instancesMutex);
    for (int i = m_activeInstances.size() - 1; i >= 0; --i) {
        if (m_activeInstances[i].eventPath.startsWith(bankName + "/")) {
            m_activeInstances[i].isPlaying = false;
        }
    }
    l.unlock();

    // Unload the old bank
    unloadBank(bankName);

    // Reload it
    loadBank(filePath);

    emit bankReloaded(bankName);
    qInfo() << "KSAudioEngine: Bank reloaded:" << bankName;
}

void KSAudioEngine::onBankFileChanged(const QString& path) {
    if (!m_hotReloadEnabled) return;

    QFileInfo info(path);
    QString bankName = info.baseName();

    // Small delay to ensure the file write is complete
    QTimer::singleShot(200, this, [this, bankName]() {
        reloadBank(bankName);
    });
}

QStringList KSAudioEngine::getEvents(const QString& bankName) const {
    QStringList r;
    if (m_bankMap.contains(bankName)) {
        for (const auto& e : m_bankMap[bankName]->events) r.append(e.name);
    }
    return r;
}

AudioEvent KSAudioEngine::getEventInfo(const QString& path) const {
    QStringList p = path.split("/");
    if (p.size() >= 2) {
        auto* b = m_bankMap.value(p[0]);
        if (b) {
            auto* e = b->findEvent(p[1]);
            if (e) return *e;
        }
    }
    return AudioEvent();
}

int KSAudioEngine::playEvent(const QString& eventPath, bool paused) {
    if (!m_initialized) return -1;

    QMutexLocker l(&m_instancesMutex);

    QString audioFile;
    float volume = 1.0f, pitch = 1.0f;
    bool loop = false;

    QStringList parts = eventPath.split("/");
    if (parts.size() >= 2) {
        auto* bank = m_bankMap.value(parts[0]);
        if (bank) {
            auto* ev = bank->findEvent(parts[1]);
            if (ev) {
                audioFile = ev->audioFile;
                volume = ev->volume;
                pitch = ev->pitch;
                loop = ev->loop;
            }
        }
    }
    if (audioFile.isEmpty()) audioFile = eventPath;

    PlaybackInstance inst(m_nextInstanceId++);
    inst.eventPath = eventPath;
    inst.audioFile = audioFile;
    inst.volume = volume;
    inst.pitch = pitch;
    inst.loop = loop;
    inst.isPlaying = !paused;
    inst.is3D = true;

    QVector<float> samples;
    int sr, ch;
    if (loadAudioFile(audioFile, samples, sr, ch)) {
        inst.samples = samples;
        inst.sampleRate = sr;
        inst.channels = ch;
        inst.currentSample = 0;
        qInfo() << "KSAudioEngine: Loaded audio" << audioFile << sr << "Hz" << ch << "ch";
    } else {
        qWarning() << "KSAudioEngine: Failed to load" << audioFile << "- using test tone";
    }

    m_activeInstances.append(inst);
    emit eventStarted(inst.id, eventPath);
    qInfo() << "KSAudioEngine: Playing" << eventPath;
    return inst.id;
}

void KSAudioEngine::stopEvent(int id) {
    QMutexLocker l(&m_instancesMutex);
    for (int i = 0; i < m_activeInstances.size(); ++i) {
        if (m_activeInstances[i].id == id) {
            m_activeInstances.removeAt(i);
            emit eventStopped(id);
            return;
        }
    }
}

void KSAudioEngine::stopAllEvents() {
    QMutexLocker l(&m_instancesMutex);
    m_activeInstances.clear();
}

bool KSAudioEngine::setEventVolume(int id, float v) {
    QMutexLocker l(&m_instancesMutex);
    for (auto& i : m_activeInstances) if (i.id == id) { i.volume = v; return true; }
    return false;
}

bool KSAudioEngine::setEventPitch(int id, float v) {
    QMutexLocker l(&m_instancesMutex);
    for (auto& i : m_activeInstances) if (i.id == id) { i.pitch = v; return true; }
    return false;
}

bool KSAudioEngine::setEventPosition(int id, const QVector3D& p) {
    QMutexLocker l(&m_instancesMutex);
    for (auto& i : m_activeInstances) if (i.id == id) { i.position = p; return true; }
    return false;
}

bool KSAudioEngine::setEventParameter(int id, const QString& p, float v) {
    QMutexLocker l(&m_instancesMutex);
    for (auto& i : m_activeInstances) if (i.id == id) { i.parameters[p] = v; return true; }
    return false;
}

bool KSAudioEngine::setEventParameterByPath(const QString& eventPath, const QString& p, float v) {
    QMutexLocker l(&m_instancesMutex);
    for (auto& i : m_activeInstances) {
        if (i.eventPath == eventPath) { i.parameters[p] = v; return true; }
    }
    return false;
}

void KSAudioEngine::set3DListenerPosition(const QVector3D& p, const QVector3D& f, const QVector3D& u) {
    m_listener.position = p; m_listener.forward = f; m_listener.up = u;
}

void KSAudioEngine::set3DListenerVelocity(const QVector3D& v) {
    m_listener.velocity = v;
}
void KSAudioEngine::setMasterVolume(float v) { m_masterVolume = qBound(0.0f, v, 2.0f); }
int KSAudioEngine::activeEventCount() const { return m_activeInstances.size(); }

void KSAudioEngine::update() {
    QMutexLocker l(&m_instancesMutex);
    for (int i = m_activeInstances.size() - 1; i >= 0; --i) {
        auto& inst = m_activeInstances[i];
        if (inst.isPlaying && !inst.isPaused) {
            float rpm = inst.getParameter("rpms", 0.0f);
            if (rpm > 0) inst.pitch = 0.5f + (rpm / 8000.0f) * 1.5f;
        }
    }
}

bool KSAudioEngine::loadAudioFile(const QString& path, QVector<float>& samples, int& sr, int& ch) {
    // Check for preloaded audio from FMOD/ksaudio banks first
    for (const auto& bank : m_banks) {
        if (bank.preloadedSamples.contains(path)) {
            samples = bank.preloadedSamples[path];
            sr = static_cast<int>(bank.preloadedSampleRate[path]);
            ch = static_cast<int>(bank.preloadedChannels[path]);
            return !samples.isEmpty();
        }
        for (auto it = bank.preloadedSamples.begin(); it != bank.preloadedSamples.end(); ++it) {
            if (path.contains(it.key(), Qt::CaseInsensitive) ||
                it.key().contains(path, Qt::CaseInsensitive)) {
                samples = it.value();
                sr = static_cast<int>(bank.preloadedSampleRate[it.key()]);
                ch = static_cast<int>(bank.preloadedChannels[it.key()]);
                return !samples.isEmpty();
            }
        }
    }

    QFileInfo info(path);
    if (!info.exists()) {
        qWarning() << "KSAudioEngine: Audio file not found:" << path;
        return false;
    }

    QString ext = info.suffix().toLower();

    // WAV format
    if (ext == "wav") {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        char h[44];
        if (f.read(h, 44) != 44) return false;
        quint32 dataSize = *reinterpret_cast<quint32*>(&h[40]);
        sr = *reinterpret_cast<quint32*>(&h[24]);
        ch = *reinterpret_cast<quint16*>(&h[22]);
        quint16 bps = *reinterpret_cast<quint16*>(&h[34]);
        int bytesPerSample = bps / 8;
        int sampleCount = dataSize / bytesPerSample;
        samples.resize(sampleCount);
        char* data = new char[dataSize];
        f.read(data, dataSize);
        for (int i = 0; i < sampleCount; ++i) {
            if (bps == 16) samples[i] = *reinterpret_cast<qint16*>(&data[i * 2]) / 32768.0f;
            else if (bps == 32) samples[i] = *reinterpret_cast<float*>(&data[i * 4]);
        }
        delete[] data;
        f.close();
        return true;
    }

    // OGG, MP3, FLAC - use AudioFormatConverter
    AudioFormatConverter converter;
    AudioFormatConverter::AudioMetadata metadata;
    QAudioFormat format;

    if (ext == "ogg") {
        if (converter.decodeOgg(path, samples, format, metadata)) {
            sr = format.sampleRate();
            ch = format.channelCount();
            return true;
        }
    } else if (ext == "mp3") {
        if (converter.decodeMp3(path, samples, format, metadata)) {
            sr = format.sampleRate();
            ch = format.channelCount();
            return true;
        }
    }

    return false;
}

void KSAudioEngine::mixAudio(char* buffer, qint64 bytes) {
    memset(buffer, 0, bytes);
    int samplesNeeded = bytes / sizeof(float) / m_channels;

    QMutexLocker l(&m_instancesMutex);

    for (auto& inst : m_activeInstances) {
        if (!inst.isPlaying || inst.isPaused) continue;
        if (inst.samples.isEmpty()) continue;

        float volume = inst.volume * m_masterVolume;
        float panL = 1.0f, panR = 1.0f;

        if (inst.is3D) {
            QVector3D dir = (inst.position - m_listener.position).normalized();
            float pan = QVector3D::dotProduct(dir, m_listener.forward);
            panL = 0.5f - pan * 0.5f;
            panR = 0.5f + pan * 0.5f;
        }

        float pitchFactor = inst.pitch;
        int srcChannels = inst.channels;

        for (int i = 0; i < samplesNeeded; ++i) {
            int srcIdx = static_cast<int>(inst.currentSample) * srcChannels;
            if (srcIdx + srcChannels - 1 < inst.samples.size()) {
                float sampleL = inst.samples[srcIdx];
                float sampleR = srcChannels > 1 ? inst.samples[srcIdx + 1] : sampleL;

                if (m_channels >= 2) {
                    reinterpret_cast<float*>(buffer)[i * 2] += sampleL * volume * panL;
                    reinterpret_cast<float*>(buffer)[i * 2 + 1] += sampleR * volume * panR;
                } else {
                    reinterpret_cast<float*>(buffer)[i] += (sampleL + sampleR) * 0.5f * volume;
                }
            }

            inst.currentSample += pitchFactor;
            if (inst.currentSample * srcChannels >= inst.samples.size()) {
                if (inst.loop) {
                    inst.currentSample = 0;
                } else {
                    inst.isPlaying = false;
                    break;
                }
            }
        }
    }
}

// ============================================================================
// KSAudioIODevice
// ============================================================================

KSAudioIODevice::KSAudioIODevice(KSAudioEngine* e, QObject* p)
    : QIODevice(p), m_engine(e) { open(QIODevice::ReadOnly); }

KSAudioIODevice::~KSAudioIODevice() { close(); }

qint64 KSAudioIODevice::readData(char* data, qint64 maxlen) {
    if (m_engine) m_engine->mixAudio(data, maxlen);
    return maxlen;
}

qint64 KSAudioIODevice::writeData(const char* data, qint64 len) {
    if (!m_engine || !data || len <= 0) return 0;
    m_engine->mixAudio(const_cast<char*>(data), len);
    return len;
}

// ============================================================================
// KSAudioProject
// ============================================================================

KSAudioProject::KSAudioProject(QObject* parent) : QObject(parent) {}

bool KSAudioProject::load(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) { emit error("Cannot open: " + filePath); return false; }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject()) { emit error("Invalid JSON"); return false; }
    m_rawDocument = doc.object();
    loadFromJson(m_rawDocument);
    m_filePath = filePath;
    m_loaded = true;
    emit loaded();
    return true;
}

bool KSAudioProject::save(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) { emit error("Cannot write: " + filePath); return false; }
    f.write(QJsonDocument(toJson()).toJson());
    m_filePath = filePath;
    emit saved();
    return true;
}

SoundBank* KSAudioProject::findBank(const QString& name)
{
    for (auto& b : m_banks) if (b.name == name) return &b;
    return nullptr;
}

bool KSAudioProject::addBank(const QString& name)
{
    if (findBank(name)) return false;
    SoundBank bank;
    bank.id = QUuid::createUuid().toString();
    bank.name = name;
    m_banks.append(bank);
    return true;
}

bool KSAudioProject::removeBank(const QString& name)
{
    for (int i = 0; i < m_banks.size(); ++i) {
        if (m_banks[i].name == name) { m_banks.removeAt(i); return true; }
    }
    return false;
}

bool KSAudioProject::addEvent(const QString& bankName, const AudioEvent& event)
{
    SoundBank* bank = findBank(bankName);
    if (!bank) return false;
    bank->events.append(event);
    return true;
}

// ============================================================================
// JSON load — auto-detects v1 (simple) vs v2 (FSPRO-compatible) schema
// ============================================================================

// --- v1 support (backward compatibility) ---

static void loadV1FromJsonImpl(KSAudioProject* self, const QJsonObject& root)
{
    for (const auto& v : root["banks"].toArray()) {
        QJsonObject obj = v.toObject();
        SoundBank bank;
        bank.id = obj["id"].toString();
        bank.name = obj["name"].toString();
        bank.filePath = obj["filePath"].toString();
        for (const auto& ev : obj["events"].toArray()) {
            QJsonObject eo = ev.toObject();
            AudioEvent e;
            e.id = eo["id"].toString();
            e.name = eo["name"].toString();
            e.audioFile = eo["audioFile"].toString();
            e.volume = static_cast<float>(eo["volume"].toDouble(1.0));
            e.pitch  = static_cast<float>(eo["pitch"].toDouble(1.0));
            e.loop   = eo["loop"].toBool(false);
            bank.events.append(e);
        }
        self->banks().append(bank);
    }
}

void KSAudioProject::loadFromJson(const QJsonObject& root)
{
    m_projectName  = root["name"].toString("Untitled");
    m_schemaVersion = root["_version"].toString(
                      root.contains("format") ? "2.0" : "1.0");
    m_projectGuid  = root["guid"].toString();

    m_banks.clear();

    QString schema = root["_schema"].toString();
    bool isV2 = (schema == "ksaudio" && m_schemaVersion == "2.0")
                || root.contains("eventGroups")
                || root.contains("buses")
                || root.contains("sounds");

    if (isV2)
        loadV2FromJson(root);
    else
        loadV1FromJsonImpl(this, root);
}

// --- v2 loader — extracts runtime data from the full FSPRO-compatible format ---

static void collectV2Events(const QJsonArray& groups, QJsonArray& out)
{
    for (const auto& gv : groups) {
        QJsonObject g = gv.toObject();
        QJsonArray evs = g["events"].toArray();
        for (const auto& ev : evs) out.append(ev);
        collectV2Events(g["childGroups"].toArray(), out);
    }
}

void KSAudioProject::loadV2FromJson(const QJsonObject& root)
{
    // 1. Collect all events from eventGroups (recursively)
    QJsonArray allEvents;
    collectV2Events(root["eventGroups"].toArray(), allEvents);

    // 2. Build banks
    QJsonArray banksArr = root["banks"].toArray();
    if (banksArr.isEmpty() && !allEvents.isEmpty()) {
        // No explicit banks — create a single "Master" bank
        SoundBank bank;
        bank.id   = QUuid::createUuid().toString();
        bank.name = "Master";
        for (const auto& ev : allEvents) {
            QJsonObject eo = ev.toObject();
            AudioEvent e;
            e.id  = eo["guid"].toString(eo["name"].toString());
            e.name = eo["name"].toString();
            e.volume = static_cast<float>(eo["volume"].toDouble(1.0));
            e.pitch  = static_cast<float>(eo["pitch"].toDouble(1.0));
            e.loop   = eo["loop"].toBool(false);
            e.is3D   = (eo["type"].toString() != "2D");
            if (e.is3D) {
                QJsonObject sp = eo["spatialization"].toObject();
                e.minDistance = static_cast<float>(sp["minDistance"].toDouble(1.0));
                e.maxDistance = static_cast<float>(sp["maxDistance"].toDouble(100.0));
            }
            bank.events.append(e);
        }
        m_banks.append(bank);
    } else {
        for (const auto& bv : banksArr) {
            QJsonObject bo = bv.toObject();
            QString bankName = bo["name"].toString();
            // Collect events referenced by this bank
            QStringList eventGuids;
            for (const auto& egv : bo["eventGuids"].toArray())
                eventGuids.append(egv.toString());

            SoundBank bank;
            bank.id   = bo["guid"].toString(QUuid::createUuid().toString());
            bank.name = bankName;

            // Match events by GUID from the collected pool
            auto findEventByGuid = [&](const QString& guid) -> QJsonObject {
                for (const auto& ev : allEvents) {
                    if (ev.toObject()["guid"].toString() == guid)
                        return ev.toObject();
                }
                return {};
            };

            if (!eventGuids.isEmpty()) {
                for (const auto& guid : eventGuids) {
                    QJsonObject eo = findEventByGuid(guid);
                    if (eo.isEmpty()) continue;
                    AudioEvent e;
                    e.id  = eo["guid"].toString(eo["name"].toString());
                    e.name = eo["name"].toString();
                    e.volume = static_cast<float>(eo["volume"].toDouble(1.0));
                    e.pitch  = static_cast<float>(eo["pitch"].toDouble(1.0));
                    e.loop   = eo["loop"].toBool(false);
                    e.is3D   = (eo["type"].toString() != "2D");
                    if (e.is3D) {
                        QJsonObject sp = eo["spatialization"].toObject();
                        e.minDistance = static_cast<float>(sp["minDistance"].toDouble(1.0));
                        e.maxDistance = static_cast<float>(sp["maxDistance"].toDouble(100.0));
                    }
                    bank.events.append(e);
                }
            } else {
                // Bank has no eventGuids — add all events (flat fallback)
                for (const auto& ev : bo["events"].toArray()) {
                    QJsonObject eo = ev.toObject();
                    AudioEvent e;
                    e.id  = eo["guid"].toString(eo["name"].toString());
                    e.name = eo["name"].toString();
                    e.audioFile = eo["audioFile"].toString();
                    e.volume = static_cast<float>(eo["volume"].toDouble(1.0));
                    e.pitch  = static_cast<float>(eo["pitch"].toDouble(1.0));
                    e.loop   = eo["loop"].toBool(false);
                    e.is3D   = (eo["type"].toString() != "2D");
                    bank.events.append(e);
                }
            }
            m_banks.append(bank);
        }
    }

    // 3. Build mixer config from buses
    QJsonArray busesArr = root["buses"].toArray();
    if (!busesArr.isEmpty()) {
        BusInfo masterBus{ "Master", 1.0f, 1.0f };
        for (const auto& bv : busesArr) {
            QJsonObject bo = bv.toObject();
            BusInfo bus;
            bus.name    = bo["name"].toString();
            bus.volume  = static_cast<float>(bo["volume"].toDouble(1.0));
            bus.muted   = bo["mute"].toBool(false);
            if (bus.name != "Master")
                m_mixer.busses.append(bus);
            else
                masterBus = bus;
        }
        m_mixer.masterBus = masterBus;
    }
}

// ============================================================================
// JSON save
// ============================================================================

QJsonObject KSAudioProject::toJson() const
{
    // If we loaded a v2 document and nothing has changed, round-trip it
    if (m_schemaVersion == "2.0" && !m_rawDocument.isEmpty()) {
        // Preserve the original raw document to avoid data loss
        // Only update the runtime-relevant parts
        QJsonObject doc = m_rawDocument;
        doc["name"] = m_projectName;
        if (!m_projectGuid.isEmpty())
            doc["guid"] = m_projectGuid;
        return doc;
    }

    // v1 output (simple format)
    QJsonObject root;
    root["name"] = m_projectName;
    root["version"] = m_schemaVersion;
    QJsonArray banksArr;
    for (const auto& bank : m_banks) {
        QJsonObject bo;
        bo["id"] = bank.id;
        bo["name"] = bank.name;
        bo["filePath"] = bank.filePath;
        QJsonArray eventsArr;
        for (const auto& e : bank.events) {
            QJsonObject eo;
            eo["id"] = e.id;
            eo["name"] = e.name;
            eo["audioFile"] = e.audioFile;
            eo["volume"] = static_cast<double>(e.volume);
            eo["pitch"]  = static_cast<double>(e.pitch);
            eo["loop"]   = e.loop;
            eventsArr.append(eo);
        }
        bo["events"] = eventsArr;
        banksArr.append(bo);
    }
    root["banks"] = banksArr;
    return root;
}

// ============================================================================
// KSAudioMixer
// ============================================================================

KSAudioMixer::KSAudioMixer(QObject* parent) : QObject(parent) {}

void KSAudioMixer::setMasterVolume(float volume) { m_masterVolume = qBound(0.0f, volume, 2.0f); emit masterVolumeChanged(m_masterVolume); }

int KSAudioMixer::addBus(const QString& name, float volume)
{
    BusInfo bus;
    bus.name = name;
    bus.volume = volume;
    bus.fader = 1.0f;
    m_buses.append(bus);
    int idx = m_buses.size() - 1;
    m_busNameMap[name] = idx;
    emit busAdded(idx, name);
    return idx;
}

bool KSAudioMixer::removeBus(int busIndex)
{
    if (busIndex < 0 || busIndex >= m_buses.size()) return false;
    QString name = m_buses[busIndex].name;
    m_buses.removeAt(busIndex);
    m_busNameMap.remove(name);
    emit busRemoved(busIndex);
    return true;
}

void KSAudioMixer::clearBuses() { m_buses.clear(); m_busNameMap.clear(); }
QString KSAudioMixer::busName(int i) const { return (i >= 0 && i < m_buses.size()) ? m_buses[i].name : QString(); }
float KSAudioMixer::busVolume(int i) const { return (i >= 0 && i < m_buses.size()) ? m_buses[i].volume : 0.0f; }

void KSAudioMixer::setBusVolume(int i, float volume)
{
    if (i >= 0 && i < m_buses.size()) { m_buses[i].volume = volume; emit busVolumeChanged(i, volume); }
}

void KSAudioMixer::addEffectToBus(int busIndex, const QString& effectName)
{
    if (busIndex >= 0 && busIndex < m_buses.size()) m_buses[busIndex].effectChain.append(effectName);
}

void KSAudioMixer::removeEffectFromBus(int busIndex, int effectIndex)
{
    if (busIndex >= 0 && busIndex < m_buses.size()) {
        auto& chain = m_buses[busIndex].effectChain;
        if (effectIndex >= 0 && effectIndex < chain.size()) chain.removeAt(effectIndex);
    }
}

void KSAudioMixer::processMix(float* output, int sampleCount, int channels)
{
    if (!output || sampleCount <= 0 || channels <= 0) return;

    int totalSamples = sampleCount * channels;

    // Apply master volume and zero output (buses write into output via their event paths)
    float master = m_masterVolume;

    // For each active bus, apply volume scaling and accumulate
    float busGain = 0.0f;
    for (int b = 0; b < m_buses.size(); ++b) {
        const auto& bus = m_buses[b];
        if (!bus.enabled) continue;
        busGain += bus.volume * bus.fader * master;
    }
    // Apply combined bus gain to output
    float scale = (busGain > 0.0f) ? qMin(busGain, 4.0f) : 1.0f;
    for (int i = 0; i < totalSamples; ++i) {
        output[i] *= scale;
    }

    // Apply soft-clip limiter to prevent harsh digital clipping
    for (int i = 0; i < totalSamples; ++i) {
        float s = output[i];
        // Soft-clip using tanh approximation
        if (s > 1.0f) output[i] = 1.0f - 1.0f / (1.0f + s);
        else if (s < -1.0f) output[i] = -1.0f + 1.0f / (1.0f - s);
    }
}

float KSAudioEngine::applyAttenuation(const AudioEvent& event, const QVector3D& sourcePos) const
{
    float dist = (sourcePos - m_listener.position).length();
    if (dist <= event.minDistance) return 1.0f;
    if (dist >= event.maxDistance) return 0.0f;
    float t = (dist - event.minDistance) / (event.maxDistance - event.minDistance);
    switch (event.attenuation) {
    case AttenuationModel::Linear:      return 1.0f - t;
    case AttenuationModel::Logarithmic: return 1.0f / (1.0f + t);
    case AttenuationModel::Inverse:     return event.minDistance / (event.minDistance + (dist - event.minDistance));
    default:                            return 1.0f;
    }
}

// ============================================================================
// KSAudio3D
// ============================================================================

KSAudio3D::KSAudio3D(QObject* parent) : QObject(parent) {}

void KSAudio3D::setListenerPosition(const QVector3D& p) { m_listenerPos = p; emit listenerPositionChanged(p); }
void KSAudio3D::setListenerOrientation(const QVector3D& fwd, const QVector3D& up) { m_listenerForward = fwd; m_listenerUp = up; emit listenerOrientationChanged(fwd, up); }
void KSAudio3D::setListenerVelocity(const QVector3D& v) { m_listenerVelocity = v; }

float KSAudio3D::calculateAttenuation(const QVector3D& src, AttenuationModel model, const AttenuationSettings& s) const
{
    float dist = (src - m_listenerPos).length();
    if (dist <= s.minDistance) return 1.0f;
    if (dist >= s.maxDistance) return 0.0f;
    float t = (dist - s.minDistance) / (s.maxDistance - s.minDistance);
    switch (model) {
    case AttenuationModel::Linear: return 1.0f - t;
    case AttenuationModel::Logarithmic: return 1.0f / (1.0f + t);
    case AttenuationModel::Inverse: return s.minDistance / (s.minDistance + s.rolloffFactor * (dist - s.minDistance));
    default: return 1.0f;
    }
}

QVector3D KSAudio3D::calculatePan(const QVector3D& src) const
{
    QVector3D dir = (src - m_listenerPos).normalized();
    float right = QVector3D::dotProduct(dir, QVector3D::crossProduct(m_listenerForward, m_listenerUp).normalized());
    return QVector3D(0.5f - right * 0.5f, 0, 0.5f + right * 0.5f);
}

float KSAudio3D::calculateDopplerPitch(const QVector3D& src, const QVector3D& vel, float basePitch) const
{
    QVector3D toSource = src - m_listenerPos;
    float dist = toSource.length();
    if (dist < 0.001f) return basePitch;
    QVector3D dir = toSource / dist;
    float relativeVelocity = QVector3D::dotProduct(vel - m_listenerVelocity, dir);
    float doppler = m_speedOfSound / (m_speedOfSound - relativeVelocity * m_dopplerFactor);
    return basePitch * qBound(0.1f, doppler, 10.0f);
}

// ============================================================================
// KSAudioDSP
// ============================================================================

KSAudioDSP::KSAudioDSP(QObject* parent) : QObject(parent) {}

int KSAudioDSP::addEffect(EffectType type, const QString& name)
{
    m_effects.append(DSPEffect(name, type));
    int idx = m_effects.size() - 1;
    emit effectAdded(idx, name);
    return idx;
}

bool KSAudioDSP::removeEffect(int index)
{
    if (index < 0 || index >= m_effects.size()) return false;
    m_effects.removeAt(index);
    emit effectRemoved(index);
    return true;
}

void KSAudioDSP::clearEffects() { m_effects.clear(); }
DSPEffect* KSAudioDSP::getEffect(int index) { return (index >= 0 && index < m_effects.size()) ? &m_effects[index] : nullptr; }

void KSAudioDSP::process(float* samples, int sampleCount, int channels, int sampleRate)
{
    if (!samples || sampleCount <= 0 || channels <= 0) return;

    for (int i = 0; i < m_effects.size(); ++i) {
        if (!m_effects[i].enabled) continue;
        const auto& fx = m_effects[i];
        auto& params = fx.parameters;
        switch (fx.type) {
        case EffectType::LowPass: {
            float cutoff = params.size() > 0 ? params[0] : 1000.0f;
            float resonance = params.size() > 1 ? params[1] : 0.707f;
            processLowPass(samples, sampleCount, channels, cutoff, resonance, sampleRate);
            break;
        }
        case EffectType::HighPass: {
            float cutoff = params.size() > 0 ? params[0] : 100.0f;
            float resonance = params.size() > 1 ? params[1] : 0.707f;
            processHighPass(samples, sampleCount, channels, cutoff, resonance, sampleRate);
            break;
        }
        case EffectType::Reverb: {
            float wetDry = params.size() > 0 ? params[0] : 0.3f;
            float roomSize = params.size() > 1 ? params[1] : 0.7f;
            float damping = params.size() > 2 ? params[2] : 0.5f;
            processReverb(samples, sampleCount, channels, wetDry, roomSize, damping);
            break;
        }
        case EffectType::Delay: {
            float delayTime = params.size() > 0 ? params[0] : 0.3f;
            float feedback = params.size() > 1 ? params[1] : 0.4f;
            float mix = params.size() > 2 ? params[2] : 0.3f;
            processDelay(samples, sampleCount, channels, delayTime, feedback, mix, sampleRate);
            break;
        }
        case EffectType::Compressor: {
            float threshold = params.size() > 0 ? params[0] : -20.0f;
            float ratio = params.size() > 1 ? params[1] : 4.0f;
            float attack = params.size() > 2 ? params[2] : 10.0f;
            float release = params.size() > 3 ? params[3] : 100.0f;
            float knee = params.size() > 4 ? params[4] : 6.0f;
            processCompressor(samples, sampleCount, channels, threshold, ratio, attack, release, knee);
            break;
        }
        default:
            break;
        }
    }
}

void KSAudioDSP::setEffectParameter(int effectIndex, int paramIndex, float value)
{
    if (effectIndex < 0 || effectIndex >= m_effects.size()) return;
    auto& params = m_effects[effectIndex].parameters;
    while (params.size() <= paramIndex) params.append(0.0f);
    params[paramIndex] = value;
    emit parameterChanged(effectIndex, paramIndex, value);
}

void KSAudioDSP::enableEffect(int index, bool enable)
{
    if (index >= 0 && index < m_effects.size()) {
        m_effects[index].enabled = enable;
        emit effectEnabledChanged(index, enable);
    }
}

void KSAudioDSP::processLowPass(float* samples, int sampleCount, int channels, float cutoff, float resonance, int sampleRate)
{
    if (cutoff <= 0.0f || cutoff >= sampleRate * 0.5f) return;
    float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
    float dt = 1.0f / sampleRate;
    float alpha = dt / (rc + dt);
    float q = 1.0f / (2.0f * resonance);
    float a0 = alpha / (1.0f + q * alpha - q * alpha * alpha);
    float a1 = alpha * alpha / (1.0f + q * alpha - q * alpha * alpha);
    float b1 = 2.0f * (alpha * alpha - 1.0f) / (1.0f + q * alpha - q * alpha * alpha);
    float b2 = (1.0f - q * alpha - alpha * alpha) / (1.0f + q * alpha - q * alpha * alpha);

    for (int ch = 0; ch < channels; ++ch) {
        float y1 = 0.0f, y2 = 0.0f, x1 = 0.0f, x2 = 0.0f;
        for (int i = ch; i < sampleCount; i += channels) {
            float x0 = samples[i];
            float y0 = a0 * x0 + a1 * x1 + a1 * x2 - b1 * y1 - b2 * y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            samples[i] = y0;
        }
    }
}

void KSAudioDSP::processHighPass(float* samples, int sampleCount, int channels, float cutoff, float resonance, int sampleRate)
{
    if (cutoff <= 0.0f || cutoff >= sampleRate * 0.5f) return;
    float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
    float dt = 1.0f / sampleRate;
    float alpha = rc / (rc + dt);
    float q = 1.0f / (2.0f * resonance);
    float a0 = q + alpha * (1.0f + q);
    float a1 = -2.0f * alpha * q;
    float a2 = q * (alpha - 1.0f);
    float b1 = -2.0f * alpha * q;
    float b2 = q * (alpha - 1.0f);

    for (int ch = 0; ch < channels; ++ch) {
        float y1 = 0.0f, y2 = 0.0f, x1 = 0.0f, x2 = 0.0f;
        for (int i = ch; i < sampleCount; i += channels) {
            float x0 = samples[i];
            float y0 = (a0 * x0 + a1 * x1 + a2 * x2 - b1 * y1 - b2 * y2) / a0;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            samples[i] = y0;
        }
    }
}

void KSAudioDSP::processReverb(float* samples, int sampleCount, int channels, float wetDry, float roomSize, float damping)
{
    const int numDelays = 4;
    static const float delayTimes[4] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
    static float delayBuffers[4][4096] = {};
    static int delayPositions[4] = {};
    static float feedbackBuffers[4] = {};

    float feedback = roomSize * 0.9f;
    wetDry = qBound(0.0f, wetDry, 1.0f);
    float dry = 1.0f - wetDry;

    for (int i = 0; i < sampleCount; ++i) {
        float input = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            input += samples[i * channels + ch] / channels;

        float wet = 0.0f;
        for (int d = 0; d < numDelays; ++d) {
            int delayLen = static_cast<int>(delayTimes[d] * 44100);
            int pos = delayPositions[d];
            float delayed = delayBuffers[d][pos];
            delayBuffers[d][pos] = input + delayed * feedback * (1.0f - damping);
            delayPositions[d] = (pos + 1) % qMax(delayLen, 1);
            wet += delayed;
        }
        wet /= numDelays;

        for (int ch = 0; ch < channels; ++ch)
            samples[i * channels + ch] = samples[i * channels + ch] * dry + wet * wetDry;
    }
}

void KSAudioDSP::processDelay(float* samples, int sampleCount, int channels, float delayTime, float feedback, float mix, int sampleRate)
{
    int delaySamples = qBound(1, static_cast<int>(delayTime * sampleRate), sampleRate * 2);
    static float delayBuffer[88200] = {};
    static int writePos = 0;

    mix = qBound(0.0f, mix, 1.0f);
    feedback = qBound(0.0f, feedback, 0.95f);

    for (int i = 0; i < sampleCount; ++i) {
        float input = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            input += samples[i * channels + ch] / channels;

        int readPos = (writePos - delaySamples + 88200) % 88200;
        float delayed = delayBuffer[readPos];
        delayBuffer[writePos] = input + delayed * feedback;
        writePos = (writePos + 1) % 88200;

        float output = input * (1.0f - mix) + delayed * mix;
        for (int ch = 0; ch < channels; ++ch)
            samples[i * channels + ch] = output;
    }
}

void KSAudioDSP::processCompressor(float* samples, int sampleCount, int channels, float threshold, float ratio, float attack, float release, float knee)
{
    threshold = qPow(10.0f, threshold / 20.0f);
    attack = qExp(-1.0f / (attack * 0.001f * 44100.0f));
    release = qExp(-1.0f / (release * 0.001f * 44100.0f));

    float envelope = 0.0f;

    for (int i = 0; i < sampleCount; ++i) {
        float input = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            input += samples[i * channels + ch] / channels;

        float absInput = qAbs(input);
        float level = (absInput > envelope) ? attack * (envelope - absInput) + absInput : release * (envelope - absInput) + absInput;
        envelope = qMax(level, 0.0001f);

        float gain = 1.0f;
        if (envelope > threshold) {
            float overDb = 20.0f * std::log10(envelope / threshold);
            float compressedDb = overDb / ratio;
            gain = qPow(10.0f, (compressedDb - overDb) / 20.0f);
        }

        for (int ch = 0; ch < channels; ++ch)
            samples[i * channels + ch] *= gain;
    }
}

// ============================================================================
// KSAudioBankExporter
// ============================================================================

KSAudioBankExporter::KSAudioBankExporter(QObject* parent) : QObject(parent) {}

bool KSAudioBankExporter::exportBank(const SoundBank& bank, const QString& outputPath)
{
    emit exportStarted(bank.name);

    // Use KSBankWriter to generate FMOD FEV2 format .bank files
    fileformat::KSBankWriter writer;
    QString assetsDir = QFileInfo(outputPath).absolutePath() + "/Assets/audio";

    // Build a temporary project with this bank
    KSAudioProject tempProj;
    tempProj.setProjectName(bank.name);

    QJsonObject raw;
    raw["_schema"]  = QStringLiteral("ksaudio");
    raw["_version"] = QStringLiteral("2.0.0");
    raw["name"]     = bank.name;

    QJsonObject bankObj;
    bankObj["guid"] = bank.id.isEmpty() ? QUuid::createUuid().toString() : bank.id;
    bankObj["name"] = bank.name;
    QJsonArray eventsArr;
    QJsonArray eventGuidsArr;
    QJsonObject eventGroup;
    eventGroup["guid"] = QUuid::createUuid().toString();
    eventGroup["name"] = bank.name;
    QJsonArray groupEvents;
    for (const auto& e : bank.events) {
        QString guid = e.id.isEmpty() ? QUuid::createUuid().toString() : e.id;
        QJsonObject eo;
        eo["guid"] = guid;
        eo["name"] = e.name;
        eo["type"] = e.is3D ? "3D" : "2D";
        eo["audioFile"] = e.audioFile;
        eo["volume"] = static_cast<double>(e.volume);
        eo["pitch"]  = static_cast<double>(e.pitch);
        eo["loop"]   = e.loop;
        groupEvents.append(eo);

        // Sound asset reference
        if (!e.audioFile.isEmpty()) {
            QJsonObject so;
            so["guid"]   = QUuid::createUuid().toString();
            so["name"]   = QFileInfo(e.audioFile).baseName();
            so["file"]   = e.audioFile;
            so["format"] = QFileInfo(e.audioFile).suffix();
            QJsonArray soundsArr = raw["sounds"].toArray();
            soundsArr.append(so);
            raw["sounds"] = soundsArr;
        }
    }
    eventGroup["events"] = groupEvents;
    QJsonArray groupsArr;
    groupsArr.append(eventGroup);
    raw["eventGroups"] = groupsArr;

    bankObj["eventGuids"] = eventGuidsArr;
    QJsonArray banksArr;
    banksArr.append(bankObj);
    raw["banks"] = banksArr;

    tempProj.setRawDocument(raw);

    if (!writer.writeProjectBanks(tempProj, QFileInfo(outputPath).absolutePath())) {
        emit exportFailed(writer.lastError());
        return false;
    }

    emit exportCompleted(outputPath);
    qInfo() << "KSAudioBankExporter: Exported FMOD bank" << bank.name << "to" << outputPath;
    return true;
}

bool KSAudioBankExporter::exportProject(const KSAudioProject& project, const QString& outputDir)
{
    QDir dir(outputDir);
    if (!dir.exists()) QDir().mkpath(outputDir);

    emit exportStarted(project.projectName());

    for (int i = 0; i < project.banks().size(); ++i) {
        const SoundBank& bank = project.banks()[i];
        QString bankPath = outputDir + "/" + bank.name + ".ksbank";
        if (!exportBank(bank, bankPath)) return false;
    }

    QJsonObject manifest;
    manifest["name"] = project.projectName();
    manifest["version"] = "1.0";
    manifest["bankCount"] = project.banks().size();
    manifest["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray banksArr;
    for (const auto& bank : project.banks()) {
        QJsonObject bo;
        bo["name"] = bank.name;
        bo["eventCount"] = bank.events.size();
        bo["file"] = bank.name + ".ksbank";
        banksArr.append(bo);
    }
    manifest["banks"] = banksArr;

    QFile manifestFile(outputDir + "/manifest.json");
    if (manifestFile.open(QIODevice::WriteOnly)) {
        manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        manifestFile.close();
    }

    emit exportCompleted(outputDir);
    qInfo() << "KSAudioBankExporter: Exported project" << project.projectName() << "to" << outputDir;
    return true;
}

bool KSAudioBankExporter::writeBankHeader(QFile& file, const SoundBank& bank)
{
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << static_cast<quint32>(0x4B53424B);
    stream << static_cast<quint32>(1);
    stream << static_cast<quint32>(bank.events.size());

    QByteArray nameBytes = bank.name.toUtf8();
    stream << static_cast<quint32>(nameBytes.size());
    file.write(nameBytes);

    return true;
}

bool KSAudioBankExporter::writeEventData(QFile& file, const AudioEvent& event)
{
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << static_cast<quint32>(0x4B534556);

    QByteArray nameBytes = event.name.toUtf8();
    QByteArray audioBytes = event.audioFile.toUtf8();
    stream << static_cast<quint32>(nameBytes.size());
    file.write(nameBytes);
    stream << static_cast<quint32>(audioBytes.size());
    file.write(audioBytes);

    stream << event.volume;
    stream << event.pitch;
    stream << static_cast<quint8>(event.loop ? 1 : 0);
    stream << static_cast<quint8>(event.is3D ? 1 : 0);
    stream << static_cast<quint32>(static_cast<quint8>(event.attenuation));
    stream << event.minDistance;
    stream << event.maxDistance;

    stream << event.position.x();
    stream << event.position.y();
    stream << event.position.z();

    stream << static_cast<quint32>(event.parameters.size());
    for (auto it = event.parameters.begin(); it != event.parameters.end(); ++it) {
        QByteArray keyBytes = it.key().toUtf8();
        stream << static_cast<quint32>(keyBytes.size());
        file.write(keyBytes);
        stream << it.value().toFloat();
    }

    return true;
}

} // namespace audio
} // namespace ks
