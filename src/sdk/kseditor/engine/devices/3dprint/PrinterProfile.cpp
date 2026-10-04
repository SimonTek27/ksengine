#include "PrinterProfile.h"
#include "3DPrintTypes.h"
#include <QJsonDocument>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

namespace ks {
namespace printing {

PrinterProfile::PrinterProfile(QObject* parent) : QObject(parent) {
    initDefaults();
}

void PrinterProfile::initDefaults() {
    // Default dates
    createdDate = QDateTime::currentDateTime();
    modifiedDate = QDateTime::currentDateTime();

    // Default start G-code (Marlin-style)
    startGcode = R"(
; Start G-code
G21 ; Set units to millimeters
G90 ; Absolute positioning
M82 ; Absolute extrusion
G28 ; Home all axes
G1 Z5 F5000 ; Lift nozzle
G1 X0 Y0 F5000 ; Move to front-left
M104 S{nozzle_temp} ; Set hotend temp
M140 S{bed_temp} ; Set bed temp
M190 S{bed_temp} ; Wait for bed temp
M109 S{nozzle_temp} ; Wait for hotend temp
G92 E0 ; Reset extruder
G1 X10 Y10 Z0.2 F3000 ; Move to start position
G1 X100 E15 F1000 ; Prime line
G92 E0 ; Reset extruder
G1 F{travel_speed}
)";

    endGcode = R"(
; End G-code
G91 ; Relative positioning
G1 E-2 F2700 ; Retract
G1 Z+5 F5000 ; Lift nozzle
G90 ; Absolute positioning
G1 X0 Y{bed_max_y} F5000 ; Move bed forward
M104 S0 ; Turn off hotend
M140 S0 ; Turn off bed
M84 ; Disable motors
)";

    layerChangeGcode = "; Layer {layer_num}\n";
    toolChangeGcode = "T{tool_index}\n";
    beforePrintGcode = "";
    afterPrintGcode = "";
    pauseGcode = "M600\n";
    resumeGcode = "";
    cancelGcode = "M112\n";

    // Default slicing settings
    defaultSettings.printerProfileId = id;
}

bool PrinterProfile::validate(QString* error) const {
    if (id.isEmpty()) {
        if (error) *error = "Printer profile ID is empty";
        return false;
    }
    if (name.isEmpty()) {
        if (error) *error = "Printer name is empty";
        return false;
    }
    if (buildVolumeX <= 0 || buildVolumeY <= 0 || buildVolumeZ <= 0) {
        if (error) *error = "Invalid build volume";
        return false;
    }
    if (nozzleDiameter <= 0 || nozzleDiameter > 2.0) {
        if (error) *error = "Invalid nozzle diameter";
        return false;
    }
    if (maxExtruders < 1 || maxExtruders > 8) {
        if (error) *error = "Invalid extruder count";
        return false;
    }
    if (maxHotendTemp < 0 || maxHotendTemp > 500) {
        if (error) *error = "Invalid max hotend temperature";
        return false;
    }
    if (maxBedTemp < 0 || maxBedTemp > 300) {
        if (error) *error = "Invalid max bed temperature";
        return false;
    }
    return true;
}

QString PrinterProfile::validationError() const {
    QString error;
    validate(&error);
    return error;
}

