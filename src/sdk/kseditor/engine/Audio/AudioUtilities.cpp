#include "AudioUtilities.h"
#include "AudioTypes.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QAudioFormat>
#include <cmath>

namespace ks {
namespace audio {

// ─── KSAudioTypes ────────────────────────────────────────────────────────────

QString KSAudioTypes::formatToString(SampleFormat format)
{
    switch (format) {
        case Int8: return "8-bit Integer";
        case Int16: return "16-bit Integer";
        case Int24: return "24-bit Integer";
        case Int32: return "32-bit Integer";
        case Float32: return "32-bit Float";
        case Float64: return "64-bit Float";
        default: return "Unknown";
    }
}

int KSAudioTypes::formatToBytes(SampleFormat format)
{
    switch (format) {
        case Int8: return 1;
        case Int16: return 2;
        case Int24: return 3;
        case Int32: return 4;
        case Float32: return 4;
        case Float64: return 8;
        default: return 2;
    }
}

QString KSAudioTypes::channelConfigToString(ChannelConfig config)
{
    switch (config) {
        case Mono: return "Mono";
        case Stereo: return "Stereo";
        case Surround51: return "5.1 Surround";
        case Surround71: return "7.1 Surround";
        default: return "Unknown";
    }
}

int KSAudioTypes::channelCount(ChannelConfig config)
{
    switch (config) {
        case Mono: return 1;
        case Stereo: return 2;
        case Surround51: return 6;
        case Surround71: return 8;
        default: return 2;
    }
}

// ─── KSAudioExtensions ───────────────────────────────────────────────────────

QStringList KSAudioExtensions::s_extensions;

QStringList KSAudioExtensions::getSupportedExtensions()
{
    if (s_extensions.isEmpty()) {
        s_extensions = {"wav", "ogg", "mp3", "flac", "aiff", "aac", "wma"};
    }
    return s_extensions;
}

bool KSAudioExtensions::isSupported(const QString& extension)
{
    return getSupportedExtensions().contains(extension.toLower());
}

QString KSAudioExtensions::getFileFilter()
{
    return "Audio Files (*.wav *.ogg *.mp3 *.flac *.aiff);;WAV Files (*.wav);;OGG Files (*.ogg);;MP3 Files (*.mp3);;FLAC Files (*.flac)";
}

int KSAudioExtensions::getSampleSize(const QString& format)
{
    if (format == "8bit") return 1;
    if (format == "16bit") return 2;
    if (format == "24bit") return 3;
    if (format == "32bit") return 4;
    return 2;
}

int KSAudioExtensions::getSampleRate(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 44100;

    QByteArray header = file.read(44);
    if (header.size() < 28) return 44100;

    return *reinterpret_cast<const quint32*>(header.constData() + 24);
}

// ─── KSAudioPresetManager ────────────────────────────────────────────────────

void KSAudioPresetManager::savePreset(const QString& name, const QString& category, const QJsonObject& data)
{
    Preset preset;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = name;
    preset.category = category;
    preset.data = data;
    m_presets[preset.id] = preset;
    emit presetSaved(preset.id);
}

bool KSAudioPresetManager::loadPreset(const QString& presetId, QJsonObject& data)
{
    if (m_presets.contains(presetId)) {
        data = m_presets[presetId].data;
        emit presetLoaded(presetId);
        return true;
    }
    return false;
}

QVector<KSAudioPresetManager::Preset> KSAudioPresetManager::getPresets(const QString& category) const
{
    QVector<Preset> results;
    for (const Preset& p : m_presets.values()) {
        if (category.isEmpty() || p.category == category) {
            results.append(p);
        }
    }
    return results;
}

bool KSAudioPresetManager::deletePreset(const QString& presetId)
{
    if (m_presets.remove(presetId)) {
        emit presetDeleted(presetId);
        return true;
    }
    return false;
}

void KSAudioPresetManager::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();
    m_presets.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QJsonObject obj = it.value().toObject();
        Preset p;
        p.id = it.key();
        p.name = obj["name"].toString();
        p.category = obj["category"].toString();
        p.data = obj["data"].toObject();
        m_presets[p.id] = p;
    }
}

