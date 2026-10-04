#include "ThreeDPrintModule.h"
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QStandardPaths>
#include <QDir>

namespace ks {
namespace printing {

ThreeDPrintModule* ThreeDPrintModule::s_instance = nullptr;

ThreeDPrintModule::ThreeDPrintModule(QObject* parent) : QObject(parent) {
    // Initialize components
    m_slicer = std::make_unique<SlicerEngine>(this);
    m_gcodeGenerator = std::make_unique<GCodeGenerator>(this);
    m_supportGenerator = std::make_unique<SupportGenerator>(this);
    m_preview = std::make_unique<PrintPreview>(this);

    // Connect internal signals
    connect(m_slicer.get(), &SlicerEngine::sliceProgress, this, &ThreeDPrintModule::sliceProgress);
    connect(m_slicer.get(), &SlicerEngine::sliceComplete, this, &ThreeDPrintModule::onSliceComplete);
    connect(m_slicer.get(), &SlicerEngine::sliceError, this, &ThreeDPrintModule::sliceError);
    connect(m_gcodeGenerator.get(), &GCodeGenerator::generationProgress, this, &ThreeDPrintModule::gcodeGenerationProgress);
    connect(m_gcodeGenerator.get(), &GCodeGenerator::generationComplete, this, &ThreeDPrintModule::onGCodeComplete);

    // Load built-in printer profiles
    loadBuiltinProfiles();

    // Create default settings
    m_defaultSettings = std::make_unique<SliceSettings>();
    m_defaultSettings->fromJson(SliceSettings::createDefault().toJson());

    // Set default active printer
    if (!m_printers.empty()) {
        m_activePrinterId = m_printers.begin()->first;
    }
}

ThreeDPrintModule::~ThreeDPrintModule() = default;

ThreeDPrintModule* ThreeDPrintModule::instance() {
    if (!s_instance) {
        s_instance = new ThreeDPrintModule();
    }
    return s_instance;
}

void ThreeDPrintModule::destroyInstance() {
    delete s_instance;
    s_instance = nullptr;
}

void ThreeDPrintModule::loadBuiltinProfiles() {
    for (PrinterProfile* profile : PrinterProfile::builtinProfiles()) {
        profile->setParent(this);
        m_printers.emplace(profile->id, std::unique_ptr<PrinterProfile>(profile));
    }
}

// ============================================================================
// Printer Profiles
// ============================================================================

QVector<PrinterProfile*> ThreeDPrintModule::availablePrinters() const {
    QVector<PrinterProfile*> result;
    for (const auto& [id, ptr] : m_printers) {
        result.append(ptr.get());
    }
    return result;
}

PrinterProfile* ThreeDPrintModule::getPrinter(const QString& id) const {
    auto it = m_printers.find(id);
    return it != m_printers.end() ? it->second.get() : nullptr;
}

PrinterProfile* ThreeDPrintModule::activePrinter() const {
    return getPrinter(m_activePrinterId);
}

void ThreeDPrintModule::setActivePrinter(const QString& id) {
    if (m_printers.find(id) != m_printers.end() && id != m_activePrinterId) {
        m_activePrinterId = id;
        emit activePrinterChanged(id);
        emit settingsChanged();
    }
}

bool ThreeDPrintModule::addPrinterProfile(PrinterProfile* profile) {
    if (!profile || profile->id.isEmpty() || m_printers.find(profile->id) != m_printers.end()) {
        return false;
    }
    if (!profile->validate()) {
        qWarning() << "Invalid printer profile:" << profile->id;
        return false;
    }
    profile->setParent(this);
    m_printers[profile->id] = std::unique_ptr<PrinterProfile>(profile);
    emit settingsChanged();
    return true;
}

bool ThreeDPrintModule::removePrinterProfile(const QString& id) {
    if (id == m_activePrinterId) return false; // Can't remove active
    return m_printers.erase(id) > 0;
}

bool ThreeDPrintModule::updatePrinterProfile(const QString& id, const PrinterProfile& profile) {
    auto it = m_printers.find(id);
    if (it == m_printers.end()) return false;

    PrinterProfile* existing = it->second.get();
    existing->fromJson(profile.toJson());
    existing->updateModifiedDate();
    emit settingsChanged();
    return true;
}

// ============================================================================
// Slice Settings
// ============================================================================

SliceSettings* ThreeDPrintModule::defaultSettings() const {
    return m_defaultSettings.get();
}

SliceSettings* ThreeDPrintModule::settingsForPrinter(const QString& printerId) const {
    auto it = m_printerSettings.find(printerId);
    if (it != m_printerSettings.end()) return it->second.get();

    PrinterProfile* printer = getPrinter(printerId);
    if (printer) return &printer->defaultSettings;

    return m_defaultSettings.get();
}

void ThreeDPrintModule::setDefaultSettings(SliceSettings* settings) {
    if (settings) {
        m_defaultSettings.reset(settings);
        emit settingsChanged();
    }
}

void ThreeDPrintModule::applyMaterialPreset(const QString& material) {
    if (material.compare("PLA", Qt::CaseInsensitive) == 0) {
        *m_defaultSettings = SliceSettings::createForQuality(QualityProfile::Standard);
    } else if (material.compare("PETG", Qt::CaseInsensitive) == 0) {
        *m_defaultSettings = SliceSettings::createForQuality(QualityProfile::Standard);
    } else if (material.compare("ABS", Qt::CaseInsensitive) == 0) {
        *m_defaultSettings = SliceSettings::createForQuality(QualityProfile::High);
    } else if (material.compare("TPU", Qt::CaseInsensitive) == 0) {
        *m_defaultSettings = SliceSettings::createForQuality(QualityProfile::Draft);
    }
    emit settingsChanged();
}

QStringList ThreeDPrintModule::availableMaterials() const {
    return {"PLA", "PETG", "ABS", "TPU", "ASA", "PC", "Nylon", "HIPS", "PVA"};
}

// ============================================================================
// Slicing
// ============================================================================

SliceInfo ThreeDPrintModule::sliceMesh(const MeshTriangles& triangles, const QString& printerId,
                                       SliceSettings* settings) {
    PrinterProfile* printer = getPrinter(printerId.isEmpty() ? m_activePrinterId : printerId);
    if (!printer) {
        SliceInfo result;
        result.success = false;
        result.errors.append("Printer not found");
        return result;
    }

    SliceSettings* useSettings = settings ? settings : settingsForPrinter(printer->id);
    emit sliceStarted();

    SliceInfo result = m_slicer->slice(triangles, *useSettings, *printer);
    result.printerProfileId = printer->id;
    result.settings = *useSettings;

    return result;
}

SliceInfo ThreeDPrintModule::sliceScene(const std::vector<MeshTriangles>& objects, const QString& printerId,
                                        SliceSettings* settings) {
    PrinterProfile* printer = getPrinter(printerId.isEmpty() ? m_activePrinterId : printerId);
    if (!printer) {
        SliceInfo result;
        result.success = false;
        result.errors.append("Printer not found");
        return result;
    }

    SliceSettings* useSettings = settings ? settings : settingsForPrinter(printer->id);
    emit sliceStarted();

    SliceInfo result = m_slicer->sliceScene(objects, *useSettings, *printer);
    result.printerProfileId = printer->id;
    result.settings = *useSettings;

    return result;
}

void ThreeDPrintModule::sliceAsync(const MeshTriangles& triangles, const QString& printerId,
                                   SliceSettings* settings, SliceCallback callback) {
    if (m_slicing) {
        qWarning() << "Already slicing, cancel first";
        return;
    }

    PrinterProfile* printer = getPrinter(printerId.isEmpty() ? m_activePrinterId : printerId);
    if (!printer) {
        if (callback) {
            SliceInfo result;
            result.success = false;
            result.errors.append("Printer not found");
            callback(result);
        }
        return;
    }

    m_slicing = true;
    m_pendingTriangles = triangles;
    m_pendingPrinterId = printer->id;
    m_pendingSettings.reset(settings ? new SliceSettings(*settings) : new SliceSettings(*settingsForPrinter(printer->id)));
    m_pendingCallback = std::move(callback);

    emit sliceStarted();
    m_slicer->sliceAsync(triangles, *m_pendingSettings, *printer,
                         [this](int percent, const QString& msg) {
                             emit sliceProgress(percent, msg);
                         },
                         [this](const SliceInfo& result) {
                             onSliceComplete(result);
                         });
}

void ThreeDPrintModule::cancelSlice() {
    if (m_slicing) {
        m_slicer->cancel();
        m_slicing = false;
    }
}

void ThreeDPrintModule::onSliceComplete(const SliceInfo& result) {
    m_slicing = false;
    SliceInfo finalResult = result;
    finalResult.printerProfileId = m_pendingPrinterId;
    finalResult.settings = *m_pendingSettings;

    if (m_pendingCallback) {
        m_pendingCallback(finalResult);
        m_pendingCallback = nullptr;
    }
    emit sliceComplete(finalResult);
}

// ============================================================================
// G-code Generation
// ============================================================================

QString ThreeDPrintModule::generateGCode(const SliceInfo& sliceInfo) {
    PrinterProfile* printer = getPrinter(sliceInfo.printerProfileId);
    if (!printer) return QString();

    return m_gcodeGenerator->generateGCode(sliceInfo, *printer, sliceInfo.settings);
}

void ThreeDPrintModule::generateGCodeAsync(const SliceInfo& sliceInfo, GCodeCallback callback) {
    PrinterProfile* printer = getPrinter(sliceInfo.printerProfileId);
    if (!printer) {
        if (callback) callback(QString());
        return;
    }

    m_gcodeGenerator->generateAsync(sliceInfo, *printer, sliceInfo.settings,
                                    [this](int percent, const QString& msg) {
                                        emit gcodeGenerationProgress(percent);
                                    },
                                    [callback](const QString& gcode) {
                                        if (callback) callback(gcode);
                                    });
}

void ThreeDPrintModule::onGCodeComplete(const QString& gcode) {
    emit gcodeGenerated(gcode);
}

// ============================================================================
// Export
// ============================================================================

bool ThreeDPrintModule::exportGCode(const SliceInfo& sliceInfo, const QString& filePath) {
    QString gcode = generateGCode(sliceInfo);
    if (gcode.isEmpty()) return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << gcode;
    return true;
}

bool ThreeDPrintModule::exportProject(const SliceInfo& sliceInfo, const QString& filePath) {
    QJsonObject project;
    project["version"] = 1;
    project["type"] = "kseditor-print-project";
    project["sliceInfo"] = sliceInfo.toJson();
    project["gcode"] = generateGCode(sliceInfo);

    // Generate and encode thumbnail
    QImage thumb = sliceInfo.thumbnail;
    if (thumb.isNull() && !sliceInfo.slices.empty()) {
        int midLayer = qBound(0, sliceInfo.totalLayers / 2, static_cast<int>(sliceInfo.slices.size()) - 1);
        thumb = m_preview->generateFrame(sliceInfo, midLayer, QSize(400, 300));
    }
    if (!thumb.isNull()) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        thumb.save(&buffer, "PNG");
        project["thumbnail"] = "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
    } else {
        project["thumbnail"] = "";
    }

