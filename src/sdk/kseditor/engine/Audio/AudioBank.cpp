#include "AudioBank.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QDataStream>

namespace ks { namespace audio {

// ============================================================================
// AudioBank
// ============================================================================

bool AudioBank::generate(const QStringList& inputFiles)
{
    if (inputFiles.isEmpty()) {
        emit error("No input files provided");
        emit finished(false);
        return false;
    }

    QDir dir(m_config.outputDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit error("Cannot create output directory: " + m_config.outputDir);
            emit finished(false);
            return false;
        }
    }

    emit status("Generating audio bank...");
    emit progress(0);

    QJsonObject bankMeta;
    bankMeta["format"] = (m_config.format == FormatWAV) ? "wav" : (m_config.format == FormatOGG) ? "ogg" : "ksa";
    bankMeta["sampleRate"] = m_config.sampleRate;
    bankMeta["channels"] = m_config.channels;
    bankMeta["encrypted"] = m_config.encrypt;
    bankMeta["eventCount"] = inputFiles.size();

    QJsonArray events;
    for (int i = 0; i < inputFiles.size(); ++i) {
        QFileInfo fi(inputFiles[i]);
        if (!fi.exists()) {
            emit error("Input file not found: " + inputFiles[i]);
            continue;
        }

        QJsonObject evt;
        evt["id"] = QString("evt_%1").arg(i);
        evt["name"] = fi.baseName();
        evt["source"] = fi.absoluteFilePath();
        evt["size"] = fi.size();
        events.append(evt);

        emit progress(static_cast<int>((i + 1) * 100.0 / inputFiles.size()));
    }

    bankMeta["events"] = events;

    QString bankPath = dir.filePath("audio_bank.json");
    QFile bankFile(bankPath);
    if (bankFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(bankMeta);
        bankFile.write(doc.toJson(QJsonDocument::Indented));
        bankFile.close();
    }

    if (m_config.encrypt && !m_config.encryptionKey.isEmpty()) {
        QFile dataFile(bankPath);
        if (dataFile.open(QIODevice::ReadOnly)) {
            QByteArray data = dataFile.readAll();
            dataFile.close();
            QByteArray hash = QCryptographicHash::hash(m_config.encryptionKey.toUtf8(), QCryptographicHash::Sha256);
            QByteArray encrypted;
            for (int i = 0; i < data.size(); ++i) {
                encrypted.append(data[i] ^ hash[i % hash.size()]);
            }
            if (dataFile.open(QIODevice::WriteOnly)) {
                dataFile.write(encrypted);
                dataFile.close();
            }
        }
    }

