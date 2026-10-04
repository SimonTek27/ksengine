#pragma once

#include "3DPrintTypes.h"
#include "SlicerEngine.h"
#include <QObject>
#include <QImage>
#include <QVector>
#include <QMatrix4x4>

namespace ks {
namespace printing {

class PrintPreview : public QObject
{
    Q_OBJECT
public:
    explicit PrintPreview(QObject* parent = nullptr);
    ~PrintPreview() override;

    // Generate preview frames from slice info
    QVector<PrintPreviewFrame> generateFrames(const SliceInfo& sliceInfo,
                                               int frameCount = -1,
                                               const QSize& resolution = QSize(800, 600));

    // Generate single frame
    QImage generateFrame(const SliceInfo& sliceInfo, int layerIndex,
                         const QSize& resolution = QSize(800, 600));

    // Generate animation (sequence of frames)
    QVector<QImage> generateAnimation(const SliceInfo& sliceInfo,
                                       int fps = 30,
                                       const QSize& resolution = QSize(800, 600));

    // Simulation state for step-by-step preview
    PrintSimulationState simulatePrint(const SliceInfo& sliceInfo,
                                        const PrinterProfile& printer,
                                        const SliceSettings& settings);

    // Step simulation
    bool stepSimulation(PrintSimulationState& state, double dt);
    void resetSimulation(PrintSimulationState& state, const SliceInfo& sliceInfo);

    // G-code visualization
    QVector<Vector3> parseGCodePath(const QString& gcode);
    QVector<Vector3> parseGCodeTravel(const QString& gcode);

    // Color schemes
    enum class ColorScheme {
        Default,
        Speed,        // Color by print speed
        Layer,        // Color by layer number
        Feature,      // Perimeter/Infill/Support different colors
        Temperature,  // Color by nozzle temp
        FlowRate      // Color by extrusion rate
    };

    void setColorScheme(ColorScheme scheme) { m_colorScheme = scheme; }
    void setShowTravelMoves(bool show) { m_showTravelMoves = show; }
    void setShowRetractions(bool show) { m_showRetractions = show; }
    void setLayerOpacity(double opacity) { m_layerOpacity = opacity; }

signals:
    void frameGenerated(int layerIndex, const QImage& frame);
    void animationProgress(int percent);
    void simulationStep(double time, int layer);

private:
    struct Impl;
    std::unique_ptr<Impl> d;

    ColorScheme m_colorScheme = ColorScheme::Feature;
    bool m_showTravelMoves = true;
    bool m_showRetractions = false;
    double m_layerOpacity = 1.0;
    double m_currentX = 0;
    double m_currentY = 0;
    double m_currentZ = 0;
    double m_currentE = 0;

    QColor featureColor(LayerFeatureType feature) const;
    QColor speedColor(double speed, double minSpeed, double maxSpeed) const;
    QColor layerColor(int layer, int totalLayers) const;

    void renderLayer(QPainter& painter, const LayerSlice& layer, const QRectF& viewport,
                     double scale, const QPointF& offset);
    void renderGCodePath(QPainter& painter, const QVector<Vector3>& path, const QRectF& viewport,
                         double scale, const QPointF& offset, const QColor& color);
};

} // namespace printing
} // namespace ks