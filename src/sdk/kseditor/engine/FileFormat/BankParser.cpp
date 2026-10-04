#include "BankParser.h"
#include "BankParserInterface.h"
#include "BankParserFactory.h"
#include "BankVersion.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>

namespace ks { namespace fileformat {

KSBankParser::KSBankParser(QObject* parent) : QObject(parent) {}

ParsedBankData KSBankParser::parse(const QString& bankPath) {
    if (m_cache.contains(bankPath))
        return m_cache[bankPath];

    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly)) {
        ParsedBankData result;
        emit parseError(bankPath, "Cannot open file: " + bankPath);
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    ParsedBankData result = parseFromData(data);
    result.filePath = bankPath;
    result.name = QFileInfo(bankPath).completeBaseName();

    if (result.isValid) {
        m_cache[bankPath] = result;
        emit bankParsed(bankPath, result);
    }
    return result;
}

ParsedBankData KSBankParser::parseFromData(const QByteArray& data) {
    // Auto-detect version and game target
    BankVersion version = BankVersionManager::detectVersion(data);
    GameTarget target = BankVersionManager::detectGameTarget(data);

    // Get appropriate parser from factory
    IBankParser* parser = BankParserFactory::instance().getParserForData(data);
    if (!parser) {
        ParsedBankData result;
        emit parseError("", "No parser available for bank format");
        return result;
    }

    ParsedBankData result = parser->parseFromData(data);
    result.detectedVersion = version;
    result.detectedGame = target;
    return result;
}

ParsedBankData KSBankParser::parseWithVersion(const QString& bankPath, BankVersion version) {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly)) {
        ParsedBankData result;
        emit parseError(bankPath, "Cannot open file: " + bankPath);
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    IBankParser* parser = BankParserFactory::instance().getParser(version);
    if (!parser) {
        ParsedBankData result;
        emit parseError(bankPath, QString("No parser registered for version %1")
            .arg(BankVersionManager::versionToString(version)));
        return result;
    }

    ParsedBankData result = parser->parseFromData(data);
    result.filePath = bankPath;
    result.name = QFileInfo(bankPath).completeBaseName();
    result.detectedVersion = version;
    result.detectedGame = BankVersionManager::detectGameTarget(data);

    if (result.isValid) {
        m_cache[bankPath] = result;
        emit bankParsed(bankPath, result);
    }
    return result;
}

ParsedBankData KSBankParser::parseWithGameTarget(const QString& bankPath, GameTarget target) {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly)) {
        ParsedBankData result;
        emit parseError(bankPath, "Cannot open file: " + bankPath);
        return result;
    }

    QByteArray data = file.readAll();
    file.close();

    IBankParser* parser = BankParserFactory::instance().getParser(target);
    if (!parser) {
        ParsedBankData result;
        emit parseError(bankPath, QString("No parser registered for game %1")
            .arg(BankVersionManager::gameTargetToString(target)));
        return result;
    }

    ParsedBankData result = parser->parseFromData(data);
    result.filePath = bankPath;
    result.name = QFileInfo(bankPath).completeBaseName();
    result.detectedVersion = BankVersionManager::detectVersion(data);
    result.detectedGame = target;

    if (result.isValid) {
        m_cache[bankPath] = result;
        emit bankParsed(bankPath, result);
    }
    return result;
}

bool KSBankParser::isValidBank(const QString& bankPath) const {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 8) return false;
    quint32 magic = 0;
    file.read(reinterpret_cast<char*>(&magic), 4);
    return magic == 0x46455632 || magic == 0x52494646;  // FEV2 or RIFF
}

bool KSBankParser::isEncrypted(const QString& bankPath) const {
    QFile file(bankPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 12) return false;
    file.seek(8);
    quint32 flags = 0;
    file.read(reinterpret_cast<char*>(&flags), 4);
    return (flags & 0x1) != 0;
}

QStringList KSBankParser::getEventPaths(const QString& bankPath) const {
    ParsedBankData data = const_cast<KSBankParser*>(this)->parse(bankPath);
    QStringList paths;
    for (const auto& e : data.events) paths << e.path;
    return paths;
}

QStringList KSBankParser::getEventNames(const QString& bankPath) const {
    ParsedBankData data = const_cast<KSBankParser*>(this)->parse(bankPath);
    QStringList names;
    for (const auto& e : data.events) names << e.name;
    return names;
}

QStringList KSBankParser::getBusPaths(const QString& bankPath) const {
    ParsedBankData data = const_cast<KSBankParser*>(this)->parse(bankPath);
    QStringList paths;
    for (const auto& b : data.buses) paths << b.path;
    return paths;
}

QStringList KSBankParser::getVCAPaths(const QString& bankPath) const {
    ParsedBankData data = const_cast<KSBankParser*>(this)->parse(bankPath);
    QStringList paths;
    for (const auto& v : data.vcas) paths << v.path;
    return paths;
}

BankVersion KSBankParser::detectVersion(const QByteArray& data) {
    return BankVersionManager::detectVersion(data);
}

GameTarget KSBankParser::detectGameTarget(const QByteArray& data, const QString& bankName) {
    return BankVersionManager::detectGameTarget(data, bankName);
}

void KSBankParser::clearCache() {
    m_cache.clear();
}

bool KSBankParser::extractAudioData(ParsedBankData& bankData) const {
    // Delegate to the appropriate parser based on detected version
    IBankParser* parser = BankParserFactory::instance().getParser(bankData.detectedVersion);
    if (parser) {
        return parser->extractAudioData(bankData);
    }
    return false;
}

}} // namespace ks::fileformat