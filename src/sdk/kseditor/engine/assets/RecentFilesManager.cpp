#include "RecentFilesManager.h"

#include <QSettings>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

namespace ks {

RecentFilesManager* RecentFilesManager::s_instance = nullptr;

RecentFilesManager* RecentFilesManager::instance()
{
    if (!s_instance)
        s_instance = new RecentFilesManager();
    return s_instance;
}

RecentFilesManager::RecentFilesManager(QObject* parent)
    : QObject(parent)
{
    load();
}

RecentFilesManager::~RecentFilesManager()
{
    save();
    s_instance = nullptr;
}

void RecentFilesManager::addFile(const QString& path, const QString& category)
{
    // Remove existing entry for same path
    m_files.removeIf([&](const RecentEntry& e){ return e.path == path; });

    RecentEntry e;
    e.path       = path;
    e.name       = QFileInfo(path).fileName();
    e.category   = category;
    e.lastOpened = QDateTime::currentDateTime();
    e.openCount  = 1;

    m_files.prepend(e);

    // Trim to max
    while (m_files.size() > m_maxFiles)
        m_files.removeLast();

    save();
    emit recentFilesChanged(getFiles());
}

void RecentFilesManager::removeFile(const QString& path)
{
    int before = m_files.size();
    m_files.removeIf([&](const RecentEntry& e){ return e.path == path; });
    if (m_files.size() != before) {
        save();
        emit recentFilesChanged(getFiles());
    }
}

void RecentFilesManager::clear()
{
    m_files.clear();
    save();
    emit recentFilesChanged({});
}

QVector<RecentFilesManager::RecentEntry> RecentFilesManager::getFiles(const QString& category) const
{
    if (category.isEmpty()) return m_files;
    QVector<RecentEntry> out;
    for (const auto& e : m_files)
        if (e.category == category) out << e;
    return out;
}

QStringList RecentFilesManager::getPaths(const QString& category) const
{
    QStringList out;
    for (const auto& e : getFiles(category))
        out << e.path;
    return out;
}

bool RecentFilesManager::contains(const QString& path) const
{
    for (const auto& e : m_files)
        if (e.path == path) return true;
    return false;
}

void RecentFilesManager::purgeNonexistent()
{
    int before = m_files.size();
    m_files.removeIf([](const RecentEntry& e){ return !QFileInfo::exists(e.path); });
    if (m_files.size() != before) {
        save();
        emit recentFilesChanged(getFiles());
    }
}

void RecentFilesManager::setMaxFiles(int max)
{
    m_maxFiles = qMax(1, max);
    while (m_files.size() > m_maxFiles) m_files.removeLast();
}

void RecentFilesManager::save() const
{
    QSettings s("kseditor", "kseditor");
    s.beginGroup("RecentFiles");
    s.remove(""); // clear group
    s.setValue("count", m_files.size());
    for (int i = 0; i < m_files.size(); ++i) {
        s.setValue(QString("path_%1").arg(i),     m_files[i].path);
        s.setValue(QString("name_%1").arg(i),     m_files[i].name);
        s.setValue(QString("cat_%1").arg(i),      m_files[i].category);
        s.setValue(QString("opened_%1").arg(i),   m_files[i].lastOpened.toString(Qt::ISODate));
        s.setValue(QString("count_%1").arg(i),    m_files[i].openCount);
    }
    s.endGroup();
}

void RecentFilesManager::load()
{
    QSettings s("kseditor", "kseditor");
    s.beginGroup("RecentFiles");
    int count = s.value("count", 0).toInt();
    m_files.clear();
    for (int i = 0; i < count && i < m_maxFiles; ++i) {
        RecentEntry e;
        e.path       = s.value(QString("path_%1").arg(i)).toString();
        e.name       = s.value(QString("name_%1").arg(i)).toString();
        e.category   = s.value(QString("cat_%1").arg(i)).toString();
        e.lastOpened = QDateTime::fromString(
            s.value(QString("opened_%1").arg(i)).toString(), Qt::ISODate);
        e.openCount  = s.value(QString("count_%1").arg(i), 1).toInt();
        if (!e.path.isEmpty()) m_files << e;
    }
    s.endGroup();
}

} // namespace ks
