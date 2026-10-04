#include "BankParserInterface.h"
#include "BankVersion.h"
#include "BankParser.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>

namespace ks { namespace fileformat {

// ============================================================================
// BankParserFactory implementation
// ============================================================================

BankParserFactory& BankParserFactory::instance() {
    static BankParserFactory factory;
    return factory;
}

void BankParserFactory::registerParser(BankVersion version, IBankParser* parser) {
    if (parser) {
        m_versionParsers[version] = parser;
        qDebug() << "Registered bank parser for version:" << BankVersionManager::versionToString(version);
    }
}

void BankParserFactory::registerParser(GameTarget target, IBankParser* parser) {
    if (parser) {
        m_gameParsers[target] = parser;
        qDebug() << "Registered bank parser for game:" << BankVersionManager::gameTargetToString(target);
    }
}

IBankParser* BankParserFactory::getParser(BankVersion version) {
    auto it = m_versionParsers.find(version);
    if (it != m_versionParsers.end()) {
        return it.value();
    }
    return nullptr;
}

IBankParser* BankParserFactory::getParser(GameTarget target) {
    auto it = m_gameParsers.find(target);
    if (it != m_gameParsers.end()) {
        return it.value();
    }
    return nullptr;
}

IBankParser* BankParserFactory::getParserForData(const QByteArray& data, const QString& bankName) {
    // First try game-specific detection
    GameTarget target = BankVersionManager::detectGameTarget(data, bankName);
    if (target != GameTarget::AutoDetect) {
        IBankParser* parser = getParser(target);
        if (parser && parser->canParse(data)) {
            return parser;
        }
    }

    // Fall back to version detection
    BankVersion version = BankVersionManager::detectVersion(data);
    if (version != BankVersion::Unknown) {
        IBankParser* parser = getParser(version);
        if (parser && parser->canParse(data)) {
            return parser;
        }
    }

    // Last resort: try all registered parsers
    for (auto parser : m_versionParsers) {
        if (parser && parser->canParse(data)) {
            return parser;
        }
    }
    for (auto parser : m_gameParsers) {
        if (parser && parser->canParse(data)) {
            return parser;
        }
    }

    return nullptr;
}

IBankParser* BankParserFactory::getParserForFile(const QString& bankPath) {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly)) return nullptr;

    QByteArray header = file.read(16);
    file.close();

    QString bankName = QFileInfo(bankPath).baseName();
    return getParserForData(header, bankName);
}

void BankParserFactory::unregisterParser(BankVersion version) {
    m_versionParsers.remove(version);
}

void BankParserFactory::unregisterParser(GameTarget target) {
    m_gameParsers.remove(target);
}

}} // namespace ks::fileformat