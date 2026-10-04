#include "ThreeDPrintQmlBridge.h"
#include "ThreeDPrintModule.h"
#include "SlicerEngine.h"
#include "PrinterProfile.h"
#include "PrintPreview.h"
#include <QQmlEngine>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include "modules/modellingEditor/3DModeling_io.h"
#include "modules/modellingEditor/3DModeling.h"

namespace ks {
namespace printing {

ThreeDPrintQmlBridge::ThreeDPrintQmlBridge(QObject* parent) : QObject(parent) {
    m_module = ThreeDPrintModule::instance();
    if (m_module) {
        connect(m_module, &ThreeDPrintModule::activePrinterChanged, this, &ThreeDPrintQmlBridge::activePrinterChanged);
        connect(m_module, &ThreeDPrintModule::sliceStarted, this, &ThreeDPrintQmlBridge::sliceStarted);
        connect(m_module, &ThreeDPrintModule::sliceProgress, this, &ThreeDPrintQmlBridge::sliceProgress);
        connect(m_module, &ThreeDPrintModule::sliceComplete, this, [this](const SliceInfo& result) {
            SliceResultImageProvider::setSliceResult(result);
            emit sliceComplete(sliceInfoToMap(result));
        });
        connect(m_module, &ThreeDPrintModule::sliceError, this, &ThreeDPrintQmlBridge::sliceError);
        connect(m_module, &ThreeDPrintModule::gcodeGenerated, this, &ThreeDPrintQmlBridge::gcodeGenerated);
        connect(m_module, &ThreeDPrintModule::gcodeGenerationProgress, this, &ThreeDPrintQmlBridge::gcodeGenerationProgress);
        connect(m_module, &ThreeDPrintModule::settingsChanged, this, &ThreeDPrintQmlBridge::settingsChanged);
    }
}

void ThreeDPrintQmlBridge::registerQmlTypes() {
    qmlRegisterType<ThreeDPrintQmlBridge>("KSEditor.Printing", 1, 0, "PrintManager");
    qmlRegisterUncreatableType<PrinterProfile>("KSEditor.Printing", 1, 0, "PrinterProfile", "Use PrintManager.getPrinter()");
    qmlRegisterUncreatableType<SliceInfo>("KSEditor.Printing", 1, 0, "SliceInfo", "Use PrintManager.sliceFile()");
}

void ThreeDPrintQmlBridge::registerQmlContext(QQmlContext* context, ThreeDPrintModule* module) {
    if (module) {
        context->setContextProperty("printManager", module);
    }
}

// ============================================================================
// Printer Profiles
// ============================================================================

QVariantList ThreeDPrintQmlBridge::getPrinters() const {
    QVariantList list;
    if (!m_module) return list;

    for (PrinterProfile* p : m_module->availablePrinters()) {
        list.append(printerToMap(p));
    }
    return list;
}

QVariantMap ThreeDPrintQmlBridge::getPrinter(const QString& id) const {
    if (!m_module) return {};
    PrinterProfile* p = m_module->getPrinter(id);
    return p ? printerToMap(p) : QVariantMap();
}

QVariantMap ThreeDPrintQmlBridge::getActivePrinter() const {
    if (!m_module) return {};
    return printerToMap(m_module->activePrinter());
}

bool ThreeDPrintQmlBridge::setActivePrinter(const QString& id) {
    if (!m_module) return false;
    m_module->setActivePrinter(id);
    return true;
}

bool ThreeDPrintQmlBridge::addCustomPrinter(const QVariantMap& profile) {
    if (!m_module) return false;
    auto* printer = new PrinterProfile();
    printer->fromJson(QJsonObject::fromVariantMap(profile));
    return m_module->addPrinterProfile(printer);
}

bool ThreeDPrintQmlBridge::updatePrinter(const QString& id, const QVariantMap& profile) {
    if (!m_module) return false;
    PrinterProfile updated;
    updated.fromJson(QJsonObject::fromVariantMap(profile));
    return m_module->updatePrinterProfile(id, updated);
}

bool ThreeDPrintQmlBridge::removePrinter(const QString& id) {
    if (!m_module) return false;
    return m_module->removePrinterProfile(id);
}

// ============================================================================
// Slice Settings
// ============================================================================

QVariantMap ThreeDPrintQmlBridge::getDefaultSettings() const {
    if (!m_module) return {};
    return settingsToMap(m_module->defaultSettings());
}

QVariantMap ThreeDPrintQmlBridge::getSettingsForPrinter(const QString& printerId) const {
    if (!m_module) return {};
    return settingsToMap(m_module->settingsForPrinter(printerId));
}

void ThreeDPrintQmlBridge::setDefaultSettings(const QVariantMap& settings) {
    if (!m_module) return;
    auto* s = new SliceSettings();
    s->fromJson(QJsonObject::fromVariantMap(settings));
    m_module->setDefaultSettings(s);
}

void ThreeDPrintQmlBridge::applyMaterialPreset(const QString& material) {
    if (m_module) m_module->applyMaterialPreset(material);
}

QStringList ThreeDPrintQmlBridge::availableMaterials() const {
    if (!m_module) return {};
    return m_module->availableMaterials();
}

QStringList ThreeDPrintQmlBridge::availableInfillPatterns() const {
    return {"Grid", "Lines", "Triangles", "Cubic", "Gyroid", "Concentric", "Honeycomb", "Octet"};
}

QStringList ThreeDPrintQmlBridge::availableSupportTypes() const {
    return {"None", "Normal", "Tree", "Snug", "Custom"};
}

QStringList ThreeDPrintQmlBridge::availableAdhesionTypes() const {
    return {"None", "Skirt", "Brim", "Raft"};
}

QStringList ThreeDPrintQmlBridge::availableQualityProfiles() const {
    return {"Draft", "Standard", "High", "Ultra"};
}

// ============================================================================
// Slicing Operations
// ============================================================================

QVariantMap ThreeDPrintQmlBridge::sliceFile(const QString& filePath, const QString& printerId,
                                            const QVariantMap& settings) {
    if (!m_module) return {};

    // Load mesh from file
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return {{"success", false}, {"error", "File not found"}};
    }

