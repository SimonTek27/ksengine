#include "BankParserFmod1x.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QFileInfo>
#include <QtEndian>
#define STB_VORBIS_HEADER_ONLY
#include "../../../external/stb/stb_vorbis.c"

namespace ks { namespace fileformat {

static constexpr quint32 FMOD_BANK_MAGIC_FEV2 = 0x46455632;
static constexpr quint32 FMOD_BANK_MAGIC_RIFF = 0x52494646;
static constexpr quint32 FMOD_FSB5_MAGIC      = 0x46534235;
static constexpr quint32 FMOD_DECRYPT_KEY = 0xAE12B3F4;

ParsedBankData BankParserFmod1x::parse(const QString& bankPath) {
    if (m_cache.contains(bankPath))
        return m_cache[bankPath];

    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly)) {
        ParsedBankData result;
        emit parseError(bankPath, "Cannot open file: " + bankPath);
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    ParsedBankData result = parseFromData(data);
    result.filePath = bankPath;
    result.name = QFileInfo(bankPath).completeBaseName();
    result.detectedVersion = BankVersion::FMOD_1_08;
    result.detectedGame = GameTarget::AssettoCorsa1;

    if (result.isValid) {
        m_cache[bankPath] = result;
        emit bankParsed(bankPath, result);
    }
    return result;
}

ParsedBankData BankParserFmod1x::parseFromData(const QByteArray& rawData) {
    ParsedBankData result;
    result.size = rawData.size();

    if (rawData.size() < 8) {
        emit parseError("", "Data too small for bank header");
        return result;
    }

    quint32 magic = 0;
    memcpy(&magic, rawData.constData(), 4);

    if (magic == FMOD_BANK_MAGIC_FEV2) {
        if (rawData.size() >= 12) {
            quint32 flags = 0;
            memcpy(&flags, rawData.constData() + 8, 4);
            result.isEncrypted = (flags & 0x1) != 0;
        }

        QByteArray workData = rawData;
        if (result.isEncrypted) {
            workData = decryptData(rawData, FMOD_DECRYPT_KEY);
        }

        result.isValid = parseFEV2(workData, result);

        if (result.isValid) {
            QByteArray audioData = result.isEncrypted ? workData : rawData;
            parseFSB5Data(audioData, result);
        }
    } else if (magic == FMOD_BANK_MAGIC_RIFF) {
        result.isValid = true;
        result.isLegacy = true;
        result.version = 0;
        qWarning() << "KSBankParser: Legacy RIFF/FEV bank — limited parsing";
    } else {
        emit parseError("", QString("Unknown bank magic 0x%1").arg(magic, 8, 16, QChar('0')));
    }
    return result;
}

bool BankParserFmod1x::canParse(const QByteArray& data) const {
    if (data.size() < 4) return false;
    quint32 magic = 0;
    memcpy(&magic, data.constData(), 4);
    return magic == FMOD_BANK_MAGIC_FEV2 || magic == FMOD_BANK_MAGIC_RIFF;
}

bool BankParserFmod1x::isValidBank(const QString& bankPath) const {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 8) return false;
    quint32 magic = 0;
    file.read(reinterpret_cast<char*>(&magic), 4);
    return magic == FMOD_BANK_MAGIC_FEV2 || magic == FMOD_BANK_MAGIC_RIFF;
}

bool BankParserFmod1x::isEncrypted(const QString& bankPath) const {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 12) return false;
    file.seek(8);
    quint32 flags = 0;
    file.read(reinterpret_cast<char*>(&flags), 4);
    return (flags & 0x1) != 0;
}

bool BankParserFmod1x::extractAudioData(ParsedBankData& bankData) const {
    // Already done during parsing for FMOD 1.x
    return !bankData.sounds.isEmpty();
}

QStringList BankParserFmod1x::getEventPaths(const QString& bankPath) const {
    const_cast<BankParserFmod1x*>(this)->parse(bankPath);
    QStringList paths;
    if (m_cache.contains(bankPath))
        for (const auto& e : m_cache[bankPath].events) paths << e.path;
    return paths;
}

QStringList BankParserFmod1x::getEventNames(const QString& bankPath) const {
    const_cast<BankParserFmod1x*>(this)->parse(bankPath);
    QStringList names;
    if (m_cache.contains(bankPath))
        for (const auto& e : m_cache[bankPath].events) names << e.name;
    return names;
}

QStringList BankParserFmod1x::getBusPaths(const QString& bankPath) const {
    const_cast<BankParserFmod1x*>(this)->parse(bankPath);
    QStringList paths;
    if (m_cache.contains(bankPath))
        for (const auto& b : m_cache[bankPath].buses) paths << b.path;
    return paths;
}

