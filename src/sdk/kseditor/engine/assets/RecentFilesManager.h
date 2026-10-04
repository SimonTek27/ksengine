#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>

namespace ks {

class RecentFilesManager : public QObject
{
    Q_OBJECT

public:
    static RecentFilesManager* instance();

    void addFile(const QString& path, const QString& category = QString());
    void removeFile(const QString& path);
    void clear();

    struct RecentEntry {
        QString path;
        QString name;
        QString category;
        QDateTime lastOpened;
        int openCount = 0;
    };

    QVector<RecentEntry> getFiles(const QString& category = QString()) const;
    QStringList getPaths(const QString& category = QString()) const;

    bool contains(const QString& path) const;
    void purgeNonexistent();

    void setMaxFiles(int max);
    int getMaxFiles() const { return m_maxFiles; }

signals:
    void recentFilesChanged(const QVector<RecentEntry>& files);

private:
    RecentFilesManager(QObject* parent = nullptr);
    ~RecentFilesManager();
    Q_DISABLE_COPY(RecentFilesManager)

    void save() const;
    void load();

    static RecentFilesManager* s_instance;

    int m_maxFiles = 20;
    QVector<RecentEntry> m_files;
};

} // namespace ks
