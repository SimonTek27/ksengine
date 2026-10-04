#include "FSPROImporter.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QDebug>
#include <functional>

namespace ks { namespace fileformat {

KSFSPROImporter::KSFSPROImporter(QObject* parent)
    : QObject(parent) {}

// ============================================================================
// Public API
// ============================================================================

FSPROProject KSFSPROImporter::parseFile(const QString& fsproPath) {
    m_lastError.clear();
    emit parseStarted(fsproPath);

    QFile file(fsproPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = "Cannot open file: " + fsproPath;
        emit parseFailed(m_lastError);
        return {};
    }
    QByteArray data = file.readAll();
    file.close();

    // Automatically derive asset base path from the .fspro's directory
    m_assetBasePath = QFileInfo(fsproPath).absolutePath();

    return parseData(data);
}

FSPROProject KSFSPROImporter::parseData(const QByteArray& xmlData) {
    m_lastError.clear();

    if (xmlData.isEmpty()) {
        m_lastError = "Empty XML data";
        emit parseFailed(m_lastError);
        return {};
    }

    return parseXml(xmlData);
}

QJsonObject KSFSPROImporter::toKSAudioJson(const FSPROProject& project) {
    QJsonObject root;
    root["_schema"]  = QStringLiteral("ksaudio");
    root["_version"] = QStringLiteral("2.0.0");
    root["name"]     = project.name;
    root["guid"]     = project.guid;
    root["format"]   = QStringLiteral("fmod.fspro.1.08.12");

    // -- Global parameters --
    QJsonArray globalParamsArr;
    for (const auto& p : project.globalParameters) {
        QJsonObject obj;
        obj["guid"]    = p.guid;
        obj["name"]    = p.name;
        obj["type"]    = p.type;
        obj["scope"]   = p.scope;
        obj["unit"]    = p.unit;
        obj["min"]     = static_cast<double>(p.minVal);
        obj["max"]     = static_cast<double>(p.maxVal);
        obj["default"] = static_cast<double>(p.defaultVal);
        globalParamsArr.append(obj);
    }
    if (!globalParamsArr.isEmpty())
        root["globalParameters"] = globalParamsArr;

    // -- Event groups (recursive) --
    QJsonArray groupsArr;
    std::function<QJsonObject(const FSPROEventGroup&)> serializeGroup;
    serializeGroup = [&](const FSPROEventGroup& g) -> QJsonObject {
        QJsonObject go;
        go["guid"] = g.guid;
        go["name"] = g.name;
        QJsonArray evs;
        for (const auto& e : g.events) {
            QJsonObject eo;
            eo["guid"]   = e.guid;
            eo["name"]   = e.name;
            eo["type"]   = e.typeStr;
            eo["maxInstances"] = static_cast<int>(e.maxInstances);
            eo["priority"]     = e.priority;

            // Local parameters
            QJsonArray lps;
            for (const auto& p : e.localParameters) {
                QJsonObject po;
                po["guid"]    = p.guid;
                po["name"]    = p.name;
                po["type"]    = p.type;
                po["scope"]   = p.scope;
                po["unit"]    = p.unit;
                po["min"]     = static_cast<double>(p.minVal);
                po["max"]     = static_cast<double>(p.maxVal);
                po["default"] = static_cast<double>(p.defaultVal);
                lps.append(po);
            }
            if (!lps.isEmpty()) eo["parameters"] = lps;

            // Timeline
            if (!e.timeline.tracks.isEmpty()) {
                QJsonObject tlObj;
                QJsonArray tracksArr;
                for (const auto& t : e.timeline.tracks) {
                    QJsonObject to;
                    to["name"] = t.name;
                    to["id"]   = t.id;
                    QJsonArray kfs;
                    for (const auto& kf : t.keyframes) {
                        QJsonObject kfo;
                        kfo["time"] = static_cast<double>(kf.time);
                        kfo["id"]   = kf.id;
                        QJsonObject ao;
                        ao["type"] = kf.action.type;
                        if (!kf.action.soundGuid.isEmpty())
                            ao["sound"] = kf.action.soundGuid;
                        if (!kf.action.parameterGuid.isEmpty())
                            ao["parameter"] = kf.action.parameterGuid;
                        if (qFuzzyCompare(1.0f, 1.0f + kf.action.parameterValue) == false)
                            ao["value"] = static_cast<double>(kf.action.parameterValue);
                        kfo["action"] = ao;
                        kfs.append(kfo);
                    }
                    to["keyframes"] = kfs;
                    tracksArr.append(to);
                }
                tlObj["tracks"] = tracksArr;
                eo["timeline"] = tlObj;
            }

            // Spatialization
            QJsonObject sp;
            sp["enabled"]     = e.spatializationEnabled;
            sp["panLevel"]    = static_cast<double>(e.panLevel);
            sp["speakerMode"] = e.speakerMode;
            sp["minDistance"] = static_cast<double>(e.attenuationMin);
            sp["maxDistance"] = static_cast<double>(e.attenuationMax);
            sp["model"]       = e.attenuationModel;
            eo["spatialization"] = sp;

            evs.append(eo);
        }
        go["events"] = evs;

        // Child groups
        QJsonArray cgs;
        for (const auto& cg : g.childGroups)
            cgs.append(serializeGroup(cg));
        if (!cgs.isEmpty())
            go["childGroups"] = cgs;

        return go;
    };
    for (const auto& g : project.eventGroups)
        groupsArr.append(serializeGroup(g));
    if (!groupsArr.isEmpty())
        root["eventGroups"] = groupsArr;

    // -- Buses (recursive) --
    QJsonArray busesArr;
    std::function<QJsonObject(const FSPROBus&)> serializeBus;
    serializeBus = [&](const FSPROBus& b) -> QJsonObject {
        QJsonObject bo;
        bo["guid"]   = b.guid;
        bo["name"]   = b.name;
        bo["volume"] = static_cast<double>(b.volume);
        bo["mute"]   = b.mute;
        bo["solo"]   = b.solo;

        if (!b.linkedVCAs.isEmpty()) {
            QJsonArray vcaArr;
            for (const auto& v : b.linkedVCAs) vcaArr.append(v);
            bo["linkedVCAs"] = vcaArr;
        }

        // Effects
        if (!b.effects.isEmpty()) {
            QJsonArray effArr;
            for (const auto& e : b.effects) {
                QJsonObject eo;
                eo["name"]    = e.name;
                eo["type"]    = e.type;
                eo["enabled"] = e.enabled;
                if (!e.parameters.isEmpty()) {
                    QJsonObject po;
                    for (auto it = e.parameters.constBegin(); it != e.parameters.constEnd(); ++it)
                        po[it.key()] = static_cast<double>(it.value());
                    eo["parameters"] = po;
                }
                effArr.append(eo);
            }
            bo["effects"] = effArr;
        }

        // Child buses
        if (!b.childBuses.isEmpty()) {
            QJsonArray cbArr;
            for (const auto& cb : b.childBuses)
                cbArr.append(serializeBus(cb));
            bo["childBuses"] = cbArr;
        }
        return bo;
    };
    for (const auto& b : project.buses)
        busesArr.append(serializeBus(b));
    if (!busesArr.isEmpty())
        root["buses"] = busesArr;

    // -- VCAs --
    if (!project.vcas.isEmpty()) {
        QJsonArray vcasArr;
        for (const auto& v : project.vcas) {
            QJsonObject vo;
            vo["guid"]   = v.guid;
            vo["name"]   = v.name;
            vo["volume"] = static_cast<double>(v.volume);
            vcasArr.append(vo);
        }
        root["vcas"] = vcasArr;
    }

    // -- Snapshots --
    if (!project.snapshots.isEmpty()) {
        QJsonArray snapsArr;
        for (const auto& s : project.snapshots) {
            QJsonObject so;
            so["guid"] = s.guid;
            so["name"] = s.name;
            so["type"] = s.type;
            snapsArr.append(so);
        }
        root["snapshots"] = snapsArr;
    }

    // -- Sounds --
    if (!project.sounds.isEmpty()) {
        QJsonArray sndsArr;
        for (const auto& s : project.sounds) {
            QJsonObject so;
            so["guid"]        = s.guid;
            so["name"]        = s.name;
            so["file"]        = s.filePath;
            so["format"]      = s.format;
            so["sampleRate"]  = static_cast<int>(s.sampleRate);
            so["channels"]    = static_cast<int>(s.channels);
            so["loopStart"]   = static_cast<qint64>(s.loopStart);
            so["loopEnd"]     = static_cast<qint64>(s.loopEnd);
            so["compression"] = s.compression;
            sndsArr.append(so);
        }
        root["sounds"] = sndsArr;
    }

    // -- Banks --
    if (!project.banks.isEmpty()) {
        QJsonArray banksArr;
        for (const auto& b : project.banks) {
            QJsonObject bo;
            bo["guid"] = b.guid;
            bo["name"] = b.name;
            if (!b.eventGuids.isEmpty()) {
                QJsonArray evArr;
                for (const auto& eg : b.eventGuids) evArr.append(eg);
                bo["eventGuids"] = evArr;
            }
            banksArr.append(bo);
        }
        root["banks"] = banksArr;
    }

    // -- Mixer --
    if (!project.mixer.snapshots.isEmpty()) {
        QJsonObject mx;
        QJsonArray msArr;
        for (const auto& ms : project.mixer.snapshots) {
            QJsonObject mso;
            mso["guid"] = ms.guid;
            mso["name"] = ms.name;
            QJsonObject vals;
            for (auto it = ms.busVolumes.constBegin(); it != ms.busVolumes.constEnd(); ++it)
                vals[it.key()] = static_cast<double>(it.value());
            mso["values"] = vals;
            msArr.append(mso);
        }
        mx["snapshots"] = msArr;
        root["mixer"] = mx;
    }

    return root;
}

bool KSFSPROImporter::convertFile(const QString& fsproPath, const QString& ksaudioPath) {
    FSPROProject project = parseFile(fsproPath);
    if (!m_lastError.isEmpty())
        return false;

    emit parseProgress(80);

    QJsonObject json = toKSAudioJson(project);

    QFile outFile(ksaudioPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        m_lastError = "Cannot write output: " + ksaudioPath;
        emit parseFailed(m_lastError);
        return false;
    }
    outFile.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    outFile.close();

    emit parseCompleted(ksaudioPath);
    return true;
}

// ============================================================================
// XML parser
// ============================================================================

FSPROProject KSFSPROImporter::parseXml(const QByteArray& xml) {
    FSPROProject project;
    project.name = "ImportedProject";

    ::QXmlStreamReader reader(xml);
    m_xml = &reader;

    // Find root <studioProject>
    while (!reader.atEnd() && !reader.isStartElement())
        reader.readNext();

    if (reader.name().toString() == "studioProject") {
        project.name = reader.attributes().value("name").toString();
        project.guid = reader.attributes().value("guid").toString();
        parseStudioProject(project);
    }

    m_xml = nullptr;

    if (reader.hasError()) {
        m_lastError = QString("XML parse error: %1 at line %2")
            .arg(reader.errorString())
            .arg(reader.lineNumber());
        emit parseFailed(m_lastError);
        return {};
    }

    emit parseProgress(60);
    return project;
}

void KSFSPROImporter::parseStudioProject(FSPROProject& proj) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        const auto name = m_xml->name().toString();
        if (name == "eventGroup") {
            FSPROEventGroup group;
            group.name = m_xml->attributes().value("name").toString();
            group.guid = readGuid();
            parseEventGroup(group);
            proj.eventGroups.append(group);
        } else if (name == "bus") {
            FSPROBus bus;
            bus.name   = m_xml->attributes().value("name").toString();
            bus.guid   = readGuid();
            { auto v = m_xml->attributes().value("volume"); bus.volume = v.isNull() ? 1.0f : v.toFloat(); }
            bus.mute   = m_xml->attributes().value("mute").toString() == "true";
            bus.solo   = m_xml->attributes().value("solo").toString() == "true";
            parseBus(bus);
            proj.buses.append(bus);
        } else if (name == "vca") {
            FSPROVCA vca;
            vca.name   = m_xml->attributes().value("name").toString();
            vca.guid   = readGuid();
            { auto v = m_xml->attributes().value("volume"); vca.volume = v.isNull() ? 1.0f : v.toFloat(); }
            proj.vcas.append(vca);
        } else if (name == "snapshot") {
            FSPROSnapshot snap;
            snap.name = m_xml->attributes().value("name").toString();
            snap.guid = readGuid();
            snap.type = m_xml->attributes().value("type").toString();
            proj.snapshots.append(snap);
            m_xml->skipCurrentElement();
        } else if (name == "parameter") {
            FSPROParameter param;
            param.name  = m_xml->attributes().value("name").toString();
            param.guid  = readGuid();
            { auto _v = m_xml->attributes().value("type"); param.type  = _v.isNull() ? QStringLiteral("float")  : _v.toString(); }
            { auto _v = m_xml->attributes().value("scope"); param.scope = _v.isNull() ? QStringLiteral("global") : _v.toString(); }
            parseParameter(param);
            proj.globalParameters.append(param);
        } else if (name == "sound") {
            FSPROSound sound;
            sound.name   = m_xml->attributes().value("name").toString();
            sound.guid   = readGuid();
            parseSound(sound);
            proj.sounds.append(sound);
        } else if (name == "bank") {
            FSPROBank bank;
            bank.name = m_xml->attributes().value("name").toString();
            bank.guid = readGuid();
            parseBank(bank);
            proj.banks.append(bank);
        } else if (name == "mixer") {
            parseMixer(proj.mixer);
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseEventGroup(FSPROEventGroup& group) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        const auto name = m_xml->name().toString();
        if (name == "event") {
            FSPROEvent ev;
            ev.name         = m_xml->attributes().value("name").toString();
            ev.guid         = readGuid();
            { auto _v = m_xml->attributes().value("type"); ev.typeStr      = _v.isNull() ? QStringLiteral("2D") : _v.toString(); }
            { auto _v = m_xml->attributes().value("maxInstances"); ev.maxInstances = _v.isNull() ? 0 : _v.toUInt(); }
            { auto _v = m_xml->attributes().value("priority"); ev.priority     = _v.isNull() ? 0 : _v.toInt(); }
            parseEvent(ev);
            group.events.append(ev);
        } else if (name == "eventGroup") {
            FSPROEventGroup child;
            child.name = m_xml->attributes().value("name").toString();
            child.guid = readGuid();
            parseEventGroup(child);
            group.childGroups.append(child);
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseEvent(FSPROEvent& ev) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        const auto name = m_xml->name().toString();
        if (name == "property") {
            // Skip top-level event properties (handled by name)
            m_xml->skipCurrentElement();
        } else if (name == "parameter") {
            FSPROParameter param;
            param.name  = m_xml->attributes().value("name").toString();
            param.guid  = readGuid();
            { auto _v = m_xml->attributes().value("type"); param.type  = _v.isNull() ? QStringLiteral("float") : _v.toString(); }
            { auto _v = m_xml->attributes().value("scope"); param.scope = _v.isNull() ? QStringLiteral("local") : _v.toString(); }
            parseParameter(param);
            ev.localParameters.append(param);
        } else if (name == "timeline") {
            parseTimeline(ev.timeline);
        } else if (name == "spatialization") {
            ev.spatializationEnabled =
                m_xml->attributes().value("enabled").toString() != "false";
            while (m_xml->readNextStartElement()) {
                const auto pn = m_xml->name().toString();
                if (pn == "property") {
                    QString pname  = m_xml->attributes().value("name").toString();
                    QString pvalue = m_xml->attributes().value("value").toString();
                    if (pname == "panLevel")
                        ev.panLevel = pvalue.isEmpty() ? 1.0f : pvalue.toFloat();
                    else if (pname == "speakerMode")
                        ev.speakerMode = pvalue;
                    else if (pname == "minDistance")
                        ev.attenuationMin = pvalue.isEmpty() ? 1.0f : pvalue.toFloat();
                    else if (pname == "maxDistance")
                        ev.attenuationMax = pvalue.isEmpty() ? 100.0f : pvalue.toFloat();
                    else if (pname == "model")
                        ev.attenuationModel = pvalue;
                }
                m_xml->skipCurrentElement();
            }
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseTimeline(FSPROTimeline& tl) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "track") {
            FSPROTrack track;
            track.name = m_xml->attributes().value("name").toString();
            track.id   = readGuid("id");
            parseTrack(track);
            tl.tracks.append(track);
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseTrack(FSPROTrack& track) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "keyframe") {
            FSPROKeyframe kf;
            kf.time = m_xml->attributes().value("time").toFloat();
            kf.id   = readGuid("id");
            parseKeyframe(kf);
            track.keyframes.append(kf);
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseKeyframe(FSPROKeyframe& kf) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "action") {
            QString actionType = m_xml->attributes().value("type").toString();
            parseAction(kf.action, actionType);
        }
        m_xml->skipCurrentElement();
    }
}

void KSFSPROImporter::parseAction(FSPROAction& action, const QString& actionType) {
    if (!m_xml) return;
    action.type = actionType;
    // Read action attributes
    const auto& attrs = m_xml->attributes();
    action.soundGuid      = attrs.value("sound").toString();
    action.parameterGuid  = attrs.value("parameter").toString();
    action.parameterValue = attrs.value("value").toFloat();
    // Also read any <property> children
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "property") {
            QString pn = m_xml->attributes().value("name").toString();
            if (pn == "sound") action.soundGuid = m_xml->attributes().value("value").toString();
            else if (pn == "parameter") action.parameterGuid = m_xml->attributes().value("value").toString();
            else if (pn == "value") action.parameterValue = m_xml->attributes().value("value").toFloat();
        }
        m_xml->skipCurrentElement();
    }
}