QStringList BankParserFmod1x::getVCAPaths(const QString& bankPath) const {
    const_cast<BankParserFmod1x*>(this)->parse(bankPath);
    QStringList paths;
    if (m_cache.contains(bankPath))
        for (const auto& v : m_cache[bankPath].vcas) paths << v.path;
    return paths;
}

// ============================================================================
// FEV2 Parser Implementation (from original BankParser.cpp)
// ============================================================================

bool BankParserFmod1x::parseFEV2(const QByteArray& data, ParsedBankData& out) {
    QDataStream s(data);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);

    quint32 magic; s >> magic;
    quint32 version; s >> version;
    out.version = version;

    quint32 flags; s >> flags;
    out.isEncrypted = (flags & 0x1) != 0;

    while (!s.atEnd()) {
        if (s.device()->pos() + 8 > data.size()) break;

        quint32 chunkTag; s >> chunkTag;
        quint32 chunkSize; s >> chunkSize;

        qint64 chunkStart = s.device()->pos();
        qint64 chunkEnd = chunkStart + chunkSize;
        if (chunkEnd > data.size()) break;

        switch (chunkTag) {
        case 0x53545254: readChunkStringTable(s, out, chunkSize); break;  // STRT
        case 0x45565453: readChunkEvents(s, out, chunkSize); break;       // EVTS
        case 0x42555353: readChunkBuses(s, out, chunkSize); break;        // BUSS
        case 0x56434153: readChunkVCAs(s, out, chunkSize); break;         // VCAS
        case 0x534E4150: readChunkSnapshots(s, out, chunkSize); break;    // SNAP
        case 0x534E4453: readChunkSounds(s, out, chunkSize); break;       // SNDS
        default: break;
        }
        s.device()->seek(chunkEnd);
    }
    return true;
}

void BankParserFmod1x::readChunkStringTable(QDataStream& s, ParsedBankData& out, quint32 size) {
    Q_UNUSED(out);
    quint32 count; s >> count;
    m_stringTable.clear();
    m_stringTable.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        quint32 len; s >> len;
        QByteArray bytes(len, '\0');
        s.readRawData(bytes.data(), len);
        m_stringTable.append(QString::fromUtf8(bytes));
    }
}

void BankParserFmod1x::readChunkEvents(QDataStream& s, ParsedBankData& out, quint32 /*size*/) {
    quint32 count; s >> count;
    for (quint32 i = 0; i < count; ++i) {
        BankEventInfo ev;
        quint8 guidBytes[16]; s.readRawData(reinterpret_cast<char*>(guidBytes), 16);
        ev.guid = formatGUID(guidBytes);

        quint32 nameIdx, pathIdx;
        s >> nameIdx >> pathIdx;
        ev.name = stringAt(nameIdx);
        ev.path = stringAt(pathIdx);
        if (!ev.path.startsWith("event:/"))
            ev.path = "event:/" + ev.path;

        s >> ev.flags >> ev.category >> ev.maxInstances >> ev.length;

        quint32 paramCount; s >> paramCount;
        ev.parameters = (int)paramCount;
        for (quint32 p = 0; p < paramCount; ++p) {
            quint32 pNameIdx; s >> pNameIdx;
            float pDefault; s >> pDefault;
            ev.parameterNames.append(stringAt(pNameIdx));
            ev.parameterDefaults.append(pDefault);
        }
        out.events.append(ev);
    }
}

void BankParserFmod1x::readChunkBuses(QDataStream& s, ParsedBankData& out, quint32 /*size*/) {
    quint32 count; s >> count;
    for (quint32 i = 0; i < count; ++i) {
        BankBusInfo bus;
        quint32 nameIdx, pathIdx; s >> nameIdx >> pathIdx;
        bus.name = stringAt(nameIdx);
        bus.path = stringAt(pathIdx);
        if (!bus.path.startsWith("bus:/"))
            bus.path = "bus:/" + bus.path;
        s >> bus.volume;
        quint8 flags; s >> flags;
        bus.muted = (flags & 0x01) != 0;
        bus.solo = (flags & 0x02) != 0;
        quint32 childCount; s >> childCount;
        for (quint32 c = 0; c < childCount; ++c) {
            quint32 idx; s >> idx;
            bus.childBuses.append(stringAt(idx));
        }
        quint32 vcaCount; s >> vcaCount;
        for (quint32 v = 0; v < vcaCount; ++v) {
            quint32 idx; s >> idx;
            bus.linkedVCAs.append(stringAt(idx));
        }
        out.buses.append(bus);
    }
}

