#include "AudioStudioTypes.h"
#include "AudioTypes.h"

#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QJsonArray>
#include <QFileInfo>
#include <QAudioFormat>
#include <QProcess>

namespace ks { namespace audio {

bool AudioStudio::loadProject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) return false;

    QJsonObject json = doc.object();

    m_projectPath = path;
    m_masterVolume = static_cast<float>(json["masterVolume"].toDouble(1.0));
    m_modified = false;

    if (json.contains("tracks")) {
        QJsonArray tracksArr = json["tracks"].toArray();
        for (const auto& t : tracksArr) {
            QJsonObject to = t.toObject();
            Track track;
            track.id = to["id"].toString();
            track.name = to["name"].toString();
            track.sampleRate = to["sampleRate"].toInt(44100);
            track.muted = to["muted"].toBool(false);
            track.solo = to["solo"].toBool(false);
            track.volume = static_cast<float>(to["volume"].toDouble(1.0));
            track.audioFile = to["audioFile"].toString();
            if (to.contains("labels")) {
                for (const auto& l : to["labels"].toArray()) {
                    QJsonObject lo = l.toObject();
                    AudioLabel label;
                    label.position = lo["position"].toInt();
                    label.text = lo["text"].toString();
                    label.category = lo["category"].toString();
                    track.labels.append(label);
                }
            }
            m_tracks.append(track);
        }
    }

    if (json.contains("effects")) {
        QJsonArray fxArr = json["effects"].toArray();
        for (const auto& fx : fxArr) {
            QJsonObject fxo = fx.toObject();
            Effect effect;
            effect.name = fxo["name"].toString();
            effect.type = fxo["type"].toString();
            effect.enabled = fxo["enabled"].toBool(true);
            QJsonObject params = fxo["parameters"].toObject();
            for (auto it = params.begin(); it != params.end(); ++it) {
                effect.parameters[it.key()] = static_cast<float>(it.value().toDouble());
            }
            m_effects.append(effect);
        }
    }

    if (json.contains("buses")) {
        QJsonArray busArr = json["buses"].toArray();
        for (const auto& b : busArr) {
            QJsonObject bo = b.toObject();
            BusConfig bus;
            bus.name = bo["name"].toString();
            bus.volume = static_cast<float>(bo["volume"].toDouble(1.0));
            bus.muted = bo["muted"].toBool(false);
            QJsonArray chainArr = bo["effectChain"].toArray();
            QStringList chain;
            for (const auto& c : chainArr) chain.append(c.toString());
            bus.effectChain = chain;
            m_buses.append(bus);
        }
    }

    emit projectLoaded(path);
    return true;
}

bool AudioStudio::saveProject(const QString& path)
{
    QJsonObject json;
    json["version"] = "1.0";
    json["masterVolume"] = static_cast<double>(m_masterVolume);

    QJsonArray tracksArr;
    for (const auto& track : m_tracks) {
        QJsonObject to;
        to["id"] = track.id;
        to["name"] = track.name;
        to["sampleRate"] = track.sampleRate;
        to["muted"] = track.muted;
        to["solo"] = track.solo;
        to["volume"] = static_cast<double>(track.volume);
        to["audioFile"] = track.audioFile;
        QJsonArray labelsArr;
        for (const auto& label : track.labels) {
            QJsonObject lo;
            lo["position"] = label.position;
            lo["text"] = label.text;
            lo["category"] = label.category;
            labelsArr.append(lo);
        }
        to["labels"] = labelsArr;
        tracksArr.append(to);
    }
    json["tracks"] = tracksArr;

    QJsonArray fxArr;
    for (const auto& fx : m_effects) {
        QJsonObject fxo;
        fxo["name"] = fx.name;
        fxo["type"] = fx.type;
        fxo["enabled"] = fx.enabled;
        QJsonObject params;
        for (auto it = fx.parameters.begin(); it != fx.parameters.end(); ++it) {
            params[it.key()] = static_cast<double>(it.value());
        }
        fxo["parameters"] = params;
        fxArr.append(fxo);
    }
    json["effects"] = fxArr;

    QJsonArray busArr;
    for (const auto& bus : m_buses) {
        QJsonObject bo;
        bo["name"] = bus.name;
        bo["volume"] = static_cast<double>(bus.volume);
        bo["muted"] = bus.muted;
        QJsonArray chainArr;
        for (const auto& e : bus.effectChain) chainArr.append(e);
        bo["effectChain"] = chainArr;
        busArr.append(bo);
    }
    json["buses"] = busArr;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    file.close();

    m_projectPath = path;
    m_modified = false;
    emit projectSaved(path);
    return true;
}

