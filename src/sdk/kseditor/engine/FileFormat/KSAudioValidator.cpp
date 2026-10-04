#include "KSAudioValidator.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

namespace ks { namespace fileformat {

KSAudioValidator::KSAudioValidator(QObject* parent)
    : QObject(parent) {}

// ============================================================================
// Public API
// ============================================================================

KSAudioValidator::ValidationResult KSAudioValidator::validate(const QString& projectPath)
{
    ValidationResult result;
    emit validationStarted(projectPath);

    QFileInfo fi(projectPath);
    if (!fi.exists()) {
        addIssue(result, Error, "File", "Project file not found: " + projectPath);
        m_lastResult = result;
        emit validationCompleted(result);
        return result;
    }

    if (!fi.suffix().toLower().endsWith("ksaudio")) {
        addIssue(result, Warning, "File", "File does not have .ksaudio extension");
    }

    QJsonObject root = loadJson(projectPath);
    if (root.isEmpty()) {
        addIssue(result, Error, "JSON", "Invalid JSON in file: " + projectPath);
        m_lastResult = result;
        emit validationCompleted(result);
        return result;
    }

    QString projectDir = fi.absolutePath();
    result = validateJson(root, projectDir);

    m_lastResult = result;
    emit validationCompleted(result);
    return result;
}

KSAudioValidator::ValidationResult KSAudioValidator::validateJson(
    const QJsonObject& root,
    const QString& projectDir)
{
    ValidationResult result;

    validateSchema(result, root);
    validateMetadata(result, root);
    validateEvents(result, root, projectDir);
    validateBanks(result, root);
    validateBuses(result, root);
    validateSounds(result, root, projectDir);
    validateCrossReferences(result, root);

    result.valid = (result.errorCount == 0);
    return result;
}

KSAudioValidator::ValidationResult KSAudioValidator::validateStructure(const QJsonObject& root)
{
    ValidationResult result;

    validateSchema(result, root);
    validateMetadata(result, root);

    // Check top-level arrays exist
    if (!root.contains("eventGroups") && !root.contains("events")) {
        addIssue(result, Warning, "Structure", "No eventGroups or events found",
                 "root", "Add at least one eventGroup");
    }

    if (!root.contains("banks")) {
        addIssue(result, Info, "Structure", "No banks defined",
                 "root", "Add a banks array for runtime bank export");
    }

    result.valid = (result.errorCount == 0);
    return result;
}

bool KSAudioValidator::isValidGuid(const QString& guid)
{
    // Accept GUIDs with or without braces
    static QRegularExpression re(
        "^\\{?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\}?$");
    return re.match(guid).hasMatch();
}

// ============================================================================
// Private validation helpers
// ============================================================================

void KSAudioValidator::addIssue(ValidationResult& result, Severity severity,
                                 const QString& category, const QString& message,
                                 const QString& path, const QString& suggestion)
{
    Issue issue;
    issue.severity = severity;
    issue.category = category;
    issue.message = message;
    issue.path = path;
    issue.suggestion = suggestion;

    result.issues.append(issue);

    switch (severity) {
    case Error:   result.errorCount++; break;
    case Warning: result.warningCount++; break;
    case Info:    result.infoCount++; break;
    }

    emit issueFound(issue);
}

void KSAudioValidator::validateSchema(ValidationResult& result, const QJsonObject& root)
{
    // Check _schema field
    QString schema = root["_schema"].toString();
    if (schema.isEmpty()) {
        addIssue(result, Warning, "Schema", "Missing '_schema' field",
                 "root._schema", "Set to \"ksaudio\" for v2 projects");
    } else if (schema != "ksaudio") {
        addIssue(result, Error, "Schema", "Unknown schema: " + schema,
                 "root._schema", "Expected \"ksaudio\"");
    }

    // Check _version
    QString version = root["_version"].toString();
    if (version.isEmpty()) {
        addIssue(result, Warning, "Schema", "Missing '_version' field",
                 "root._version", "Set to \"2.0.0\" for v2 projects");
    } else if (version != "2.0.0" && version != "2.0" && version != "1.0") {
        addIssue(result, Info, "Schema", "Unexpected version: " + version,
                 "root._version", "Supported versions: 1.0, 2.0, 2.0.0");
    }
}

void KSAudioValidator::validateMetadata(ValidationResult& result, const QJsonObject& root)
{
    // Project name
    QString name = root["name"].toString();
    if (name.isEmpty()) {
        addIssue(result, Error, "Metadata", "Project name is empty",
                 "root.name", "Set a descriptive project name");
    } else if (name == "Untitled") {
        addIssue(result, Info, "Metadata", "Project name is default (\"Untitled\")",
                 "root.name", "Rename to something descriptive");
    }

    // GUID
    QString guid = root["guid"].toString();
    if (guid.isEmpty()) {
        addIssue(result, Warning, "Metadata", "Missing project GUID",
                 "root.guid", "Add a unique GUID for project identification");
    } else if (!isValidGuid(guid)) {
        addIssue(result, Error, "Metadata", "Invalid GUID format: " + guid,
                 "root.guid", "Use standard GUID format: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}");
    }

    // Format
    QString format = root["format"].toString();
    if (format.isEmpty()) {
        addIssue(result, Info, "Metadata", "Missing 'format' field",
                 "root.format", "Set to \"fmod.fspro.1.08.12\" for FMOD compatibility");
    }
}

void KSAudioValidator::validateEvents(ValidationResult& result, const QJsonObject& root,
                                       const QString& projectDir)
{
    QSet<QString> eventGuids;
    QSet<QString> eventNames;

    // Collect events from eventGroups (recursive)
    std::function<void(const QJsonArray&)> collectEvents;
    collectEvents = [&](const QJsonArray& groups) {
        for (const auto& gv : groups) {
            QJsonObject group = gv.toObject();

            // Validate group
            QString groupGuid = group["guid"].toString();
            if (!groupGuid.isEmpty() && !isValidGuid(groupGuid)) {
                addIssue(result, Error, "EventGroup", "Invalid group GUID: " + groupGuid,
                         "eventGroups[].guid");
            }

            // Validate events in this group
            QJsonArray events = group["events"].toArray();
            for (const auto& ev : events) {
                QJsonObject evObj = ev.toObject();
                QString evName = evObj["name"].toString();
                QString evGuid = evObj["guid"].toString();

                // Check name
                if (evName.isEmpty()) {
                    addIssue(result, Error, "Event", "Event has empty name",
                             "eventGroups[].events[].name");
                } else if (eventNames.contains(evName)) {
                    addIssue(result, Warning, "Event", "Duplicate event name: " + evName,
                             "eventGroups[].events[].name",
                             "Rename to ensure uniqueness");
                }
                eventNames.insert(evName);

                // Check GUID
                if (evGuid.isEmpty()) {
                    addIssue(result, Warning, "Event", "Event missing GUID: " + evName,
                             "eventGroups[].events[].guid");
                } else if (!isValidGuid(evGuid)) {
                    addIssue(result, Error, "Event", "Invalid event GUID: " + evGuid,
                             "eventGroups[].events[].guid");
                } else if (eventGuids.contains(evGuid)) {
                    addIssue(result, Error, "Event", "Duplicate event GUID: " + evGuid,
                             "eventGroups[].events[].guid");
                }
                eventGuids.insert(evGuid);

                // Check audio file reference
                QString audioFile = evObj["audioFile"].toString();
                if (!audioFile.isEmpty() && !projectDir.isEmpty()) {
                    QString fullPath = projectDir + "/Assets/audio/" + audioFile;
                    if (!QFile::exists(fullPath)) {
                        addIssue(result, Warning, "Event",
                                 "Audio file not found: " + audioFile,
                                 "eventGroups[].events[].audioFile",
                                 "Ensure the file exists in Assets/audio/");
                    }
                }

                // Check type
                QString type = evObj["type"].toString();
                if (!type.isEmpty() && type != "2D" && type != "3D") {
                    addIssue(result, Warning, "Event",
                             "Unknown event type: " + type,
                             "eventGroups[].events[].type",
                             "Use \"2D\" or \"3D\"");
                }

                // Check volume range
                double volume = evObj["volume"].toDouble(-1.0);
                if (volume < 0 || volume > 10.0) {
                    addIssue(result, Warning, "Event",
                             "Volume out of range: " + QString::number(volume),
                             "eventGroups[].events[].volume",
                             "Use 0.0 to 10.0 (1.0 = unity)");
                }

                // Check pitch range
                double pitch = evObj["pitch"].toDouble(-1.0);
                if (pitch < 0 || pitch > 24.0) {
                    addIssue(result, Warning, "Event",
                             "Pitch out of range: " + QString::number(pitch),
                             "eventGroups[].events[].pitch",
                             "Use 0.0 to 24.0 semitones (0.0 = no shift)");
                }
            }

            // Recurse into child groups
            collectEvents(group["childGroups"].toArray());
        }
    };

    collectEvents(root["eventGroups"].toArray());
}

void KSAudioValidator::validateBanks(ValidationResult& result, const QJsonObject& root)
{
    QJsonArray banksArr = root["banks"].toArray();
    QSet<QString> bankNames;
    QSet<QString> bankGuids;

    for (const auto& bv : banksArr) {
        QJsonObject bank = bv.toObject();
        QString name = bank["name"].toString();
        QString guid = bank["guid"].toString();

        // Check name
        if (name.isEmpty()) {
            addIssue(result, Error, "Bank", "Bank has empty name",
                     "banks[].name");
        } else if (bankNames.contains(name)) {
            addIssue(result, Error, "Bank", "Duplicate bank name: " + name,
                     "banks[].name");
        }
        bankNames.insert(name);

        // Check GUID
        if (guid.isEmpty()) {
            addIssue(result, Warning, "Bank", "Bank missing GUID: " + name,
                     "banks[].guid");
        } else if (!isValidGuid(guid)) {
            addIssue(result, Error, "Bank", "Invalid bank GUID: " + guid,
                     "banks[].guid");
        } else if (bankGuids.contains(guid)) {
            addIssue(result, Error, "Bank", "Duplicate bank GUID: " + guid,
                     "banks[].guid");
        }
        bankGuids.insert(guid);

        // Check event references
        QJsonArray eventGuids = bank["eventGuids"].toArray();
        if (eventGuids.isEmpty()) {
            addIssue(result, Warning, "Bank",
                     "Bank has no event references: " + name,
                     "banks[].eventGuids",
                     "Add event GUIDs to populate the bank");
        }

        for (const auto& egv : eventGuids) {
            QString eventGuid = egv.toString();
            if (eventGuid.isEmpty()) {
                addIssue(result, Error, "Bank",
                         "Empty event GUID in bank: " + name,
                         "banks[].eventGuids[]");
            } else if (!isValidGuid(eventGuid)) {
                addIssue(result, Error, "Bank",
                         "Invalid event GUID in bank: " + eventGuid,
                         "banks[].eventGuids[]");
            }
        }
    }
}

void KSAudioValidator::validateBuses(ValidationResult& result, const QJsonObject& root)
{
    QJsonArray busesArr = root["buses"].toArray();
    QSet<QString> busNames;

    std::function<void(const QJsonArray&, const QString&)> validateBusArray;
    validateBusArray = [&](const QJsonArray& buses, const QString& parentPath) {
        for (const auto& bv : buses) {
            QJsonObject bus = bv.toObject();
            QString name = bus["name"].toString();
            QString guid = bus["guid"].toString();
            QString path = parentPath + "/" + name;

            if (name.isEmpty()) {
                addIssue(result, Error, "Bus", "Bus has empty name",
                         path + ".name");
            } else if (busNames.contains(name)) {
                addIssue(result, Warning, "Bus",
                         "Duplicate bus name: " + name, path + ".name");
            }
            busNames.insert(name);

            if (!guid.isEmpty() && !isValidGuid(guid)) {
                addIssue(result, Error, "Bus", "Invalid bus GUID: " + guid,
                         path + ".guid");
            }

            double volume = bus["volume"].toDouble(-1.0);
            if (volume < 0 || volume > 10.0) {
                addIssue(result, Warning, "Bus",
                         "Volume out of range: " + QString::number(volume),
                         path + ".volume");
            }

            // Recurse into child buses
            validateBusArray(bus["childBuses"].toArray(), path);
        }
    };

    validateBusArray(busesArr, "buses");
}

void KSAudioValidator::validateSounds(ValidationResult& result, const QJsonObject& root,
                                       const QString& projectDir)
{
    QJsonArray soundsArr = root["sounds"].toArray();
    QSet<QString> soundNames;

    for (const auto& sv : soundsArr) {
        QJsonObject snd = sv.toObject();
        QString name = snd["name"].toString();

        if (name.isEmpty()) {
            addIssue(result, Error, "Sound", "Sound has empty name",
                     "sounds[].name");
        } else if (soundNames.contains(name)) {
            addIssue(result, Warning, "Sound",
                     "Duplicate sound name: " + name, "sounds[].name");
        }
        soundNames.insert(name);

        int sampleRate = snd["sampleRate"].toInt(0);
        if (sampleRate <= 0 || sampleRate > 192000) {
            addIssue(result, Warning, "Sound",
                     "Invalid sample rate: " + QString::number(sampleRate),
                     "sounds[].sampleRate",
                     "Use 8000 to 192000 Hz");
        }

        int channels = snd["channels"].toInt(0);
        if (channels <= 0 || channels > 12) {
            addIssue(result, Warning, "Sound",
                     "Invalid channel count: " + QString::number(channels),
                     "sounds[].channels",
                     "Use 1 (mono) to 12 (7.1.4)");
        }

        // Check if audio file exists
        QString file = snd["file"].toString();
        if (!file.isEmpty() && !projectDir.isEmpty()) {
            QString fullPath = projectDir + "/Assets/audio/" + file;
            if (!QFile::exists(fullPath)) {
                addIssue(result, Warning, "Sound",
                         "Sound file not found: " + file,
                         "sounds[].file",
                         "Ensure the file exists in Assets/audio/");
            }
        }
    }
}

void KSAudioValidator::validateCrossReferences(ValidationResult& result, const QJsonObject& root)
{
    // Collect all event GUIDs defined in eventGroups
    QSet<QString> definedEventGuids;

    std::function<void(const QJsonArray&)> collectGuids;
    collectGuids = [&](const QJsonArray& groups) {
        for (const auto& gv : groups) {
            QJsonObject group = gv.toObject();
            QJsonArray events = group["events"].toArray();
            for (const auto& ev : events) {
                QString guid = ev.toObject()["guid"].toString();
                if (!guid.isEmpty()) definedEventGuids.insert(guid);
            }
            collectGuids(group["childGroups"].toArray());
        }
    };
    collectGuids(root["eventGroups"].toArray());

    // Validate bank event references
    QJsonArray banksArr = root["banks"].toArray();
    for (const auto& bv : banksArr) {
        QJsonObject bank = bv.toObject();
        QJsonArray eventGuids = bank["eventGuids"].toArray();
        for (const auto& egv : eventGuids) {
            QString eventGuid = egv.toString();
            if (!eventGuid.isEmpty() && !definedEventGuids.contains(eventGuid)) {
                addIssue(result, Error, "CrossRef",
                         "Bank references undefined event GUID: " + eventGuid,
                         "banks[].eventGuids[]",
                         "Ensure the event exists in eventGroups");
            }
        }
    }
}

QJsonObject KSAudioValidator::loadJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (doc.isNull() || !doc.isObject()) return QJsonObject();
    return doc.object();
}

}} // namespace ks::fileformat
