#pragma once

#include "ThreeDPrintModule.h"
#include <QObject>
#include <QQmlEngine>
#include <QQmlContext>
#include <QImage>
#include <QQuickImageProvider>
#include <QPainter>
#include <QPainterPath>

namespace ks {
namespace printing {

class ThreeDPrintQmlBridge : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ThreeDPrintQmlBridge)

public:
    explicit ThreeDPrintQmlBridge(QObject* parent = nullptr);
    ~ThreeDPrintQmlBridge() override = default;

    // Register QML types and context properties
    static void registerQmlTypes();
    static void registerQmlContext(QQmlContext* context, ThreeDPrintModule* module);

    // ========================================================================
    // Printer Profiles (QML accessible)
    // ========================================================================
    Q_INVOKABLE QVariantList getPrinters() const;
    Q_INVOKABLE QVariantMap getPrinter(const QString& id) const;
    Q_INVOKABLE QVariantMap getActivePrinter() const;
    Q_INVOKABLE bool setActivePrinter(const QString& id);
    Q_INVOKABLE bool addCustomPrinter(const QVariantMap& profile);
    Q_INVOKABLE bool updatePrinter(const QString& id, const QVariantMap& profile);
    Q_INVOKABLE bool removePrinter(const QString& id);

    // ========================================================================
    // Slice Settings (QML accessible)
    // ========================================================================
    Q_INVOKABLE QVariantMap getDefaultSettings() const;
    Q_INVOKABLE QVariantMap getSettingsForPrinter(const QString& printerId) const;
    Q_INVOKABLE void setDefaultSettings(const QVariantMap& settings);
    Q_INVOKABLE void applyMaterialPreset(const QString& material);
    Q_INVOKABLE QStringList availableMaterials() const;
    Q_INVOKABLE QStringList availableInfillPatterns() const;
    Q_INVOKABLE QStringList availableSupportTypes() const;
    Q_INVOKABLE QStringList availableAdhesionTypes() const;
    Q_INVOKABLE QStringList availableQualityProfiles() const;

    // ========================================================================
    // Slicing Operations
    // ========================================================================
    // Slice from file path (STL, OBJ, etc.)
    Q_INVOKABLE QVariantMap sliceFile(const QString& filePath, const QString& printerId = "",
                                      const QVariantMap& settings = QVariantMap());

    // Slice from mesh data (vertices + indices)
    Q_INVOKABLE QVariantMap sliceMesh(const QVariantList& vertices, const QVariantList& indices,
                                      const QString& printerId = "", const QVariantMap& settings = QVariantMap());

    // Async slicing with callbacks
    Q_INVOKABLE void sliceFileAsync(const QString& filePath, const QString& printerId = "",
                                    const QVariantMap& settings = QVariantMap());
    Q_INVOKABLE void sliceMeshAsync(const QVariantList& vertices, const QVariantList& indices,
                                    const QString& printerId = "", const QVariantMap& settings = QVariantMap());
    Q_INVOKABLE void cancelSlice();

    // ========================================================================
    // G-code Generation & Export
    // ========================================================================
    Q_INVOKABLE QString generateGCode(const QVariantMap& sliceInfo);
    Q_INVOKABLE void generateGCodeAsync(const QVariantMap& sliceInfo);
    Q_INVOKABLE bool exportGCode(const QVariantMap& sliceInfo, const QString& filePath);
    Q_INVOKABLE bool exportProject(const QVariantMap& sliceInfo, const QString& filePath);

    // ========================================================================
    // Preview & Analysis
    // ========================================================================
    Q_INVOKABLE QVariantMap getSliceInfo(const QVariantMap& sliceInfo) const;
    Q_INVOKABLE QVariantMap getLayerPreview(const QVariantMap& sliceInfo, int layerIndex) const;
    Q_INVOKABLE QImage getThumbnail(const QVariantMap& sliceInfo, int width = 400, int height = 300) const;
    Q_INVOKABLE QVariantMap analyzeModel(const QString& filePath) const;
    Q_INVOKABLE QVariantMap estimatePrint(const QString& filePath, const QString& printerId = "") const;

