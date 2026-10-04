#pragma once
#include "BankParserInterface.h"
#include "BankParser.h"
#include <QMap>

namespace ks { namespace fileformat {

// ============================================================================
// BankParserFmod1x — FMOD Studio 1.x parser (AC1)
// Handles FEV2 format with XOR encryption
// ============================================================================

class BankParserFmod1x : public IBankParser {
    Q_OBJECT
public:
    explicit BankParserFmod1x(QObject* parent = nullptr) : IBankParser(parent) {}

    ParsedBankData parse(const QString& bankPath) override;
    ParsedBankData parseFromData(const QByteArray& data) override;

    bool canParse(const QByteArray& data) const override;
    BankVersion version() const override { return BankVersion::FMOD_1_08; }
    GameTarget gameTarget() const override { return GameTarget::AssettoCorsa1; }

    bool isValidBank(const QString& bankPath) const override;
    bool isEncrypted(const QString& bankPath) const override;

    bool extractAudioData(ParsedBankData& bankData) const override;

    QStringList getEventPaths(const QString& bankPath) const override;
    QStringList getEventNames(const QString& bankPath) const override;
    QStringList getBusPaths(const QString& bankPath) const override;
    QStringList getVCAPaths(const QString& bankPath) const override;

private:
    // Reuse the existing parsing logic from BankParser
    // We'll delegate to the original implementation
    QMap<QString, ParsedBankData> m_cache;
    QStringList m_stringTable;

    bool parseFEV2(const QByteArray& data, ParsedBankData& out);
    void readChunkStringTable(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkEvents(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkBuses(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkVCAs(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkSnapshots(QDataStream& s, ParsedBankData& out, quint32 size);
    void readChunkSounds(QDataStream& s, ParsedBankData& out, quint32 size);

    bool parseFSB5Data(const QByteArray& bankData, ParsedBankData& out) const;

    QString stringAt(quint32 idx) const;
    static QString formatGUID(const quint8 bytes[16]);
    QByteArray decryptData(const QByteArray& data, quint32 key) const;
};

}} // namespace ks::fileformat