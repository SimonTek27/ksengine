#include "ShowroomPPEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextStream>

namespace ks {

ShowroomPPEditorModule::ShowroomPPEditorModule(QWidget* parent) : EditorModule(parent) {}
bool ShowroomPPEditorModule::initialize() { LOG_INFO("ShowroomPPEditorModule", "Initialized"); return true; }
void ShowroomPPEditorModule::shutdown() {     if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* ShowroomPPEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Showroom PP Editor"), mainWindow);
    m_dockWidget->setObjectName("ShowroomPPEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    m_tabWidget = new QTabWidget();

    // Auto Exposure tab
    auto* aeWidget = new QWidget(); auto* aeLayout = new QGridLayout(aeWidget);
    m_autoExposureCheck = new QCheckBox(tr("Enabled")); aeLayout->addWidget(m_autoExposureCheck, 0, 0, 1, 2);
    m_aeDelaySpin = new QDoubleSpinBox(); m_aeDelaySpin->setRange(0, 10); aeLayout->addWidget(new QLabel(tr("Delay:")), 1, 0); aeLayout->addWidget(m_aeDelaySpin, 1, 1);
    m_aeTargetSpin = new QDoubleSpinBox(); m_aeTargetSpin->setRange(0, 1); aeLayout->addWidget(new QLabel(tr("Target:")), 2, 0); aeLayout->addWidget(m_aeTargetSpin, 2, 1);
    m_aeMinSpin = new QDoubleSpinBox(); m_aeMinSpin->setRange(0, 1); aeLayout->addWidget(new QLabel(tr("Min:")), 3, 0); aeLayout->addWidget(m_aeMinSpin, 3, 1);
    m_aeMaxSpin = new QDoubleSpinBox(); m_aeMaxSpin->setRange(0, 10); aeLayout->addWidget(new QLabel(tr("Max:")), 4, 0); aeLayout->addWidget(m_aeMaxSpin, 4, 1);
    m_tabWidget->addTab(aeWidget, tr("Auto Exposure"));

    // Tone Mapping tab
    auto* tmWidget = new QWidget(); auto* tmLayout = new QGridLayout(tmWidget);
    m_exposureSpin = new QDoubleSpinBox(); m_exposureSpin->setRange(0, 5); tmLayout->addWidget(new QLabel(tr("Exposure:")), 0, 0); tmLayout->addWidget(m_exposureSpin, 0, 1);
    m_gammaSpin = new QDoubleSpinBox(); m_gammaSpin->setRange(0.1, 5); tmLayout->addWidget(new QLabel(tr("Gamma:")), 1, 0); tmLayout->addWidget(m_gammaSpin, 1, 1);
    m_tabWidget->addTab(tmWidget, tr("Tone Mapping"));

    // DOF tab
    auto* dofWidget = new QWidget(); auto* dofLayout = new QGridLayout(dofWidget);
    m_dofCheck = new QCheckBox(tr("Enabled")); dofLayout->addWidget(m_dofCheck, 0, 0, 1, 2);
    m_dofApertureSpin = new QDoubleSpinBox(); m_dofApertureSpin->setRange(0.1, 100); dofLayout->addWidget(new QLabel(tr("Aperture:")), 1, 0); dofLayout->addWidget(m_dofApertureSpin, 1, 1);
    m_tabWidget->addTab(dofWidget, tr("DOF"));

    // Glare tab
    auto* glWidget = new QWidget(); auto* glLayout = new QGridLayout(glWidget);
    m_glareCheck = new QCheckBox(tr("Enabled")); glLayout->addWidget(m_glareCheck, 0, 0, 1, 2);
    m_glareLuminanceSpin = new QDoubleSpinBox(); m_glareLuminanceSpin->setRange(0, 100); glLayout->addWidget(new QLabel(tr("Luminance:")), 1, 0); glLayout->addWidget(m_glareLuminanceSpin, 1, 1);
    m_glareThresholdSpin = new QDoubleSpinBox(); m_glareThresholdSpin->setRange(0, 100); glLayout->addWidget(new QLabel(tr("Threshold:")), 2, 0); glLayout->addWidget(m_glareThresholdSpin, 2, 1);
    m_tabWidget->addTab(glWidget, tr("Glare"));

    // God Rays tab
    auto* grWidget = new QWidget(); auto* grLayout = new QGridLayout(grWidget);
    m_godRaysCheck = new QCheckBox(tr("Enabled")); grLayout->addWidget(m_godRaysCheck, 0, 0, 1, 2);
    m_godRaysLengthSpin = new QDoubleSpinBox(); m_godRaysLengthSpin->setRange(0, 100); grLayout->addWidget(new QLabel(tr("Length:")), 1, 0); grLayout->addWidget(m_godRaysLengthSpin, 1, 1);
    m_tabWidget->addTab(grWidget, tr("God Rays"));

    // Color tab
    auto* cWidget = new QWidget(); auto* cLayout = new QGridLayout(cWidget);
    m_saturationSpin = new QDoubleSpinBox(); m_saturationSpin->setRange(0, 5); cLayout->addWidget(new QLabel(tr("Saturation:")), 0, 0); cLayout->addWidget(m_saturationSpin, 0, 1);
    m_brightnessSpin = new QDoubleSpinBox(); m_brightnessSpin->setRange(0, 5); cLayout->addWidget(new QLabel(tr("Brightness:")), 1, 0); cLayout->addWidget(m_brightnessSpin, 1, 1);
    m_contrastSpin = new QDoubleSpinBox(); m_contrastSpin->setRange(0, 5); cLayout->addWidget(new QLabel(tr("Contrast:")), 2, 0); cLayout->addWidget(m_contrastSpin, 2, 1);
    m_colorTempSpin = new QSpinBox(); m_colorTempSpin->setRange(1000, 20000); cLayout->addWidget(new QLabel(tr("Color Temp:")), 3, 0); cLayout->addWidget(m_colorTempSpin, 3, 1);
    m_tabWidget->addTab(cWidget, tr("Color"));

    mainLayout->addWidget(m_tabWidget);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load ppeffects.ini")); m_saveBtn = new QPushButton(tr("Save ppeffects.ini")); m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready")); mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &ShowroomPPEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &ShowroomPPEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &ShowroomPPEditorModule::onResetDefaults);

    // Connect UI widgets to handlers for live state updates
    connect(m_autoExposureCheck, &QCheckBox::toggled, this, &ShowroomPPEditorModule::onAutoExposureToggled);
    connect(m_aeDelaySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onAEDelayChanged);
    connect(m_aeTargetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onAETargetChanged);
    connect(m_aeMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onAEMinChanged);
    connect(m_aeMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onAEMaxChanged);
    connect(m_exposureSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onExposureChanged);
    connect(m_gammaSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onGammaChanged);
    connect(m_dofCheck, &QCheckBox::toggled, this, &ShowroomPPEditorModule::onDOFToggled);
    connect(m_dofApertureSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onDOFApertureChanged);
    connect(m_glareCheck, &QCheckBox::toggled, this, &ShowroomPPEditorModule::onGlareToggled);
    connect(m_glareLuminanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onGlareLuminanceChanged);
    connect(m_glareThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onGlareThresholdChanged);
    connect(m_godRaysCheck, &QCheckBox::toggled, this, &ShowroomPPEditorModule::onGodRaysToggled);
    connect(m_godRaysLengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onGodRaysLengthChanged);
    connect(m_saturationSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onSaturationChanged);
    connect(m_brightnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onBrightnessChanged);
    connect(m_contrastSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShowroomPPEditorModule::onContrastChanged);
    connect(m_colorTempSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ShowroomPPEditorModule::onColorTempChanged);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void ShowroomPPEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void ShowroomPPEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void ShowroomPPEditorModule::onActivation()
{
    if (m_dockWidget) {
        m_dockWidget->show();
        m_dockWidget->raise();
    }
}

