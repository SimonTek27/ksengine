#include "BankWriter.h"
#include "BankWriterInterface.h"
#include "BankWriterFactory.h"
#include "BankVersion.h"
#include "../Audio/AudioTypes.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QUuid>
#include <QCryptographicHash>

namespace ks { namespace fileformat {

KSBankWriter::KSBankWriter(QObject* parent)
    : QObject(parent) {}

// ============================================================================
// Public API
// ============================================================================

bool KSBankWriter::writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                                      const QString& assetsDir) {
    // Use default version/target if set, otherwise auto-detect (fallback to FMOD 1.08)
    IBankWriter* writer = getWriter(project);
    if (writer) {
        // Connect signals
        connect(writer, &IBankWriter::writeStarted, this, &KSBankWriter::writeStarted);
        connect(writer, &IBankWriter::writeProgress, this, &KSBankWriter::writeProgress);
        connect(writer, &IBankWriter::writeCompleted, this, &KSBankWriter::writeCompleted);
        connect(writer, &IBankWriter::writeFailed, this, &KSBankWriter::writeFailed);

        return writer->writeProjectBanks(project, outputDir, assetsDir);
    }

    // Fallback: original implementation (FMOD 1.08 style)
    QDir dir(outputDir);
    if (!dir.exists()) dir.mkpath(".");

    // Try to load the raw v2 document for full FSPRO detail
    QJsonObject raw = project.rawDocument();
    FSPROProject fspro;

    if (!raw.isEmpty() && raw["_schema"].toString() == "ksaudio") {
        // We have a v2 document — we can extract full FSPRO detail
        fspro.name = project.projectName();

        // Extract event groups from raw document
        for (const auto& gv : raw["eventGroups"].toArray()) {
            FSPROEventGroup group;
            QJsonObject go = gv.toObject();
            group.name = go["name"].toString();
            group.guid = go["guid"].toString();
            // Flatten: events at this level
            for (const auto& ev : go["events"].toArray()) {
                QJsonObject eo = ev.toObject();
                FSPROEvent e;
                e.guid   = eo["guid"].toString();
                e.name   = eo["name"].toString();
                e.typeStr = eo["type"].toString("2D");
                // Read spatialization
                QJsonObject sp = eo["spatialization"].toObject();
                e.spatializationEnabled = sp["enabled"].toBool(true);
                e.attenuationMin = static_cast<float>(sp["minDistance"].toDouble(1.0));
                e.attenuationMax = static_cast<float>(sp["maxDistance"].toDouble(100.0));
                // Parameters
                for (const auto& pv : eo["parameters"].toArray()) {
                    QJsonObject po = pv.toObject();
                    FSPROParameter p;
                    p.name = po["name"].toString();
                    p.guid = po["guid"].toString();
                    p.type = po["type"].toString("float");
                    p.minVal = static_cast<float>(po["min"].toDouble(0.0));
                    p.maxVal = static_cast<float>(po["max"].toDouble(1.0));
                    p.defaultVal = static_cast<float>(po["default"].toDouble(0.0));
                    e.localParameters.append(p);
                }
                group.events.append(e);
            }
            // Child groups
            for (const auto& cgv : go["childGroups"].toArray()) {
                QJsonObject cgo = cgv.toObject();
                FSPROEventGroup cg;
                cg.name = cgo["name"].toString();
                cg.guid = cgo["guid"].toString();
                // Flatten child group events too
                for (const auto& cev : cgo["events"].toArray()) {
                    QJsonObject ceo = cev.toObject();
                    FSPROEvent e;
                    e.guid   = ceo["guid"].toString();
                    e.name   = ceo["name"].toString();
                    e.typeStr = ceo["type"].toString("2D");
                    group.events.append(e);
                }
                group.childGroups.append(cg);
            }
            fspro.eventGroups.append(group);
        }

        // Extract buses from raw document
        for (const auto& bv : raw["buses"].toArray()) {
            QJsonObject bo = bv.toObject();
            FSPROBus bus;
            bus.guid   = bo["guid"].toString();
            bus.name   = bo["name"].toString();
            bus.volume = static_cast<float>(bo["volume"].toDouble(1.0));
            bus.mute   = bo["mute"].toBool(false);
            bus.solo   = bo["solo"].toBool(false);
            fspro.buses.append(bus);
        }

        // Extract VCAs
        for (const auto& vv : raw["vcas"].toArray()) {
            QJsonObject vo = vv.toObject();
            FSPROVCA vca;
            vca.guid   = vo["guid"].toString();
            vca.name   = vo["name"].toString();
            vca.volume = static_cast<float>(vo["volume"].toDouble(1.0));
            fspro.vcas.append(vca);
        }

        // Extract sounds from raw document
        for (const auto& sv : raw["sounds"].toArray()) {
            QJsonObject so = sv.toObject();
            FSPROSound snd;
            snd.guid       = so["guid"].toString();
            snd.name       = so["name"].toString();
            snd.filePath   = so["file"].toString();
            snd.format     = so["format"].toString("wav");
            snd.sampleRate = so["sampleRate"].toVariant().toUInt();
            snd.channels   = so["channels"].toVariant().toUInt();
            snd.loopStart  = so["loopStart"].toVariant().toLongLong();
            snd.loopEnd    = so["loopEnd"].toVariant().toLongLong();
            fspro.sounds.append(snd);
        }

        // Extract banks
        for (const auto& bv : raw["banks"].toArray()) {
            QJsonObject bo = bv.toObject();
            FSPROBank bank;
            bank.guid = bo["guid"].toString();
            bank.name = bo["name"].toString();
            for (const auto& egv : bo["eventGuids"].toArray())
                bank.eventGuids.append(egv.toString());
            fspro.banks.append(bank);
        }
    }

    // If no v2 document, build from project banks directly
    if (fspro.eventGroups.isEmpty() && fspro.banks.isEmpty()) {
        for (const auto& bank : project.banks()) {
            FSPROBank fb;
            fb.guid = QUuid::createUuid().toString();
            fb.name = bank.name;
            FSPROEventGroup group;
            group.guid = QUuid::createUuid().toString();
            group.name = bank.name;
            for (const auto& e : bank.events) {
                FSPROEvent fe;
                fe.guid = QUuid::createUuid().toString();
                fe.name = e.name;
                fe.typeStr = e.is3D ? "3D" : "2D";
                group.events.append(fe);
                fb.eventGuids.append(fe.guid);

                if (!e.audioFile.isEmpty()) {
                    FSPROSound snd;
                    snd.guid = QUuid::createUuid().toString();
                    snd.name = e.name + "_sound";
                    snd.filePath = e.audioFile;
                    snd.format = "wav";
                    fspro.sounds.append(snd);
                }
            }
            fspro.eventGroups.append(group);
            fspro.banks.append(fb);
        }
    }

    // --- Write each bank ---
    emit writeStarted(project.projectName());

    QStringList bankPaths;
    for (int i = 0; i < fspro.banks.size(); ++i) {
        const auto& bank = fspro.banks[i];

        // Build string table
        QMap<QString, quint32> strMap;

        auto regStr = [&](const QString& s) -> quint32 {
            if (strMap.contains(s)) return strMap[s];
            quint32 idx = static_cast<quint32>(strMap.size());
            strMap[s] = idx;
            return idx;
        };

        // Build event entries
        QVector<EventEntry> eventEntries;
        for (const auto& eg : fspro.eventGroups) {
            for (const auto& e : eg.events) {
                if (!bank.eventGuids.contains(e.guid) && !bank.eventGuids.isEmpty())
                    continue;

                QString fullPath = "event:/" + bank.name + "/" + e.name;
                regStr(e.name);
                regStr(fullPath);

                EventEntry ee;
                ee.guid = makeGUID(e.guid.isEmpty() ? e.name + bank.name : e.guid);
                ee.nameIdx = strMap[e.name];
                ee.pathIdx = strMap[fullPath];
                ee.flags = 0;
                ee.category = 0;
                ee.maxInstances = e.maxInstances;
                ee.length = 0;

                // Parameters
                for (const auto& p : e.localParameters) {
                    regStr(p.name);
                    ee.paramNameIdxs.append(strMap[p.name]);
                    ee.paramDefaults.append(p.defaultVal);
                }

                eventEntries.append(ee);
            }
        }

        // Register bus names + paths
        for (const auto& b : fspro.buses) {
            if (!strMap.contains(b.name)) {
                regStr(b.name);
                regStr("bus:/" + b.name);
            }
        }

        // Register VCA names + paths
        for (const auto& v : fspro.vcas) {
            if (!strMap.contains(v.name)) {
                regStr(v.name);
                regStr("vca:/" + v.name);
            }
        }

        // Register sound names and read audio data
        QVector<SoundEntry> soundEntries;
        QVector<QByteArray> audioBuffers;
        for (const auto& s : fspro.sounds) {
            regStr(s.name);
            SoundEntry se;
            se.nameIdx = strMap[s.name];
            se.sampleRate = s.sampleRate;
            se.channels   = s.channels;
            se.length     = 0;
            se.format     = 0; // PCM

            // Try to read audio data
            if (!assetsDir.isEmpty() && !s.filePath.isEmpty()) {
                QString fullPath = assetsDir + "/" + s.filePath;
                QByteArray pcm;
                quint32 sr, ch;
                if (readAudioData(fullPath, pcm, sr, ch)) {
                    se.length = pcm.size();
                    se.sampleRate = sr;
                    se.channels = ch;
                    audioBuffers.append(pcm);
                } else {
                    audioBuffers.append(QByteArray());
                }
            } else {
                audioBuffers.append(QByteArray());
            }

            soundEntries.append(se);
        }

        // Build binary string table
        QByteArray strTableData;
        QDataStream strStream(&strTableData, QIODevice::WriteOnly);
        strStream.setByteOrder(QDataStream::LittleEndian);
        strStream << static_cast<quint32>(strMap.size());
        // Write strings in index order
        QStringList sorted(strMap.keys());
        // But we need to write them by index, not alphabetically
        // Build reverse map: index → string
        QMap<quint32, QString> idxToString;
        for (auto it = strMap.constBegin(); it != strMap.constEnd(); ++it)
            idxToString[it.value()] = it.key();

        for (auto it = idxToString.constBegin(); it != idxToString.constEnd(); ++it) {
            QByteArray utf8 = it.value().toUtf8();
            strStream << static_cast<quint32>(utf8.size());
            strTableData.append(utf8);
        }

        // --- Write the .bank file ---
        QString bankPath = outputDir + "/" + bank.name + ".bank";
        QFile outFile(bankPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            m_lastError = "Cannot create bank file: " + bankPath;
            emit writeFailed(m_lastError);
            return false;
        }

        QDataStream s(&outFile);
        s.setByteOrder(QDataStream::LittleEndian);
        s.setFloatingPointPrecision(QDataStream::SinglePrecision);

        // 1. FEV2 Header
        writeFEV2Header(s, FEV2Header{});

        // 2. STRT Chunk
        writeSTRTChunk(s, strTableData);

        // 3. EVTS Chunk
        writeEVTSChunk(s, eventEntries);

        // 4. BUSS Chunk
        {
            QByteArray busData;
            QDataStream bs(&busData, QIODevice::WriteOnly);
            bs.setByteOrder(QDataStream::LittleEndian);
            bs.setFloatingPointPrecision(QDataStream::SinglePrecision);
            bs << static_cast<quint32>(fspro.buses.size());

            for (const auto& b : fspro.buses) {
                writeBUSSEntry(bs, b, strMap);
            }

            s << static_cast<quint32>(0x42555353); // "BUSS"
            s << static_cast<quint32>(busData.size());
            outFile.write(busData);
        }

        // 5. VCAS Chunk
        writeVCASChunk(s, fspro.vcas, strMap);

        // 6. SNDS Chunk
        writeSNDSChunk(s, soundEntries);

        // 7. Audio data — embed raw PCM data referenced by SNDS entries
        for (const auto& pcm : audioBuffers) {
            if (!pcm.isEmpty())
                outFile.write(pcm);
        }

        outFile.close();

        emit writeProgress(static_cast<int>((i + 1) * 90 / fspro.banks.size()));
        bankPaths.append(bankPath);
    }

    emit writeCompleted(outputDir);
    return true;
}

bool KSBankWriter::writeBank(const SoundBank& bank,
                              const QString& assetsDir,
                              const QString& outputPath) {
    // Build a minimal KSAudioProject and delegate
    KSAudioProject proj;
    proj.setProjectName(bank.name);
    // Create a raw JSON with the bank data
    QJsonObject raw;
    raw["_schema"] = "ksaudio";
    raw["_version"] = "2.0.0";
    raw["name"] = bank.name;
    QJsonArray banksArr;
    QJsonObject bo;
    bo["name"] = bank.name;
    bo["guid"] = bank.id.isEmpty() ? QUuid::createUuid().toString() : bank.id;
    QJsonArray eventsArr;
    for (const auto& e : bank.events) {
        QJsonObject eo;
        eo["guid"] = e.id.isEmpty() ? QUuid::createUuid().toString() : e.id;
        eo["name"] = e.name;
        eo["type"] = e.is3D ? "3D" : "2D";
        eo["audioFile"] = e.audioFile;
        eventsArr.append(eo);
    }
    bo["events"] = eventsArr;
    banksArr.append(bo);
    raw["banks"] = banksArr;
    proj.setRawDocument(raw);

    return writeProjectBanks(proj, QFileInfo(outputPath).absolutePath(), assetsDir);
}

bool KSBankWriter::writeGUIDsFile(const KSAudioProject& project, const QString& outputPath) {
    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly)) {
        m_lastError = "Cannot write GUIDs file: " + outputPath;
        return false;
    }

    QTextStream out(&f);
    QJsonObject raw = project.rawDocument();

    // Extract events from raw document
    for (const auto& gv : raw["eventGroups"].toArray()) {
        QJsonObject go = gv.toObject();
        QString groupName = go["name"].toString();
        for (const auto& ev : go["events"].toArray()) {
            QJsonObject eo = ev.toObject();
            QString guid = eo["guid"].toString();
            QString name = eo["name"].toString();
            if (!guid.isEmpty()) {
                // Format: {GUID} event:/group/name
                out << guid << "\t" << "event:/" << groupName << "/" << name << "\n";
            }
        }
        // Child groups
        for (const auto& cgv : go["childGroups"].toArray()) {
            QJsonObject cgo = cgv.toObject();
            for (const auto& cev : cgo["events"].toArray()) {
                QJsonObject ceo = cev.toObject();
                QString guid = ceo["guid"].toString();
                QString name = ceo["name"].toString();
                if (!guid.isEmpty())
                    out << guid << "\t" << "event:/" << groupName << "/" << name << "\n";
            }
        }
    }

    // Also write from banks section
    for (const auto& bv : raw["banks"].toArray()) {
        QJsonObject bo = bv.toObject();
        QString bankName = bo["name"].toString();
        for (const auto& egv : bo["events"].toArray()) {
            QJsonObject eo = egv.toObject();
            QString guid = eo["guid"].toString(eo["id"].toString());
            QString name = eo["name"].toString();
            if (!guid.isEmpty())
                out << guid << "\t" << "event:/" << bankName << "/" << name << "\n";
        }
    }

    f.close();
    return true;
}

