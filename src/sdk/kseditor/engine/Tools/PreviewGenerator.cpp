#include "PreviewGenerator.h"

#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QThreadPool>
#include <QRunnable>
#include <QDebug>

namespace ks {

static int sizeForType(ThumbnailSize size)
{
    switch (size) {
    case ThumbnailSize::Small:     return 64;
    case ThumbnailSize::Medium:    return 128;
    case ThumbnailSize::Large:     return 256;
    case ThumbnailSize::XLarge:    return 512;
    default:                       return 128;
    }
}

ThumbnailGenerator* ThumbnailGenerator::s_instance = nullptr;

ThumbnailGenerator* ThumbnailGenerator::instance()
{
    if (!s_instance) s_instance = new ThumbnailGenerator();
    return s_instance;
}

ThumbnailGenerator::ThumbnailGenerator(QObject* parent)
    : QObject(parent)
{
    m_outputDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                  + "/thumbnails";
    QDir().mkpath(m_outputDir);
}

ThumbnailGenerator::~ThumbnailGenerator() { s_instance = nullptr; }

void ThumbnailGenerator::setDefaultSize(ThumbnailSize size) { m_defaultSize = size; }
void ThumbnailGenerator::setOutputDirectory(const QString& dir) { m_outputDir = dir; QDir().mkpath(dir); }
void ThumbnailGenerator::setCacheEnabled(bool enabled) { m_cacheEnabled = enabled; }

int ThumbnailGenerator::getSizeForType(ThumbnailSize size) { return sizeForType(size); }

QString ThumbnailGenerator::cacheKey(const QString& src, ThumbnailSize size) const
{
    QString raw = src + QString::number(static_cast<int>(size));
    return QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex();
}

QString ThumbnailGenerator::getCachedThumbnail(const QString& src, ThumbnailSize size) const
{
    QString path = QDir(m_outputDir).filePath(cacheKey(src, size) + ".png");
    return QFile::exists(path) ? path : QString();
}

bool ThumbnailGenerator::hasCachedThumbnail(const QString& src, ThumbnailSize size) const
{
    return !getCachedThumbnail(src, size).isEmpty();
}

void ThumbnailGenerator::clearCache()
{
    for (const auto& fi : QDir(m_outputDir).entryInfoList({"*.png"}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());
}

void ThumbnailGenerator::clearCacheForFile(const QString& src)
{
    for (int i = 0; i < 4; ++i)
        QFile::remove(QDir(m_outputDir).filePath(
            cacheKey(src, static_cast<ThumbnailSize>(i)) + ".png"));
}

void ThumbnailGenerator::generateThumbnailAsync(const QString& src, ThumbnailSize size)
{
    // Check cache first
    if (m_cacheEnabled && hasCachedThumbnail(src, size)) {
        QImage img(getCachedThumbnail(src, size));
        emit thumbnailGenerated(src, img);
        return;
    }

    // Run generation in thread pool
    auto* gen = this;
    QThreadPool::globalInstance()->start([gen, src, size]() {
        int px = sizeForType(size);
        QImage result(px, px, QImage::Format_ARGB32);
        result.fill(Qt::transparent);

        QFileInfo fi(src);
        QString ext = fi.suffix().toLower();

        if (ext == "png" || ext == "jpg" || ext == "jpeg" ||
            ext == "dds" || ext == "bmp" || ext == "tga") {
            // Load image directly
            QImage src_img(src);
            if (!src_img.isNull())
                result = src_img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } else {
            // Generate placeholder icon with file extension label
            int m = px / 12;
            QPainter p(&result);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::TextAntialiasing);

            // Document body
            QRectF body(m, m * 1.5, px - m * 2, px - m * 2.5);
            p.setBrush(QColor(50, 52, 62));
            p.setPen(QPen(QColor(70, 72, 82), 1.5));
            p.drawRoundedRect(body, m / 2, m / 2);

            // Top color bar (file type indicator)
            QRectF bar(m, m * 1.5, px - m * 2, m * 1.5);
            QLinearGradient grad(0, 0, 0, bar.height());
            grad.setColorAt(0, QColor(80, 120, 200));
            grad.setColorAt(1, QColor(60, 90, 170));
            p.setBrush(grad);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(bar, m / 2, m / 2);
            p.drawRect(QRectF(m, m * 1.5 + m / 2, px - m * 2, m));

            // Extension label
            p.setPen(QColor(180, 185, 200));
            QFont extFont("Segoe UI", px / 6, QFont::Bold);
            p.setFont(extFont);
            p.drawText(QRectF(m, m * 3.5, px - m * 2, px - m * 5),
                       Qt::AlignCenter, ext.toUpper());

            // Bottom-hint: unsupported file
            p.setPen(QColor(100, 102, 115));
            QFont hintFont("Segoe UI", px / 14);
            p.setFont(hintFont);
            p.drawText(QRectF(m, px - m * 3.5, px - m * 2, m * 2),
                       Qt::AlignCenter, "no preview");
        }

        // Cache to disk
        if (gen->m_cacheEnabled) {
            QString cachePath = QDir(gen->m_outputDir).filePath(
                gen->cacheKey(src, size) + ".png");
            result.save(cachePath);
        }

        // Emit on main thread via queued connection
        QMetaObject::invokeMethod(gen, [gen, src, result]() {
            emit gen->thumbnailGenerated(src, result);
        }, Qt::QueuedConnection);
    });
}

void ThumbnailGenerator::generateThumbnailsAsync(const QStringList& paths, ThumbnailSize size)
{
    for (const auto& path : paths)
        generateThumbnailAsync(path, size);
}

} // namespace ks