void ShowroomPPEditorModule::onDeactivation()
{
    if (m_dockWidget) m_dockWidget->hide();
}
void ShowroomPPEditorModule::onLoadFile() { QString p = QFileDialog::getOpenFileName(this, tr("Open ppeffects.ini"), QString(), tr("PP INI (*.ini)")); if (!p.isEmpty()) { m_filePath = p; loadFileToUI(); } }
void ShowroomPPEditorModule::onSaveFile() { QString p = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save ppeffects.ini"), QString(), tr("PP INI (*.ini)")) : m_filePath; if (!p.isEmpty()) { m_filePath = p; saveFileFromUI(); } }
void ShowroomPPEditorModule::onResetDefaults() {
    m_autoExposureCheck->setChecked(true); m_aeDelaySpin->setValue(0.0); m_aeTargetSpin->setValue(0.32);
    m_aeMinSpin->setValue(0.2); m_aeMaxSpin->setValue(0.5); m_exposureSpin->setValue(0.28);
    m_gammaSpin->setValue(1.2); m_dofCheck->setChecked(true); m_dofApertureSpin->setValue(12.0);
    m_glareCheck->setChecked(true); m_glareLuminanceSpin->setValue(1.6); m_glareThresholdSpin->setValue(5.0);
    m_godRaysCheck->setChecked(true); m_godRaysLengthSpin->setValue(11.0);
    m_saturationSpin->setValue(0.95); m_brightnessSpin->setValue(1.0); m_contrastSpin->setValue(1.0);
    m_colorTempSpin->setValue(6400);
    syncStateFromUI();
    m_statusLabel->setText(tr("Reset to defaults")); m_modified = false;
}
void ShowroomPPEditorModule::onAutoExposureToggled(bool v) { m_state.autoExposure = v; m_modified = true; m_statusLabel->setText(tr("Modified (Auto Exposure)")); }
void ShowroomPPEditorModule::onAEDelayChanged(double v) { m_state.aeDelay = v; m_modified = true; m_statusLabel->setText(tr("Modified (AE Delay)")); }
void ShowroomPPEditorModule::onAETargetChanged(double v) { m_state.aeTarget = v; m_modified = true; m_statusLabel->setText(tr("Modified (AE Target)")); }
void ShowroomPPEditorModule::onAEMinChanged(double v) { m_state.aeMin = v; m_modified = true; m_statusLabel->setText(tr("Modified (AE Min)")); }
void ShowroomPPEditorModule::onAEMaxChanged(double v) { m_state.aeMax = v; m_modified = true; m_statusLabel->setText(tr("Modified (AE Max)")); }
void ShowroomPPEditorModule::onExposureChanged(double v) { m_state.exposure = v; m_modified = true; m_statusLabel->setText(tr("Modified (Exposure)")); }
void ShowroomPPEditorModule::onGammaChanged(double v) { m_state.gamma = v; m_modified = true; m_statusLabel->setText(tr("Modified (Gamma)")); }
void ShowroomPPEditorModule::onDOFToggled(bool v) { m_state.dof = v; m_modified = true; m_statusLabel->setText(tr("Modified (DOF)")); }
void ShowroomPPEditorModule::onDOFApertureChanged(double v) { m_state.dofAperture = v; m_modified = true; m_statusLabel->setText(tr("Modified (DOF Aperture)")); }
void ShowroomPPEditorModule::onGlareToggled(bool v) { m_state.glare = v; m_modified = true; m_statusLabel->setText(tr("Modified (Glare)")); }
void ShowroomPPEditorModule::onGlareLuminanceChanged(double v) { m_state.glareLuminance = v; m_modified = true; m_statusLabel->setText(tr("Modified (Glare Luminance)")); }
void ShowroomPPEditorModule::onGlareThresholdChanged(double v) { m_state.glareThreshold = v; m_modified = true; m_statusLabel->setText(tr("Modified (Glare Threshold)")); }
void ShowroomPPEditorModule::onGodRaysToggled(bool v) { m_state.godRays = v; m_modified = true; m_statusLabel->setText(tr("Modified (God Rays)")); }
void ShowroomPPEditorModule::onGodRaysLengthChanged(double v) { m_state.godRaysLength = v; m_modified = true; m_statusLabel->setText(tr("Modified (God Rays Length)")); }
void ShowroomPPEditorModule::onSaturationChanged(double v) { m_state.saturation = v; m_modified = true; m_statusLabel->setText(tr("Modified (Saturation)")); }
void ShowroomPPEditorModule::onBrightnessChanged(double v) { m_state.brightness = v; m_modified = true; m_statusLabel->setText(tr("Modified (Brightness)")); }
void ShowroomPPEditorModule::onContrastChanged(double v) { m_state.contrast = v; m_modified = true; m_statusLabel->setText(tr("Modified (Contrast)")); }
void ShowroomPPEditorModule::onColorTempChanged(int v) { m_state.colorTemp = v; m_modified = true; m_statusLabel->setText(tr("Modified (Color Temp)")); }
void ShowroomPPEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void ShowroomPPEditorModule::syncStateFromUI() {
    m_state.autoExposure = m_autoExposureCheck->isChecked();
    m_state.aeDelay = m_aeDelaySpin->value(); m_state.aeTarget = m_aeTargetSpin->value();
    m_state.aeMin = m_aeMinSpin->value(); m_state.aeMax = m_aeMaxSpin->value();
    m_state.exposure = m_exposureSpin->value(); m_state.gamma = m_gammaSpin->value();
    m_state.dof = m_dofCheck->isChecked(); m_state.dofAperture = m_dofApertureSpin->value();
    m_state.glare = m_glareCheck->isChecked();
    m_state.glareLuminance = m_glareLuminanceSpin->value(); m_state.glareThreshold = m_glareThresholdSpin->value();
    m_state.godRays = m_godRaysCheck->isChecked(); m_state.godRaysLength = m_godRaysLengthSpin->value();
    m_state.saturation = m_saturationSpin->value(); m_state.brightness = m_brightnessSpin->value();
    m_state.contrast = m_contrastSpin->value(); m_state.colorTemp = m_colorTempSpin->value();
}

void ShowroomPPEditorModule::loadFileToUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString c = file.readAll(); file.close();
    auto val = [&](const QString& sec, const QString& key, double def) -> double {
        int si = c.indexOf("[" + sec); if (si < 0) return def;
        int ei = c.indexOf("[", si + 1); if (ei < 0) ei = c.length();
        QString s = c.mid(si, ei - si);
        for (const QString& l : s.split("\n")) { QString t = l.trimmed(); if (t.startsWith(key + "=")) return t.mid(key.length() + 1).toDouble(); }
        return def;
    };
    m_autoExposureCheck->setChecked(val("AUTO_EXPOSURE", "ENABLED", 1) > 0.5);
    m_aeDelaySpin->setValue(val("AUTO_EXPOSURE", "DELAY", 0.0));
    m_aeTargetSpin->setValue(val("AUTO_EXPOSURE", "TARGET", 0.32));
    m_aeMinSpin->setValue(val("AUTO_EXPOSURE", "MIN_VALUE", 0.2));
    m_aeMaxSpin->setValue(val("AUTO_EXPOSURE", "MAX_VALUE", 0.5));
    m_exposureSpin->setValue(val("TONEMAPPING", "EXPOSURE", 0.28));
    m_gammaSpin->setValue(val("TONEMAPPING", "GAMMA", 1.2));
    m_dofCheck->setChecked(val("DOF", "ENABLED", 1) > 0.5);
    m_dofApertureSpin->setValue(val("DOF", "APERTURE_FNUMBER", 12.0));
    m_glareCheck->setChecked(val("GLARE", "ENABLED", 1) > 0.5);
    m_glareLuminanceSpin->setValue(val("GLARE", "LUMINANCE", 1.6));
    m_glareThresholdSpin->setValue(val("GLARE", "THRESHOLD", 5.0));
    m_godRaysCheck->setChecked(val("GODRAYS", "ENABLED", 1) > 0.5);
    m_godRaysLengthSpin->setValue(val("GODRAYS", "LENGTH", 11.0));
    m_saturationSpin->setValue(val("COLOR", "SATURATION", 0.95));
    m_brightnessSpin->setValue(val("COLOR", "BRIGHTNESS", 1.0));
    m_contrastSpin->setValue(val("COLOR", "CONTRAST", 1.0));
    m_colorTempSpin->setValue((int)val("COLOR", "COLOR_TEMP", 6400));
    syncStateFromUI();
    m_modified = false;
    m_statusLabel->setText(tr("Loaded: %1").arg(QFileInfo(m_filePath).fileName()));
}