QJsonObject PrinterProfile::toJson() const {
    QJsonObject obj;

    obj["id"] = id;
    obj["name"] = name;
    obj["vendor"] = vendor;
    obj["model"] = model;
    obj["variant"] = variant;

    obj["buildVolumeX"] = buildVolumeX;
    obj["buildVolumeY"] = buildVolumeY;
    obj["buildVolumeZ"] = buildVolumeZ;
    obj["circularBed"] = circularBed;
    obj["bedDiameter"] = bedDiameter;
    obj["bedOriginX"] = bedOriginX;
    obj["bedOriginY"] = bedOriginY;

    obj["nozzleDiameter"] = nozzleDiameter;
    obj["maxExtruders"] = maxExtruders;
    QJsonArray nozzleSizes;
    for (double s : availableNozzleSizes) nozzleSizes.append(s);
    obj["availableNozzleSizes"] = nozzleSizes;
    QJsonArray extruderCounts;
    for (int c : extruderCount) extruderCounts.append(c);
    obj["extruderCount"] = extruderCounts;

    obj["gcodeFlavor"] = static_cast<int>(gcodeFlavor);

    obj["hasHeatedBed"] = hasHeatedBed;
    obj["hasEnclosure"] = hasEnclosure;
    obj["hasFilamentSensor"] = hasFilamentSensor;
    obj["hasPowerLossRecovery"] = hasPowerLossRecovery;
    obj["hasAutoBedLeveling"] = hasAutoBedLeveling;
    obj["hasCamera"] = hasCamera;
    obj["supportsArcMoves"] = supportsArcMoves;
    obj["supportsVolumetricExtrusion"] = supportsVolumetricExtrusion;
    obj["supportsFirmwareRetract"] = supportsFirmwareRetract;
    obj["supportsPressureAdvance"] = supportsPressureAdvance;
    obj["maxPressureAdvance"] = maxPressureAdvance;
    obj["supportsLinearAdvance"] = supportsLinearAdvance;
    obj["maxLinearAdvance"] = maxLinearAdvance;
    obj["supportsInputShaping"] = supportsInputShaping;
    obj["supportsArcWelder"] = supportsArcWelder;

    obj["maxSpeedX"] = maxSpeedX;
    obj["maxSpeedY"] = maxSpeedY;
    obj["maxSpeedZ"] = maxSpeedZ;
    obj["maxSpeedE"] = maxSpeedE;
    obj["maxAccelX"] = maxAccelX;
    obj["maxAccelY"] = maxAccelY;
    obj["maxAccelZ"] = maxAccelZ;
    obj["maxAccelE"] = maxAccelE;
    obj["maxJerkX"] = maxJerkX;
    obj["maxJerkY"] = maxJerkY;
    obj["maxJerkZ"] = maxJerkZ;
    obj["maxJerkE"] = maxJerkE;

    obj["maxHotendTemp"] = maxHotendTemp;
    obj["maxBedTemp"] = maxBedTemp;

    obj["bedSurface"] = bedSurface;
    obj["bedRequiresAdhesive"] = bedRequiresAdhesive;
    obj["bedIsFlexible"] = bedIsFlexible;

    obj["startGcode"] = startGcode;
    obj["endGcode"] = endGcode;
    obj["layerChangeGcode"] = layerChangeGcode;
    obj["toolChangeGcode"] = toolChangeGcode;
    obj["beforePrintGcode"] = beforePrintGcode;
    obj["afterPrintGcode"] = afterPrintGcode;
    obj["pauseGcode"] = pauseGcode;
    obj["resumeGcode"] = resumeGcode;
    obj["cancelGcode"] = cancelGcode;

    obj["defaultSettings"] = defaultSettings.toJson();

    obj["thumbnailWidth"] = thumbnailWidth;
    obj["thumbnailHeight"] = thumbnailHeight;
    obj["thumbnailFormat"] = thumbnailFormat;

    obj["description"] = description;
    obj["website"] = website;
    obj["firmwareVersion"] = firmwareVersion;
    obj["createdDate"] = createdDate.toString(Qt::ISODate);
    obj["modifiedDate"] = modifiedDate.toString(Qt::ISODate);
    obj["version"] = version;
    obj["author"] = author;
    obj["license"] = license;

    obj["isBowden"] = isBowden;
    obj["bowdenTubeLength"] = bowdenTubeLength;

    obj["isDelta"] = isDelta;
    obj["deltaRadius"] = deltaRadius;
    obj["deltaDiagonalRod"] = deltaDiagonalRod;
    obj["deltaSegmentsPerSecond"] = deltaSegmentsPerSecond;

    obj["isCoreXY"] = isCoreXY;
    obj["isIDEX"] = isIDEX;
    obj["isToolChanger"] = isToolChanger;

    QJsonArray toolOffsetsArr;
    for (const auto& offset : toolOffsets) {
        QJsonObject o;
        o["x"] = offset.x;
        o["y"] = offset.y;
        o["z"] = offset.z;
        toolOffsetsArr.append(o);
    }
    obj["toolOffsets"] = toolOffsetsArr;

    return obj;
}