void KSAudioPresetManager::saveToFile(const QString& path)
{
    QJsonObject root;
    for (auto it = m_presets.constBegin(); it != m_presets.constEnd(); ++it) {
        QJsonObject obj;
        obj["name"] = it.value().name;
        obj["category"] = it.value().category;
        obj["data"] = it.value().data;
        root[it.key()] = obj;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

// ─── KSAudioShortcutManager ──────────────────────────────────────────────────

void KSAudioShortcutManager::registerShortcut(const QString& action, const QString& keySequence)
{
    m_shortcuts[action] = keySequence;
}

QString KSAudioShortcutManager::getShortcut(const QString& action) const
{
    return m_shortcuts.value(action);
}

void KSAudioShortcutManager::setDefaultShortcuts()
{
    registerShortcut("save", "Ctrl+S");
    registerShortcut("open", "Ctrl+O");
    registerShortcut("undo", "Ctrl+Z");
    registerShortcut("redo", "Ctrl+Y");
    registerShortcut("cut", "Ctrl+X");
    registerShortcut("copy", "Ctrl+C");
    registerShortcut("paste", "Ctrl+V");
}

// ─── KSAudioRecentFilesManager ────────────────────────────────────────────────

void KSAudioRecentFilesManager::addFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    if (m_recentFiles.size() > m_maxFiles) {
        m_recentFiles.removeLast();
    }
    emit fileAdded(path);
}

void KSAudioRecentFilesManager::removeFile(const QString& path)
{
    if (m_recentFiles.removeAll(path)) {
        emit fileRemoved(path);
    }
}

void KSAudioRecentFilesManager::clear()
{
    m_recentFiles.clear();
    emit cleared();
}

QStringList KSAudioRecentFilesManager::recentFiles(int maxCount) const
{
    return m_recentFiles.mid(0, maxCount);
}

// ─── KSSoundBankPlayer ───────────────────────────────────────────────────────

bool KSSoundBankPlayer::loadBank(const QString& path) {
    auto* engine = KSAudioEngine::instance();
    if (!engine || !engine->isInitialized()) return false;

    bool ok = engine->loadBank(path);
    if (ok) {
        QFileInfo fi(path);
        SoundBank bank;
        bank.name = fi.baseName();
        bank.path = path;
        bank.eventCount = 0;
        m_banks.append(bank);
        emit bankLoaded(bank.name);
    }
    return ok;
}

void KSSoundBankPlayer::unloadBank(const QString& name) {
    for (int i = 0; i < m_banks.size(); ++i) {
        if (m_banks[i].name == name) {
            m_banks.removeAt(i);
            break;
        }
    }
}

QString KSSoundBankPlayer::playEvent(const QString& eventName) {
    auto* engine = KSAudioEngine::instance();
    if (!engine || !engine->isInitialized()) return {};

    int instanceId = engine->playEvent(eventName);
    if (instanceId < 0) return {};

    QString id = QString::number(instanceId);
    emit eventStarted(id);
    return id;
}

void KSSoundBankPlayer::stopEvent(const QString& playbackId) {
    auto* engine = KSAudioEngine::instance();
    if (!engine || !engine->isInitialized()) return;

    bool ok = false;
    int id = playbackId.toInt(&ok);
    if (ok) {
        engine->stopEvent(id);
        emit eventStopped(playbackId);
    }
}

void KSSoundBankPlayer::stopAll() {
    auto* engine = KSAudioEngine::instance();
    if (engine && engine->isInitialized()) {
        engine->stopAllEvents();
    }
}

// ─── KSMultiAudioTrackEditor ─────────────────────────────────────────────────

void KSMultiAudioTrackEditor::addTrack(const QString& name)
{
    Track track;
    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    track.name = name;
    track.sampleRate = 44100;
    track.muted = false;
    track.solo = false;
    track.volume = 1.0f;
    m_tracks[track.id] = track;
    emit trackAdded(track.id);
}

void KSMultiAudioTrackEditor::removeTrack(const QString& trackId)
{
    m_tracks.remove(trackId);
    emit trackRemoved(trackId);
}

KSMultiAudioTrackEditor::Track KSMultiAudioTrackEditor::getTrack(const QString& trackId) const
{
    return m_tracks.value(trackId);
}

void KSMultiAudioTrackEditor::setTrackAudio(const QString& trackId, const QVector<float>& audio)
{
    if (m_tracks.contains(trackId)) {
        m_tracks[trackId].audioData = audio;
    }
}

void KSMultiAudioTrackEditor::setTrackVolume(const QString& trackId, float volume)
{
    if (m_tracks.contains(trackId)) {
        m_tracks[trackId].volume = qBound(0.0f, volume, 1.0f);
    }
}

void KSMultiAudioTrackEditor::setTrackMute(const QString& trackId, bool muted)
{
    if (m_tracks.contains(trackId)) {
        m_tracks[trackId].muted = muted;
    }
}

void KSMultiAudioTrackEditor::setTrackSolo(const QString& trackId, bool solo)
{
    if (m_tracks.contains(trackId)) {
        m_tracks[trackId].solo = solo;
    }
}

QVector<float> KSMultiAudioTrackEditor::mix(int sampleRate)
{
    QVector<float> result;
    bool anySolo = false;

    for (const Track& t : m_tracks) {
        if (t.solo) { anySolo = true; break; }
    }

    int maxLen = 0;
    for (const Track& t : m_tracks) {
        bool active = anySolo ? t.solo : !t.muted;
        if (active) maxLen = qMax(maxLen, t.audioData.size());
    }

    result.resize(maxLen);
    for (const Track& t : m_tracks) {
        bool active = anySolo ? t.solo : !t.muted;
        if (!active) continue;
        for (int i = 0; i < t.audioData.size() && i < maxLen; ++i) {
            result[i] += t.audioData[i] * t.volume;
        }
    }

    float peak = 0;
    for (float s : result) peak = qMax(peak, qAbs(s));
    if (peak > 1.0f) {
        for (float& s : result) s /= peak;
    }

    emit mixComplete(result);
    return result;
}

// ─── KSSilenceFinder ────────────────────────────────────────────────────────

QVector<KSSilenceFinder::SilenceRegion> KSSilenceFinder::find(const QVector<float>& audio, int sampleRate)
{
    QVector<SilenceRegion> regions;

    float thresholdLinear = qPow(10.0f, m_threshold / 20.0f);
    bool inSilence = false;
    int regionStart = 0;

    for (int i = 0; i < audio.size(); ++i) {
        bool isSilent = qAbs(audio[i]) < thresholdLinear;

        if (isSilent && !inSilence) {
            inSilence = true;
            regionStart = i;
        } else if (!isSilent && inSilence) {
            inSilence = false;
            int regionLen = i - regionStart;
            float durationMs = static_cast<float>(regionLen) / sampleRate * 1000.0f;
            if (durationMs >= m_minDuration) {
                SilenceRegion r;
                r.startSample = regionStart;
                r.endSample = i;
                r.durationMs = durationMs;
                regions.append(r);
            }
        }
    }

    if (inSilence) {
        int regionLen = audio.size() - regionStart;
        float durationMs = static_cast<float>(regionLen) / sampleRate * 1000.0f;
        if (durationMs >= m_minDuration) {
            SilenceRegion r;
            r.startSample = regionStart;
            r.endSample = audio.size();
            r.durationMs = durationMs;
            regions.append(r);
        }
    }

    emit found(regions);
    return regions;
}

// ─── KSLabelAudioTrack ──────────────────────────────────────────────────────

void KSLabelAudioTrack::addLabel(int position, const QString& text, const QString& category)
{
    Label label;
    label.position = position;
    label.text = text;
    label.category = category;
    m_labels.append(label);
    emit labelAdded(label);
}

void KSLabelAudioTrack::removeLabel(int position)
{
    for (int i = 0; i < m_labels.size(); ++i) {
        if (m_labels[i].position == position) {
            m_labels.removeAt(i);
            emit labelRemoved(position);
            return;
        }
    }
}

void KSLabelAudioTrack::updateLabel(int position, const QString& text)
{
    for (Label& label : m_labels) {
        if (label.position == position) {
            label.text = text;
            return;
        }
    }
}

QVector<KSLabelAudioTrack::Label> KSLabelAudioTrack::getLabelsInRange(int start, int end) const
{
    QVector<Label> result;
    for (const Label& l : m_labels) {
        if (l.position >= start && l.position <= end) {
            result.append(l);
        }
    }
    return result;
}

// ─── KSVSTEffectsChain ──────────────────────────────────────────────────────

void KSVSTEffectsChain::loadPlugin(const QString& path)
{
    VSTPlugin plugin;
    plugin.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plugin.path = path;
    plugin.name = QFileInfo(path).baseName();
    plugin.enabled = true;
    m_plugins[plugin.id] = plugin;
    emit pluginLoaded(plugin.id);
}

void KSVSTEffectsChain::unloadPlugin(const QString& pluginId)
{
    m_plugins.remove(pluginId);
    emit pluginUnloaded(pluginId);
}

KSVSTEffectsChain::VSTPlugin KSVSTEffectsChain::getPlugin(const QString& pluginId) const
{
    return m_plugins.value(pluginId);
}

void KSVSTEffectsChain::addEffect(const QString& pluginId)
{
    if (m_plugins.contains(pluginId)) {
        m_effectOrder.append(pluginId);
        emit chainChanged();
    }
}

void KSVSTEffectsChain::removeEffect(int index)
{
    if (index >= 0 && index < m_effectOrder.size()) {
        m_effectOrder.removeAt(index);
        emit chainChanged();
    }
}

void KSVSTEffectsChain::moveEffect(int from, int to)
{
    if (from < 0 || from >= m_effectOrder.size()) return;
    if (to < 0 || to >= m_effectOrder.size()) return;
    m_effectOrder.move(from, to);
    emit chainChanged();
}

QVector<float> KSVSTEffectsChain::process(const QVector<float>& input, int sampleRate)
{
    if (m_bypass || m_effectOrder.isEmpty()) return input;

    QVector<float> output = input;
    for (const QString& pluginId : m_effectOrder) {
        const VSTPlugin& plugin = m_plugins.value(pluginId);
        if (!plugin.enabled) continue;

        QString name = plugin.name.toLower();

        if (name.contains("gain") || name.contains("volume")) {
            float gain = plugin.parameters.value("gain", 1.0f);
            for (float& s : output) s *= gain;

        } else if (name.contains("lowpass") || name.contains("lp")) {
            float cutoff = plugin.parameters.value("cutoff", 0.2f);
            cutoff = qBound(0.01f, cutoff, 0.99f);
            float a = 1.0f - cutoff;
            float prev = 0.0f;
            for (float& s : output) { prev = s * a + prev * (1.0f - a); s = prev; }

        } else if (name.contains("highpass") || name.contains("hp")) {
            float cutoff = plugin.parameters.value("cutoff", 0.2f);
            cutoff = qBound(0.01f, cutoff, 0.99f);
            float a = 1.0f - cutoff;
            float prevIn = 0.0f, prevOut = 0.0f;
            for (float& s : output) { float in = s; s = a * (prevOut + in - prevIn); prevIn = in; prevOut = s; }

        } else if (name.contains("delay") || name.contains("echo")) {
            int delaySamples = static_cast<int>(plugin.parameters.value("delay", 0.3f) * sampleRate);
            float feedback = qBound(0.0f, plugin.parameters.value("feedback", 0.3f), 0.95f);
            if (delaySamples < 1) continue;
            QVector<float> wet(output.size(), 0.0f);
            for (int i = 0; i < output.size(); ++i) {
                if (i >= delaySamples) wet[i] = output[i - delaySamples] * feedback;
                output[i] += wet[i];
            }

        } else if (name.contains("distortion") || name.contains("overdrive") || name.contains("clipper")) {
            float drive = plugin.parameters.value("drive", 2.0f);
            for (float& s : output) { s = qBound(-1.0f, s * drive, 1.0f); }

        } else if (name.contains("noisegate") || name.contains("gate")) {
            float threshold = plugin.parameters.value("threshold", 0.01f);
            float attack = plugin.parameters.value("attack", 0.001f);
            float release = plugin.parameters.value("release", 0.05f);
            float gain = 1.0f;
            float attackCoeff = exp(-1.0f / (attack * sampleRate + 1.0f));
            float releaseCoeff = exp(-1.0f / (release * sampleRate + 1.0f));
            for (float& s : output) {
                float absS = fabs(s);
                if (absS < threshold) gain *= releaseCoeff;
                else gain = qMin(1.0f, gain / attackCoeff);
                s *= gain;
            }

        } else if (name.contains("compressor") || name.contains("comp")) {
            float threshold = plugin.parameters.value("threshold", 0.5f);
            float ratio = plugin.parameters.value("ratio", 4.0f);
            float makeup = plugin.parameters.value("makeup", 1.0f);
            for (float& s : output) {
                float absS = fabs(s);
                if (absS > threshold) s = (s > 0 ? 1 : -1) * (threshold + (absS - threshold) / ratio);
                s *= makeup;
            }
        }
    }
    return output;
}

} // namespace audio
} // namespace ks