void BankParserFmod1x::readChunkVCAs(QDataStream& s, ParsedBankData& out, quint32 /*size*/) {
    quint32 count; s >> count;
    for (quint32 i = 0; i < count; ++i) {
        BankVCAInfo vca;
        quint32 nameIdx, pathIdx; s >> nameIdx >> pathIdx;
        vca.name = stringAt(nameIdx);
        vca.path = stringAt(pathIdx);
        if (!vca.path.startsWith("vca:/"))
            vca.path = "vca:/" + vca.path;
        s >> vca.volume;
        out.vcas.append(vca);
    }
}

void BankParserFmod1x::readChunkSnapshots(QDataStream& s, ParsedBankData& out, quint32 /*size*/) {
    quint32 count; s >> count;
    for (quint32 i = 0; i < count; ++i) {
        BankSnapshotInfo snap;
        quint32 nameIdx, pathIdx; s >> nameIdx >> pathIdx;
        snap.name = stringAt(nameIdx);
        snap.path = stringAt(pathIdx);
        if (!snap.path.startsWith("snapshot:/"))
            snap.path = "snapshot:/" + snap.path;
        out.snapshots.append(snap);
    }
}

void BankParserFmod1x::readChunkSounds(QDataStream& s, ParsedBankData& out, quint32 /*size*/) {
    quint32 count; s >> count;
    for (quint32 i = 0; i < count; ++i) {
        BankSoundInfo snd;
        quint32 nameIdx; s >> nameIdx;
        snd.name = stringAt(nameIdx);
        s >> snd.sampleRate >> snd.channels >> snd.length >> snd.format;
        out.sounds.append(snd);
    }
}

// ============================================================================
// FSB5 Parsing (from original BankParser.cpp)
// ============================================================================

