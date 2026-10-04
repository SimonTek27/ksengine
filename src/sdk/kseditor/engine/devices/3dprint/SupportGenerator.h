#pragma once

#include "3DPrintTypes.h"
#include "PrinterProfile.h"
#include <QObject>
#include <QVector>
#include <QString>

namespace ks {
namespace printing {

class SupportGenerator : public QObject
{
    Q_OBJECT
public:
    explicit SupportGenerator(QObject* parent = nullptr);
    ~SupportGenerator() override;

    // Main generation
    Polygons2D generateForLayer(const LayerSlice& layer, const LayerSlices& allLayers,
                                const SliceSettings& settings, const PrinterProfile& printer);

    // Generate support for entire model
    void generateSupports(const LayerSlices& slices, const SliceSettings& settings, const PrinterProfile& printer,
                          std::function<void(int layerIndex, const Polygons2D& support)> layerCallback);

    // Support blockers/enforcers
    void addBlocker(const SupportBlocker& blocker);
    void addEnforcer(const SupportEnforcer& enforcer);
    void addCustomSupport(const CustomSupportPoint& point);
    void clearBlockers() { m_blockers.clear(); }
    void clearEnforcers() { m_enforcers.clear(); }
    void clearCustomSupports() { m_customSupports.clear(); }

    // Tree support generation (organic)
    Polygons2D generateTreeSupports(const LayerSlice& layer, const LayerSlices& allLayers,
                                    const SliceSettings& settings, const PrinterProfile& printer);

    // Normal grid support
    Polygons2D generateGridSupports(const LayerSlice& layer, const SliceSettings& settings);

    // Support interface (dense roof/floor)
    Polygons2D generateSupportInterface(const Polygons2D& support, const SliceSettings& settings);

    // Check if point needs support
    bool needsSupport(const Vector2& point, const LayerSlice& layer, const LayerSlices& allLayers,
                      const SliceSettings& settings, int layerIndex) const;

    // Support area calculation
    Polygons2D calculateSupportArea(const LayerSlice& layer, const LayerSlices& allLayers,
                                    const SliceSettings& settings, int layerIndex) const;

signals:
    void supportProgress(int percent, const QString& message);
    void supportComplete(int layerCount);

public:
    // Tree support structures
    struct TreeBranch {
        Vector2 base;           // Base on build plate
        Vector2 tip;            // Tip touching model
        double diameter = 2.0;
        std::vector<TreeBranch> children;
        int layerStart = 0;
        int layerEnd = 0;
    };
private:
    struct Impl;
    std::unique_ptr<Impl> d;

    QVector<SupportBlocker> m_blockers;
    QVector<SupportEnforcer> m_enforcers;
    QVector<CustomSupportPoint> m_customSupports;

    // Utility
    bool isInBlocker(const Vector2& point) const;
    bool isInEnforcer(const Vector2& point) const;
    double overhangAngle(const Vector3& normal) const;
};

} // namespace printing
} // namespace ks