    QJsonDocument doc(project);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool ThreeDPrintModule::export3MF(const SliceInfo& sliceInfo, const QString& filePath) {
    QByteArray packageData;
    QBuffer buf(&packageData);
    buf.open(QIODevice::WriteOnly);

    QByteArray contentTypes = R"(<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
  <Default Extension="png" ContentType="image/png"/>
</Types>)";

    QByteArray rels = R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>)";

    QString modelXml;
    modelXml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    modelXml += "<model unit=\"millimeter\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n";
    modelXml += "  <metadata name=\"Application\">ksEditor 3D Print Module</metadata>\n";
    modelXml += "  <resources>\n";
    modelXml += "    <object id=\"1\" type=\"model\">\n";
    modelXml += "      <mesh>\n";
    modelXml += "        <vertices>\n";

    modelXml += QString("          <!-- Layers: %1, Print time: %2s -->\n")
        .arg(sliceInfo.totalLayers).arg(sliceInfo.printTime, 0, 'f', 1);

    modelXml += "        </vertices>\n";
    modelXml += "        <triangles>\n";

    for (int i = 0; i < sliceInfo.totalLayers && i < 10; ++i) {
        double z = i < (int)sliceInfo.layerHeights.size() ? sliceInfo.layerHeights[i] : 0.0;
        modelXml += QString("          <!-- Layer %1 z=%2 -->\n").arg(i).arg(z, 0, 'f', 3);
    }