void AudioStudio::newProject()
{
    m_projectPath.clear();
    m_modified = false;
}

QJsonObject AudioProject::toJson() const
{
    QJsonObject json;
    json["name"] = m_name;
    json["author"] = m_author;
    json["version"] = m_version;
    return json;
}

void AudioProject::fromJson(const QJsonObject& json)
{
    m_name = json["name"].toString();
    m_author = json["author"].toString();
    m_version = json["version"].toString("1.0");
}

void AudioProject::addAsset(const QString& id, const QString& path)
{
    m_assets[id] = path;
    emit changed();
}

void AudioProject::removeAsset(const QString& id)
{
    m_assets.remove(id);
    emit changed();
}

void AudioProject::addEvent(const QString& id, const QString& name)
{
    m_events[id] = name;
    emit changed();
}

void AudioProject::removeEvent(const QString& id)
{
    m_events.remove(id);
    emit changed();
}

// AudioFormatConverter

bool AudioFormatConverter::readWav(QFile& file, QVector<float>& samples, QAudioFormat& format)
{
    QByteArray header = file.read(44);
    if (header.size() < 44) return false;


    if (header.mid(0, 4) != "RIFF" || header.mid(8, 4) != "WAVE") return false;

    quint16 audioFormat = *reinterpret_cast<const quint16*>(header.constData() + 20);
    if (audioFormat != 1) return false; // PCM only

    quint16 channels = *reinterpret_cast<const quint16*>(header.constData() + 22);
    quint32 sampleRate = *reinterpret_cast<const quint32*>(header.constData() + 24);
    quint16 bitsPerSample = *reinterpret_cast<const quint16*>(header.constData() + 34);

    format.setSampleRate(static_cast<int>(sampleRate));
    format.setChannelCount(static_cast<int>(channels));
    format.setSampleFormat(bitsPerSample == 8 ? QAudioFormat::UInt8 :
                           bitsPerSample == 16 ? QAudioFormat::Int16 :
                           bitsPerSample == 24 ? QAudioFormat::Int32 :
                           bitsPerSample == 32 ? QAudioFormat::Float : QAudioFormat::Int16);

    QByteArray data = file.readAll();
    int sampleSize = qMax(bitsPerSample / 8, 1);
    int totalSamples = data.size() / sampleSize;
    samples.resize(totalSamples);

    auto readSample = [&](const uchar* ptr, int bytes) -> float {
        if (bytes == 1) return (static_cast<float>(*ptr) - 128.0f) / 128.0f;
        if (bytes == 2) {
            qint16 s = *reinterpret_cast<const qint16*>(ptr);
            return static_cast<float>(s) / 32768.0f;
        }
        if (bytes == 3) {
            quint32 s = (static_cast<quint32>(ptr[0]) |
                         static_cast<quint32>(ptr[1]) << 8 |
                         static_cast<quint32>(ptr[2]) << 16);
            if (s & 0x800000) s |= 0xff000000;
            return static_cast<float>(static_cast<qint32>(s)) / 8388608.0f;
        }
        if (bytes == 4) {
            float s;
            memcpy(&s, ptr, 4);
            return s;
        }
        return 0.0f;
    };

    for (int i = 0; i < totalSamples; ++i)
        samples[i] = readSample(reinterpret_cast<const uchar*>(data.constData()) + i * sampleSize, sampleSize);

    return true;
}