void PrinterProfile::fromJson(const QJsonObject& obj) {
    auto getDouble = [&](const QString& key, double def) { return obj.contains(key) ? obj[key].toDouble() : def; };
    auto getInt = [&](const QString& key, int def) { return obj.contains(key) ? obj[key].toInt() : def; };
    auto getBool = [&](const QString& key, bool def) { return obj.contains(key) ? obj[key].toBool() : def; };
    auto getString = [&](const QString& key, const QString& def) { return obj.contains(key) ? obj[key].toString() : def; };

    id = getString("id", "");
    name = getString("name", "");
    vendor = getString("vendor", "");
    model = getString("model", "");
    variant = getString("variant", "");

    buildVolumeX = getDouble("buildVolumeX", 250.0);
    buildVolumeY = getDouble("buildVolumeY", 210.0);
    buildVolumeZ = getDouble("buildVolumeZ", 220.0);
    circularBed = getBool("circularBed", false);
    bedDiameter = getDouble("bedDiameter", 0.0);
    bedOriginX = getDouble("bedOriginX", 0.0);
    bedOriginY = getDouble("bedOriginY", 0.0);

    nozzleDiameter = getDouble("nozzleDiameter", 0.4);
    maxExtruders = getInt("maxExtruders", 1);
    availableNozzleSizes.clear();
    if (obj.contains("availableNozzleSizes")) {
        QJsonArray arr = obj["availableNozzleSizes"].toArray();
        for (const auto& val : arr) availableNozzleSizes.append(val.toDouble());
    }
    extruderCount.clear();
    if (obj.contains("extruderCount")) {
        QJsonArray arr = obj["extruderCount"].toArray();
        for (const auto& val : arr) extruderCount.append(val.toInt());
    }

    gcodeFlavor = static_cast<GCodeFlavor>(getInt("gcodeFlavor", static_cast<int>(GCodeFlavor::Marlin)));

    hasHeatedBed = getBool("hasHeatedBed", true);
    hasEnclosure = getBool("hasEnclosure", false);
    hasFilamentSensor = getBool("hasFilamentSensor", false);
    hasPowerLossRecovery = getBool("hasPowerLossRecovery", false);
    hasAutoBedLeveling = getBool("hasAutoBedLeveling", true);
    hasCamera = getBool("hasCamera", false);
    supportsArcMoves = getBool("supportsArcMoves", false);
    supportsVolumetricExtrusion = getBool("supportsVolumetricExtrusion", false);
    supportsFirmwareRetract = getBool("supportsFirmwareRetract", false);
    supportsPressureAdvance = getBool("supportsPressureAdvance", false);
    maxPressureAdvance = getDouble("maxPressureAdvance", 0.0);
    supportsLinearAdvance = getBool("supportsLinearAdvance", false);
    maxLinearAdvance = getDouble("maxLinearAdvance", 0.0);
    supportsInputShaping = getBool("supportsInputShaping", false);
    supportsArcWelder = getBool("supportsArcWelder", false);

    maxSpeedX = getDouble("maxSpeedX", 200.0);
    maxSpeedY = getDouble("maxSpeedY", 200.0);
    maxSpeedZ = getDouble("maxSpeedZ", 20.0);
    maxSpeedE = getDouble("maxSpeedE", 50.0);
    maxAccelX = getDouble("maxAccelX", 1000.0);
    maxAccelY = getDouble("maxAccelY", 1000.0);
    maxAccelZ = getDouble("maxAccelZ", 100.0);
    maxAccelE = getDouble("maxAccelE", 5000.0);
    maxJerkX = getDouble("maxJerkX", 10.0);
    maxJerkY = getDouble("maxJerkY", 10.0);
    maxJerkZ = getDouble("maxJerkZ", 2.0);
    maxJerkE = getDouble("maxJerkE", 5.0);

    maxHotendTemp = getInt("maxHotendTemp", 300);
    maxBedTemp = getInt("maxBedTemp", 120);

    bedSurface = getString("bedSurface", "PEI");
    bedRequiresAdhesive = getBool("bedRequiresAdhesive", false);
    bedIsFlexible = getBool("bedIsFlexible", false);

    startGcode = getString("startGcode", "");
    endGcode = getString("endGcode", "");
    layerChangeGcode = getString("layerChangeGcode", "");
    toolChangeGcode = getString("toolChangeGcode", "");
    beforePrintGcode = getString("beforePrintGcode", "");
    afterPrintGcode = getString("afterPrintGcode", "");
    pauseGcode = getString("pauseGcode", "");
    resumeGcode = getString("resumeGcode", "");
    cancelGcode = getString("cancelGcode", "");

    if (obj.contains("defaultSettings")) {
        defaultSettings.fromJson(obj["defaultSettings"].toObject());
    }

    thumbnailWidth = getInt("thumbnailWidth", 400);
    thumbnailHeight = getInt("thumbnailHeight", 300);
    thumbnailFormat = getString("thumbnailFormat", "PNG");

    description = getString("description", "");
    website = getString("website", "");
    firmwareVersion = getString("firmwareVersion", "");
    createdDate = QDateTime::fromString(getString("createdDate", ""), Qt::ISODate);
    if (!createdDate.isValid()) createdDate = QDateTime::currentDateTime();
    modifiedDate = QDateTime::fromString(getString("modifiedDate", ""), Qt::ISODate);
    if (!modifiedDate.isValid()) modifiedDate = QDateTime::currentDateTime();
    version = getInt("version", 1);
    author = getString("author", "");
    license = getString("license", "");

    isBowden = getBool("isBowden", false);
    bowdenTubeLength = getDouble("bowdenTubeLength", 0.0);

    isDelta = getBool("isDelta", false);
    deltaRadius = getDouble("deltaRadius", 0.0);
    deltaDiagonalRod = getDouble("deltaDiagonalRod", 0.0);
    deltaSegmentsPerSecond = getDouble("deltaSegmentsPerSecond", 200);

    isCoreXY = getBool("isCoreXY", false);
    isIDEX = getBool("isIDEX", false);
    isToolChanger = getBool("isToolChanger", false);

    toolOffsets.clear();
    if (obj.contains("toolOffsets")) {
        QJsonArray arr = obj["toolOffsets"].toArray();
        for (const auto& val : arr) {
            QJsonObject o = val.toObject();
            toolOffsets.append({o["x"].toDouble(), o["y"].toDouble(), o["z"].toDouble()});
        }
    }
}

void PrinterProfile::updateModifiedDate() {
    modifiedDate = QDateTime::currentDateTime();
    emit profileChanged();
}

// ============================================================================
// Built-in Printer Profiles
// ============================================================================

QStringList PrinterProfile::builtinProfileIds() {
    return {
        "prusa_mk4", "prusa_mk4s", "prusa_xl", "prusa_mini",
        "bambu_x1c", "bambu_p1p", "bambu_p1s", "bambu_a1", "bambu_a1_mini",
        "creality_ender3v2", "creality_ender3v3", "creality_ender3v3ke",
        "creality_k1", "creality_k1c", "creality_k1max",
        "voron_2_4", "voron_trident", "ratrig_vcore3",
        "elegoo_neptune4", "anycubic_kobra2", "sovol_sv06",
        "flashforge_adventurer5m", "artillery_sidewinder_x2",
        "generic_marlin", "generic_klipper", "generic_reprap"
    };
}