bool KSBankWriter::convertKSAudioToBank(const QString& ksaudioPath,
                                         const QString& assetsDir,
                                         const QString& bankOutputPath) {
    KSAudioProject proj;
    if (!proj.load(ksaudioPath)) {
        m_lastError = "Failed to load: " + ksaudioPath;
        emit writeFailed(m_lastError);
        return false;
    }
    return writeBank(proj.banks().isEmpty() ? SoundBank{} : proj.banks().first(),
                     assetsDir, bankOutputPath);
}

// ============================================================================
// FEV2 chunk writers
// ============================================================================

void KSBankWriter::writeFEV2Header(QDataStream& s, const FEV2Header& hdr) {
    s << hdr.magic;
    s << hdr.version;
    s << hdr.flags;
}

void KSBankWriter::writeSTRTChunk(QDataStream& s, const QByteArray& strTable) {
    s << static_cast<quint32>(0x53545254); // "STRT"
    s << static_cast<quint32>(strTable.size());
    s.device()->write(strTable);
}

void KSBankWriter::writeEVTSChunk(QDataStream& s, const QVector<EventEntry>& events) {
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

    ds << static_cast<quint32>(events.size());
    for (const auto& e : events) {
        // GUID (16 bytes)
        ds.device()->write(e.guid);
        ds << e.nameIdx;
        ds << e.pathIdx;
        ds << e.flags;
        ds << e.category;
        ds << e.maxInstances;
        ds << e.length;
        ds << static_cast<quint32>(e.paramNameIdxs.size());
        for (int i = 0; i < e.paramNameIdxs.size(); ++i) {
            ds << e.paramNameIdxs[i];
            ds << e.paramDefaults[i];
        }
    }

    s << static_cast<quint32>(0x45565453); // "EVTS"
    s << static_cast<quint32>(data.size());
    s.device()->write(data);
}

