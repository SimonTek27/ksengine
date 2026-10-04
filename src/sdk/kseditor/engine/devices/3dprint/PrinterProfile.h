#pragma once

#include "3DPrintTypes.h"
#include <QObject>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QDateTime>

namespace ks {
namespace printing {

class PrinterProfile : public QObject
{
    Q_OBJECT
public:
    explicit PrinterProfile(QObject* parent = nullptr);
    ~PrinterProfile() override = default;

    // --- Identification ---
    QString id;                            // Unique ID (e.g., "prusa_mk4", "bambu_x1c")
    QString name;                          // Display name
    QString vendor;                        // Manufacturer
    QString model;                         // Model name
    QString variant;                       // Variant (e.g., "single_extruder", "mm1", "ams")

    // --- Build Volume ---
    double buildVolumeX = 250.0;           // mm
    double buildVolumeY = 210.0;
    double buildVolumeZ = 220.0;
    bool circularBed = false;
    double bedDiameter = 0.0;              // For delta/round beds
    double bedOriginX = 0.0;               // Bed center offset
    double bedOriginY = 0.0;

    // --- Nozzle/Extruder ---
    double nozzleDiameter = 0.4;           // mm
    int maxExtruders = 1;
    QVector<double> availableNozzleSizes = {0.25, 0.4, 0.6, 0.8};
    QVector<int> extruderCount = {1};      // Number of extruders per tool

    // --- G-code Flavor ---
    GCodeFlavor gcodeFlavor = GCodeFlavor::Marlin;

    // --- Firmware Capabilities ---
    bool hasHeatedBed = true;
    bool hasEnclosure = false;
    bool hasFilamentSensor = false;
    bool hasPowerLossRecovery = false;
    bool hasAutoBedLeveling = true;
    bool hasCamera = false;
    bool supportsArcMoves = false;         // G2/G3
    bool supportsVolumetricExtrusion = false;
    bool supportsFirmwareRetract = false;  // G10/G11
    bool supportsPressureAdvance = false;  // Linear advance / pressure advance
    double maxPressureAdvance = 0.0;
    bool supportsLinearAdvance = false;    // Prusa-style Linear Advance
    double maxLinearAdvance = 0.0;
    bool supportsInputShaping = false;     // Klipper input shaping
    bool supportsArcWelder = false;        // Arc Welder post-processing

    // --- Motion Limits ---
    double maxSpeedX = 200.0;              // mm/s
    double maxSpeedY = 200.0;
    double maxSpeedZ = 20.0;
    double maxSpeedE = 50.0;
    double maxAccelX = 1000.0;             // mm/s²
    double maxAccelY = 1000.0;
    double maxAccelZ = 100.0;
    double maxAccelE = 5000.0;
    double maxJerkX = 10.0;                // mm/s
    double maxJerkY = 10.0;
    double maxJerkZ = 2.0;
    double maxJerkE = 5.0;

    // --- Temperature Limits ---
    int maxHotendTemp = 300;               // °C
    int maxBedTemp = 120;                  // °C

    // --- Bed Surface ---
    QString bedSurface = "PEI";            // PEI, Glass, BuildTak, BuildPlate, Custom
    bool bedRequiresAdhesive = false;
    bool bedIsFlexible = false;

    // --- Custom G-code Scripts ---
    QString startGcode;
    QString endGcode;
    QString layerChangeGcode;
    QString toolChangeGcode;
    QString beforePrintGcode;
    QString afterPrintGcode;
    QString pauseGcode;
    QString resumeGcode;
    QString cancelGcode;

    // --- Slicing Defaults ---
    SliceSettings defaultSettings;

    // --- Thumbnail ---
    int thumbnailWidth = 400;
    int thumbnailHeight = 300;
    QString thumbnailFormat = "PNG";

    // --- Metadata ---
    QString description;
    QString website;
    QString firmwareVersion;
    QDateTime createdDate;
    QDateTime modifiedDate;
    int version = 1;
    QString author;
    QString license;

    // --- Machine-Specific Settings ---
    // Bowden vs Direct Drive
    bool isBowden = false;
    double bowdenTubeLength = 0.0;         // mm

    // Delta specific
    bool isDelta = false;
    double deltaRadius = 0.0;
    double deltaDiagonalRod = 0.0;
    double deltaSegmentsPerSecond = 200;

    // CoreXY specific
    bool isCoreXY = false;

    // IDEX / Tool Changer
    bool isIDEX = false;
    bool isToolChanger = false;
    QVector<Vector3> toolOffsets;          // Offsets for each tool

    // --- Validation ---
    bool validate(QString* error = nullptr) const;
    QString validationError() const;

    // --- Serialization ---
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

    // --- Built-in Profiles ---
    static QVector<PrinterProfile*> builtinProfiles();
    static PrinterProfile* createBuiltin(const QString& id);
    static QStringList builtinProfileIds();

    // --- Factory Methods for Popular Printers ---
    static PrinterProfile* createPrusaMK4();
    static PrinterProfile* createPrusaMK4S();
    static PrinterProfile* createPrusaXL();
    static PrinterProfile* createPrusaMINI();
    static PrinterProfile* createBambuX1C();
    static PrinterProfile* createBambuP1P();
    static PrinterProfile* createBambuP1S();
    static PrinterProfile* createBambuA1();
    static PrinterProfile* createBambuA1Mini();
    static PrinterProfile* createCrealityEnder3V2();
    static PrinterProfile* createCrealityEnder3V3();
    static PrinterProfile* createCrealityEnder3V3KE();
    static PrinterProfile* createCrealityK1();
    static PrinterProfile* createCrealityK1C();
    static PrinterProfile* createCrealityK1Max();
    static PrinterProfile* createVoron2_4();
    static PrinterProfile* createVoronTrident();
    static PrinterProfile* createRatRigVCore3();
    static PrinterProfile* createElegooNeptune4();
    static PrinterProfile* createAnycubicKobra2();
    static PrinterProfile* createSovolSV06();
    static PrinterProfile* createFlashforgeAdventurer5M();
    static PrinterProfile* createArtillerySidewinderX2();
    static PrinterProfile* createGenericMarlin();
    static PrinterProfile* createGenericKlipper();
    static PrinterProfile* createGenericRepRap();

signals:
    void profileChanged();

public slots:
    void updateModifiedDate();

private:
    void initDefaults();
};

} // namespace printing
} // namespace ks