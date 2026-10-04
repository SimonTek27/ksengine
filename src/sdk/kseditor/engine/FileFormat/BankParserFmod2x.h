#pragma once
#include "BankParserInterface.h"
#include "BankParser.h"
#include <QMap>

namespace ks { namespace fileformat {

// ============================================================================
// BankParserFmod2x — FMOD Studio 2.x parser (ACC, ACR, ACE)
// Handles FEV2 format with different chunk layout, no XOR encryption typically
// ============================================================================

class BankParserFmod2x : public IBankParser {
    Q_OBJECT
public:
    explicit BankParserFmod2x(QObject* parent = nullptr) : IBankParser(parent) {}

    ParsedBankData parse(const QString& bankPath) override;
    ParsedBankData parseFromData(const QByteArray& data) override;

    bool canParse(const QByteArray& data) const override;
    BankVersion version() const override { return BankVersion::FMOD_2_02; }
    GameTarget gameTarget() const override { return GameTarget::AssettoCorsaCompetizione; }

    bool isValidBank(const QString& bankPath) const override;
    bool isEncrypted(const QString& bankPath) const override;

    bool extractAudioData(ParsedBankData& bankData) const override;

    QStringList getEventPaths(const QString& bankPath) const override;
    QStringList getEventNames(const QString& bankPath) const override;
    QStringList getBusPaths(const QString& bankPath) const override;
    QStringList getVCAPaths(const QString& bankPath) const override;

    // Set specific game target for parsing variations
    void setGameTarget(GameTarget target) { m_gameTarget = target; }

private:
    GameTarget m_gameTarget = GameTarget::AssettoCorsaCompetizione;
    QMap<QString, ParsedBankData> m_cache;
    QStringList m_stringTable;

    // FMOD 2.x specific chunk types (some differ from 1.x)
    enum ChunkType : quint32 {
        CHUNK_STRT = 0x53545254,  // "STRT" - String table
        CHUNK_EVTS = 0x45565453,  // "EVTS" - Events
        CHUNK_BUSS = 0x42555353,  // "BUSS" - Buses
        CHUNK_VCAS = 0x56434153,  // "VCAS" - VCAs
        CHUNK_SNAP = 0x534E4150,  // "SNAP" - Snapshots
        CHUNK_SNDS = 0x534E4453,  // "SNDS" - Sounds
        // FMOD 2.x additional chunks
        CHUNK_PLUG = 0x504C5547,  // "PLUG" - Plugins
        CHUNK_MIXR = 0x4D495852,  // "MIXR" - Mixer
        CHUNK_BANK = 0x4B4E4142,  // "BANK" - Bank info
    };

    bool parseFEV2(const QByteArray& data, ParsedBankData& out);
    void readChunkStringTable(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkEvents(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkBuses(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkVCAs(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkSnapshots(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkSounds(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkPlugins(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkMixer(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkBankInfo(QDataStream& s, ParsedBankData& out, quint32 size);

    bool parseFSB5Data(const QByteArray& bankData, ParsedBankData& out) const;

    QString stringAt(quint32 idx) const;
    static QString formatGUID(const quint8 bytes[16]);
    QByteArray decryptData(const QByteArray& data, quint32 key) const;
};

}} // namespace ks::fileformat