QVector<PrinterProfile*> PrinterProfile::builtinProfiles() {
    QVector<PrinterProfile*> profiles;
    for (const QString& id : builtinProfileIds()) {
        profiles.append(createBuiltin(id));
    }
    return profiles;
}

PrinterProfile* PrinterProfile::createBuiltin(const QString& id) {
    if (id == "prusa_mk4") return createPrusaMK4();
    if (id == "prusa_mk4s") return createPrusaMK4S();
    if (id == "prusa_xl") return createPrusaXL();
    if (id == "prusa_mini") return createPrusaMINI();
    if (id == "bambu_x1c") return createBambuX1C();
    if (id == "bambu_p1p") return createBambuP1P();
    if (id == "bambu_p1s") return createBambuP1S();
    if (id == "bambu_a1") return createBambuA1();
    if (id == "bambu_a1_mini") return createBambuA1Mini();
    if (id == "creality_ender3v2") return createCrealityEnder3V2();
    if (id == "creality_ender3v3") return createCrealityEnder3V3();
    if (id == "creality_ender3v3ke") return createCrealityEnder3V3KE();
    if (id == "creality_k1") return createCrealityK1();
    if (id == "creality_k1c") return createCrealityK1C();
    if (id == "creality_k1max") return createCrealityK1Max();
    if (id == "voron_2_4") return createVoron2_4();
    if (id == "voron_trident") return createVoronTrident();
    if (id == "ratrig_vcore3") return createRatRigVCore3();
    if (id == "elegoo_neptune4") return createElegooNeptune4();
    if (id == "anycubic_kobra2") return createAnycubicKobra2();
    if (id == "sovol_sv06") return createSovolSV06();
    if (id == "flashforge_adventurer5m") return createFlashforgeAdventurer5M();
    if (id == "artillery_sidewinder_x2") return createArtillerySidewinderX2();
    if (id == "generic_marlin") return createGenericMarlin();
    if (id == "generic_klipper") return createGenericKlipper();
    if (id == "generic_reprap") return createGenericRepRap();
    return createGenericMarlin();
}

// ============================================================================
// Specific Printer Implementations
// ============================================================================

PrinterProfile* PrinterProfile::createPrusaMK4() {
    auto* p = new PrinterProfile();
    p->id = "prusa_mk4";
    p->name = "Prusa MK4";
    p->vendor = "Prusa Research";
    p->model = "MK4";
    p->buildVolumeX = 250;
    p->buildVolumeY = 210;
    p->buildVolumeZ = 220;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->availableNozzleSizes = {0.25, 0.4, 0.6, 0.8};
    p->gcodeFlavor = GCodeFlavor::Prusa;
    p->hasHeatedBed = true;
    p->hasEnclosure = false;
    p->hasFilamentSensor = true;
    p->hasPowerLossRecovery = true;
    p->hasAutoBedLeveling = true;
    p->supportsLinearAdvance = true;
    p->maxLinearAdvance = 0.2;
    p->maxSpeedX = 200;
    p->maxSpeedY = 200;
    p->maxSpeedZ = 30;
    p->maxAccelX = 1000;
    p->maxAccelY = 1000;
    p->maxAccelZ = 200;
    p->maxHotendTemp = 300;
    p->maxBedTemp = 120;
    p->bedSurface = "PEI Textured";
    p->isBowden = false;
    p->description = "Prusa MK4 with Nextruder, Input Shaper, and Loadcell";
    p->website = "https://www.prusa3d.com/original-prusa-mk4/";
    p->startGcode = R"(
; Prusa MK4 Start G-code
M862.3 P"Prusa MK4" ; Model check
M862.1 P0.4 ; Nozzle diameter check
G21 ; Set units to millimeters
G90 ; Absolute positioning
M83 ; Relative extrusion
M104 S{nozzle_temp} ; Set hotend temp
M140 S{bed_temp} ; Set bed temp
M190 S{bed_temp} ; Wait for bed
M109 S{nozzle_temp} ; Wait for hotend
G28 ; Home all
G80 ; Bed mesh leveling
G1 Z0.2 F240
G92 E0
G1 X10 Y10 F5000
G1 X100 E15 F1000 ; Prime line
G92 E0
)";
    p->endGcode = R"(
; Prusa MK4 End G-code
M104 S0 ; Turn off hotend
M140 S0 ; Turn off bed
G91 ; Relative
G1 E-2 F2700 ; Retract
G1 Z5 F1000 ; Lift
G90 ; Absolute
G1 X0 Y210 F5000 ; Present print
M84 ; Disable motors
M300 S100 P1000 ; Beep
)";
    p->defaultSettings.nozzleTemp = 215;
    p->defaultSettings.bedTemp = 60;
    p->defaultSettings.retractDistance = 0.8;
    p->defaultSettings.retractSpeed = 35;
    p->defaultSettings.enableCombing = true;
    p->defaultSettings.avoidCrossingPerimeters = true;
    return p;
}

