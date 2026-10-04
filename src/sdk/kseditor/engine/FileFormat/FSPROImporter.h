#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>

class QXmlStreamReader;

namespace ks { namespace fileformat {

// ============================================================================
// Data structures mirroring FMOD Studio 1.08.12 .fspro entities
// ============================================================================

struct FSPROSound {
    QString guid, name, filePath, format;
    quint32 sampleRate = 44100;
    quint32 channels = 2;
    qint64  loopStart = 0, loopEnd = 0;
    QString compression;
};

struct FSPROParameter {
    QString guid, name, type = "float", scope = "global", unit;
    float minVal = 0.0f, maxVal = 1.0f, defaultVal = 0.5f;
};

struct FSPROAction {
    QString type;              // "play", "stop", "setParameter", etc.
    QString soundGuid;
    QString parameterGuid;
    float   parameterValue = 0.0f;
};

struct FSPROKeyframe {
    float     time = 0.0f;
    QString   id;
    FSPROAction action;
};

struct FSPROTrack {
    QString name, id;
    QVector<FSPROKeyframe> keyframes;
};

struct FSPROTimeline {
    QVector<FSPROTrack> tracks;
};

struct FSPROEvent {
    QString guid, name, typeStr = "2D";
    quint32 maxInstances = 0;
    int     priority = 0;
    FSPROTimeline timeline;
    bool    spatializationEnabled = true;
    float   panLevel = 1.0f;
    QString speakerMode = "auto";
    float   attenuationMin = 1.0f, attenuationMax = 100.0f;
    QString attenuationModel = "inverse";
    QVector<FSPROParameter> localParameters;
};

struct FSPROEventGroup {
    QString guid, name;
    QVector<FSPROEvent> events;
    QVector<FSPROEventGroup> childGroups;
};

struct FSPROEffect {
    QString name, type;
    bool enabled = true;
    QMap<QString, float> parameters;
};

struct FSPROBus {
    QString guid, name;
    float   volume = 1.0f;
    bool    mute = false, solo = false;
    QVector<FSPROBus> childBuses;
    QStringList linkedVCAs;
    QVector<FSPROEffect> effects;
};

struct FSPROVCA {
    QString guid, name;
    float   volume = 1.0f;
    QStringList objects;
};

struct FSPROSnapshot {
    QString guid, name, type;
    float strength = 1.0f;
    QMap<QString, float> busVolumes;
};

struct FSPROMixerSnapshot {
    QString guid, name;
    QMap<QString, float> busVolumes;  // bus guid -> volume
};

struct FSPROMixer {
    QVector<FSPROMixerSnapshot> snapshots;
};

struct FSPROBank {
    QString   guid, name;
    QStringList eventGuids;
};

struct FSPROProject {
    QString name, guid;
    QVector<FSPROEventGroup> eventGroups;
    QVector<FSPROBus> buses;
    QVector<FSPROVCA> vcas;
    QVector<FSPROSnapshot> snapshots;
    QVector<FSPROParameter> globalParameters;
    QVector<FSPROSound> sounds;
    QVector<FSPROBank> banks;
    FSPROMixer mixer;
};

// ============================================================================
// KSFSPROImporter - reads FMOD Studio .fspro XML → .ksaudio JSON
// ============================================================================

class KSFSPROImporter : public QObject {
    Q_OBJECT
public:
    explicit KSFSPROImporter(QObject* parent = nullptr);

    // Parse an .fspro file into a project object
    FSPROProject parseFile(const QString& fsproPath);
    FSPROProject parseData(const QByteArray& xmlData);

    // Convert an FSPROProject to .ksaudio JSON (v2 schema)
    QJsonObject toKSAudioJson(const FSPROProject& project);

    // Convenience: read .fspro, write .ksaudio in one call
    bool convertFile(const QString& fsproPath, const QString& ksaudioPath);

    // Resolve referenced sounds relative to the .fspro directory
    void setAssetBasePath(const QString& path) { m_assetBasePath = path; }
    QString assetBasePath() const { return m_assetBasePath; }

    QString lastError() const { return m_lastError; }

signals:
    void parseStarted(const QString& path);
    void parseProgress(int percent);
    void parseCompleted(const QString& ksaudioPath);
    void parseFailed(const QString& error);

private:
    QString m_assetBasePath;
    QString m_lastError;

    FSPROProject parseXml(const QByteArray& xml);
    void parseStudioProject(FSPROProject& proj);
    void parseEventGroup(FSPROEventGroup& group);
    void parseEvent(FSPROEvent& ev);
    void parseTimeline(FSPROTimeline& tl);
    void parseTrack(FSPROTrack& track);
    void parseKeyframe(FSPROKeyframe& kf);
    void parseAction(FSPROAction& action, const QString& actionType);
    void parseBus(FSPROBus& bus);
    void parseVCA(FSPROVCA& vca);
    void parseSnapshot(FSPROSnapshot& snap);
    void parseParameter(FSPROParameter& param);
    void parseSound(FSPROSound& sound);
    void parseBank(FSPROBank& bank);
    void parseMixer(FSPROMixer& mixer);

    // XML reader state (valid during parseData)
    QXmlStreamReader* m_xml = nullptr;

    // Helper: read a GUID attribute
    QString readGuid(const QString& attrName = "guid") const;
    // Helper: read a property element's value by name
    QString readProperty(const QString& name) const;
};

}} // namespace ks::fileformat