void KSBankWriter::writeBUSSEntry(QDataStream& s, const FSPROBus& bus,
                                    const QMap<QString, quint32>& strMap) {
    s << strMap.value(bus.name);
    s << strMap.value("bus:/" + bus.name);
    s << bus.volume;
    quint8 flags = (bus.mute ? 0x01 : 0) | (bus.solo ? 0x02 : 0);
    s << flags;
    s << static_cast<quint32>(bus.childBuses.size());
    for (const auto& child : bus.childBuses) {
        writeBUSSEntry(s, child, strMap);
    }
    s << static_cast<quint32>(bus.linkedVCAs.size());
    for (const auto& vcaName : bus.linkedVCAs)
        s << strMap.value(vcaName, 0);
}

void KSBankWriter::writeVCASChunk(QDataStream& s, const QVector<FSPROVCA>& vcas,
                                    const QMap<QString, quint32>& strMap) {
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

    ds << static_cast<quint32>(vcas.size());
    for (const auto& v : vcas) {
        ds << strMap.value(v.name);
        ds << strMap.value("vca:/" + v.name);
        ds << v.volume;
    }

    s << static_cast<quint32>(0x56434153); // "VCAS"
    s << static_cast<quint32>(data.size());
    s.device()->write(data);
}

void KSBankWriter::writeSNDSChunk(QDataStream& s, const QVector<SoundEntry>& sounds) {
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    ds << static_cast<quint32>(sounds.size());
    for (const auto& so : sounds) {
        ds << so.nameIdx;
        ds << so.sampleRate;
        ds << so.channels;
        ds << so.length;
        ds << so.format;
    }

    s << static_cast<quint32>(0x534E4453); // "SNDS"
    s << static_cast<quint32>(data.size());
    s.device()->write(data);
}