PrinterProfile* PrinterProfile::createPrusaMK4S() {
    auto* p = createPrusaMK4();
    p->id = "prusa_mk4s";
    p->name = "Prusa MK4S";
    p->model = "MK4S";
    p->maxExtruders = 5; // MMU3
    p->extruderCount = {1, 1, 1, 1, 1};
    p->isToolChanger = true;
    p->description = "Prusa MK4S with MMU3 (5 filaments)";
    return p;
}

PrinterProfile* PrinterProfile::createPrusaXL() {
    auto* p = new PrinterProfile();
    p->id = "prusa_xl";
    p->name = "Prusa XL";
    p->vendor = "Prusa Research";
    p->model = "XL";
    p->buildVolumeX = 360;
    p->buildVolumeY = 360;
    p->buildVolumeZ = 360;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 5;
    p->availableNozzleSizes = {0.25, 0.4, 0.6, 0.8};
    p->gcodeFlavor = GCodeFlavor::Prusa;
    p->hasHeatedBed = true;
    p->hasEnclosure = true;
    p->hasFilamentSensor = true;
    p->hasPowerLossRecovery = true;
    p->hasAutoBedLeveling = true;
    p->supportsLinearAdvance = true;
    p->isCoreXY = true;
    p->isToolChanger = true;
    p->toolOffsets = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    p->description = "Prusa XL - Large format CoreXY with tool changer";
    p->website = "https://www.prusa3d.com/original-prusa-xl/";
    p->defaultSettings.nozzleTemp = 215;
    p->defaultSettings.bedTemp = 60;
    return p;
}

PrinterProfile* PrinterProfile::createPrusaMINI() {
    auto* p = new PrinterProfile();
    p->id = "prusa_mini";
    p->name = "Prusa MINI+";
    p->vendor = "Prusa Research";
    p->model = "MINI+";
    p->buildVolumeX = 180;
    p->buildVolumeY = 180;
    p->buildVolumeZ = 180;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->availableNozzleSizes = {0.25, 0.4, 0.6};
    p->gcodeFlavor = GCodeFlavor::Prusa;
    p->hasHeatedBed = true;
    p->hasEnclosure = false;
    p->hasFilamentSensor = true;
    p->hasPowerLossRecovery = false;
    p->hasAutoBedLeveling = true;
    p->supportsLinearAdvance = true;
    p->isBowden = true;
    p->bowdenTubeLength = 650;
    p->description = "Prusa MINI+ - Compact Bowden printer";
    p->website = "https://www.prusa3d.com/original-prusa-mini/";
    p->defaultSettings.nozzleTemp = 215;
    p->defaultSettings.bedTemp = 60;
    p->defaultSettings.retractDistance = 3.2;
    p->defaultSettings.retractSpeed = 40;
    return p;
}

PrinterProfile* PrinterProfile::createBambuX1C() {
    auto* p = new PrinterProfile();
    p->id = "bambu_x1c";
    p->name = "Bambu Lab X1 Carbon";
    p->vendor = "Bambu Lab";
    p->model = "X1 Carbon";
    p->buildVolumeX = 256;
    p->buildVolumeY = 256;
    p->buildVolumeZ = 256;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->availableNozzleSizes = {0.2, 0.4, 0.6, 0.8};
    p->gcodeFlavor = GCodeFlavor::Bambu;
    p->hasHeatedBed = true;
    p->hasEnclosure = true;
    p->hasFilamentSensor = true;
    p->hasPowerLossRecovery = true;
    p->hasAutoBedLeveling = true;
    p->hasCamera = true;
    p->supportsInputShaping = true;
    p->supportsPressureAdvance = true;
    p->maxPressureAdvance = 0.05;
    p->maxSpeedX = 500;
    p->maxSpeedY = 500;
    p->maxSpeedZ = 100;
    p->maxAccelX = 10000;
    p->maxAccelY = 10000;
    p->maxAccelZ = 1000;
    p->maxHotendTemp = 300;
    p->maxBedTemp = 110;
    p->bedSurface = "PEI Textured";
    p->isCoreXY = true;
    p->description = "Bambu Lab X1 Carbon - High-speed CoreXY with AMS";
    p->website = "https://bambulab.com/x1-carbon";
    p->defaultSettings.nozzleTemp = 210;
    p->defaultSettings.bedTemp = 55;
    p->defaultSettings.printSpeed = 150;
    p->defaultSettings.travelSpeed = 300;
    p->defaultSettings.retractDistance = 0.8;
    p->defaultSettings.retractSpeed = 40;
    p->defaultSettings.enableInputShaping = true;
    return p;
}

PrinterProfile* PrinterProfile::createBambuP1P() {
    auto* p = createBambuX1C();
    p->id = "bambu_p1p";
    p->name = "Bambu Lab P1P";
    p->model = "P1P";
    p->buildVolumeX = 256;
    p->buildVolumeY = 256;
    p->buildVolumeZ = 256;
    p->hasEnclosure = false;
    p->hasCamera = false;
    p->description = "Bambu Lab P1P - High-speed CoreXY (no enclosure)";
    p->website = "https://bambulab.com/p1p";
    return p;
}

