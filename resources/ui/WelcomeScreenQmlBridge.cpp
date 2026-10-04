#include "WelcomeScreenQmlBridge.h"
#include "modules/help/HelpSystem.h"
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>

WelcomeScreenQmlBridge::WelcomeScreenQmlBridge(QObject* parent)
    : QObject(parent)
    , m_settings(QGuiApplication::organizationName(), QGuiApplication::applicationName())
{
    loadRecentProjects();
}

void WelcomeScreenQmlBridge::launchApp(const QString& mode) {
    m_launchMode = mode;
    emit completed();
}

void WelcomeScreenQmlBridge::openRecent(const QString& path) {
    if (path.isEmpty() || !QFileInfo(path).exists()) return;
    m_recentProjectPath = path;
    emit completed();
}

void WelcomeScreenQmlBridge::openHelp() {
    emit helpRequested();
    ks::HelpSystem::instance()->showHelp();
}

void WelcomeScreenQmlBridge::loadRecentProjects() {
    m_recentProjects.clear();
    QStringList recent = m_settings.value("recentProjects").toStringList();

    for (const QString& path : recent) {
        QFileInfo info(path);
        if (!info.exists()) continue;

        QString displayName = info.fileName();
        QDir parentDir = info.dir();
        QString parentName = parentDir.dirName();
        QString category;
        if (parentName == "cars") { displayName += "  [Car]"; category = "car"; }
        else if (parentName == "tracks") { displayName += "  [Track]"; category = "track"; }
        else if (parentName == "driver") { displayName += "  [Character]"; category = "character"; }
        else if (parentName == "sfx") { displayName += "  [Sound]"; category = "sound"; }
        else { category = "folder"; }

        m_recentProjects.append(QVariantMap{
            { "displayName", displayName },
            { "path", path },
            { "category", category }
        });
    }
}
