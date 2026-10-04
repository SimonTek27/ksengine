#pragma once

#include "editor/EditorModule.h"
#include "sdk/kseditor/engine/Config/PPFilterPreset.h"
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QSpinBox>

namespace ks {

class ShowroomPPEditorModule : public EditorModule {
    Q_OBJECT
public:
    explicit ShowroomPPEditorModule(QWidget* parent = nullptr);
    ~ShowroomPPEditorModule() override = default;

    bool initialize() override;
    void shutdown() override;
    QString moduleName() const override { return "Showroom PP Editor"; }
    QString moduleId() const override { return "showroomPPEditor"; }
    int getModulePriority() const override { return 37; }
    QDockWidget* getOrCreateDockWidget(QMainWindow* mainWindow) override;

    void importFile(const QString& filePath) override;
    void exportFile(const QString& filePath) override;

    QJsonObject serializeProject() const override;
    void deserializeProject(const QJsonObject& data) override;

protected:
    void onActivation() override;
    void onDeactivation() override;

private slots:
    void onLoadFile();
    void onSaveFile();
    void onResetDefaults();

    // Auto Exposure
    void onAutoExposureToggled(bool c);
    void onAEDelayChanged(double v);
    void onAETargetChanged(double v);
    void onAEMinChanged(double v);
    void onAEMaxChanged(double v);

    // Tone Mapping
    void onExposureChanged(double v);
    void onGammaChanged(double v);

    // DOF
    void onDOFToggled(bool c);
    void onDOFApertureChanged(double v);

    // Glare
    void onGlareToggled(bool c);
    void onGlareLuminanceChanged(double v);
    void onGlareThresholdChanged(double v);

    // God Rays
    void onGodRaysToggled(bool c);
    void onGodRaysLengthChanged(double v);

    // Color
    void onSaturationChanged(double v);
    void onBrightnessChanged(double v);
    void onContrastChanged(double v);
    void onColorTempChanged(int v);

private:
    void setupUi();
    void loadFileToUI();
    void saveFileFromUI();
    void syncStateFromUI();

    QDockWidget* m_dockWidget = nullptr;
    QTabWidget* m_tabWidget = nullptr;

    // Auto Exposure
    QCheckBox* m_autoExposureCheck = nullptr;
    QDoubleSpinBox* m_aeDelaySpin = nullptr;
    QDoubleSpinBox* m_aeTargetSpin = nullptr;
    QDoubleSpinBox* m_aeMinSpin = nullptr;
    QDoubleSpinBox* m_aeMaxSpin = nullptr;

    // Tone Mapping
    QDoubleSpinBox* m_exposureSpin = nullptr;
    QDoubleSpinBox* m_gammaSpin = nullptr;

    // DOF
    QCheckBox* m_dofCheck = nullptr;
    QDoubleSpinBox* m_dofApertureSpin = nullptr;

    // Glare
    QCheckBox* m_glareCheck = nullptr;
    QDoubleSpinBox* m_glareLuminanceSpin = nullptr;
    QDoubleSpinBox* m_glareThresholdSpin = nullptr;

    // God Rays
    QCheckBox* m_godRaysCheck = nullptr;
    QDoubleSpinBox* m_godRaysLengthSpin = nullptr;

    // Color
    QDoubleSpinBox* m_saturationSpin = nullptr;
    QDoubleSpinBox* m_brightnessSpin = nullptr;
    QDoubleSpinBox* m_contrastSpin = nullptr;
    QSpinBox* m_colorTempSpin = nullptr;

    QPushButton* m_loadBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    QString m_filePath;
    bool m_modified = false;

    // Internal state for PP filter values
    struct PPState {
        bool autoExposure = true;
        double aeDelay = 0.0, aeTarget = 0.32, aeMin = 0.2, aeMax = 0.5;
        double exposure = 0.28, gamma = 1.2;
        bool dof = true;
        double dofAperture = 12.0;
        bool glare = true;
        double glareLuminance = 1.6, glareThreshold = 5.0;
        bool godRays = true;
        double godRaysLength = 11.0;
        double saturation = 0.95, brightness = 1.0, contrast = 1.0;
        int colorTemp = 6400;
    } m_state;
};

} // namespace ks
