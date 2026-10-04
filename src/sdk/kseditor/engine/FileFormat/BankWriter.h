#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>

#include "FSPROImporter.h"
#include "../Audio/AudioTypes.h"
#include "BankVersion.h"

namespace ks { namespace fileformat {

class IBankWriter;

using ks::audio::KSAudioProject;
using ks::audio::SoundBank;

// ============================================================================
// KSBankWriter — unified facade for version-aware bank writing
//
// Delegates to IBankWriter implementations based on BankVersion or GameTarget.
// Supports FMOD 1.x (AC1) and FMOD 2.x (ACC/ACR/ACE) formats.
// ============================================================================

class KSBankWriter : public QObject {
    Q_OBJECT
public:
    explicit KSBankWriter(QObject* parent = nullptr);

    // Write a .bank file from a KSAudioProject (auto-detects version)
    bool writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                            const QString& assetsDir = QString());

    // Write a single .bank file from a SoundBank + audio asset directory
    bool writeBank(const SoundBank& bank,
                    const QString& assetsDir,
                    const QString& outputPath);

    // Write GUIDs.txt for the project events
    bool writeGUIDsFile(const KSAudioProject& project, const QString& outputPath);

    // Convert .ksaudio to .bank in one step
    bool convertKSAudioToBank(const QString& ksaudioPath,
                              const QString& assetsDir,
                              const QString& bankOutputPath);

    // Write with explicit version override
    bool writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                            const QString& assetsDir, BankVersion version);
    bool writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                            const QString& assetsDir, GameTarget target);

    QString lastError() const { return m_lastError; }

    // Set default version/target for writing
    void setDefaultVersion(BankVersion version) { m_defaultVersion = version; }
    void setDefaultTarget(GameTarget target) { m_defaultTarget = target; }

signals:
    void writeStarted(const QString& bankName);
    void writeProgress(int percent);
    void writeCompleted(const QString& bankPath);
    void writeFailed(const QString& error);

private:
    QString m_lastError;
    BankVersion m_defaultVersion = BankVersion::FMOD_1_08;
    GameTarget m_defaultTarget = GameTarget::AutoDetect;

    // Get the appropriate writer for the project
    IBankWriter* getWriter(const KSAudioProject& project);
    IBankWriter* getWriter(BankVersion version);
    IBankWriter* getWriter(GameTarget target);

    // Internal structures
    struct FEV2Header {
        quint32 magic = 0x46455632;
        quint32 version = 0x00010800;
        quint32 flags = 0;
    };

    struct EventEntry {
        QByteArray guid;
        quint32 nameIdx = 0, pathIdx = 0;
        quint32 flags = 0;
        quint32 category = 0;
        quint32 maxInstances = 0;
        quint32 length = 0;
        QVector<quint32> paramNameIdxs;
        QVector<float> paramDefaults;
    };

    struct SoundEntry {
        quint32 nameIdx = 0;
        quint32 sampleRate = 44100;
        quint32 channels = 2;
        quint32 length = 0;
        quint32 format = 0;
    };

    // FEV2 chunk writers
    void writeFEV2Header(QDataStream& s, const FEV2Header& hdr);
    void writeSTRTChunk(QDataStream& s, const QByteArray& strTable);
    void writeEVTSChunk(QDataStream& s, const QVector<EventEntry>& events);
    void writeBUSSEntry(QDataStream& s, const FSPROBus& bus, const QMap<QString, quint32>& strMap);
    void writeVCASChunk(QDataStream& s, const QVector<FSPROVCA>& vcas, const QMap<QString, quint32>& strMap);
    void writeSNDSChunk(QDataStream& s, const QVector<SoundEntry>& sounds);

    QByteArray makeGUID(const QString& name);
    bool readAudioData(const QString& filePath, QByteArray& outData, quint32& sampleRate, quint32& channels);
};

}} // namespace ks::fileformat