void ShowroomPPEditorModule::saveFileFromUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&file);
    o << "[AUTO_EXPOSURE]\nENABLED=" << (m_autoExposureCheck->isChecked() ? 1 : 0) << "\nDELAY=" << m_aeDelaySpin->value() << "\nTARGET=" << m_aeTargetSpin->value() << "\nMIN_VALUE=" << m_aeMinSpin->value() << "\nMAX_VALUE=" << m_aeMaxSpin->value() << "\n\n";
    o << "[TONEMAPPING]\nEXPOSURE=" << m_exposureSpin->value() << "\nGAMMA=" << m_gammaSpin->value() << "\n\n";
    o << "[DOF]\nENABLED=" << (m_dofCheck->isChecked() ? 1 : 0) << "\nAPERTURE_FNUMBER=" << m_dofApertureSpin->value() << "\n\n";
    o << "[GLARE]\nENABLED=" << (m_glareCheck->isChecked() ? 1 : 0) << "\nLUMINANCE=" << m_glareLuminanceSpin->value() << "\nTHRESHOLD=" << m_glareThresholdSpin->value() << "\n\n";
    o << "[GODRAYS]\nENABLED=" << (m_godRaysCheck->isChecked() ? 1 : 0) << "\nLENGTH=" << m_godRaysLengthSpin->value() << "\n\n";
    o << "[COLOR]\nSATURATION=" << m_saturationSpin->value() << "\nBRIGHTNESS=" << m_brightnessSpin->value() << "\nCONTRAST=" << m_contrastSpin->value() << "\nCOLOR_TEMP=" << m_colorTempSpin->value() << "\n";
    file.close();
    syncStateFromUI();
    m_modified = false;
    m_statusLabel->setText(tr("Saved: %1").arg(QFileInfo(m_filePath).fileName()));
}