bool BankParserFmod1x::parseFSB5Data(const QByteArray& bankData, ParsedBankData& out) const {
    for (int i = 0; i < bankData.size() - 4; ++i) {
        quint32 magic = 0;
        memcpy(&magic, bankData.constData() + i, 4);
        if (magic == FMOD_FSB5_MAGIC) {
            QByteArray fsbChunk = bankData.mid(i);

            struct FSB5Header {
                quint32 magic, version, numSamples, sampleHeaderSize, totalDataSize, reserved;
            };
            struct FSB5SampleHeader {
                QString name;
                quint32 sampleRate, channels, format, length, dataOffset, dataSize;
                quint32 loopStart, loopEnd, mode;
            };

            FSB5Header hdr;
            QVector<FSB5SampleHeader> headers;

            QDataStream s(fsbChunk);
            s.setByteOrder(QDataStream::LittleEndian);
            s >> hdr.magic >> hdr.version >> hdr.numSamples >> hdr.sampleHeaderSize
              >> hdr.totalDataSize >> hdr.reserved;

            if (hdr.magic != FMOD_FSB5_MAGIC || hdr.numSamples == 0) continue;

            quint32 headerStart = (hdr.version >= 1) ? 30 : 24;
            if (hdr.version >= 1) s.skipRawData(6);
            s.device()->seek(headerStart);

            for (quint32 i = 0; i < hdr.numSamples; ++i) {
                FSB5SampleHeader sh;
                QByteArray nameBytes;
                char c;
                do {
                    if (s.atEnd()) break;
                    s.readRawData(&c, 1);
                    if (c != '\0') nameBytes.append(c);
                } while (c != '\0');
                sh.name = QString::fromUtf8(nameBytes);

                qint64 nameEnd = s.device()->pos();
                qint64 nameStart = nameEnd - nameBytes.size() - 1;
                qint64 padding = (4 - (nameEnd - headerStart) % 4) % 4;
                s.skipRawData(static_cast<int>(padding));

                s >> sh.sampleRate >> sh.channels >> sh.format >> sh.length
                  >> sh.dataOffset >> sh.dataSize >> sh.loopStart >> sh.loopEnd >> sh.mode;
                s.skipRawData(4);
                headers.append(sh);
            }

            if (hdr.numSamples == 0 || headers.isEmpty()) return true;

            QVector<QVector<float>> decodedSamples;
            quint32 totalHeaderSize = headerStart;
            for (const auto& sh : headers) {
                quint32 nameLen = (sh.name.toUtf8().size() + 1 + 3) & ~3;
                totalHeaderSize += nameLen + 44;
            }

            decodedSamples.resize(headers.size());
            for (int i = 0; i < headers.size(); ++i) {
                const auto& sh = headers[i];
                quint32 dataStart = totalHeaderSize + sh.dataOffset;
                quint32 dataSize = sh.dataSize;
                if (dataStart + dataSize > (quint32)fsbChunk.size()) continue;

                QVector<float> samples;
                switch (sh.format) {
                case 1: // PCM16
                    samples.resize(dataSize / 2);
                    {
                        const qint16* pcm = reinterpret_cast<const qint16*>(fsbChunk.constData() + dataStart);
                        for (int j = 0; j < samples.size(); ++j)
                            samples[j] = qBound(-1.0f, pcm[j] / 32768.0f, 1.0f);
                    }
                    break;
                case 0: // PCM8
                    samples.resize(dataSize);
                    {
                        const quint8* pcm = reinterpret_cast<const quint8*>(fsbChunk.constData() + dataStart);
                        for (quint32 j = 0; j < dataSize; ++j)
                            samples[j] = qBound(-1.0f, (pcm[j] / 128.0f) - 1.0f, 1.0f);
                    }
                    break;
                case 6: // Vorbis
                    {
                        const unsigned char* vorbisData =
                            reinterpret_cast<const unsigned char*>(fsbChunk.constData() + dataStart);
                        int vorbisLen = static_cast<int>(dataSize);
                        int error = 0;
                        stb_vorbis* v = stb_vorbis_open_memory(vorbisData, vorbisLen, &error, nullptr);
                        if (v) {
                            stb_vorbis_info info = stb_vorbis_get_info(v);
                            unsigned int totalFrames = stb_vorbis_stream_length_in_samples(v);
                            int numChannels = info.channels;
                            int numFloats = static_cast<int>(totalFrames) * numChannels;
                            if (numFloats > 0) {
                                samples.resize(numFloats);
                                int decoded = stb_vorbis_get_samples_float_interleaved(v, numChannels,
                                    samples.data(), numFloats);
                                stb_vorbis_close(v);
                                if (decoded > 0 && decoded * numChannels < numFloats)
                                    samples.resize(decoded * numChannels);
                            } else {
                                stb_vorbis_close(v);
                            }
                        }
                    }
                    break;
                default:
                    samples.resize(sh.length * sh.channels, 0.0f);
                    break;
                }
                decodedSamples[i] = samples;
            }

            for (int si = 0; si < headers.size() && si < decodedSamples.size(); ++si) {
                for (auto& snd : out.sounds) {
                    if (snd.name == headers[si].name) {
                        snd.samples = decodedSamples[si];
                        snd.hasAudioData = true;
                        snd.channels = headers[si].channels;
                        snd.sampleRate = headers[si].sampleRate;
                        snd.length = headers[si].length;
                        snd.dataOffset = headers[si].dataOffset;
                        snd.dataSize = headers[si].dataSize;
                        break;
                    }
                }
            }
            return true;
        }
    }
    return false;
}

QString BankParserFmod1x::stringAt(quint32 idx) const {
    if (idx < (quint32)m_stringTable.size())
        return m_stringTable.at(idx);
    return QString("str_%1").arg(idx);
}

QString BankParserFmod1x::formatGUID(const quint8 bytes[16]) {
    return QString("{%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
        .arg(bytes[0],2,16,QChar('0')).arg(bytes[1],2,16,QChar('0'))
        .arg(bytes[2],2,16,QChar('0')).arg(bytes[3],2,16,QChar('0'))
        .arg(bytes[4],2,16,QChar('0')).arg(bytes[5],2,16,QChar('0'))
        .arg(bytes[6],2,16,QChar('0')).arg(bytes[7],2,16,QChar('0'))
        .arg(bytes[8],2,16,QChar('0')).arg(bytes[9],2,16,QChar('0'))
        .arg(bytes[10],2,16,QChar('0')).arg(bytes[11],2,16,QChar('0'))
        .arg(bytes[12],2,16,QChar('0')).arg(bytes[13],2,16,QChar('0'))
        .arg(bytes[14],2,16,QChar('0')).arg(bytes[15],2,16,QChar('0'));
}

QByteArray BankParserFmod1x::decryptData(const QByteArray& data, quint32 key) const {
    QByteArray decrypted = data;
    quint32* words = reinterpret_cast<quint32*>(decrypted.data());
    int wordCount = decrypted.size() / 4;
    quint32 prev = 0;
    for (int i = 0; i < wordCount; ++i) {
        quint32 cur = words[i];
        words[i] ^= key ^ prev;
        prev = cur;
    }
    return decrypted;
}

}} // namespace ks::fileformat