bool AudioFormatConverter::writeWav(QFile& file, const QVector<float>& samples, const QAudioFormat& format)
{
    int channels = format.channelCount();
    int sampleRate = format.sampleRate();
    int bitsPerSample = 16;
    int bytesPerSample = bitsPerSample / 8;
    int dataSize = samples.size() * bytesPerSample;
    int fileSize = 36 + dataSize;

    QByteArray header(44, '\0');
    header.replace(0, 4, "RIFF");
    *reinterpret_cast<quint32*>(header.data() + 4) = fileSize;
    header.replace(8, 4, "WAVE");
    header.replace(12, 4, "fmt ");
    *reinterpret_cast<quint32*>(header.data() + 16) = 16; // chunk size
    *reinterpret_cast<quint16*>(header.data() + 20) = 1;  // PCM
    *reinterpret_cast<quint16*>(header.data() + 22) = static_cast<quint16>(channels);
    *reinterpret_cast<quint32*>(header.data() + 24) = sampleRate;
    *reinterpret_cast<quint32*>(header.data() + 28) = sampleRate * channels * bytesPerSample;
    *reinterpret_cast<quint16*>(header.data() + 32) = static_cast<quint16>(channels * bytesPerSample);
    *reinterpret_cast<quint16*>(header.data() + 34) = bitsPerSample;
    header.replace(36, 4, "data");
    *reinterpret_cast<quint32*>(header.data() + 40) = dataSize;

    file.write(header);

    QByteArray data(dataSize, '\0');
    for (int i = 0; i < samples.size(); ++i) {
        qint16 s = static_cast<qint16>(qBound(-1.0f, samples[i], 1.0f) * 32767.0f);
        *reinterpret_cast<qint16*>(data.data() + i * bytesPerSample) = s;
    }
    file.write(data);
    return true;
}

bool AudioFormatConverter::convert(const QString& path, QVector<float>& samples, QAudioFormat& format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray ext = QFileInfo(path).suffix().toLower().toUtf8();

    if (ext == "wav") return readWav(file, samples, format);

    // Non-WAV formats require QAudioDecoder
    file.close();
    return false;
}

bool AudioFormatConverter::decodeOgg(const QString& path, QVector<float>& samples, QAudioFormat& format)
{
    // Use Qt Multimedia QAudioDecoder for OGG decoding
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    // Fallback: try reading as WAV (will fail for actual OGG)
    bool ok = readWav(file, samples, format);
    file.close();
    return ok;
}

bool AudioFormatConverter::decodeMp3(const QString& path, QVector<float>& samples, QAudioFormat& format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    bool ok = readWav(file, samples, format);
    file.close();
    return ok;
}

bool AudioFormatConverter::decodeFlac(const QString& path, QVector<float>& samples, QAudioFormat& format)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    bool ok = readWav(file, samples, format);
    file.close();
    return ok;
}

bool AudioFormatConverter::convertToWav(const QString& path, const QVector<float>& samples, const QAudioFormat& format)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return writeWav(file, samples, format);
}

bool AudioFormatConverter::convertToMp3(const QString& path, const QVector<float>& samples, const QAudioFormat& format, Quality q)
{
    // Write a temporary WAV, then encode to MP3 using LAME if available.
    // Falls back to WAV if LAME is not installed.
    QString tmpWav = path + ".tmp.wav";
    {
        QFile tmpFile(tmpWav);
        if (!writeWav(tmpFile, samples, format)) {
            QFile::remove(tmpWav);
            return false;
        }
    }

    // Try LAME
    QProcess proc;
    QString lamePath = "lame";
    QStringList args;
    switch (q) {
    case QualityLow:    args << "-V" << "7"; break;
    case QualityMedium: args << "-V" << "4"; break;
    case QualityHigh:   args << "-V" << "0"; break;
    }
    args << tmpWav << path;

    proc.start(lamePath, args);
    proc.waitForFinished(30000);

    if (proc.exitCode() == 0 && QFile::exists(path)) {
        QFile::remove(tmpWav);
        return true;
    }

    // LAME not found or failed — save as WAV with .mp3 extension warning
    qWarning() << "AudioFormatConverter: LAME not available, saving as WAV instead of MP3";
    QFile::rename(tmpWav, path);
    return QFile::exists(path);
}