// ============================================================================
// Helpers
// ============================================================================

QByteArray KSBankWriter::makeGUID(const QString& name) {
    // Create a deterministic GUID from a name
    QByteArray hash = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Md5);
    // Set version bits (UUID v3 style, but deterministic)
    hash[6] = (hash[6] & 0x0F) | 0x30;
    hash[8] = (hash[8] & 0x3F) | 0x80;
    return hash;
}

bool KSBankWriter::readAudioData(const QString& filePath, QByteArray& outData,
                                  quint32& sampleRate, quint32& channels) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;

    // Try parsing WAV header
    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);

    quint32 riff, wave;
    quint32 fileSize;
    s >> riff >> fileSize >> wave;
    if (riff == 0x46464952 /*RIFF*/ && wave == 0x45564157 /*WAVE*/) {
        // Parse WAV chunks to find fmt + data
        while (!s.atEnd()) {
            quint32 chunkId, chunkSize;
            s >> chunkId >> chunkSize;
            qint64 next = f.pos() + chunkSize;

            if (chunkId == 0x20746D66) { // "fmt "
                quint16 audioFmt;
                s >> audioFmt;
                quint16 ch;
                s >> ch;
                channels = ch;
                quint32 srate;
                s >> srate;
                sampleRate = srate;
            } else if (chunkId == 0x61746164) { // "data"
                outData = f.read(chunkSize);
                return true;
            }
            f.seek(next);
        }
    }

    // Not a WAV or failed to parse — read as raw
    f.reset();
    outData = f.readAll();
    return true;
}