void KSFSPROImporter::parseBus(FSPROBus& bus) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        const auto name = m_xml->name().toString();
        if (name == "childBus") {
            QString childGuid = readGuid();
            bus.childBuses.append(FSPROBus());
            // Store child GUID reference (resolved later)
            bus.childBuses.last().guid = childGuid;
        } else if (name == "vca") {
            QString vcaGuid = readGuid();
            bus.linkedVCAs.append(vcaGuid);
            m_xml->skipCurrentElement();
        } else if (name == "effect") {
            FSPROEffect effect;
            effect.name    = m_xml->attributes().value("name").toString();
            effect.type    = m_xml->attributes().value("type").toString();
            effect.enabled = m_xml->attributes().value("enabled").toString() != "false";
            while (m_xml->readNextStartElement()) {
                if (m_xml->name().toString() == "property") {
                    QString pn = m_xml->attributes().value("name").toString();
                    float pv   = m_xml->attributes().value("value").toFloat();
                    effect.parameters[pn] = pv;
                }
                m_xml->skipCurrentElement();
            }
            bus.effects.append(effect);
        } else if (name == "property") {
            m_xml->skipCurrentElement();
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseParameter(FSPROParameter& param) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "property") {
            QString pname  = m_xml->attributes().value("name").toString();
            QString pvalue = m_xml->attributes().value("value").toString();
            if (pname == "min")     param.minVal     = pvalue.toFloat();
            else if (pname == "max")     param.maxVal     = pvalue.isEmpty() ? 1.0f : pvalue.toFloat();
            else if (pname == "default") param.defaultVal = pvalue.isEmpty() ? 0.5f : pvalue.toFloat();
        }
        m_xml->skipCurrentElement();
    }
}