    // ========================================================================
    // Utility
    // ========================================================================
    Q_INVOKABLE bool checkBuildVolume(const QVariantMap& bounds, const QString& printerId, QString* error = nullptr) const;
    Q_INVOKABLE QVariantMap suggestOrientation(const QVariantList& vertices, const QVariantList& indices) const;

signals:
    void activePrinterChanged(const QString& id);
    void sliceStarted();
    void sliceProgress(int percent, const QString& message);
    void sliceComplete(const QVariantMap& result);
    void sliceError(const QString& error);
    void gcodeGenerated(const QString& gcode);
    void gcodeGenerationProgress(int percent);
    void settingsChanged();

private:
    ThreeDPrintModule* m_module = nullptr;

    // Conversion helpers
    static QVariantMap printerToMap(const PrinterProfile* p);
    static QVariantMap settingsToMap(const SliceSettings* s);
    static QVariantMap sliceInfoToMap(const SliceInfo& info);
    static QVariantMap layerToMap(const LayerSlice& layer);
    static QVariantMap boundsToMap(const BoundingBox& b);
    static SliceSettings mapToSettings(const QVariantMap& map);
    static MeshTriangles mapToTriangles(const QVariantList& vertices, const QVariantList& indices);
    static SliceInfo mapToSliceInfo(const QVariantMap& map);

    // Async callbacks
    void onSliceComplete(const SliceInfo& result);
    void onGCodeComplete(const QString& gcode);

    // Pending async operations
    std::function<void(const SliceInfo&)> m_pendingSliceCallback;
    std::function<void(const QString&)> m_pendingGCodeCallback;
};

class SliceResultImageProvider : public QQuickImageProvider
{
public:
    SliceResultImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    // Store slice result for rendering (called by QmlBridge after slicing)
    static void setSliceResult(const SliceInfo& result) {
        s_cachedSlice = result;
        s_hasSliceData = true;
    }

    static void clearSliceResult() {
        s_hasSliceData = false;
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        int w = requestedSize.width() > 0 ? requestedSize.width() : 400;
        int h = requestedSize.height() > 0 ? requestedSize.height() : 300;

        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(QColor(20, 20, 25));

        if (!s_hasSliceData || s_cachedSlice.slices.empty()) {
            drawPlaceholder(img);
            if (size) *size = img.size();
            return img;
        }

        // Parse id format: "slice:<layerIndex>" or just render overview
        int layerIndex = -1;
        QStringList parts = id.split(':');
        if (parts.size() >= 2) {
            bool ok;
            layerIndex = parts.last().toInt(&ok);
            if (!ok) layerIndex = -1;
        }

        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing);

        if (layerIndex >= 0 && layerIndex < static_cast<int>(s_cachedSlice.slices.size())) {
            renderLayer(painter, s_cachedSlice.slices[layerIndex], w, h, img);
        } else {
            renderOverview(painter, w, h);
        }

        painter.end();
        if (size) *size = img.size();
        return img;
    }