    modelXml += "        </triangles>\n";
    modelXml += "      </mesh>\n";
    modelXml += "    </object>\n";
    modelXml += "  </resources>\n";
    modelXml += "  <build>\n";
    modelXml += "    <item objectid=\"1\"/>\n";
    modelXml += "  </build>\n";

    modelXml += "  <production metadata=\"LayerHeight\">" + QString::number(sliceInfo.settings.layerHeight, 'f', 3) + "</production>\n";
    modelXml += "  <production metadata=\"NozzleDiameter\">" + QString::number(sliceInfo.settings.nozzleDiameter, 'f', 3) + "</production>\n";
    modelXml += "  <production metadata=\"PrintSpeed\">" + QString::number(sliceInfo.settings.printSpeed, 'f', 1) + "</production>\n";
    modelXml += "  <production metadata=\"InfillDensity\">" + QString::number(sliceInfo.settings.infillDensity, 'f', 1) + "</production>\n";
    modelXml += "</model>\n";

    auto writeZipEntry = [&](const QString& name, const QByteArray& data) {
        QByteArray header(30, 0);
        quint32 crc = qChecksum(data.constData(), data.size());
        header[0] = 0x50; header[1] = 0x4B; header[2] = 0x03; header[3] = 0x04;
        header[4] = 0x14;
        header[14] = data.size() & 0xFF;
        header[15] = (data.size() >> 8) & 0xFF;
        header[16] = (data.size() >> 16) & 0xFF;
        header[17] = (data.size() >> 24) & 0xFF;
        header[18] = data.size() & 0xFF;
        header[19] = (data.size() >> 8) & 0xFF;
        header[20] = (data.size() >> 16) & 0xFF;
        header[21] = (data.size() >> 24) & 0xFF;
        header[26] = name.size() & 0xFF;
        header[27] = (name.size() >> 8) & 0xFF;
        buf.write(header);
        buf.write(name.toUtf8());
        buf.write(data);
    };

