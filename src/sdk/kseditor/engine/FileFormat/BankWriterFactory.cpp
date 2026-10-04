#include "BankWriterInterface.h"
#include <QDebug>

namespace ks { namespace fileformat {

BankWriterFactory& BankWriterFactory::instance() {
    static BankWriterFactory factory;
    return factory;
}

void BankWriterFactory::registerWriter(BankVersion version, IBankWriter* writer) {
    if (writer) {
        m_versionWriters[version] = writer;
        qDebug() << "Registered bank writer for version:" << BankVersionManager::versionToString(version);
    }
}

void BankWriterFactory::registerWriter(GameTarget target, IBankWriter* writer) {
    if (writer) {
        m_gameWriters[target] = writer;
        qDebug() << "Registered bank writer for game:" << BankVersionManager::gameTargetToString(target);
    }
}

IBankWriter* BankWriterFactory::getWriter(BankVersion version) {
    auto it = m_versionWriters.find(version);
    if (it != m_versionWriters.end()) {
        return it.value();
    }
    return nullptr;
}

IBankWriter* BankWriterFactory::getWriter(GameTarget target) {
    auto it = m_gameWriters.find(target);
    if (it != m_gameWriters.end()) {
        return it.value();
    }
    return nullptr;
}

void BankWriterFactory::unregisterWriter(BankVersion version) {
    m_versionWriters.remove(version);
}

void BankWriterFactory::unregisterWriter(GameTarget target) {
    m_gameWriters.remove(target);
}

}} // namespace ks::fileformat