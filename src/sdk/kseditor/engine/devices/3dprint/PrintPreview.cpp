#include "PrintPreview.h"
#include "GCodeGenerator.h"
#include <QPainter>
#include <QImage>
#include <QDebug>
#include <cmath>
#include <algorithm>

namespace ks {
namespace printing {

struct PrintPreview::Impl {
    QVector<QColor> layerColorMap;
    QMap<int, QColor> featureColorMap;

    Impl() {
        // Feature colors
        featureColorMap[0] = QColor(200, 200, 200);  // Perimeter
        featureColorMap[1] = QColor(100, 100, 255, 180); // Infill
        featureColorMap[2] = QColor(255, 100, 100, 180); // Support
        featureColorMap[3] = QColor(255, 255, 0, 180);   // Brim/Skirt
        featureColorMap[4] = QColor(255, 0, 255, 100);   // Travel

        // Layer color map (rainbow)
        for (int i = 0; i < 360; ++i) {
            QColor c;
            c.setHsv(i, 200, 255, 200);
            layerColorMap.append(c);
        }
    }
};

PrintPreview::PrintPreview(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}
PrintPreview::~PrintPreview() = default;

// ============================================================================
// Frame Generation
// ============================================================================

QVector<PrintPreviewFrame> PrintPreview::generateFrames(const SliceInfo& sliceInfo,
                                                         int frameCount,
                                                         const QSize& resolution) {
    QVector<PrintPreviewFrame> frames;
    int totalLayers = sliceInfo.slices.size();
    if (totalLayers == 0) return frames;

    int step = (frameCount > 0 && frameCount < totalLayers) ? std::max(1, totalLayers / frameCount) : 1;
    frames.reserve((totalLayers + step - 1) / step);

    for (int i = 0; i < totalLayers; i += step) {
        PrintPreviewFrame frame;
        frame.layerIndex = i;
        frame.z = sliceInfo.slices[i].z;

        // Merge all geometry for this layer
        const auto& layer = sliceInfo.slices[i];
        frame.geometry = layer.perimeters;
        frame.geometry.insert(frame.geometry.end(), layer.infill.begin(), layer.infill.end());
        frame.geometry.insert(frame.geometry.end(), layer.support.begin(), layer.support.end());
        frame.geometry.insert(frame.geometry.end(), layer.brim.begin(), layer.brim.end());

        frames.push_back(std::move(frame));
        emit frameGenerated(i, QImage()); // Could render here if needed
    }

    return frames;
}

QImage PrintPreview::generateFrame(const SliceInfo& sliceInfo, int layerIndex,
                                    const QSize& resolution) {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(sliceInfo.slices.size())) {
        return QImage();
    }

