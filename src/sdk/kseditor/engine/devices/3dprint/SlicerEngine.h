#pragma once

#include "3DPrintTypes.h"
#include "PrinterProfile.h"
#include <QObject>
#include <functional>

namespace ks {
namespace printing {

class SliceSettings;
class SupportGenerator;

class SlicerEngine : public QObject
{
    Q_OBJECT
public:
    explicit SlicerEngine(QObject* parent = nullptr);
    ~SlicerEngine();

    // ========================================================================
    // Main Slicing Interface
    // ========================================================================

    // Slice a mesh from triangles
    SliceInfo slice(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer);

    // Slice from scene objects (multiple meshes)
    SliceInfo sliceScene(const std::vector<MeshTriangles>& objects, const SliceSettings& settings, const PrinterProfile& printer);

    // Async slicing with progress callback
    using ProgressCallback = std::function<void(int percent, const QString& message)>;
    using CompleteCallback = std::function<void(const SliceInfo& result)>;

    void sliceAsync(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer,
                    ProgressCallback progress, CompleteCallback complete);

    // Cancel ongoing async slice
    void cancel();

    // ========================================================================
    // Slicing Pipeline Steps (can be called individually)
    // ========================================================================

    // Step 1: Generate layer Z heights
    std::vector<double> generateLayerHeights(const BoundingBox& bounds, const SliceSettings& settings);

    // Step 2: Intersect mesh with plane to get 2D polygons
    Polygons2D intersectMeshAtZ(const MeshTriangles& triangles, double z, double thickness);

    // Step 3: Generate perimeters (offset polygons inward)
    Polygons2D generatePerimeters(const Polygons2D& crossSection, const SliceSettings& settings, int layerIndex);

    // Step 4: Generate infill
    Polygons2D generateInfill(const Polygons2D& perimeters, const SliceSettings& settings, int layerIndex);

    // Step 5: Generate support structures (requires SupportGenerator)
    Polygons2D generateSupports(const LayerSlices& slices, const SliceSettings& settings, const SupportGenerator* supportGen = nullptr);

    // Step 6: Generate brim/skirt/raft
    Polygons2D generateBedAdhesion(const Polygons2D& firstLayerPerimeters, const SliceSettings& settings);

    // ========================================================================
    // Utility
    // ========================================================================

    // Check if model fits on build plate
    bool checkBuildVolume(const BoundingBox& bounds, const PrinterProfile& printer, QString* error = nullptr) const;

    // Calculate optimal orientation (basic)
    QMatrix4x4 suggestOrientation(const MeshTriangles& triangles);

    // Estimate print time without full slicing
    double estimatePrintTimeQuick(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer);

signals:
    void sliceProgress(int percent, const QString& message);
    void sliceComplete(const SliceInfo& result);
    void sliceError(const QString& error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;

    // Internal state for async slicing
    bool m_cancelRequested = false;
    MeshTriangles m_currentTriangles;
    SliceSettings m_currentSettings;
    const PrinterProfile* m_currentPrinter = nullptr;
    ProgressCallback m_progressCallback;
    CompleteCallback m_completeCallback;

    void processSlice();
    LayerSlices processLayers(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer);
    LayerSlice processSingleLayer(const MeshTriangles& triangles, double z, double thickness, const SliceSettings& settings, int layerIndex, int totalLayers);
};

} // namespace printing
} // namespace ks