#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>
#include "BankParser.h"
#include "BankVersion.h"

namespace ks { namespace fileformat {

// ============================================================================
// IBankParser — Abstract interface for version-specific bank parsers
// ============================================================================

class IBankParser : public QObject {
    Q_OBJECT
public:
    explicit IBankParser(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IBankParser() = default;

    // Parse a bank file and return parsed data
    virtual ParsedBankData parse(const QString& bankPath) = 0;
    virtual ParsedBankData parseFromData(const QByteArray& data) = 0;

    // Check if this parser can handle the given data
    virtual bool canParse(const QByteArray& data) const = 0;

    // Get the version this parser handles
    virtual BankVersion version() const = 0;

    // Get the game target this parser is optimized for
    virtual GameTarget gameTarget() const = 0;

    // Validate a bank file
    virtual bool isValidBank(const QString& bankPath) const = 0;
    virtual bool isEncrypted(const QString& bankPath) const = 0;

    // Extract audio data from parsed bank
    virtual bool extractAudioData(ParsedBankData& bankData) const = 0;

    // Get event/bus/VCA/sound lists
    virtual QStringList getEventPaths(const QString& bankPath) const = 0;
    virtual QStringList getEventNames(const QString& bankPath) const = 0;
    virtual QStringList getBusPaths(const QString& bankPath) const = 0;
    virtual QStringList getVCAPaths(const QString& bankPath) const = 0;

signals:
    void bankParsed(const QString& bankPath, const ParsedBankData& data);
    void parseError(const QString& bankPath, const QString& message);
};

// ============================================================================
// BankParserFactory — Creates appropriate parser for bank version/game
// ============================================================================

class BankParserFactory : public QObject {
    Q_OBJECT
public:
    static BankParserFactory& instance();

    // Register a parser for a specific version
    void registerParser(BankVersion version, IBankParser* parser);
    void registerParser(GameTarget target, IBankParser* parser);

    // Get parser for version (creates if needed)
    IBankParser* getParser(BankVersion version);
    IBankParser* getParser(GameTarget target);

    // Auto-detect and get parser
    IBankParser* getParserForData(const QByteArray& data, const QString& bankName = QString());

    // Get parser for file
    IBankParser* getParserForFile(const QString& bankPath);

    // Unregister parser
    void unregisterParser(BankVersion version);
    void unregisterParser(GameTarget target);

private:
    BankParserFactory(QObject* parent = nullptr) : QObject(parent) {}
    QMap<BankVersion, IBankParser*> m_versionParsers;
    QMap<GameTarget, IBankParser*> m_gameParsers;
};

}} // namespace ks::fileformat