bool KSBankWriter::writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                        const QString& assetsDir, BankVersion version) {
    IBankWriter* writer = BankWriterFactory::instance().getWriter(version);
    if (!writer) {
        m_lastError = "No writer registered for version: " + BankVersionManager::versionToString(version);
        emit writeFailed(m_lastError);
        return false;
    }

    // Connect signals
    connect(writer, &IBankWriter::writeStarted, this, &KSBankWriter::writeStarted);
    connect(writer, &IBankWriter::writeProgress, this, &KSBankWriter::writeProgress);
    connect(writer, &IBankWriter::writeCompleted, this, &KSBankWriter::writeCompleted);
    connect(writer, &IBankWriter::writeFailed, this, &KSBankWriter::writeFailed);

    return writer->writeProjectBanks(project, outputDir, assetsDir);
}

bool KSBankWriter::writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                        const QString& assetsDir, GameTarget target) {
    IBankWriter* writer = BankWriterFactory::instance().getWriter(target);
    if (!writer) {
        m_lastError = "No writer registered for target: " + BankVersionManager::gameTargetToString(target);
        emit writeFailed(m_lastError);
        return false;
    }

    connect(writer, &IBankWriter::writeStarted, this, &KSBankWriter::writeStarted);
    connect(writer, &IBankWriter::writeProgress, this, &KSBankWriter::writeProgress);
    connect(writer, &IBankWriter::writeCompleted, this, &KSBankWriter::writeCompleted);
    connect(writer, &IBankWriter::writeFailed, this, &KSBankWriter::writeFailed);

    return writer->writeProjectBanks(project, outputDir, assetsDir);
}

IBankWriter* KSBankWriter::getWriter(const KSAudioProject& project) {
    // Auto-detect from project or use default
    if (m_defaultTarget != GameTarget::AutoDetect) {
        return BankWriterFactory::instance().getWriter(m_defaultTarget);
    }
    if (m_defaultVersion != BankVersion::Unknown) {
        return BankWriterFactory::instance().getWriter(m_defaultVersion);
    }
    // Default to FMOD 1.08 (AC1)
    return BankWriterFactory::instance().getWriter(BankVersion::FMOD_1_08);
}

IBankWriter* KSBankWriter::getWriter(BankVersion version) {
    return BankWriterFactory::instance().getWriter(version);
}

IBankWriter* KSBankWriter::getWriter(GameTarget target) {
    return BankWriterFactory::instance().getWriter(target);
}

}} // namespace ks::fileformat