private:
    static inline SliceInfo s_cachedSlice;
    static inline bool s_hasSliceData = false;

    void drawPlaceholder(QImage& img) {
        QPainter p(&img);
        p.setPen(QColor(80, 80, 80));
        p.setFont(QFont("monospace", 10));
        p.drawText(img.rect(), Qt::AlignCenter, "No slice data\nSlice a model to preview layers");
        p.end();
    }

    void renderLayer(QPainter& painter, const LayerSlice& layer, int w, int h, QImage& img) {
        // Calculate bounds of all polygons
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;

        auto updateBounds = [&](const Polygons2D& polys) {
            for (const auto& poly : polys) {
                for (const auto& v : poly.vertices) {
                    minX = qMin(minX, v.x); maxX = qMax(maxX, v.x);
                    minY = qMin(minY, v.y); maxY = qMax(maxY, v.y);
                }
            }
        };

        updateBounds(layer.perimeters);
        updateBounds(layer.infill);
        updateBounds(layer.support);
        updateBounds(layer.brim);

        if (minX >= maxX || minY >= maxY) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawText(img.rect(), Qt::AlignCenter, "Empty layer");
            return;
        }

        double rangeX = maxX - minX;
        double rangeY = maxY - minY;
        double margin = 20.0;
        double scaleX = (w - margin * 2) / rangeX;
        double scaleY = (h - margin * 2) / rangeY;
        double scale = qMin(scaleX, scaleY);

        double offsetX = margin + (w - margin * 2 - rangeX * scale) / 2.0;
        double offsetY = margin + (h - margin * 2 - rangeY * scale) / 2.0;

        auto toScreen = [&](double x, double y) -> QPointF {
            return QPointF(offsetX + (x - minX) * scale, offsetY + (maxY - y) * scale);
        };

        // Draw support (light blue, semi-transparent)
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(100, 150, 255, 80));
        drawPolygons(painter, layer.support, toScreen);

        // Draw infill (blue lines)
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(80, 120, 220, 150), 0.5));
        drawPolygons(painter, layer.infill, toScreen);

        // Draw brim (yellow)
        painter.setPen(QPen(QColor(200, 180, 50, 120), 0.5));
        painter.setBrush(QColor(200, 180, 50, 40));
        drawPolygons(painter, layer.brim, toScreen);

        // Draw perimeters (white/light gray, opaque)
        painter.setPen(QPen(QColor(200, 200, 210), 1.0));
        painter.setBrush(QColor(200, 200, 210, 60));
        drawPolygons(painter, layer.perimeters, toScreen);

        // Layer info
        painter.setPen(QColor(120, 120, 130));
        painter.setFont(QFont("monospace", 9));
        painter.drawText(5, 15, QString("Z: %1 mm  |  Perimeters: %2  |  Infill: %3")
            .arg(layer.z, 0, 'f', 2)
            .arg(layer.perimeters.size())
            .arg(layer.infill.size()));
    }

    void renderOverview(QPainter& painter, int w, int h) {
        int totalLayers = static_cast<int>(s_cachedSlice.slices.size());
        int maxLayersToShow = qMin(totalLayers, h / 2);

        painter.setPen(Qt::NoPen);

        for (int i = 0; i < maxLayersToShow; ++i) {
            int sliceIdx = static_cast<int>((static_cast<double>(i) / maxLayersToShow) * totalLayers);
            const auto& layer = s_cachedSlice.slices[sliceIdx];

            double y = (static_cast<double>(i) / maxLayersToShow) * h;

            for (const auto& poly : layer.perimeters) {
                if (poly.vertices.size() < 3) continue;
                QPainterPath path;
                path.moveTo(poly.vertices[0].x, poly.vertices[0].y);
                for (size_t j = 1; j < poly.vertices.size(); ++j) {
                    path.lineTo(poly.vertices[j].x, poly.vertices[j].y);
                }
                path.closeSubpath();

                double brightness = 60.0 + (static_cast<double>(i) / maxLayersToShow) * 80.0;
                painter.setBrush(QColor(static_cast<int>(brightness), static_cast<int>(brightness), static_cast<int>(brightness + 20)));
                painter.drawPath(path);
            }
        }

        // Stats overlay
        painter.setPen(QColor(180, 180, 190));
        painter.setFont(QFont("monospace", 10));
        QString stats = QString("Layers: %1  |  Time: %2  |  Material: %3g")
            .arg(totalLayers)
            .arg(formatTime(s_cachedSlice.printTime))
            .arg(s_cachedSlice.filamentWeight, 0, 'f', 1);
        painter.drawText(QRect(0, h - 25, w, 25), Qt::AlignCenter, stats);
    }

    void drawPolygons(QPainter& painter, const Polygons2D& polys,
                       std::function<QPointF(double, double)> toScreen) {
        for (const auto& poly : polys) {
            if (poly.vertices.size() < 3) continue;
            QPainterPath path;
            QPointF p0 = toScreen(poly.vertices[0].x, poly.vertices[0].y);
            path.moveTo(p0);
            for (size_t j = 1; j < poly.vertices.size(); ++j) {
                QPointF p = toScreen(poly.vertices[j].x, poly.vertices[j].y);
                path.lineTo(p);
            }
            path.closeSubpath();

            // Cut holes
            for (const auto& hole : poly.holes) {
                if (hole.size() < 3) continue;
                QPainterPath holePath;
                QPointF hp0 = toScreen(hole[0].x, hole[0].y);
                holePath.moveTo(hp0);
                for (size_t j = 1; j < hole.size(); ++j) {
                    holePath.lineTo(toScreen(hole[j].x, hole[j].y));
                }
                holePath.closeSubpath();
                path = path.united(holePath);
            }

            painter.drawPath(path);
        }
    }

    static QString formatTime(double seconds) {
        int h = static_cast<int>(seconds) / 3600;
        int m = (static_cast<int>(seconds) % 3600) / 60;
        int s = static_cast<int>(seconds) % 60;
        if (h > 0) return QString("%1h %2m").arg(h).arg(m);
        if (m > 0) return QString("%1m %2s").arg(m).arg(s);
        return QString("%1s").arg(s);
    }
};

} // namespace printing
} // namespace ks