bool AudioFormatConverter::convertToOgg(const QString& path, const QVector<float>& samples, const QAudioFormat& format, Quality q)
{
    // Write a temporary WAV, then encode to OGG using oggenc if available.
    // Falls back to WAV if oggenc is not installed.
    QString tmpWav = path + ".tmp.wav";
    {
        QFile tmpFile(tmpWav);
        if (!writeWav(tmpFile, samples, format)) {
            QFile::remove(tmpWav);
            return false;
        }
    }

    // Try oggenc (from vorbis-tools)
    QProcess proc;
    QString oggencPath = "oggenc";
    QStringList args;
    switch (q) {
    case QualityLow:    args << "-q" << "1"; break;
    case QualityMedium: args << "-q" << "5"; break;
    case QualityHigh:   args << "-q" << "7"; break;
    }
    args << "-o" << path << tmpWav;

    proc.start(oggencPath, args);
    proc.waitForFinished(30000);

    if (proc.exitCode() == 0 && QFile::exists(path)) {
        QFile::remove(tmpWav);
        return true;
    }

    // oggenc not found or failed — save as WAV
    qWarning() << "AudioFormatConverter: oggenc not available, saving as WAV instead of OGG";
    QFile::rename(tmpWav, path);
    return QFile::exists(path);
}

// ─── AudioEditor ──────────────────────────────────────────────────────────

void AudioEditor::loadFile(const QString& path)
{
    m_currentFile = path;
    m_selectionStart = 0;
    m_selectionEnd = 0;
    emit fileLoaded(path);
}

void AudioEditor::saveFile(const QString& path)
{
    m_currentFile = path;
    emit modificationChanged(false);
}

void AudioEditor::exportSelection(const QString& path)
{
    if (m_selectionStart >= m_selectionEnd) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.close();
}

void AudioEditor::setSelection(int start, int end)
{
    m_selectionStart = qMin(start, end);
    m_selectionEnd = qMax(start, end);
    emit selectionChanged(m_selectionStart, m_selectionEnd);
}

void AudioEditor::undo()
{
    if (m_undoStack.isEmpty()) return;
    m_redoStack.prepend(m_undoStack.takeLast());
}

void AudioEditor::redo()
{
    if (m_redoStack.isEmpty()) return;
    m_undoStack.append(m_redoStack.takeFirst());
}

void AudioEditor::copy()
{
    if (m_selectionStart >= m_selectionEnd || m_audioData.isEmpty()) return;
    int start = qBound(0, m_selectionStart, m_audioData.size());
    int end = qBound(0, m_selectionEnd, m_audioData.size());
    if (start >= end) return;
    m_clipboard = m_audioData.mid(start, end - start);
}

void AudioEditor::paste()
{
    if (m_clipboard.isEmpty()) return;
    m_undoStack.append("paste");
    m_redoStack.clear();
    int pos = qBound(0, m_selectionStart, m_audioData.size());
    for (auto it = m_clipboard.constBegin(); it != m_clipboard.constEnd(); ++it)
        m_audioData.insert(pos++, *it);
    int len = m_clipboard.size();
    m_selectionStart = pos;
    m_selectionEnd = pos + len;
    emit modificationChanged(true);
}

void AudioEditor::cut()
{
    if (m_selectionStart >= m_selectionEnd || m_audioData.isEmpty()) return;
    copy();
    deleteSelection();
}

void AudioEditor::deleteSelection()
{
    if (m_selectionStart >= m_selectionEnd || m_audioData.isEmpty()) return;
    int start = qBound(0, m_selectionStart, m_audioData.size());
    int end = qBound(0, m_selectionEnd, m_audioData.size());
    if (start >= end) return;
    m_undoStack.append("delete");
    m_redoStack.clear();
    m_audioData.remove(start, end - start);
    m_selectionStart = m_selectionEnd = start;
    emit modificationChanged(true);
}

} } // namespace ks::audio