    writeZipEntry("[Content_Types].xml", contentTypes);
    writeZipEntry("_rels/.rels", rels);
    writeZipEntry("3D/3dmodel.model", modelXml.toUtf8());

    buf.close();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(packageData);
    file.close();
    return true;
}

// ============================================================================
// Utility
// ============================================================================

bool ThreeDPrintModule::checkBuildVolume(const BoundingBox& bounds, const QString& printerId, QString* error) const {
    PrinterProfile* printer = getPrinter(printerId.isEmpty() ? m_activePrinterId : printerId);
    if (!printer) {
        if (error) *error = "Printer not found";
        return false;
    }
    return m_slicer->checkBuildVolume(bounds, *printer, error);
}

QMatrix4x4 ThreeDPrintModule::suggestOrientation(const MeshTriangles& triangles) const {
    return m_slicer->suggestOrientation(triangles);
}

double ThreeDPrintModule::estimatePrintTime(const MeshTriangles& triangles, const QString& printerId) const {
    PrinterProfile* printer = getPrinter(printerId.isEmpty() ? m_activePrinterId : printerId);
    if (!printer) return 0;
    return m_slicer->estimatePrintTimeQuick(triangles, *settingsForPrinter(printer->id), *printer);
}

double ThreeDPrintModule::estimateFilamentUsage(const MeshTriangles& triangles, const QString& printerId) const {
    PrinterProfile* printer = getPrinter(printerId.isEmpty() ? m_activePrinterId : printerId);
    if (!printer) return 0;

    // Quick volume-based estimate
    double volume = 0;
    for (const auto& tri : triangles) {
        Vector3 v0 = tri.v[0];
        Vector3 v1 = tri.v[1];
        Vector3 v2 = tri.v[2];
        volume += std::abs(v0.x * (v1.y * v2.z - v1.z * v2.y) +
                          v0.y * (v1.z * v2.x - v1.x * v2.z) +
                          v0.z * (v1.x * v2.y - v1.y * v2.x)) / 6.0;
    }

    SliceSettings* settings = settingsForPrinter(printer->id);
    double filamentDiameter = settings->filamentDiameter > 0 ? settings->filamentDiameter : 1.75;
    double filamentArea = M_PI * filamentDiameter * filamentDiameter / 4.0;
    return volume / filamentArea; // mm of filament
}

} // namespace printing
} // namespace ks