#pragma once
#include "BankWriterInterface.h"
#include "BankWriter.h"
#include "FSPROImporter.h"
#include "../Audio/AudioTypes.h"
#include <QMap>
#include <QJsonObject>
#include <QUuid>
#include <QCryptographicHash>

namespace ks { namespace fileformat {

using ks::audio::KSAudioProject;
using ks::audio::SoundBank;

// ============================================================================
// BankWriterFmod1x — FMOD Studio 1.x writer (AC1)
// Uses XOR encryption, standard FEV2 chunks
// ============================================================================

class BankWriterFmod1x : public IBankWriter {
    Q_OBJECT
public:
    explicit BankWriterFmod1x(QObject* parent = nullptr) : IBankWriter(parent) {}

    bool writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                           const QString& assetsDir = QString()) override;
    bool writeBank(const SoundBank& bank, const QString& assetsDir,
                   const QString& outputPath) override;
    bool writeGUIDsFile(const KSAudioProject& project, const QString& outputPath) override;
    bool convertKSAudioToBank(const QString& ksaudioPath, const QString& assetsDir,
                              const QString& bankOutputPath) override;

    QString lastError() const override { return m_lastError; }
    BankVersion version() const override { return BankVersion::FMOD_1_08; }
    GameTarget gameTarget() const override { return GameTarget::AssettoCorsa1; }

    // Encryption settings
    void setEncryptionEnabled(bool enabled) { m_encrypt = enabled; }
    void setEncryptionKey(quint32 key) { m_xorKey = key; }

signals:
    void writeStarted(const QString& bankName);
    void writeProgress(int percent);
    void writeCompleted(const QString& bankPath);
    void writeFailed(const QString& error);

private:
    QString m_lastError;
    bool m_encrypt = true;
    quint32 m_xorKey = 0xAE12B3F4;  // AC1 default

    // Internal structures (from BankWriter)
    struct FEV2Header {
        quint32 magic = 0x46455632;
        quint32 version = 0x00010800;
        quint32 flags = 0;
    };

    struct EventEntry {
        QByteArray guid;
        quint32 nameIdx, pathIdx;
        quint32 flags = 0;
        quint32 category = 0;
        quint32 maxInstances = 0;
        quint32 length = 0;
        QVector<quint32> paramNameIdxs;
        QVector<float> paramDefaults;
    };

    struct SoundEntry {
        quint32 nameIdx;
        quint32 sampleRate = 44100;
        quint32 channels = 2;
        quint32 length = 0;
        quint32 format = 0;
    };

    // FSPRO structures — reuse the canonical types from FSPROImporter.h

    // Write methods
    void writeFEV2Header(QDataStream& s, const FEV2Header& hdr);
    void writeSTRTChunk(QDataStream& s, const QByteArray& strTable);
    void writeEVTSChunk(QDataStream& s, const QVector<EventEntry>& events);
    void writeBUSSEntry(QDataStream& s, const FSPROBus& bus, const QMap<QString, quint32>& strMap);
    void writeVCASChunk(QDataStream& s, const QVector<FSPROVCA>& vcas, const QMap<QString, quint32>& strMap);
    void writeSNDSChunk(QDataStream& s, const QVector<SoundEntry>& sounds);

    QByteArray makeGUID(const QString& name);
    bool readAudioData(const QString& filePath, QByteArray& outData, quint32& sampleRate, quint32& channels);

    QByteArray encryptData(const QByteArray& data) const;
    QMap<QString, quint32> buildStringTable(const FSPROProject& proj, QByteArray& strTableData);
};

}} // namespace ks::fileformat