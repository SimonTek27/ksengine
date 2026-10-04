#include "3DPrintTypes.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonDocument>

namespace ks {
namespace printing {

SliceSettings SliceSettings::createDefault(const QString& printerId)
{
    SliceSettings s;
    s.printerProfileId = printerId;
    return s;
}

SliceSettings SliceSettings::createForQuality(QualityProfile q, const QString& printerId)
{
    SliceSettings s = createDefault(printerId);
    s.quality = q;
    switch (q) {
        case QualityProfile::Draft:    s.layerHeight = 0.30; s.printSpeed = 60.0; break;
        case QualityProfile::Standard: s.layerHeight = 0.20; s.printSpeed = 50.0; break;
        case QualityProfile::High:     s.layerHeight = 0.15; s.printSpeed = 40.0; break;
        case QualityProfile::Ultra:    s.layerHeight = 0.10; s.printSpeed = 30.0; break;
        default: break;
    }
    return s;
}

QJsonObject SliceSettings::toJson() const
{
    QJsonObject obj;
    obj["printerProfileId"] = printerProfileId;
    obj["gcodeFlavor"] = static_cast<int>(gcodeFlavor);
    obj["quality"] = static_cast<int>(quality);
    obj["layerHeight"] = layerHeight;
    obj["initialLayerHeight"] = initialLayerHeight;
    obj["lineWidth"] = lineWidth;
    obj["initialLineWidth"] = initialLineWidth;
    obj["firstLayerSpeed"] = firstLayerSpeed;
    obj["adaptiveLayerHeight"] = adaptiveLayerHeight;
    obj["wallCount"] = wallCount;
    obj["firstLayerWallCount"] = firstLayerWallCount;
    obj["topBottomLayers"] = topBottomLayers;
    obj["bottomSolidLayers"] = bottomSolidLayers;
    obj["topSolidLayers"] = topSolidLayers;
    obj["wallThickness"] = wallThickness;
    obj["infillPattern"] = static_cast<int>(infillPattern);
    obj["infillDensity"] = infillDensity;
    obj["infillLineDistance"] = infillLineDistance;
    obj["infillLineWidth"] = infillLineWidth;
    obj["infillPatternAngle"] = infillPatternAngle;
    obj["infillOffsetPerLayer"] = infillOffsetPerLayer;
    obj["infillOverlap"] = infillOverlap;
    obj["infillBeforeWalls"] = infillBeforeWalls;
    obj["nozzleDiameter"] = nozzleDiameter;
    obj["filamentDiameter"] = filamentDiameter;
    obj["printSpeed"] = printSpeed;
    obj["outerWallSpeed"] = outerWallSpeed;
    obj["innerWallSpeed"] = innerWallSpeed;
    obj["infillSpeed"] = infillSpeed;
    obj["topBottomSpeed"] = topBottomSpeed;
    obj["travelSpeed"] = travelSpeed;
    obj["initialLayerSpeed"] = initialLayerSpeed;
    obj["supportSpeed"] = supportSpeed;
    obj["printAccel"] = printAccel;
    obj["travelAccel"] = travelAccel;
    obj["retractAccel"] = retractAccel;
    obj["printJerk"] = printJerk;
    obj["travelJerk"] = travelJerk;
    obj["nozzleTemp"] = nozzleTemp;
    obj["initialNozzleTemp"] = initialNozzleTemp;
    obj["bedTemp"] = bedTemp;
    obj["initialBedTemp"] = initialBedTemp;
    obj["standbyNozzleTemp"] = standbyNozzleTemp;
    obj["enableCooling"] = enableCooling;
    obj["fanSpeed"] = fanSpeed;
    obj["initialFanSpeed"] = initialFanSpeed;
    obj["minFanSpeed"] = minFanSpeed;
    obj["fanStartLayer"] = fanStartLayer;
    obj["minLayerTime"] = minLayerTime;
    obj["retractionDistance"] = retractionDistance;
    obj["retractionSpeed"] = retractionSpeed;
    obj["retractDistance"] = retractDistance;
    obj["retractSpeed"] = retractSpeed;
    obj["primeDistance"] = primeDistance;
    obj["minTravelForRetract"] = minTravelForRetract;
    obj["retractionEnabled"] = retractionEnabled;
    obj["zHopEnabled"] = zHopEnabled;
    obj["zHopHeight"] = zHopHeight;
    obj["zHopSpeed"] = zHopSpeed;
    obj["generateSupport"] = generateSupport;
    obj["supportType"] = static_cast<int>(supportType);
    obj["supportPattern"] = static_cast<int>(supportPattern);
    obj["supportDensity"] = supportDensity;
    obj["supportAngle"] = supportAngle;
    obj["supportXYDistance"] = supportXYDistance;
    obj["supportZDistance"] = supportZDistance;
    obj["supportInterfaceLayers"] = supportInterfaceLayers;
    obj["supportInterfaceDensity"] = supportInterfaceDensity;
    obj["supportTreeBranchDiameter"] = supportTreeBranchDiameter;
    obj["supportTreeBranchAngle"] = supportTreeBranchAngle;
    obj["adhesionType"] = static_cast<int>(adhesionType);
    obj["skirtLoops"] = skirtLoops;
    obj["skirtDistance"] = skirtDistance;
    obj["brimWidth"] = brimWidth;
    obj["raftLayers"] = raftLayers;
    obj["raftOffset"] = raftOffset;
    obj["extruderCount"] = extruderCount;
    obj["extruderTemps"] = QJsonArray::fromVariantList(QVariant::fromValue(extruderTemps).toList());
    obj["extruderAssignments"] = extruderAssignments;
    obj["enablePrimeTower"] = enablePrimeTower;
    obj["primeTowerSize"] = primeTowerSize;
    obj["enableWipeTower"] = enableWipeTower;
    obj["enableOozeShield"] = enableOozeShield;
    obj["oozeShieldDistance"] = oozeShieldDistance;
    obj["enableCombing"] = enableCombing;
    obj["maxTravelWithoutRetract"] = maxTravelWithoutRetract;
    obj["avoidCrossingPerimeters"] = avoidCrossingPerimeters;
    obj["coastingDistance"] = coastingDistance;
    obj["wipeDistance"] = wipeDistance;
    obj["enableInputShaping"] = enableInputShaping;
    obj["outputDirectory"] = outputDirectory;
    obj["outputFilename"] = outputFilename;
    obj["includeComments"] = includeComments;
    obj["includeThumbnails"] = includeThumbnails;
    obj["gzipOutput"] = gzipOutput;
    return obj;
}

SliceSettings SliceSettings::fromJson(const QJsonObject& obj)
{
    SliceSettings s;
    if (obj.contains("printerProfileId")) s.printerProfileId = obj["printerProfileId"].toString();
    if (obj.contains("gcodeFlavor")) s.gcodeFlavor = static_cast<GCodeFlavor>(obj["gcodeFlavor"].toInt());
    if (obj.contains("quality")) s.quality = static_cast<QualityProfile>(obj["quality"].toInt());
    if (obj.contains("layerHeight")) s.layerHeight = obj["layerHeight"].toDouble();
    if (obj.contains("initialLayerHeight")) s.initialLayerHeight = obj["initialLayerHeight"].toDouble();
    if (obj.contains("lineWidth")) s.lineWidth = obj["lineWidth"].toDouble();
    if (obj.contains("initialLineWidth")) s.initialLineWidth = obj["initialLineWidth"].toDouble();
    if (obj.contains("firstLayerSpeed")) s.firstLayerSpeed = obj["firstLayerSpeed"].toDouble();
    if (obj.contains("adaptiveLayerHeight")) s.adaptiveLayerHeight = obj["adaptiveLayerHeight"].toBool();
    if (obj.contains("wallCount")) s.wallCount = obj["wallCount"].toInt();
    if (obj.contains("firstLayerWallCount")) s.firstLayerWallCount = obj["firstLayerWallCount"].toInt();
    if (obj.contains("topBottomLayers")) s.topBottomLayers = obj["topBottomLayers"].toInt();
    if (obj.contains("bottomSolidLayers")) s.bottomSolidLayers = obj["bottomSolidLayers"].toInt();
    if (obj.contains("topSolidLayers")) s.topSolidLayers = obj["topSolidLayers"].toInt();
    if (obj.contains("wallThickness")) s.wallThickness = obj["wallThickness"].toDouble();
    if (obj.contains("infillPattern")) s.infillPattern = static_cast<InfillPattern>(obj["infillPattern"].toInt());
    if (obj.contains("infillDensity")) s.infillDensity = obj["infillDensity"].toDouble();
    if (obj.contains("infillLineDistance")) s.infillLineDistance = obj["infillLineDistance"].toDouble();
    if (obj.contains("infillLineWidth")) s.infillLineWidth = obj["infillLineWidth"].toDouble();
    if (obj.contains("infillPatternAngle")) s.infillPatternAngle = obj["infillPatternAngle"].toDouble();
    if (obj.contains("infillOffsetPerLayer")) s.infillOffsetPerLayer = obj["infillOffsetPerLayer"].toDouble();
    if (obj.contains("infillOverlap")) s.infillOverlap = obj["infillOverlap"].toInt();
    if (obj.contains("infillBeforeWalls")) s.infillBeforeWalls = obj["infillBeforeWalls"].toBool();
    if (obj.contains("nozzleDiameter")) s.nozzleDiameter = obj["nozzleDiameter"].toDouble();
    if (obj.contains("filamentDiameter")) s.filamentDiameter = obj["filamentDiameter"].toDouble();
    if (obj.contains("printSpeed")) s.printSpeed = obj["printSpeed"].toDouble();
    if (obj.contains("outerWallSpeed")) s.outerWallSpeed = obj["outerWallSpeed"].toDouble();
    if (obj.contains("innerWallSpeed")) s.innerWallSpeed = obj["innerWallSpeed"].toDouble();
    if (obj.contains("infillSpeed")) s.infillSpeed = obj["infillSpeed"].toDouble();
    if (obj.contains("topBottomSpeed")) s.topBottomSpeed = obj["topBottomSpeed"].toDouble();
    if (obj.contains("travelSpeed")) s.travelSpeed = obj["travelSpeed"].toDouble();
    if (obj.contains("initialLayerSpeed")) s.initialLayerSpeed = obj["initialLayerSpeed"].toDouble();
    if (obj.contains("supportSpeed")) s.supportSpeed = obj["supportSpeed"].toDouble();
    if (obj.contains("printAccel")) s.printAccel = obj["printAccel"].toDouble();
    if (obj.contains("travelAccel")) s.travelAccel = obj["travelAccel"].toDouble();
    if (obj.contains("retractAccel")) s.retractAccel = obj["retractAccel"].toDouble();
    if (obj.contains("printJerk")) s.printJerk = obj["printJerk"].toDouble();
    if (obj.contains("travelJerk")) s.travelJerk = obj["travelJerk"].toDouble();
    if (obj.contains("nozzleTemp")) s.nozzleTemp = obj["nozzleTemp"].toInt();
    if (obj.contains("initialNozzleTemp")) s.initialNozzleTemp = obj["initialNozzleTemp"].toInt();
    if (obj.contains("bedTemp")) s.bedTemp = obj["bedTemp"].toInt();
    if (obj.contains("initialBedTemp")) s.initialBedTemp = obj["initialBedTemp"].toInt();
    if (obj.contains("standbyNozzleTemp")) s.standbyNozzleTemp = obj["standbyNozzleTemp"].toInt();
    if (obj.contains("enableCooling")) s.enableCooling = obj["enableCooling"].toBool();
    if (obj.contains("fanSpeed")) s.fanSpeed = obj["fanSpeed"].toInt();
    if (obj.contains("initialFanSpeed")) s.initialFanSpeed = obj["initialFanSpeed"].toInt();
    if (obj.contains("minFanSpeed")) s.minFanSpeed = obj["minFanSpeed"].toInt();
    if (obj.contains("fanStartLayer")) s.fanStartLayer = obj["fanStartLayer"].toDouble();
    if (obj.contains("minLayerTime")) s.minLayerTime = obj["minLayerTime"].toDouble();
    if (obj.contains("retractionDistance")) s.retractionDistance = obj["retractionDistance"].toDouble();
    if (obj.contains("retractionSpeed")) s.retractionSpeed = obj["retractionSpeed"].toDouble();
    if (obj.contains("retractDistance")) s.retractDistance = obj["retractDistance"].toDouble();
    if (obj.contains("retractSpeed")) s.retractSpeed = obj["retractSpeed"].toDouble();
    if (obj.contains("primeDistance")) s.primeDistance = obj["primeDistance"].toDouble();
    if (obj.contains("minTravelForRetract")) s.minTravelForRetract = obj["minTravelForRetract"].toDouble();
    if (obj.contains("retractionEnabled")) s.retractionEnabled = obj["retractionEnabled"].toBool();
    if (obj.contains("zHopEnabled")) s.zHopEnabled = obj["zHopEnabled"].toBool();
    if (obj.contains("zHopHeight")) s.zHopHeight = obj["zHopHeight"].toDouble();
    if (obj.contains("zHopSpeed")) s.zHopSpeed = obj["zHopSpeed"].toDouble();
    if (obj.contains("generateSupport")) s.generateSupport = obj["generateSupport"].toBool();
    if (obj.contains("supportType")) s.supportType = static_cast<SupportType>(obj["supportType"].toInt());
    if (obj.contains("supportPattern")) s.supportPattern = static_cast<SupportPattern>(obj["supportPattern"].toInt());
    if (obj.contains("supportDensity")) s.supportDensity = obj["supportDensity"].toDouble();
    if (obj.contains("supportAngle")) s.supportAngle = obj["supportAngle"].toDouble();
    if (obj.contains("supportXYDistance")) s.supportXYDistance = obj["supportXYDistance"].toDouble();
    if (obj.contains("supportZDistance")) s.supportZDistance = obj["supportZDistance"].toDouble();
    if (obj.contains("supportInterfaceLayers")) s.supportInterfaceLayers = obj["supportInterfaceLayers"].toInt();
    if (obj.contains("supportInterfaceDensity")) s.supportInterfaceDensity = obj["supportInterfaceDensity"].toDouble();
    if (obj.contains("supportTreeBranchDiameter")) s.supportTreeBranchDiameter = obj["supportTreeBranchDiameter"].toDouble();
    if (obj.contains("supportTreeBranchAngle")) s.supportTreeBranchAngle = obj["supportTreeBranchAngle"].toDouble();
    if (obj.contains("adhesionType")) s.adhesionType = static_cast<BedAdhesionType>(obj["adhesionType"].toInt());
    if (obj.contains("skirtLoops")) s.skirtLoops = obj["skirtLoops"].toInt();
    if (obj.contains("skirtDistance")) s.skirtDistance = obj["skirtDistance"].toDouble();
    if (obj.contains("brimWidth")) s.brimWidth = obj["brimWidth"].toInt();
    if (obj.contains("raftLayers")) s.raftLayers = obj["raftLayers"].toInt();
    if (obj.contains("raftOffset")) s.raftOffset = obj["raftOffset"].toDouble();
    if (obj.contains("extruderCount")) s.extruderCount = obj["extruderCount"].toInt();
    if (obj.contains("extruderTemps")) {
        QJsonArray arr = obj["extruderTemps"].toArray();
        s.extruderTemps.clear();
        for (const auto& v : arr) s.extruderTemps.push_back(v.toInt());
    }
    if (obj.contains("extruderAssignments")) s.extruderAssignments = obj["extruderAssignments"].toString();
    if (obj.contains("enablePrimeTower")) s.enablePrimeTower = obj["enablePrimeTower"].toBool();
    if (obj.contains("primeTowerSize")) s.primeTowerSize = obj["primeTowerSize"].toDouble();
    if (obj.contains("enableWipeTower")) s.enableWipeTower = obj["enableWipeTower"].toBool();
    if (obj.contains("enableOozeShield")) s.enableOozeShield = obj["enableOozeShield"].toBool();
    if (obj.contains("oozeShieldDistance")) s.oozeShieldDistance = obj["oozeShieldDistance"].toDouble();
    if (obj.contains("enableCombing")) s.enableCombing = obj["enableCombing"].toBool();
    if (obj.contains("maxTravelWithoutRetract")) s.maxTravelWithoutRetract = obj["maxTravelWithoutRetract"].toDouble();
    if (obj.contains("avoidCrossingPerimeters")) s.avoidCrossingPerimeters = obj["avoidCrossingPerimeters"].toBool();
    if (obj.contains("coastingDistance")) s.coastingDistance = obj["coastingDistance"].toDouble();
    if (obj.contains("wipeDistance")) s.wipeDistance = obj["wipeDistance"].toDouble();
    if (obj.contains("enableInputShaping")) s.enableInputShaping = obj["enableInputShaping"].toBool();
    if (obj.contains("outputDirectory")) s.outputDirectory = obj["outputDirectory"].toString();
    if (obj.contains("outputFilename")) s.outputFilename = obj["outputFilename"].toString();
    if (obj.contains("includeComments")) s.includeComments = obj["includeComments"].toBool();
    if (obj.contains("includeThumbnails")) s.includeThumbnails = obj["includeThumbnails"].toBool();
    if (obj.contains("gzipOutput")) s.gzipOutput = obj["gzipOutput"].toBool();
    return s;
}

bool SliceSettings::validate() const
{
    return layerHeight > 0 && layerHeight <= 1.0 &&
           printSpeed > 0 && nozzleDiameter > 0 && filamentDiameter > 0 &&
           nozzleTemp > 0 && bedTemp >= 0;
}

QString SliceSettings::validationError() const
{
    if (layerHeight <= 0 || layerHeight > 1.0) return "Invalid layer height";
    if (printSpeed <= 0) return "Invalid print speed";
    if (nozzleDiameter <= 0) return "Invalid nozzle diameter";
    if (filamentDiameter <= 0) return "Invalid filament diameter";
    if (nozzleTemp <= 0) return "Invalid nozzle temperature";
    return QString();
}

QJsonObject SliceInfo::toJson() const
{
    QJsonObject obj;
    obj["printerProfileId"] = printerProfileId;
    obj["settings"] = settings.toJson();
    obj["totalLayers"] = totalLayers;
    obj["printTime"] = printTime;
    obj["filamentUsed"] = filamentUsed;
    obj["filamentWeight"] = filamentWeight;
    obj["materialCost"] = materialCost;
    obj["boundingBoxVolume"] = boundingBoxVolume;
    obj["modelVolume"] = modelVolume;
    obj["supportVolume"] = supportVolume;
    obj["success"] = success;
    obj["warnings"] = QJsonArray::fromStringList(warnings);
    obj["errors"] = QJsonArray::fromStringList(errors);
    return obj;
}

} // namespace printing
} // namespace ks