void KSFSPROImporter::parseSound(FSPROSound& sound) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "property") {
            QString pname  = m_xml->attributes().value("name").toString();
            QString pvalue = m_xml->attributes().value("value").toString();
            if (pname == "file") {
                sound.filePath = pvalue;
                // Resolve relative path against asset base
                if (!m_assetBasePath.isEmpty() && QDir::isRelativePath(sound.filePath))
                    sound.filePath = QDir(m_assetBasePath).absoluteFilePath(sound.filePath);
            } else if (pname == "format")      sound.format      = pvalue;
            else if (pname == "sampleRate")    sound.sampleRate  = pvalue.toUInt();
            else if (pname == "channels")      sound.channels    = pvalue.toUInt();
            else if (pname == "loopStart")     sound.loopStart   = pvalue.toLongLong();
            else if (pname == "loopEnd")       sound.loopEnd     = pvalue.toLongLong();
            else if (pname == "compression")   sound.compression = pvalue;
        }
        m_xml->skipCurrentElement();
    }
}

void KSFSPROImporter::parseBank(FSPROBank& bank) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "items") {
            while (m_xml->readNextStartElement()) {
                if (m_xml->name().toString() == "item") {
                    QString eventGuid = m_xml->attributes().value("id").toString();
                    if (!eventGuid.isEmpty())
                        bank.eventGuids.append(eventGuid);
                }
                m_xml->skipCurrentElement();
            }
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseMixer(FSPROMixer& mixer) {
    if (!m_xml) return;
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "snapshot") {
            FSPROMixerSnapshot ms;
            ms.name = m_xml->attributes().value("name").toString();
            ms.guid = readGuid();
            while (m_xml->readNextStartElement()) {
                if (m_xml->name().toString() == "value") {
                    QString busGuid = m_xml->attributes().value("guid").toString();
                    { auto v = m_xml->attributes().value("volume"); float vol = v.isNull() ? 1.0f : v.toFloat(); ms.busVolumes[busGuid] = vol; }
                }
                m_xml->skipCurrentElement();
            }
            mixer.snapshots.append(ms);
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseVCA(FSPROVCA& vca) {
    if (!m_xml) return;
    vca.guid = m_xml->attributes().value("id").toString();
    vca.name = m_xml->attributes().value("name").toString();
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "property") {
            QString pn = m_xml->attributes().value("name").toString();
            { auto _v = m_xml->attributes().value("value"); float pv = _v.isNull() ? 1.0f : _v.toFloat(); if (pn == "volume") vca.volume = pv; }
            m_xml->skipCurrentElement();
        } else if (m_xml->name().toString() == "object") {
            QString objGuid = m_xml->attributes().value("id").toString();
            vca.objects.append(objGuid);
            m_xml->skipCurrentElement();
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

void KSFSPROImporter::parseSnapshot(FSPROSnapshot& snap) {
    if (!m_xml) return;
    snap.guid = m_xml->attributes().value("id").toString();
    snap.name = m_xml->attributes().value("name").toString();
    while (m_xml->readNextStartElement()) {
        if (m_xml->name().toString() == "property") {
            QString pn = m_xml->attributes().value("name").toString();
            float pv   = m_xml->attributes().value("value").toFloat();
            if (pn == "strength") snap.strength = pv;
            m_xml->skipCurrentElement();
        } else if (m_xml->name().toString() == "value") {
            QString busGuid = m_xml->attributes().value("guid").toString();
            { auto _v = m_xml->attributes().value("volume"); float vol = _v.isNull() ? 1.0f : _v.toFloat(); snap.busVolumes[busGuid] = vol; }
            m_xml->skipCurrentElement();
        } else {
            m_xml->skipCurrentElement();
        }
    }
}

// ============================================================================
// Helpers
// ============================================================================

QString KSFSPROImporter::readGuid(const QString& attrName) const {
    if (!m_xml) return {};
    return m_xml->attributes().value(attrName).toString();
}

QString KSFSPROImporter::readProperty(const QString& name) const {
    if (!m_xml) return {};
    // Called when m_xml is on a <property> element
    if (m_xml->name().toString() == "property") {
        const auto& attrs = m_xml->attributes();
        if (attrs.value("name").toString() == name)
            return attrs.value("value").toString();
    }
    return {};
}

}} // namespace ks::fileformat