PrinterProfile* PrinterProfile::createBambuP1S() {
    auto* p = createBambuX1C();
    p->id = "bambu_p1s";
    p->name = "Bambu Lab P1S";
    p->model = "P1S";
    p->hasEnclosure = true;
    p->hasCamera = false;
    p->description = "Bambu Lab P1S - Enclosed high-speed CoreXY";
    p->website = "https://bambulab.com/p1s";
    return p;
}

PrinterProfile* PrinterProfile::createBambuA1() {
    auto* p = new PrinterProfile();
    p->id = "bambu_a1";
    p->name = "Bambu Lab A1";
    p->vendor = "Bambu Lab";
    p->model = "A1";
    p->buildVolumeX = 256;
    p->buildVolumeY = 256;
    p->buildVolumeZ = 256;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->availableNozzleSizes = {0.2, 0.4, 0.6, 0.8};
    p->gcodeFlavor = GCodeFlavor::Bambu;
    p->hasHeatedBed = true;
    p->hasEnclosure = false;
    p->hasFilamentSensor = true;
    p->hasPowerLossRecovery = true;
    p->hasAutoBedLeveling = true;
    p->supportsInputShaping = true;
    p->maxSpeedX = 500;
    p->maxSpeedY = 500;
    p->maxSpeedZ = 50;
    p->maxAccelX = 10000;
    p->maxAccelY = 10000;
    p->maxAccelZ = 500;
    p->maxHotendTemp = 300;
    p->maxBedTemp = 100;
    p->bedSurface = "PEI Textured";
    p->description = "Bambu Lab A1 - Bed-slinger with AMS Lite";
    p->website = "https://bambulab.com/a1";
    p->defaultSettings.nozzleTemp = 210;
    p->defaultSettings.bedTemp = 55;
    p->defaultSettings.printSpeed = 150;
    return p;
}

PrinterProfile* PrinterProfile::createBambuA1Mini() {
    auto* p = createBambuA1();
    p->id = "bambu_a1_mini";
    p->name = "Bambu Lab A1 mini";
    p->model = "A1 mini";
    p->buildVolumeX = 180;
    p->buildVolumeY = 180;
    p->buildVolumeZ = 180;
    p->description = "Bambu Lab A1 mini - Compact bed-slinger";
    p->website = "https://bambulab.com/a1-mini";
    return p;
}

PrinterProfile* PrinterProfile::createCrealityEnder3V2() {
    auto* p = new PrinterProfile();
    p->id = "creality_ender3v2";
    p->name = "Creality Ender 3 V2";
    p->vendor = "Creality";
    p->model = "Ender 3 V2";
    p->buildVolumeX = 220;
    p->buildVolumeY = 220;
    p->buildVolumeZ = 250;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->availableNozzleSizes = {0.2, 0.4, 0.6, 0.8};
    p->gcodeFlavor = GCodeFlavor::Marlin;
    p->hasHeatedBed = true;
    p->hasEnclosure = false;
    p->hasFilamentSensor = false;
    p->hasPowerLossRecovery = true;
    p->hasAutoBedLeveling = true;
    p->isBowden = true;
    p->bowdenTubeLength = 650;
    p->maxSpeedX = 180;
    p->maxSpeedY = 180;
    p->maxSpeedZ = 20;
    p->maxAccelX = 500;
    p->maxAccelY = 500;
    p->maxHotendTemp = 260;
    p->maxBedTemp = 100;
    p->bedSurface = "Carborundum Glass";
    p->description = "Creality Ender 3 V2 - Popular budget Bowden printer";
    p->website = "https://www.creality.com/goods-detail/ender-3-v2";
    p->defaultSettings.nozzleTemp = 200;
    p->defaultSettings.bedTemp = 60;
    p->defaultSettings.retractDistance = 5.0;
    p->defaultSettings.retractSpeed = 40;
    p->startGcode = R"(
; Ender 3 V2 Start G-code
G21
G90
M82
M104 S{nozzle_temp}
M140 S{bed_temp}
G28
M190 S{bed_temp}
M109 S{nozzle_temp}
G1 Z5 F5000
G1 X0.1 Y20 Z0.3 F5000
G92 E0
G1 X0.1 Y200.0 E15 F1500
G92 E0
)";
    p->endGcode = R"(
; Ender 3 V2 End G-code
M104 S0
M140 S0
G91
G1 E-2 F2700
G1 Z5 F1000
G90
G1 X0 Y220 F5000
M84
)";
    return p;
}

PrinterProfile* PrinterProfile::createCrealityEnder3V3() {
    auto* p = createCrealityEnder3V2();
    p->id = "creality_ender3v3";
    p->name = "Creality Ender 3 V3";
    p->model = "Ender 3 V3";
    p->hasAutoBedLeveling = true;
    p->maxSpeedX = 250;
    p->maxSpeedY = 250;
    p->maxAccelX = 2000;
    p->maxAccelY = 2000;
    p->description = "Creality Ender 3 V3 - Direct drive upgrade";
    p->isBowden = false;
    p->defaultSettings.retractDistance = 0.8;
    return p;
}

