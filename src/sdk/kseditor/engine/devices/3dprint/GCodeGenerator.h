#pragma once

#include "3DPrintTypes.h"
#include "SlicerEngine.h"
#include "PrinterProfile.h"
#include <QObject>
#include <QVector>
#include <QString>

namespace ks {
namespace printing {

class GCodeGenerator : public QObject
{
    Q_OBJECT
public:
    explicit GCodeGenerator(QObject* parent = nullptr);
    ~GCodeGenerator() override;

    // Main generation
    QString generateGCode(const SliceInfo& sliceInfo, const PrinterProfile& printer, const SliceSettings& settings);

    // Generate from layer slices directly
    QString generateFromLayers(const LayerSlices& slices, const PrinterProfile& printer, const SliceSettings& settings,
                               const BoundingBox& bounds = BoundingBox{});

    // Generation with progress callback
    using ProgressCallback = std::function<void(int percent, const QString& message)>;
    void generateAsync(const SliceInfo& sliceInfo, const PrinterProfile& printer, const SliceSettings& settings,
                       ProgressCallback progress, std::function<void(const QString& gcode)> complete);

    // Individual section generators
    QString generateHeader(const PrinterProfile& printer, const SliceSettings& settings, const BoundingBox& bounds);
    QString generateStartup(const PrinterProfile& printer, const SliceSettings& settings);
    QString generateLayer(const LayerSlice& layer, int layerIndex, int totalLayers,
                          const PrinterProfile& printer, const SliceSettings& settings);
    QString generateLayerChange(int layerIndex, const PrinterProfile& printer, const SliceSettings& settings);
    QString generateShutdown(const PrinterProfile& printer, const SliceSettings& settings);
    QString generateFooter(const PrinterProfile& printer, const SliceSettings& settings);

    // Toolpath generation for specific features
    QString generatePerimeters(const Polygons2D& perimeters, const SliceSettings& settings, bool outerWall);
    QString generateInfill(const Polygons2D& infill, const SliceSettings& settings);
    QString generateSupport(const Polygons2D& support, const SliceSettings& settings);
    QString generateBrim(const Polygons2D& brim, const SliceSettings& settings);
    QString generateSkirt(const Polygons2D& skirt, const SliceSettings& settings);

    // Travel moves
    QString generateTravel(const Vector3& from, const Vector3& to, const SliceSettings& settings);
    QString generateRetract(const SliceSettings& settings);
    QString generatePrime(const SliceSettings& settings);
    QString generateZHop(double z, const SliceSettings& settings);

    // Temperature
    QString generateSetHotendTemp(double temp, bool wait, const PrinterProfile& printer);
    QString generateSetBedTemp(double temp, bool wait, const PrinterProfile& printer);
    QString generateFanSpeed(int speed, const SliceSettings& settings);

    // Utility
    QString formatCoordinate(double value, int precision = 3) const;
    QString formatExtrusion(double value, int precision = 5) const;
    double calculateExtrusion(double distance, double lineWidth, double layerHeight, double filamentDiameter) const;

signals:
    void generationProgress(int percent, const QString& message);
    void generationComplete(const QString& gcode);
    void generationError(const QString& error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;

    // Default G-code templates
    QString defaultStartupGcode(const PrinterProfile& printer, const SliceSettings& settings);
    QString defaultShutdownGcode(const PrinterProfile& printer, const SliceSettings& settings);

    // State during generation
    double m_currentX = 0, m_currentY = 0, m_currentZ = 0;
    double m_currentE = 0;
    double m_currentFeedrate = 0;
    bool m_relativeExtrusion = false;
    bool m_relativePositioning = false;
    int m_currentLayer = 0;
    int m_totalLayers = 0;
    double m_totalFilament = 0;
    double m_totalTime = 0;

    void resetState();
    void writeLine(QString& output, const QString& line);
    void writeComment(QString& output, const QString& comment);
    void moveTo(QString& output, double x, double y, double z, double feedrate, double extrusion = 0);
    void travelTo(QString& output, double x, double y, double z);
    void setFeedrate(QString& output, double feedrate);
    void setExtrusionMode(QString& output, bool relative);
    void setPositioningMode(QString& output, bool relative);
    void setFanSpeed(QString& output, int speed);
};

} // namespace printing
} // namespace ks