    // Import mesh using existing 3D modeling I/O
    ks::io::ImportExport3D importer;
    auto* scene = importer.importScene(filePath);
    if (!scene || scene->allObjects().isEmpty()) {
        return {{"success", false}, {"error", "Failed to load mesh"}};
    }

    // Convert to triangles
    MeshTriangles triangles;
    for (auto* obj : scene->allObjects()) {
        if (obj->mesh) {
            auto verts = obj->mesh->vertices();
            auto indices = obj->mesh->indices();
            QMatrix4x4 xform = obj->transform;
            for (const auto& v : verts) {
                triangles.push_back(Triangle3D(Vector3(xform.map(v)), Vector3(), Vector3())); // Simplified
            }
        }
    }
    delete scene;

    SliceSettings* useSettings = nullptr;
    if (!settings.isEmpty()) {
        useSettings = new SliceSettings();
        useSettings->fromJson(QJsonObject::fromVariantMap(settings));
    }

    SliceInfo result = m_module->sliceMesh(triangles, printerId, useSettings);
    return sliceInfoToMap(result);
}

QVariantMap ThreeDPrintQmlBridge::sliceMesh(const QVariantList& vertices, const QVariantList& indices,
                                            const QString& printerId, const QVariantMap& settings) {
    if (!m_module) return {};

    MeshTriangles triangles = mapToTriangles(vertices, indices);

    SliceSettings* useSettings = nullptr;
    if (!settings.isEmpty()) {
        useSettings = new SliceSettings();
        useSettings->fromJson(QJsonObject::fromVariantMap(settings));
    }

    SliceInfo result = m_module->sliceMesh(triangles, printerId, useSettings);
    return sliceInfoToMap(result);
}

void ThreeDPrintQmlBridge::sliceFileAsync(const QString& filePath, const QString& printerId,
                                          const QVariantMap& settings) {
    // Load mesh in background
    QtConcurrent::run([this, filePath, printerId, settings]() {
        QVariantMap result = sliceFile(filePath, printerId, settings);
        QMetaObject::invokeMethod(this, [this, result]() {
            if (result["success"].toBool()) {
                emit sliceComplete(result);
            } else {
                emit sliceError(result["error"].toString());
            }
        });
    });
}

void ThreeDPrintQmlBridge::sliceMeshAsync(const QVariantList& vertices, const QVariantList& indices,
                                          const QString& printerId, const QVariantMap& settings) {
    QtConcurrent::run([this, vertices, indices, printerId, settings]() {
        QVariantMap result = sliceMesh(vertices, indices, printerId, settings);
        QMetaObject::invokeMethod(this, [this, result]() {
            if (result["success"].toBool()) {
                emit sliceComplete(result);
            } else {
                emit sliceError(result["error"].toString());
            }
        });
    });
}

void ThreeDPrintQmlBridge::cancelSlice() {
    if (m_module) m_module->cancelSlice();
}

// ============================================================================
// G-code Generation & Export
// ============================================================================

QString ThreeDPrintQmlBridge::generateGCode(const QVariantMap& sliceInfo) {
    if (!m_module) return QString();
    SliceInfo info = mapToSliceInfo(sliceInfo);
    return m_module->generateGCode(info);
}

void ThreeDPrintQmlBridge::generateGCodeAsync(const QVariantMap& sliceInfo) {
    if (!m_module) return;
    SliceInfo info = mapToSliceInfo(sliceInfo);
    m_module->generateGCodeAsync(info, [this](const QString& gcode) {
        emit gcodeGenerated(gcode);
    });
}

bool ThreeDPrintQmlBridge::exportGCode(const QVariantMap& sliceInfo, const QString& filePath) {
    if (!m_module) return false;
    SliceInfo info = mapToSliceInfo(sliceInfo);
    return m_module->exportGCode(info, filePath);
}

bool ThreeDPrintQmlBridge::exportProject(const QVariantMap& sliceInfo, const QString& filePath) {
    if (!m_module) return false;
    SliceInfo info = mapToSliceInfo(sliceInfo);
    return m_module->exportProject(info, filePath);
}

// ============================================================================
// Preview & Analysis
// ============================================================================

QVariantMap ThreeDPrintQmlBridge::getSliceInfo(const QVariantMap& sliceInfo) const {
    return sliceInfo; // Pass through
}

QVariantMap ThreeDPrintQmlBridge::getLayerPreview(const QVariantMap& sliceInfo, int layerIndex) const {
    SliceInfo info = mapToSliceInfo(sliceInfo);
    // Use PrintPreview to generate frame for the layer
    if (!m_module) return {};
    auto* preview = m_module->preview();
    if (!preview) return {};
    
    if (layerIndex < 0 || layerIndex >= info.totalLayers) return {};
    
    QImage frame = preview->generateFrame(info, layerIndex);
    QVariantMap result;
    result["image"] = frame;
    result["layerIndex"] = layerIndex;
    return result;
}

QImage ThreeDPrintQmlBridge::getThumbnail(const QVariantMap& sliceInfo, int width, int height) const {
    SliceInfo info = mapToSliceInfo(sliceInfo);
    if (!m_module) return QImage();
    auto* preview = m_module->preview();
    if (!preview) return QImage();
    
    // Generate frame for middle layer as thumbnail
    int layerIndex = qMin(info.totalLayers / 2, qMax(0, info.totalLayers - 1));
    QImage frame = preview->generateFrame(info, layerIndex, QSize(width, height));
    return frame;
}

QVariantMap ThreeDPrintQmlBridge::analyzeModel(const QString& filePath) const {
    QFileInfo fi(filePath);
    QVariantMap result;
    result["fileName"] = fi.fileName();
    result["fileSize"] = fi.size();
    result["exists"] = fi.exists();

    if (!fi.exists()) return result;

    ks::io::ImportExport3D importer;
    auto* scene = importer.importScene(filePath);
    if (scene) {
        result["objectCount"] = scene->allObjects().size();
        BoundingBox bounds;
        bool first = true;
        for (auto* obj : scene->allObjects()) {
            if (obj->mesh) {
                for (const auto& v : obj->mesh->vertices()) {
                    Vector3 tv = Vector3(obj->transform.map(v));
                    if (first) { bounds.min = bounds.max = tv; first = false; }
                    else { bounds.expand(tv); }
                }
            }
        }
        if (!first) {
            result["width"] = bounds.size().x;
            result["height"] = bounds.size().y;
            result["depth"] = bounds.size().z;
            result["volume"] = bounds.size().x * bounds.size().y * bounds.size().z; // Approximate
        }
        delete scene;
    }
    return result;
}

QVariantMap ThreeDPrintQmlBridge::estimatePrint(const QString& filePath, const QString& printerId) const {
    if (!m_module) return {};

    QVariantMap result;
    ks::io::ImportExport3D importer;
    auto* scene = importer.importScene(filePath);
    if (!scene) return {{"error", "Failed to load model"}};

    MeshTriangles triangles;
    for (auto* obj : scene->allObjects()) {
        if (obj->mesh) {
            auto verts = obj->mesh->vertices();
            auto indices = obj->mesh->indices();
            QMatrix4x4 xform = obj->transform;
            for (size_t i = 0; i < indices.size(); i += 3) {
                Triangle3D tri;
                tri.v[0] = Vector3(xform.map(verts[indices[i]]));
                tri.v[1] = Vector3(xform.map(verts[indices[i+1]]));
                tri.v[2] = Vector3(xform.map(verts[indices[i+2]]));
                triangles.push_back(tri);
            }
        }
    }
    delete scene;

    double time = m_module->estimatePrintTime(triangles, printerId);
    double filament = m_module->estimateFilamentUsage(triangles, printerId);

    result["estimatedTime"] = time;
    result["estimatedFilament"] = filament;
    result["estimatedTimeFormatted"] = QString("%1h %2m").arg(int(time/3600)).arg(int((time/60))%60);
    return result;
}

// ============================================================================
// Utility
// ============================================================================

bool ThreeDPrintQmlBridge::checkBuildVolume(const QVariantMap& bounds, const QString& printerId, QString* error) const {
    if (!m_module) return false;
    BoundingBox bb;
    bb.min = {bounds["minX"].toDouble(), bounds["minY"].toDouble(), bounds["minZ"].toDouble()};
    bb.max = {bounds["maxX"].toDouble(), bounds["maxY"].toDouble(), bounds["maxZ"].toDouble()};
    return m_module->checkBuildVolume(bb, printerId, error);
}

QVariantMap ThreeDPrintQmlBridge::suggestOrientation(const QVariantList& vertices, const QVariantList& indices) const {
    if (!m_module) return {};
    MeshTriangles triangles = mapToTriangles(vertices, indices);
    QMatrix4x4 mat = m_module->suggestOrientation(triangles);
    QVariantMap result;
    result["matrix"] = QVariant::fromValue(mat);
    return result;
}

// ============================================================================
// Conversion Helpers
// ============================================================================

QVariantMap ThreeDPrintQmlBridge::printerToMap(const PrinterProfile* p) {
    if (!p) return {};
    QVariantMap m;
    m["id"] = p->id;
    m["name"] = p->name;
    m["vendor"] = p->vendor;
    m["model"] = p->model;
    m["variant"] = p->variant;
    m["buildVolumeX"] = p->buildVolumeX;
    m["buildVolumeY"] = p->buildVolumeY;
    m["buildVolumeZ"] = p->buildVolumeZ;
    m["nozzleDiameter"] = p->nozzleDiameter;
    m["maxExtruders"] = p->maxExtruders;
    m["gcodeFlavor"] = static_cast<int>(p->gcodeFlavor);
    m["hasHeatedBed"] = p->hasHeatedBed;
    m["hasEnclosure"] = p->hasEnclosure;
    m["hasFilamentSensor"] = p->hasFilamentSensor;
    m["hasAutoBedLeveling"] = p->hasAutoBedLeveling;
    m["maxHotendTemp"] = p->maxHotendTemp;
    m["maxBedTemp"] = p->maxBedTemp;
    m["bedSurface"] = p->bedSurface;
    m["description"] = p->description;
    m["website"] = p->website;
    return m;
}

QVariantMap ThreeDPrintQmlBridge::settingsToMap(const SliceSettings* s) {
    if (!s) return {};
    return s->toJson().toVariantMap();
}

QVariantMap ThreeDPrintQmlBridge::sliceInfoToMap(const SliceInfo& info) {
    QVariantMap m;
    m["success"] = info.success;
    m["error"] = info.errors.join("; ");
    m["printerProfileId"] = info.printerProfileId;
    m["layerCount"] = info.totalLayers;
    m["estimatedPrintTime"] = info.printTime;
    m["estimatedFilament"] = info.filamentUsed;
    m["estimatedFilamentWeight"] = info.filamentWeight;
    m["estimatedMaterialCost"] = info.materialCost;
    m["bounds"] = boundsToMap(info.boundingBox);
    m["settings"] = info.settings.toJson().toVariantMap();

    // Layers summary
    QVariantList layers;
    for (const auto& layer : info.slices) {
        layers.append(layerToMap(layer));
    }
    m["layers"] = layers;

    return m;
}

QVariantMap ThreeDPrintQmlBridge::layerToMap(const LayerSlice& layer) {
    QVariantMap m;
    m["z"] = layer.z;
    m["thickness"] = layer.thickness;
    m["layerIndex"] = layer.layerIndex;
    m["isFirstLayer"] = layer.isFirstLayer;
    m["isLastLayer"] = layer.isLastLayer;
    m["printTimeEstimate"] = layer.printTimeEstimate;
    m["filamentUsed"] = layer.filamentUsed;
    m["perimeterCount"] = layer.perimeters.size();
    m["infillCount"] = layer.infill.size();
    m["supportCount"] = layer.support.size();
    m["brimCount"] = layer.brim.size();
    return m;
}

QVariantMap ThreeDPrintQmlBridge::boundsToMap(const BoundingBox& b) {
    QVariantMap m;
    m["minX"] = b.min.x;
    m["minY"] = b.min.y;
    m["minZ"] = b.min.z;
    m["maxX"] = b.max.x;
    m["maxY"] = b.max.y;
    m["maxZ"] = b.max.z;
    m["width"] = b.size().x;
    m["height"] = b.size().y;
    m["depth"] = b.size().z;
    m["centerX"] = b.center().x;
    m["centerY"] = b.center().y;
    m["centerZ"] = b.center().z;
    return m;
}

SliceSettings ThreeDPrintQmlBridge::mapToSettings(const QVariantMap& map) {
    SliceSettings s;
    s.fromJson(QJsonObject::fromVariantMap(map));
    return s;
}

MeshTriangles ThreeDPrintQmlBridge::mapToTriangles(const QVariantList& vertices, const QVariantList& indices) {
    MeshTriangles triangles;

    std::vector<Vector3> verts;
    verts.reserve(vertices.size() / 3);
    for (int i = 0; i + 2 < vertices.size(); i += 3) {
        verts.push_back({vertices[i].toDouble(), vertices[i+1].toDouble(), vertices[i+2].toDouble()});
    }

    for (int i = 0; i + 2 < indices.size(); i += 3) {
        Triangle3D tri;
        tri.v[0] = verts[indices[i].toInt()];
        tri.v[1] = verts[indices[i+1].toInt()];
        tri.v[2] = verts[indices[i+2].toInt()];
        triangles.push_back(tri);
    }

    return triangles;
}

SliceInfo ThreeDPrintQmlBridge::mapToSliceInfo(const QVariantMap& map) {
    SliceInfo info;
    info.success = map["success"].toBool();
    info.errors = map["error"].toString().split("; ", Qt::SkipEmptyParts);
    info.printerProfileId = map["printerProfileId"].toString();
    info.totalLayers = map["layerCount"].toInt();
    info.printTime = map["estimatedPrintTime"].toDouble();
    info.filamentUsed = map["estimatedFilament"].toDouble();
    info.filamentWeight = map["estimatedFilamentWeight"].toDouble();
    info.materialCost = map["estimatedMaterialCost"].toDouble();

    if (map.contains("bounds")) {
        QVariantMap b = map["bounds"].toMap();
        info.boundingBox.min = {b["minX"].toDouble(), b["minY"].toDouble(), b["minZ"].toDouble()};
        info.boundingBox.max = {b["maxX"].toDouble(), b["maxY"].toDouble(), b["maxZ"].toDouble()};
    }

    if (map.contains("settings")) {
        info.settings.fromJson(QJsonObject::fromVariantMap(map["settings"].toMap()));
    }

    // Note: Full layer data not serialized in QVariantMap for performance
    return info;
}

} // namespace printing
} // namespace ks