PrinterProfile* PrinterProfile::createCrealityEnder3V3KE() {
    auto* p = createCrealityEnder3V3();
    p->id = "creality_ender3v3ke";
    p->name = "Creality Ender 3 V3 KE";
    p->model = "Ender 3 V3 KE";
    p->hasAutoBedLeveling = true;
    p->description = "Creality Ender 3 V3 KE - Klipper-based";
    p->gcodeFlavor = GCodeFlavor::Klipper;
    p->supportsInputShaping = true;
    return p;
}

PrinterProfile* PrinterProfile::createCrealityK1() {
    auto* p = new PrinterProfile();
    p->id = "creality_k1";
    p->name = "Creality K1";
    p->vendor = "Creality";
    p->model = "K1";
    p->buildVolumeX = 220;
    p->buildVolumeY = 220;
    p->buildVolumeZ = 220;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->gcodeFlavor = GCodeFlavor::Klipper;
    p->hasHeatedBed = true;
    p->hasEnclosure = true;
    p->hasFilamentSensor = true;
    p->hasPowerLossRecovery = true;
    p->hasAutoBedLeveling = true;
    p->supportsInputShaping = true;
    p->isCoreXY = true;
    p->maxSpeedX = 600;
    p->maxSpeedY = 600;
    p->maxAccelX = 20000;
    p->maxAccelY = 20000;
    p->maxHotendTemp = 300;
    p->maxBedTemp = 100;
    p->description = "Creality K1 - High-speed CoreXY with Klipper";
    p->website = "https://www.creality.com/goods-detail/k1";
    p->defaultSettings.nozzleTemp = 210;
    p->defaultSettings.bedTemp = 60;
    p->defaultSettings.printSpeed = 300;
    p->defaultSettings.travelSpeed = 400;
    return p;
}

PrinterProfile* PrinterProfile::createCrealityK1C() {
    auto* p = createCrealityK1();
    p->id = "creality_k1c";
    p->name = "Creality K1C";
    p->model = "K1C";
    p->maxHotendTemp = 300;
    p->description = "Creality K1C - High-temp CoreXY for engineering materials";
    return p;
}

PrinterProfile* PrinterProfile::createCrealityK1Max() {
    auto* p = createCrealityK1();
    p->id = "creality_k1max";
    p->name = "Creality K1 Max";
    p->model = "K1 Max";
    p->buildVolumeX = 300;
    p->buildVolumeY = 300;
    p->buildVolumeZ = 300;
    p->hasCamera = true;
    p->description = "Creality K1 Max - Large format high-speed CoreXY";
    return p;
}

PrinterProfile* PrinterProfile::createVoron2_4() {
    auto* p = new PrinterProfile();
    p->id = "voron_2_4";
    p->name = "Voron 2.4";
    p->vendor = "Voron Design";
    p->model = "2.4";
    p->buildVolumeX = 300;
    p->buildVolumeY = 300;
    p->buildVolumeZ = 300;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->gcodeFlavor = GCodeFlavor::Klipper;
    p->hasHeatedBed = true;
    p->hasEnclosure = true;
    p->supportsInputShaping = true;
    p->supportsPressureAdvance = true;
    p->isCoreXY = true;
    p->maxSpeedX = 300;
    p->maxSpeedY = 300;
    p->maxAccelX = 5000;
    p->maxAccelY = 5000;
    p->maxHotendTemp = 300;
    p->maxBedTemp = 130;
    p->description = "Voron 2.4 - High-performance CoreXY DIY kit";
    p->website = "https://vorondesign.com/voron2.4";
    p->defaultSettings.nozzleTemp = 210;
    p->defaultSettings.bedTemp = 60;
    return p;
}

PrinterProfile* PrinterProfile::createVoronTrident() {
    auto* p = createVoron2_4();
    p->id = "voron_trident";
    p->name = "Voron Trident";
    p->model = "Trident";
    p->buildVolumeX = 300;
    p->buildVolumeY = 300;
    p->buildVolumeZ = 300;
    p->description = "Voron Trident - Triple leadscrew CoreXY";
    p->website = "https://vorondesign.com/trident";
    return p;
}

PrinterProfile* PrinterProfile::createRatRigVCore3() {
    auto* p = createVoron2_4();
    p->id = "ratrig_vcore3";
    p->name = "RatRig V-Core 3";
    p->vendor = "RatRig";
    p->model = "V-Core 3";
    p->buildVolumeX = 300;
    p->buildVolumeY = 300;
    p->buildVolumeZ = 300;
    p->description = "RatRig V-Core 3 - Open source CoreXY";
    p->website = "https://ratrig.com/vcore3";
    return p;
}

PrinterProfile* PrinterProfile::createElegooNeptune4() {
    auto* p = createCrealityEnder3V2();
    p->id = "elegoo_neptune4";
    p->name = "Elegoo Neptune 4";
    p->vendor = "Elegoo";
    p->model = "Neptune 4";
    p->maxSpeedX = 500;
    p->maxSpeedY = 500;
    p->maxAccelX = 5000;
    p->maxAccelY = 5000;
    p->gcodeFlavor = GCodeFlavor::Klipper;
    p->supportsInputShaping = true;
    p->description = "Elegoo Neptune 4 - Klipper-based budget printer";
    p->website = "https://www.elegoo.com/neptune-4";
    p->defaultSettings.printSpeed = 150;
    return p;
}