    emit status("Audio bank generated successfully");
    emit finished(true);
    return true;
}

bool AudioBank::buildBanks()
{
    return generate(QStringList());
}

// ============================================================================
// AudioBankEx
// ============================================================================

bool AudioBankEx::loadBank(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    file.close();

    if (m_encrypted && !m_encryptionKey.isEmpty()) {
        QByteArray hash = QCryptographicHash::hash(m_encryptionKey.toUtf8(), QCryptographicHash::Sha256);
        QByteArray decrypted;
        for (int i = 0; i < data.size(); ++i) {
            decrypted.append(data[i] ^ hash[i % hash.size()]);
        }
        data = decrypted;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return false;

    QJsonObject root = doc.object();
    QJsonArray eventsArr = root["events"].toArray();

    m_events.clear();
    for (const QJsonValue& val : eventsArr) {
        QJsonObject evt = val.toObject();
        BankEvent be;
        be.id = evt["id"].toString();
        be.name = evt["name"].toString();
        be.path = evt["source"].toString();
        QJsonObject params = evt["parameters"].toObject();
        for (auto it = params.begin(); it != params.end(); ++it) {
            be.parameters[it.key()] = static_cast<float>(it.value().toDouble());
        }
        m_events.append(be);
    }

    BankInfo info;
    info.name = QFileInfo(path).baseName();
    info.path = path;
    info.eventCount = m_events.size();
    info.encrypted = m_encrypted;
    m_banks.append(info);

    emit bankLoaded(info.name);
    return true;
}

bool AudioBankEx::saveBank(const QString& path)
{
    QJsonObject root;
    QJsonArray eventsArr;
    for (const auto& evt : m_events) {
        QJsonObject obj;
        obj["id"] = evt.id;
        obj["name"] = evt.name;
        obj["source"] = evt.path;
        QJsonObject params;
        for (auto it = evt.parameters.begin(); it != evt.parameters.end(); ++it) {
            params[it.key()] = it.value();
        }
        obj["parameters"] = params;
        eventsArr.append(obj);
    }
    root["events"] = eventsArr;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    emit bankSaved(QFileInfo(path).baseName());
    return true;
}

void AudioBankEx::addEvent(const BankEvent& event)
{
    m_events.append(event);
}

void AudioBankEx::removeEvent(const QString& eventId)
{
    for (int i = 0; i < m_events.size(); ++i) {
        if (m_events[i].id == eventId) {
            m_events.removeAt(i);
            return;
        }
    }
}

// ============================================================================
// FSBExtractor
// ============================================================================

bool FSBExtractor::isValidFSB(const QString& fsbPath)
{
    QFile file(fsbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray header = file.read(4);
    file.close();

    return header.startsWith("FSB");
}

bool FSBExtractor::extractFile(const QString& fsbPath, const QString& outputDir)
{
    QFile file(fsbPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    m_fsbData = file.readAll();
    file.close();

    if (!parseHeader(m_fsbData)) {
        emit error("Invalid FSB header");
        emit extractionComplete(false);
        return false;
    }

    QDir dir(outputDir);
    if (!dir.exists()) dir.mkpath(".");

    emit status("Extracting samples...");
    emit progress(0);

    bool success = extractSamples(outputDir);

    emit extractionComplete(success);
    return success;
}

bool FSBExtractor::parseHeader(const QByteArray& data)
{
    if (data.size() < 32) return false;

    const char* ptr = data.constData();
    if (ptr[0] != 'F' || ptr[1] != 'S' || ptr[2] != 'B') return false;

    quint32 version = static_cast<quint32>(static_cast<unsigned char>(ptr[3]));
    quint32 numSamples = *reinterpret_cast<const quint32*>(ptr + 4);
    quint32 sampleHeadersOffset = *reinterpret_cast<const quint32*>(ptr + 8);
    quint32 dataOffset = *reinterpret_cast<const quint32*>(ptr + 12);

    m_samples.clear();
    for (quint32 i = 0; i < numSamples; ++i) {
        size_t offset = sampleHeadersOffset + i * 32;
        if (offset + 32 > static_cast<size_t>(data.size())) break;

        FSBSample sample;
        sample.name = QString("sample_%1").arg(i);
        sample.offset = *reinterpret_cast<const quint32*>(data.constData() + offset + 0);
        sample.size = *reinterpret_cast<const quint32*>(data.constData() + offset + 4);
        sample.sampleRate = *reinterpret_cast<const quint32*>(data.constData() + offset + 8);
        sample.channels = *reinterpret_cast<const quint16*>(data.constData() + offset + 12);

        quint16 formatId = *reinterpret_cast<const quint16*>(data.constData() + offset + 14);
        sample.format = (formatId == 0) ? "PCM16" : (formatId == 1) ? "PCM8" : (formatId == 2) ? "ADPCM" : "UNKNOWN";

        m_samples.append(sample);
    }

    return true;
}

bool FSBExtractor::extractSamples(const QString& outputDir)
{
    for (int i = 0; i < m_samples.size(); ++i) {
        const auto& sample = m_samples[i];
        if (sample.offset + sample.size > static_cast<quint32>(m_fsbData.size())) continue;

        QString outPath = QDir(outputDir).filePath(sample.name + ".wav");
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly)) continue;

        quint32 dataSize = sample.size;
        quint16 numChannels = sample.channels;
        quint32 sampleRate = sample.sampleRate;
        quint16 bitsPerSample = 16;
        quint32 byteRate = sampleRate * numChannels * bitsPerSample / 8;
        quint16 blockAlign = numChannels * bitsPerSample / 8;

        QDataStream out(&outFile);
        out.setByteOrder(QDataStream::LittleEndian);

        out.writeRawData("RIFF", 4);
        quint32 fileSize = 36 + dataSize;
        out << static_cast<quint32>(fileSize);
        out.writeRawData("WAVE", 4);

        out.writeRawData("fmt ", 4);
        out << static_cast<quint32>(16);
        out << static_cast<quint16>(1);
        out << numChannels;
        out << sampleRate;
        out << byteRate;
        out << blockAlign;
        out << bitsPerSample;

        out.writeRawData("data", 4);
        out << dataSize;

        out.writeRawData(m_fsbData.constData() + sample.offset, dataSize);

        outFile.close();

        emit progress(static_cast<int>((i + 1) * 100.0 / m_samples.size()));
    }

    return true;
}

// ============================================================================
// AudioExporter
// ============================================================================

bool AudioExporter::exportFile(const QString& inputPath, const QString& outputPath)
{
    QFile inFile(inputPath);
    if (!inFile.open(QIODevice::ReadOnly)) return false;

    QByteArray data = inFile.readAll();
    inFile.close();

    emit exportProgress(0);

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) return false;

    QDataStream out(&outFile);
    out.setByteOrder(QDataStream::LittleEndian);

    switch (m_format) {
        case WAV: {
            out.writeRawData("RIFF", 4);
            out << static_cast<quint32>(data.size() + 36);
            out.writeRawData("WAVE", 4);
            out.writeRawData("fmt ", 4);
            out << static_cast<quint32>(16);
            out << static_cast<quint16>(1);
            out << static_cast<quint16>(1);
            out << static_cast<quint32>(44100);
            out << static_cast<quint32>(88200);
            out << static_cast<quint16>(2);
            out << static_cast<quint16>(16);
            out.writeRawData("data", 4);
            out << static_cast<quint32>(data.size());
            out.writeRawData(data.constData(), data.size());
            break;
        }
        case OGG:
        case MP3:
        case FLAC:
        case AC3: {
            out.writeRawData(data.constData(), data.size());
            break;
        }
    }

    outFile.close();
    emit exportProgress(100);
    emit exportComplete(true);
    return true;
}

bool AudioExporter::exportBuffer(const QVector<float>& audio, const QString& outputPath, int sampleRate)
{
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) return false;

    QDataStream out(&outFile);
    out.setByteOrder(QDataStream::LittleEndian);

    quint32 dataSize = audio.size() * 2;
    quint32 fileSize = dataSize + 36;

    out.writeRawData("RIFF", 4);
    out << fileSize;
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << static_cast<quint32>(16);
    out << static_cast<quint16>(1);
    out << static_cast<quint16>(1);
    out << static_cast<quint32>(sampleRate);
    out << static_cast<quint32>(sampleRate * 2);
    out << static_cast<quint16>(2);
    out << static_cast<quint16>(16);
    out.writeRawData("data", 4);
    out << dataSize;

    for (float sample : audio) {
        qint16 val = static_cast<qint16>(qBound(-1.0f, sample, 1.0f) * 32767.0f);
        out << val;
    }

    outFile.close();
    emit exportProgress(100);
    emit exportComplete(true);
    return true;
}

} } // namespace ks::audio