QJsonObject ShowroomPPEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    QJsonObject ae;
    ae["enabled"] = m_state.autoExposure;
    ae["delay"] = m_state.aeDelay;
    ae["target"] = m_state.aeTarget;
    ae["min"] = m_state.aeMin;
    ae["max"] = m_state.aeMax;
    data["autoExposure"] = ae;
    data["exposure"] = m_state.exposure;
    data["gamma"] = m_state.gamma;
    QJsonObject dof;
    dof["enabled"] = m_state.dof;
    dof["aperture"] = m_state.dofAperture;
    data["dof"] = dof;
    QJsonObject glare;
    glare["enabled"] = m_state.glare;
    glare["luminance"] = m_state.glareLuminance;
    glare["threshold"] = m_state.glareThreshold;
    data["glare"] = glare;
    QJsonObject godRays;
    godRays["enabled"] = m_state.godRays;
    godRays["length"] = m_state.godRaysLength;
    data["godRays"] = godRays;
    data["saturation"] = m_state.saturation;
    data["brightness"] = m_state.brightness;
    data["contrast"] = m_state.contrast;
    data["colorTemp"] = m_state.colorTemp;
    return data;
}

void ShowroomPPEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_state.autoExposure = data["autoExposure"].toObject()["enabled"].toBool(true);
    m_state.aeDelay = data["autoExposure"].toObject()["delay"].toDouble(0.0);
    m_state.aeTarget = data["autoExposure"].toObject()["target"].toDouble(0.32);
    m_state.aeMin = data["autoExposure"].toObject()["min"].toDouble(0.2);
    m_state.aeMax = data["autoExposure"].toObject()["max"].toDouble(0.5);
    m_state.exposure = data["exposure"].toDouble(0.28);
    m_state.gamma = data["gamma"].toDouble(1.2);
    m_state.dof = data["dof"].toObject()["enabled"].toBool(true);
    m_state.dofAperture = data["dof"].toObject()["aperture"].toDouble(12.0);
    m_state.glare = data["glare"].toObject()["enabled"].toBool(true);
    m_state.glareLuminance = data["glare"].toObject()["luminance"].toDouble(1.6);
    m_state.glareThreshold = data["glare"].toObject()["threshold"].toDouble(5.0);
    m_state.godRays = data["godRays"].toObject()["enabled"].toBool(true);
    m_state.godRaysLength = data["godRays"].toObject()["length"].toDouble(11.0);
    m_state.saturation = data["saturation"].toDouble(0.95);
    m_state.brightness = data["brightness"].toDouble(1.0);
    m_state.contrast = data["contrast"].toDouble(1.0);
    m_state.colorTemp = (int)data["colorTemp"].toDouble(6400);
    m_modified = false;
}

} // namespace ks