PrinterProfile* PrinterProfile::createAnycubicKobra2() {
    auto* p = createElegooNeptune4();
    p->id = "anycubic_kobra2";
    p->name = "Anycubic Kobra 2";
    p->vendor = "Anycubic";
    p->model = "Kobra 2";
    p->description = "Anycubic Kobra 2 - High-speed budget printer";
    p->website = "https://www.anycubic.com/kobra-2";
    return p;
}

PrinterProfile* PrinterProfile::createSovolSV06() {
    auto* p = createCrealityEnder3V2();
    p->id = "sovol_sv06";
    p->name = "Sovol SV06";
    p->vendor = "Sovol";
    p->model = "SV06";
    p->gcodeFlavor = GCodeFlavor::Klipper;
    p->supportsInputShaping = true;
    p->maxSpeedX = 300;
    p->maxSpeedY = 300;
    p->maxAccelX = 3000;
    p->maxAccelY = 3000;
    p->description = "Sovol SV06 - Budget Klipper printer";
    p->website = "https://sovol3d.com/sv06";
    return p;
}

PrinterProfile* PrinterProfile::createFlashforgeAdventurer5M() {
    auto* p = new PrinterProfile();
    p->id = "flashforge_adventurer5m";
    p->name = "Flashforge Adventurer 5M";
    p->vendor = "Flashforge";
    p->model = "Adventurer 5M";
    p->buildVolumeX = 220;
    p->buildVolumeY = 220;
    p->buildVolumeZ = 220;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->gcodeFlavor = GCodeFlavor::Marlin;
    p->hasHeatedBed = true;
    p->hasEnclosure = true;
    p->hasFilamentSensor = true;
    p->hasAutoBedLeveling = true;
    p->hasCamera = true;
    p->supportsInputShaping = true;
    p->isCoreXY = true;
    p->maxSpeedX = 300;
    p->maxSpeedY = 300;
    p->maxAccelX = 4000;
    p->maxAccelY = 4000;
    p->maxHotendTemp = 280;
    p->maxBedTemp = 100;
    p->description = "Flashforge Adventurer 5M - Enclosed CoreXY";
    p->website = "https://www.flashforge.com/adventurer-5m";
    p->defaultSettings.nozzleTemp = 210;
    p->defaultSettings.bedTemp = 60;
    return p;
}

PrinterProfile* PrinterProfile::createArtillerySidewinderX2() {
    auto* p = createCrealityEnder3V2();
    p->id = "artillery_sidewinder_x2";
    p->name = "Artillery Sidewinder X2";
    p->vendor = "Artillery";
    p->model = "Sidewinder X2";
    p->buildVolumeX = 300;
    p->buildVolumeY = 300;
    p->buildVolumeZ = 400;
    p->isBowden = false;
    p->description = "Artillery Sidewinder X2 - Large format direct drive";
    p->website = "https://www.artillery3d.com/sidewinder-x2";
    p->defaultSettings.bedTemp = 60;
    return p;
}

PrinterProfile* PrinterProfile::createGenericMarlin() {
    auto* p = new PrinterProfile();
    p->id = "generic_marlin";
    p->name = "Generic Marlin Printer";
    p->vendor = "Generic";
    p->model = "Marlin";
    p->buildVolumeX = 220;
    p->buildVolumeY = 220;
    p->buildVolumeZ = 250;
    p->nozzleDiameter = 0.4;
    p->maxExtruders = 1;
    p->gcodeFlavor = GCodeFlavor::Marlin;
    p->hasHeatedBed = true;
    p->hasAutoBedLeveling = false;
    p->maxSpeedX = 150;
    p->maxSpeedY = 150;
    p->maxAccelX = 500;
    p->maxAccelY = 500;
    p->maxHotendTemp = 260;
    p->maxBedTemp = 110;
    p->description = "Generic Marlin firmware printer";
    p->defaultSettings.nozzleTemp = 200;
    p->defaultSettings.bedTemp = 60;
    p->defaultSettings.retractDistance = 5.0;
    p->defaultSettings.retractSpeed = 40;
    return p;
}

PrinterProfile* PrinterProfile::createGenericKlipper() {
    auto* p = createGenericMarlin();
    p->id = "generic_klipper";
    p->name = "Generic Klipper Printer";
    p->model = "Klipper";
    p->gcodeFlavor = GCodeFlavor::Klipper;
    p->supportsInputShaping = true;
    p->supportsPressureAdvance = true;
    p->maxSpeedX = 300;
    p->maxSpeedY = 300;
    p->maxAccelX = 3000;
    p->maxAccelY = 3000;
    p->description = "Generic Klipper firmware printer";
    p->defaultSettings.printSpeed = 100;
    p->defaultSettings.retractDistance = 0.8;
    return p;
}

PrinterProfile* PrinterProfile::createGenericRepRap() {
    auto* p = createGenericMarlin();
    p->id = "generic_reprap";
    p->name = "Generic RepRap Printer";
    p->model = "RepRap";
    p->gcodeFlavor = GCodeFlavor::RepRap;
    p->description = "Generic RepRap firmware printer";
    return p;
}

} // namespace printing
} // namespace ks