#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QMatrix4x4>
#include <QColor>
#include <QImage>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QVariant>
#include <memory>
#include <vector>
#include <limits>

namespace ks {
namespace printing {

// ============================================================================
// Basic Geometry Types
// ============================================================================

struct Vector3 {
    double x = 0.0, y = 0.0, z = 0.0;

    Vector3() = default;
    Vector3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    explicit Vector3(const QVector3D& v) : x(v.x()), y(v.y()), z(v.z()) {}

    operator QVector3D() const { return QVector3D(x, y, z); }

    Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vector3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vector3 operator/(double s) const { return {x / s, y / s, z / s}; }

    double length() const { return std::sqrt(x*x + y*y + z*z); }
    double lengthSquared() const { return x*x + y*y + z*z; }
    Vector3 normalized() const { double l = length(); return l > 0 ? *this / l : Vector3(0,0,0); }

    double dot(const Vector3& o) const { return x*o.x + y*o.y + z*o.z; }
    Vector3 cross(const Vector3& o) const {
        return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x};
    }
};

struct Vector2 {
    double x = 0.0, y = 0.0;
    Vector2() = default;
    Vector2(double x_, double y_) : x(x_), y(y_) {}
    explicit Vector2(const QVector2D& v) : x(v.x()), y(v.y()) {}
    operator QVector2D() const { return QVector2D(x, y); }
};

struct BoundingBox {
    Vector3 min{std::numeric_limits<double>::max(),
                 std::numeric_limits<double>::max(),
                 std::numeric_limits<double>::max()};
    Vector3 max{std::numeric_limits<double>::lowest(),
                 std::numeric_limits<double>::lowest(),
                 std::numeric_limits<double>::lowest()};

    bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    Vector3 size() const { return max - min; }
    Vector3 center() const { return (min + max) * 0.5; }
    void expand(const Vector3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    void expand(const BoundingBox& other) { expand(other.min); expand(other.max); }
    bool contains(const Vector3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
};

// ============================================================================
// Mesh Types for Slicing
// ============================================================================

struct Triangle3D {
    Vector3 v[3];
    Vector3 normal;

    Triangle3D() = default;
    Triangle3D(const Vector3& a, const Vector3& b, const Vector3& c) : v{a, b, c} {
        normal = (v[1] - v[0]).cross(v[2] - v[0]).normalized();
    }

    BoundingBox bounds() const {
        BoundingBox bb;
        bb.expand(v[0]); bb.expand(v[1]); bb.expand(v[2]);
        return bb;
    }

    double minZ() const { return std::min({v[0].z, v[1].z, v[2].z}); }
    double maxZ() const { return std::max({v[0].z, v[1].z, v[2].z}); }

    bool intersectsPlane(double z) const {
        return minZ() <= z && maxZ() >= z;
    }
};

using MeshTriangles = std::vector<Triangle3D>;

// ============================================================================
// Slice Geometry (2D polygons per layer)
// ============================================================================

struct Polygon2D {
    std::vector<Vector2> vertices;  // Outer boundary (CCW)
    std::vector<std::vector<Vector2>> holes;  // Inner boundaries (CW)

    bool isValid() const { return vertices.size() >= 3; }
    double area() const;
    BoundingBox bounds2D() const;
    void translate(const Vector2& offset);
    void scale(double factor);
};

using Polygons2D = std::vector<Polygon2D>;

struct LayerSlice {
    double z = 0.0;                    // Layer height (Z position)
    double thickness = 0.0;            // Layer thickness
    Polygons2D perimeters;             // Outer walls
    Polygons2D infill;                 // Internal infill patterns
    Polygons2D support;                // Support structures
    Polygons2D brim;                   // Brim/skirt

    // Metadata
    int layerIndex = 0;
    bool isFirstLayer = false;
    bool isLastLayer = false;
    double printTimeEstimate = 0.0;    // Estimated time for this layer (seconds)
    double filamentUsed = 0.0;         // Estimated filament (mm)
};

using LayerSlices = std::vector<LayerSlice>;

// ============================================================================
// G-Code Types
// ============================================================================

enum class GCodeFlavor {
    Marlin,      // Creality, Prusa, most FDM printers
    RepRap,      // RepRap firmware
    Klipper,     // Klipper firmware
    Bambu,       // Bambu Lab (Marlin-based with extensions)
    Prusa,       // Prusa-specific (Marlin with Prusa extensions)
    Smoothieware,
    Repet,    Mach3
};

enum class GCodeCommandType {
    Move,           // G0/G1
    Arc,            // G2/G3
    Dwell,          // G4
    SetPosition,    // G92
    SetUnits,       // G20/G21
    SetOrigin,      // G28 (home)
    SetAbsolute,    // G90
    SetRelative,    // G91
    FanControl,     // M106/M107
    TempHotend,     // M104/M109
    TempBed,        // M140/M190
    ExtruderSelect, // T0/T1...
    Extrude,        // E axis movement
    Retract,        // G10/G11 or firmware retract
    Comment,
    Custom
};

struct GCodeLine {
    GCodeCommandType type = GCodeCommandType::Comment;
    QString rawCommand;
    QString comment;
    int lineNumber = 0;

    // Parsed parameters (when applicable)
    Vector3 position{0, 0, 0};
    double feedrate = 0;        // F parameter (mm/min)
    double extrudeAmount = 0;   // E parameter
    bool relativeExtrusion = false;
    bool relativePositioning = false;
};

using GCodeProgram = std::vector<GCodeLine>;

// ============================================================================
// Slicer Settings
// ============================================================================

enum class InfillPattern {
    Grid,           // Simple grid (90° crossing)
    Lines,          // Parallel lines (alternating direction per layer)
    Triangles,      // Triangular grid (60° crossing)
    Cubic,          // 3D cubic infill
    Gyroid,         // Gyroid minimal surface
    Concentric,     // Concentric perimeters inward
    Honeycomb,      // Hexagonal honeycomb
    Octet,          // Octet truss (3D)
    QuarterCubic,   // Quarter cubic (3D)
    Lightning,      // Lightning (organic, tree-like)
    Hilbert,        // Hilbert curve (space-filling)
    Archimedean     // Archimedean chords
};

enum class SupportType {
    None,
    Normal,         // Standard grid supports
    Tree,           // Tree/organic supports (like Cura)
    Snug,           // Tight supports touching model
    Custom          // User-defined support enforcement
};

enum class SupportPattern {
    Grid,
    Lines,
    Triangles,
    Concentric,
    Cross,
    Gyroid
};

enum class LayerFeatureType {
    Perimeter,
    Infill,
    Support,
    Brim,
    Travel
};

enum class BedAdhesionType {
    None,
    Skirt,          // Single outline around object
    Brim,           // Multiple outlines attached to object
    Raft            // Full raft under object
};

enum class QualityProfile {
    Draft,          // Fast, low quality (0.3mm layer)
    Standard,       // Balanced (0.2mm layer)
    High,           // High quality (0.15mm layer)
    Ultra,          // Ultra high quality (0.1mm layer)
    Custom
};

struct SliceSettings {
    // Printer
    QString printerProfileId;
    GCodeFlavor gcodeFlavor = GCodeFlavor::Marlin;

    // Quality
    QualityProfile quality = QualityProfile::Standard;
    double layerHeight = 0.2;              // mm
    double initialLayerHeight = 0.2;       // mm (first layer)
    double lineWidth = 0.4;                // mm (nozzle diameter typically)
    double initialLineWidth = 0.4;         // mm
    double firstLayerSpeed = 20.0;         // mm/s speed for first layer
    bool adaptiveLayerHeight = false;

    // Walls/Perimeters
    int wallCount = 2;                     // Number of perimeters
    int firstLayerWallCount = 0;           // Extra walls on first layer (0 = use wallCount)
    int topBottomLayers = 4;               // Solid top/bottom layers
    int bottomSolidLayers = 4;             // Solid bottom layers count
    int topSolidLayers = 4;                // Solid top layers count
    double wallThickness = 0.8;            // mm (wallCount * lineWidth)

    // Infill
    InfillPattern infillPattern = InfillPattern::Grid;
    double infillDensity = 0.15;           // 0.0 - 1.0 (15%)
    double infillLineDistance = 2.67;      // mm (calculated from density)
    double infillLineWidth = 0.0;          // mm (0 = use lineWidth)
    double infillPatternAngle = 0.0;       // degrees
    double infillOffsetPerLayer = 0.0;     // mm offset per layer
    int infillOverlap = 15;                // % overlap with walls
    bool infillBeforeWalls = false;

    // Extrusion
    double nozzleDiameter = 0.4;           // mm
    double filamentDiameter = 1.75;        // mm

    // Speed (mm/s)
    double printSpeed = 50.0;
    double outerWallSpeed = 30.0;
    double innerWallSpeed = 50.0;
    double infillSpeed = 60.0;
    double topBottomSpeed = 30.0;
    double travelSpeed = 150.0;
    double initialLayerSpeed = 20.0;
    double supportSpeed = 40.0;

    // Acceleration (mm/s^2)
    double printAccel = 500;
    double travelAccel = 1000;
    double retractAccel = 1500;

    // Jerk (mm/s)
    double printJerk = 8;
    double travelJerk = 20;

    // Temperature (Celsius)
    int nozzleTemp = 210;
    int initialNozzleTemp = 215;
    int bedTemp = 60;
    int initialBedTemp = 65;
    int standbyNozzleTemp = 150;

    // Cooling
    bool enableCooling = true;
    int fanSpeed = 100;                    // %
    int initialFanSpeed = 0;               // %
    int minFanSpeed = 30;                  // %
    double fanStartLayer = 2.0;            // Layer number
    double minLayerTime = 10.0;            // seconds

    // Retraction
    double retractionDistance = 5.0;       // mm (alias for retractDistance)
    double retractionSpeed = 40.0;         // mm/s (alias for retractSpeed)
    double retractDistance = 5.0;          // mm
    double retractSpeed = 40.0;            // mm/s
    double primeDistance = 0.0;            // mm (extra prime after retract)
    double minTravelForRetract = 1.5;      // mm
    bool retractionEnabled = true;         // Enable retraction
    bool zHopEnabled = false;
    double zHopHeight = 0.2;               // mm
    double zHopSpeed = 0.0;                // mm/s (0 = use travelSpeed)

    // Supports
    bool generateSupport = false;
    SupportType supportType = SupportType::None;
    SupportPattern supportPattern = SupportPattern::Grid;
    double supportDensity = 0.15;
    double supportAngle = 45.0;            // degrees (overhang angle)
    double supportXYDistance = 0.5;        // mm
    double supportZDistance = 0.2;         // mm
    int supportInterfaceLayers = 2;
    double supportInterfaceDensity = 1.0;  // 100% for interface
    double supportTreeBranchDiameter = 2.0;  // mm (for tree supports)
    double supportTreeBranchAngle = 45.0;  // degrees

    // Build plate adhesion
    BedAdhesionType adhesionType = BedAdhesionType::Skirt;
    int skirtLoops = 1;
    double skirtDistance = 2.0;            // mm
    int brimWidth = 5;                     // mm
    int raftLayers = 3;
    double raftOffset = 3.0;               // mm

    // Multi-material / Extruders
    int extruderCount = 1;
    std::vector<int> extruderTemps;        // Per-extruder temps
    QString extruderAssignments;           // Which parts use which extruder

    // Advanced
    bool enablePrimeTower = false;
    double primeTowerSize = 15.0;          // mm
    bool enableWipeTower = false;
    bool enableOozeShield = false;
    double oozeShieldDistance = 2.0;       // mm
    bool enableCombing = true;
    double maxTravelWithoutRetract = 10.0; // mm
    bool avoidCrossingPerimeters = true;
    double coastingDistance = 0.0;         // mm
    double wipeDistance = 0.0;             // mm
    bool enableInputShaping = false;

    // Output
    QString outputDirectory;
    QString outputFilename;
    bool includeComments = true;
    bool includeThumbnails = true;
    bool gzipOutput = false;

    // Validation
    bool validate() const;
    QString validationError() const;

    // Serialization
    QJsonObject toJson() const;
    static SliceSettings fromJson(const QJsonObject& obj);
    static SliceSettings createDefault(const QString& printerId = "");
    static SliceSettings createForQuality(QualityProfile q, const QString& printerId = "");
};

// ============================================================================
// Printer Profile (data-only struct; QObject wrapper is in PrinterProfile.h)
// ============================================================================

struct PrinterProfileData {
    QString id;                            // Unique ID (e.g., "prusa_mk4", "bambu_x1c")
    QString name;                          // Display name
    QString vendor;                        // Manufacturer
    QString model;                         // Model name
    QString variant;                       // Variant (e.g., "single_extruder", "mm1")

    // Build volume
    double buildVolumeX = 250.0;           // mm
    double buildVolumeY = 210.0;
    double buildVolumeZ = 220.0;
    bool circularBed = false;
    double bedDiameter = 0.0;              // For delta/round beds

    // Nozzle/Extruder
    double nozzleDiameter = 0.4;           // mm
    int maxExtruders = 1;
    std::vector<double> nozzleSizes;       // Available nozzle sizes

    // G-code flavor
    GCodeFlavor gcodeFlavor = GCodeFlavor::Marlin;

    // Firmware capabilities
    bool hasHeatedBed = true;
    bool hasEnclosure = false;
    bool hasFilamentSensor = false;
    bool hasPowerLossRecovery = false;
    bool hasAutoBedLeveling = true;
    bool supportsArcMoves = false;         // G2/G3
    bool supportsVolumetricExtrusion = false;
    bool supportsFirmwareRetract = false;
    bool supportsPressureAdvance = false;
    double maxPressureAdvance = 0.0;

    // Motion limits
    double maxSpeedX = 200;                // mm/s
    double maxSpeedY = 200;
    double maxSpeedZ = 20;
    double maxSpeedE = 50;
    double maxAccelX = 1000;
    double maxAccelY = 1000;
    double maxAccelZ = 100;
    double maxAccelE = 5000;
    double maxJerkX = 10;
    double maxJerkY = 10;
    double maxJerkZ = 2;
    double maxJerkE = 5;

    // Temperature limits
    int maxHotendTemp = 300;
    int maxBedTemp = 120;

    // Bed
    QString bedSurface = "PEI";            // PEI, Glass, BuildTak, etc.
    bool bedRequiresAdhesive = false;

    // Start/End G-code scripts
    QString startGcode;
    QString endGcode;
    QString layerChangeGcode;
    QString toolChangeGcode;
    QString beforePrintGcode;
    QString afterPrintGcode;

    // Slicing defaults
    SliceSettings defaultSettings;

    // Thumbnail settings
    int thumbnailWidth = 400;
    int thumbnailHeight = 300;
    QString thumbnailFormat = "PNG";       // PNG, BMP, GIF

    // Metadata
    QString description;
    QString website;
    QString firmwareVersion;
    QDateTime createdDate;
    QDateTime modifiedDate;
    int version = 1;

    // Validation
    bool validate() const;
    QString validationError() const;

    // Serialization
    QJsonObject toJson() const;
    static PrinterProfileData fromJson(const QJsonObject& obj);
    static PrinterProfileData createBuiltin(const QString& id);

    // Built-in profile IDs
    static QStringList builtinProfiles();
};

// ============================================================================
// Slice Result / Output
// ============================================================================

struct SliceInfo {
    QString printerProfileId;
    SliceSettings settings;
    BoundingBox boundingBox;

    // Statistics
    int totalLayers = 0;
    double printTime = 0.0;                // seconds
    double filamentUsed = 0.0;             // mm
    double filamentWeight = 0.0;           // grams
    double materialCost = 0.0;             // currency units
    double boundingBoxVolume = 0.0;        // mm^3
    double modelVolume = 0.0;              // mm^3
    double supportVolume = 0.0;            // mm^3

    // Per-layer data
    std::vector<double> layerHeights;
    LayerSlices slices;

    // Per-layer info
    struct LayerInfo {
        double z = 0;
        double printTime = 0;
        double filamentUsed = 0;
        int perimeterCount = 0;
        int infillLines = 0;
        int supportAreas = 0;
    };
    std::vector<LayerInfo> layerInfos;

    // Thumbnail
    QImage thumbnail;

    // G-code
    GCodeProgram gcode;
    QString gcodeText;                     // Raw G-code as string

    // Errors/warnings
    QStringList warnings;
    QStringList errors;
    bool success = false;

    QJsonObject toJson() const;
};

// ============================================================================
// Support Structures
// ============================================================================

struct SupportBlocker {
    BoundingBox region;
    QString name;
    bool enabled = true;
};

struct SupportEnforcer {
    BoundingBox region;
    QString name;
    bool enabled = true;
};

struct CustomSupportPoint {
    Vector3 base;      // Point on build plate
    Vector3 tip;       // Point on model
    double diameter = 2.0;
    bool enabled = true;
};

// ============================================================================
// Print Preview / Simulation
// ============================================================================

struct PrintPreviewFrame {
    int layerIndex = 0;
    double z = 0.0;
    Polygons2D geometry;           // All geometry at this layer
    QColor perimeterColor = QColor(200, 200, 200);
    QColor infillColor = QColor(100, 100, 255, 180);
    QColor supportColor = QColor(255, 100, 100, 180);
    QColor brimColor = QColor(255, 255, 0, 180);
    QColor travelColor = QColor(255, 0, 255, 100);
};

struct PrintSimulationState {
    double currentTime = 0.0;              // seconds
    int currentLayer = 0;
    double currentZ = 0.0;
    Vector3 nozzlePosition{0, 0, 0};
    double filamentExtruded = 0.0;
    double filamentRetracted = 0.0;
    int hotendTemp = 0;
    int bedTemp = 0;
    int fanSpeed = 0;
    bool isPrinting = false;
    bool isPaused = false;

    // For visualization
    std::vector<Vector3> printedPath;      // All printed segments
    std::vector<Vector3> travelPath;       // Travel moves

    // Per-layer simulation state
    std::vector<SliceInfo::LayerInfo> layerInfos;
};

// ============================================================================
// Utility Functions
// ============================================================================

namespace utils {
    // Convert QVector3D to Vector3
    inline Vector3 toVector3(const QVector3D& v) { return Vector3(v); }
    inline QVector3D toQVector3D(const Vector3& v) { return QVector3D(v.x, v.y, v.z); }

    // Convert QVector2D to Vector2
    inline Vector2 toVector2(const QVector2D& v) { return Vector2(v); }
    inline QVector2D toQVector2D(const Vector2& v) { return QVector2D(v.x, v.y); }

    // Mesh to triangles
    MeshTriangles meshToTriangles(const std::vector<QVector3D>& vertices,
                                   const std::vector<uint32_t>& indices);

    // Calculate bounding box from triangles
    BoundingBox calculateBounds(const MeshTriangles& triangles);

    // Calculate bounding box from vertices
    BoundingBox calculateBounds(const std::vector<Vector3>& vertices);

    // Format G-code value
    QString formatGCodeValue(double value, int precision = 3);
    QString formatGCodeValue(int value);

    // Parse G-code line
    GCodeLine parseGCodeLine(const QString& line, int lineNumber);

    // Estimate print time from G-code
    double estimatePrintTime(const GCodeProgram& gcode, const PrinterProfileData& printer);

    // Calculate filament usage from G-code
    double calculateFilamentUsage(const GCodeProgram& gcode, double filamentDiameter = 1.75);

    // Generate thumbnail from slices
    QImage generateThumbnail(const LayerSlices& slices, int width, int height);

    // Geometry utilities
    bool pointInPolygon(const Vector2& pt, const std::vector<Vector2>& poly);
    double polygonArea(const std::vector<Vector2>& verts);
    bool lineIntersection(const Vector2& a1, const Vector2& a2,
                          const Vector2& b1, const Vector2& b2, Vector2* out = nullptr);
}

} // namespace printing
} // namespace ks

// Q_DECLARE_METATYPE for QML integration
Q_DECLARE_METATYPE(ks::printing::SliceSettings)
Q_DECLARE_METATYPE(ks::printing::PrinterProfileData)
Q_DECLARE_METATYPE(ks::printing::SliceInfo)
Q_DECLARE_METATYPE(ks::printing::PrintPreviewFrame)
Q_DECLARE_METATYPE(ks::printing::PrintSimulationState)
Q_DECLARE_METATYPE(ks::printing::QualityProfile)
Q_DECLARE_METATYPE(ks::printing::InfillPattern)
Q_DECLARE_METATYPE(ks::printing::SupportType)
Q_DECLARE_METATYPE(ks::printing::BedAdhesionType)
Q_DECLARE_METATYPE(ks::printing::GCodeFlavor)