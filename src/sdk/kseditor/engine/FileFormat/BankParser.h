#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QList>
#include "BankVersion.h"

namespace ks { namespace fileformat {

struct BankEventInfo {
    QString  guid;
    QString  name;
    QString  path;
    quint32  flags       = 0;
    quint32  category    = 0;
    int      parameters  = 0;
    quint32  maxInstances = 0;
    quint32  length      = 0;
    QStringList parameterNames;
    QVector<float> parameterDefaults;
};

struct BankBusInfo {
    QString  name, path;
    float    volume = 1.0f;
    bool     muted  = false;
    bool     solo   = false;
    QStringList childBuses;
    QStringList linkedVCAs;
};

struct BankVCAInfo {
    QString name, path;
    float   volume = 1.0f;
};

struct BankSnapshotInfo {
    QString name, path;
};

struct BankSoundInfo {
    QString  name;
    quint32  sampleRate = 44100;
    quint32  channels   = 1;
    quint32  length     = 0;
    quint32  format     = 0;

    QVector<float> samples;
    quint32  dataOffset = 0;
    quint32  dataSize   = 0;
    bool     hasAudioData = false;
};

struct ParsedBankData {
    bool     isValid     = false;
    bool     isEncrypted = false;
    bool     isLegacy    = false;
    quint32  version     = 0;
    quint32  size        = 0;
    QString  name;
    QString  filePath;
    BankVersion detectedVersion = BankVersion::Unknown;
    GameTarget detectedGame = GameTarget::AutoDetect;
    QList<BankEventInfo>    events;
    QList<BankBusInfo>      buses;
    QList<BankVCAInfo>      vcas;
    QList<BankSnapshotInfo> snapshots;
    QList<BankSoundInfo>    sounds;
};

// ============================================================================
// KSBankParser — Unified facade for version-aware bank parsing
// ============================================================================

class KSBankParser : public QObject {
    Q_OBJECT
public:
    explicit KSBankParser(QObject* parent = nullptr);

    // Main parsing API
    ParsedBankData parse(const QString& bankPath);
    ParsedBankData parseFromData(const QByteArray& data);

    // Parse with explicit version/game target (for testing/override)
    ParsedBankData parseWithVersion(const QString& bankPath, BankVersion version);
    ParsedBankData parseWithGameTarget(const QString& bankPath, GameTarget target);

    // Validation
    bool isValidBank(const QString& bankPath) const;
    bool isEncrypted(const QString& bankPath) const;

    // Query helpers
    QStringList getEventPaths(const QString& bankPath) const;
    QStringList getEventNames(const QString& bankPath) const;
    QStringList getBusPaths(const QString& bankPath) const;
    QStringList getVCAPaths(const QString& bankPath) const;

    // Version detection
    static BankVersion detectVersion(const QByteArray& data);
    static GameTarget detectGameTarget(const QByteArray& data, const QString& bankName = QString());

    // Cache management
    void clearCache();

    // Audio extraction
    bool extractAudioData(ParsedBankData& bankData) const;

    // FSB5 format constants (kept for compatibility)
    enum FSB5Format : quint32 {
        FSB5_PCM8   = 0,
        FSB5_PCM16  = 1,
        FSB5_PCM24  = 2,
        FSB5_PCM32  = 3,
        FSB5_FLOAT  = 4,
        FSB5_ADPCM  = 5,
        FSB5_VORBIS = 6
    };

signals:
    void bankParsed(const QString& bankPath, const ParsedBankData& data);
    void parseError(const QString& bankPath, const QString& message);

private:
    QMap<QString, ParsedBankData> m_cache;
};

}} // namespace ks::fileformat