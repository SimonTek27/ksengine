#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPainter>
#include <QThreadPool>
#include <QRunnable>
#include <QMetaObject>
#include <functional>
#include <QRegularExpression>

namespace ks {

class AssetPreviewTask : public QRunnable {
public:
    using PreviewCallback = std::function<void(const QPixmap&)>;

    AssetPreviewTask(const QString& filePath, int width, int height, PreviewCallback callback)
        : m_filePath(filePath), m_width(width), m_height(height), m_callback(callback) {
        setAutoDelete(true);
    }

    void run() override {
        QPixmap preview = generatePreview(m_filePath, m_width, m_height);
        if (m_callback) {
            m_callback(preview);
        }
    }

private:
    QString m_filePath;
    int m_width;
    int m_height;
    PreviewCallback m_callback;

    QPixmap generatePreview(const QString& filePath, int width, int height) {
        QFileInfo info(filePath);
        if (!info.exists()) {
            return createPlaceholderPreview(width, height, "File not found");
        }

        QString suffix = info.suffix().toLower();
        QMimeDatabase mimeDb;
        QMimeType mime = mimeDb.mimeTypeForFile(filePath);

        // Try to generate preview based on file type
        if (mime.name().startsWith("image/")) {
            return createImagePreview(filePath, width, height);
        } else if (suffix == "kn5" || suffix == "fbx" || suffix == "obj" || 
                   suffix == "dae" || suffix == "gltf" || suffix == "glb") {
            return createModelPreview(filePath, width, height);
        } else if (suffix == "wav" || suffix == "mp3" || suffix == "ogg" || 
                   suffix == "flac") {
            return createAudioPreview(filePath, width, height);
        } else if (suffix == "json" && (info.fileName().contains("manifest") || 
                                         info.fileName().contains("material"))) {
            return createDataPreview(filePath, width, height);
        } else {
            return createGenericPreview(filePath, width, height);
        }
    }

    QPixmap createImagePreview(const QString& filePath, int width, int height) {
        QPixmap pixmap(filePath);
        if (pixmap.isNull()) {
            return createPlaceholderPreview(width, height, "Invalid Image");
        }
        return pixmap.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QPixmap createModelPreview(const QString& filePath, int width, int height) {
        QFileInfo info(filePath);
        QString ext = info.suffix().toLower();
        if (ext == "obj") {
            QPixmap pixmap = renderObjWireframe(filePath, width, height);
            if (!pixmap.isNull()) return pixmap;
        }
        return createPlaceholderPreview(width, height, "3D Model (" + ext + ")", ":/icons/model.png");
    }

    struct ObjVertex { float x, y, z; };
    struct ObjFace { int v[3]; };
    QPixmap renderObjWireframe(const QString& filePath, int width, int height) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QPixmap();

        QVector<ObjVertex> verts;
        QVector<ObjFace> faces;
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("v ")) {
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    ObjVertex v;
                    v.x = parts[1].toFloat();
                    v.y = parts[2].toFloat();
                    v.z = parts[3].toFloat();
                    verts.append(v);
                }
            } else if (line.startsWith("f ")) {
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    ObjFace f;
                    f.v[0] = qAbs(parts[1].split('/')[0].toInt()) - 1;
                    f.v[1] = qAbs(parts[2].split('/')[0].toInt()) - 1;
                    f.v[2] = qAbs(parts[3].split('/')[0].toInt()) - 1;
                    faces.append(f);
                }
            }
        }
        file.close();

        if (verts.isEmpty() || faces.isEmpty()) return QPixmap();

        // Compute center and scale
        float cx = 0, cy = 0, cz = 0;
        for (const auto& v : verts) { cx += v.x; cy += v.y; cz += v.z; }
        cx /= verts.size(); cy /= verts.size(); cz /= verts.size();
        float maxDist = 0;
        for (const auto& v : verts) {
            float d = std::sqrt((v.x-cx)*(v.x-cx) + (v.y-cy)*(v.y-cy) + (v.z-cz)*(v.z-cz));
            if (d > maxDist) maxDist = d;
        }
        float scale = (maxDist > 0) ? (qMin(width, height) * 0.35f) / maxDist : 1;

        QPixmap pixmap(width, height);
        pixmap.fill(QColor(30, 30, 35));
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(width / 2.0, height / 2.0);

        // Simple orthographic projection (X right, Y up, ignore Z slightly)
        painter.setPen(QPen(QColor(100, 180, 255), 1.0));
        for (const auto& face : faces) {
            for (int i = 0; i < 3; ++i) {
                const auto& v1 = verts[face.v[i]];
                const auto& v2 = verts[face.v[(i+1)%3]];
                float x1 = (v1.x - cx) * scale;
                float y1 = -(v1.y - cy) * scale;
                float x2 = (v2.x - cx) * scale;
                float y2 = -(v2.y - cy) * scale;
                painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
            }
        }

        // Info overlay
        painter.setPen(Qt::white);
        QFont f = painter.font(); f.setPointSize(8); painter.setFont(f);
        painter.drawText(QRect(-width/2, height/2 - 40, width, 20), Qt::AlignCenter,
            QString("V:%1 F:%2").arg(verts.size()).arg(faces.size()));

        painter.end();
        return pixmap;
    }

    QPixmap createAudioPreview(const QString& filePath, int width, int height) {
        return createPlaceholderPreview(width, height, "Audio File", ":/icons/audio.png");
    }

    QPixmap createDataPreview(const QString& filePath, int width, int height) {
        return createPlaceholderPreview(width, height, "Data File", ":/icons/data.png");
    }

    QPixmap createGenericPreview(const QString& filePath, int width, int height) {
        QFileInfo info(filePath);
        return createPlaceholderPreview(width, height, info.fileName(), ":/icons/file.png");
    }

    QPixmap createPlaceholderPreview(int width, int height, const QString& text, const QString& iconPath = "") {
        QPixmap pixmap(width, height);
        pixmap.fill(QColor(45, 45, 48)); // Dark background matching theme

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw icon if provided
        if (!iconPath.isEmpty()) {
            QPixmap icon(iconPath);
            if (!icon.isNull()) {
                int iconSize = qMin(width, height) / 3;
                QPixmap scaledIcon = icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter.drawPixmap((width - iconSize) / 2, height / 4, scaledIcon);
            }
        }

        // Draw text
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(10);
        painter.setFont(font);

        QRect textRect(0, height * 2 / 3, width, height / 3);
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);

        painter.end();
        return pixmap;
    }
};

class AssetPreviewWidget : public QLabel {
    Q_OBJECT
public:
    explicit AssetPreviewWidget(QWidget* parent = nullptr);
    ~AssetPreviewWidget() override = default;

    void loadAsset(const QString& filePath, int width = 200, int height = 200);
    void clearPreview();

signals:
    void previewLoaded(const QString& filePath);

private:
    void onPreviewReady(const QPixmap& preview);

private:
    QString m_currentFilePath;
    int m_previewWidth;
    int m_previewHeight;
    QThreadPool m_threadPool;
};

} // namespace ks