#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QImage>
#include <QPixmap>
#include <QSize>

namespace ks {

enum class ThumbnailSize {
    Small = 0,
    Medium,
    Large,
    XLarge
};

class ThumbnailGenerator : public QObject
{
    Q_OBJECT

public:
    static ThumbnailGenerator* instance();

    explicit ThumbnailGenerator(QObject* parent = nullptr);
    ~ThumbnailGenerator();

    void setDefaultSize(ThumbnailSize size);
    ThumbnailSize getDefaultSize() const { return m_defaultSize; }

    void setOutputDirectory(const QString& dir);
    QString getOutputDirectory() const { return m_outputDir; }

    QImage generateThumbnail(const QString& sourcePath, ThumbnailSize size);

    void generateThumbnailAsync(const QString& sourcePath, ThumbnailSize size);
    void generateThumbnailsAsync(const QStringList& sourcePaths, ThumbnailSize size);

    void setCacheEnabled(bool enabled);
    bool isCacheEnabled() const { return m_cacheEnabled; }

    void clearCache();
    void clearCacheForFile(const QString& sourcePath);

    QString getCachedThumbnail(const QString& sourcePath, ThumbnailSize size) const;
    bool hasCachedThumbnail(const QString& sourcePath, ThumbnailSize size) const;

    static int getSizeForType(ThumbnailSize size);

signals:
    void thumbnailGenerated(const QString& sourcePath, const QImage& image);
    void progressChanged(float progress);
    void error(const QString& error);

private:
    QString cacheKey(const QString& src, ThumbnailSize size) const;

    static ThumbnailGenerator* s_instance;

    ThumbnailSize m_defaultSize = ThumbnailSize::Medium;
    QString m_outputDir;
    bool m_cacheEnabled = true;
};

} // namespace ks