    QImage image(resolution, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const auto& layer = sliceInfo.slices[layerIndex];
    const auto& bounds = sliceInfo.boundingBox;

    // Calculate viewport transform
    double modelWidth = bounds.size().x;
    double modelHeight = bounds.size().y;
    double modelMax = std::max(modelWidth, modelHeight);

    double scale = std::min(resolution.width(), resolution.height()) * 0.85 / modelMax;
    QPointF offset(resolution.width() * 0.5, resolution.height() * 0.5);
    offset -= QPointF(bounds.center().x, bounds.center().y) * scale;

    // Draw build plate outline
    painter.setPen(QPen(QColor(80, 80, 80), 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    QRectF bedRect(-bounds.size().x * 0.5 * scale + offset.x(),
                   -bounds.size().y * 0.5 * scale + offset.y(),
                   bounds.size().x * scale, bounds.size().y * scale);
    painter.drawRect(bedRect);

    // Render layer
    renderLayer(painter, layer, QRectF(0, 0, resolution.width(), resolution.height()), scale, offset);

    // Draw layer info
    painter.setPen(Qt::white);
    painter.setFont(QFont("Monospace", 10));
    painter.drawText(10, 20, QString("Layer %1 / %2 (Z=%3mm)").arg(layerIndex + 1).arg(sliceInfo.slices.size()).arg(layer.z, 0, 'f', 2));

    return image;
}

QVector<QImage> PrintPreview::generateAnimation(const SliceInfo& sliceInfo,
                                                 int fps,
                                                 const QSize& resolution) {
    QVector<QImage> frames;
    frames.reserve(sliceInfo.slices.size());

    for (size_t i = 0; i < sliceInfo.slices.size(); ++i) {
        frames.push_back(generateFrame(sliceInfo, static_cast<int>(i), resolution));
        emit animationProgress(static_cast<int>((i + 1) * 100 / sliceInfo.slices.size()));
    }

    return frames;
}

// ============================================================================
// Simulation
// ============================================================================

PrintSimulationState PrintPreview::simulatePrint(const SliceInfo& sliceInfo,
                                                  const PrinterProfile& printer,
                                                  const SliceSettings& settings) {
    PrintSimulationState state;
    state.currentLayer = 0;
    state.currentZ = 0;
    state.isPrinting = true;

    // Pre-parse all G-code paths for visualization
    QString gcode = sliceInfo.gcodeText;
    if (gcode.isEmpty()) {
        // Generate if not present
        GCodeGenerator gen;
        gcode = gen.generateFromLayers(sliceInfo.slices, printer, settings, sliceInfo.boundingBox);
    }

    {
        QVector<Vector3> printed = parseGCodePath(gcode);
        state.printedPath.assign(printed.begin(), printed.end());
    }
    {
        QVector<Vector3> travel = parseGCodeTravel(gcode);
        state.travelPath.assign(travel.begin(), travel.end());
    }

    return state;
}

bool PrintPreview::stepSimulation(PrintSimulationState& state, double dt) {
    if (!state.isPrinting || state.isPaused) return false;

    // Simplified: advance time based on print speed
    double layerTime = 0;
    if (state.currentLayer < static_cast<int>(state.layerInfos.size())) {
        layerTime = state.layerInfos[state.currentLayer].printTime;
    }

    state.currentTime += dt;
    if (state.currentTime >= layerTime) {
        state.currentTime = 0;
        state.currentLayer++;
        state.currentZ += 0.2; // Approximate layer height

        if (state.currentLayer >= static_cast<int>(state.layerInfos.size())) {
            state.isPrinting = false;
            return false;
        }
        emit simulationStep(state.currentTime, state.currentLayer);
    }

    return true;
}

void PrintPreview::resetSimulation(PrintSimulationState& state, const SliceInfo& sliceInfo) {
    state.currentTime = 0;
    state.currentLayer = 0;
    state.currentZ = sliceInfo.slices.empty() ? 0 : sliceInfo.slices[0].z;
    state.isPrinting = true;
    state.isPaused = false;
    state.printedPath.clear();
    state.travelPath.clear();
    state.layerInfos.clear();
}

// ============================================================================
// G-code Parsing
// ============================================================================

QVector<Vector3> PrintPreview::parseGCodePath(const QString& gcode) {
    QVector<Vector3> path;
    QVector<Vector3> currentLine;

    for (const QString& line : gcode.split('\n')) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';')) continue;

        // Parse G0/G1 moves with E (extrusion)
        if (trimmed.startsWith("G0 ") || trimmed.startsWith("G1 ")) {
            double x = m_currentX, y = m_currentY, z = m_currentZ, e = 0;
            bool hasE = false;

            QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                if (part.startsWith('X')) x = part.mid(1).toDouble();
                else if (part.startsWith('Y')) y = part.mid(1).toDouble();
                else if (part.startsWith('Z')) z = part.mid(1).toDouble();
                else if (part.startsWith('E')) { e = part.mid(1).toDouble(); hasE = true; }
            }

            if (hasE && e > m_currentE) {
                // Extruding move
                if (!currentLine.isEmpty()) {
                    path.append(currentLine);
                }
                currentLine.clear();
                currentLine.append({x, y, z});
            } else if (hasE && e < m_currentE) {
                // Retract - end current line
                if (!currentLine.isEmpty()) {
                    path.append(currentLine);
                    currentLine.clear();
                }
            } else if (!currentLine.isEmpty()) {
                // Travel or non-extruding
                currentLine.append({x, y, z});
            }

            m_currentX = x; m_currentY = y; m_currentZ = z; m_currentE = e;
        }
    }
    if (!currentLine.isEmpty()) path.append(currentLine);

    return path;
}

QVector<Vector3> PrintPreview::parseGCodeTravel(const QString& gcode) {
    QVector<Vector3> travel;
    // Similar parsing but for non-extruding moves
    return travel;
}

// ============================================================================
// Rendering
// ============================================================================

void PrintPreview::renderLayer(QPainter& painter, const LayerSlice& layer, const QRectF& viewport,
                                double scale, const QPointF& offset) {
    auto drawPolygons = [&](const Polygons2D& polys, const QColor& color, double width = 1.0) {
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(QBrush(color));

        for (const auto& poly : polys) {
            if (poly.vertices.size() < 3) continue;

            QPolygonF qpoly;
            qpoly.reserve(poly.vertices.size());
            for (const auto& v : poly.vertices) {
                qpoly << QPointF(v.x * scale + offset.x(), v.y * scale + offset.y());
            }
            painter.drawPolygon(qpoly);
        }
    };

    // Draw in order: brim, support, infill, perimeters (topmost)
    drawPolygons(layer.brim, d->featureColorMap[3], 1.5);
    drawPolygons(layer.support, d->featureColorMap[2], 1.0);
    drawPolygons(layer.infill, d->featureColorMap[1], 1.0);
    drawPolygons(layer.perimeters, d->featureColorMap[0], 2.0);
}

QColor PrintPreview::featureColor(LayerFeatureType feature) const {
    int idx = static_cast<int>(feature);
    return d->featureColorMap.value(idx, Qt::white);
}

QColor PrintPreview::speedColor(double speed, double minSpeed, double maxSpeed) const {
    double t = (speed - minSpeed) / std::max(1.0, maxSpeed - minSpeed);
    t = std::clamp(t, 0.0, 1.0);
    int hue = static_cast<int>((1.0 - t) * 240); // Blue (slow) to Red (fast)
    QColor c;
    c.setHsv(hue, 255, 255);
    return c;
}

QColor PrintPreview::layerColor(int layer, int totalLayers) const {
    int idx = static_cast<int>((layer / std::max(1.0, double(totalLayers))) * (d->layerColorMap.size() - 1));
    return d->layerColorMap[idx];
}

} // namespace printing
} // namespace ks