#pragma once

#include "3DPrintTypes.h"
#include "SlicerEngine.h"
#include "PrinterProfile.h"
#include "SupportGenerator.h"
#include "GCodeGenerator.h"
#include "PrintPreview.h"
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>
#include <map>

namespace ks {
namespace printing {

class ThreeDPrintModule : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ThreeDPrintModule)

public:
    static ThreeDPrintModule* instance();
    static void destroyInstance();

    ~ThreeDPrintModule() override;

    // ========================================================================
    // Printer Profiles
    // ========================================================================
    QVector<PrinterProfile*> availablePrinters() const;
    PrinterProfile* getPrinter(const QString& id) const;
    PrinterProfile* activePrinter() const;
    void setActivePrinter(const QString& id);

    // Add/remove custom profiles
    bool addPrinterProfile(PrinterProfile* profile);
    bool removePrinterProfile(const QString& id);
    bool updatePrinterProfile(const QString& id, const PrinterProfile& profile);

    // ========================================================================
    // Slice Settings
    // ========================================================================
    SliceSettings* defaultSettings() const;
    SliceSettings* settingsForPrinter(const QString& printerId) const;
    void setDefaultSettings(SliceSettings* settings);

    // Material presets
    void applyMaterialPreset(const QString& material);
    QStringList availableMaterials() const;

    // ========================================================================
    // Slicing
    // ========================================================================
    // Slice from mesh triangles
    SliceInfo sliceMesh(const MeshTriangles& triangles, const QString& printerId = "",
                        SliceSettings* settings = nullptr);

    // Slice from scene (multiple objects)
    SliceInfo sliceScene(const std::vector<MeshTriangles>& objects, const QString& printerId = "",
                         SliceSettings* settings = nullptr);

    // Async slicing
    using SliceCallback = std::function<void(const SliceInfo& result)>;
    void sliceAsync(const MeshTriangles& triangles, const QString& printerId,
                    SliceSettings* settings, SliceCallback callback);

    void cancelSlice();

    // ========================================================================
    // G-code Generation
    // ========================================================================
    QString generateGCode(const SliceInfo& sliceInfo);

    // Async G-code generation
    using GCodeCallback = std::function<void(const QString& gcode)>;
    void generateGCodeAsync(const SliceInfo& sliceInfo, GCodeCallback callback);

    // ========================================================================
    // Export
    // ========================================================================
    bool exportGCode(const SliceInfo& sliceInfo, const QString& filePath);
    bool exportProject(const SliceInfo& sliceInfo, const QString& filePath);
    bool export3MF(const SliceInfo& sliceInfo, const QString& filePath);

    // ========================================================================
    // Preview / Simulation
    // ========================================================================
    PrintPreview* preview() const { return m_preview.get(); }

    // ========================================================================
    // Support Management
    // ========================================================================
    SupportGenerator* supportGenerator() const { return m_supportGenerator.get(); }

    // ========================================================================
    // Utility
    // ========================================================================
    bool checkBuildVolume(const BoundingBox& bounds, const QString& printerId, QString* error = nullptr) const;
    QMatrix4x4 suggestOrientation(const MeshTriangles& triangles) const;
    double estimatePrintTime(const MeshTriangles& triangles, const QString& printerId = "") const;
    double estimateFilamentUsage(const MeshTriangles& triangles, const QString& printerId = "") const;

signals:
    void activePrinterChanged(const QString& printerId);
    void sliceStarted();
    void sliceProgress(int percent, const QString& message);
    void sliceComplete(const SliceInfo& result);
    void sliceError(const QString& error);
    void gcodeGenerated(const QString& gcode);
    void gcodeGenerationProgress(int percent);
    void settingsChanged();

private slots:
    void onSliceComplete(const SliceInfo& result);
    void onGCodeComplete(const QString& gcode);
    void loadBuiltinProfiles();

private:
    explicit ThreeDPrintModule(QObject* parent = nullptr);

    static ThreeDPrintModule* s_instance;

    std::map<QString, std::unique_ptr<PrinterProfile>> m_printers;
    QString m_activePrinterId;

    std::unique_ptr<SliceSettings> m_defaultSettings;
    std::map<QString, std::unique_ptr<SliceSettings>> m_printerSettings;

    std::unique_ptr<SlicerEngine> m_slicer;
    std::unique_ptr<GCodeGenerator> m_gcodeGenerator;
    std::unique_ptr<SupportGenerator> m_supportGenerator;
    std::unique_ptr<PrintPreview> m_preview;

    // Async state
    bool m_slicing = false;
    MeshTriangles m_pendingTriangles;
    QString m_pendingPrinterId;
    std::unique_ptr<SliceSettings> m_pendingSettings;
    SliceCallback m_pendingCallback;
};

} // namespace printing
} // namespace ks