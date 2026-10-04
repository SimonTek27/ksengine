#include "CspConfigEditorModule.h"
#include "editor/ServerConfigEditor/CspShaderCompiler.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>

namespace ks {

CspConfigEditorModule::CspConfigEditorModule(QWidget* parent) : EditorModule(parent) {}

bool CspConfigEditorModule::initialize()
{
    LOG_INFO("CspConfigEditorModule", "Initialized");
    return true;
}

void CspConfigEditorModule::shutdown()
{
    delete m_cspManager;
    m_cspManager = nullptr;
}

QDockWidget* CspConfigEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("CSP Config Editor"), mainWindow);
    m_dockWidget->setObjectName("CspConfigEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);
    m_tabWidget = new QTabWidget();

    // ── Tab 1: General ──────────────────────────────────────────────
    auto* genWidget = new QWidget();
    auto* genLayout = new QGridLayout(genWidget);

    genLayout->addWidget(new QLabel(tr("AC Path:")), 0, 0);
    m_acPathEdit = new QLineEdit();
    m_acPathEdit->setPlaceholderText(tr("C:/Program Files (x86)/Steam/steamapps/common/assettocorsa"));
    genLayout->addWidget(m_acPathEdit, 0, 1);
    m_browseBtn = new QPushButton(tr("Browse..."));
    genLayout->addWidget(m_browseBtn, 0, 2);

    m_cspStatusLabel = new QLabel(tr("CSP Status: Not checked"));
    genLayout->addWidget(m_cspStatusLabel, 1, 0, 1, 3);

    m_cspVersionLabel = new QLabel(tr("CSP Version: --"));
    genLayout->addWidget(m_cspVersionLabel, 2, 0, 1, 3);

    m_refreshBtn = new QPushButton(tr("Refresh Status"));
    genLayout->addWidget(m_refreshBtn, 3, 0, 1, 3);
    m_tabWidget->addTab(genWidget, tr("General"));

    // ── Tab 2: WeatherFX ────────────────────────────────────────────
    auto* weWidget = new QWidget();
    auto* weLayout = new QGridLayout(weWidget);
    m_weEnabled = new QCheckBox(tr("Enabled"));
    weLayout->addWidget(m_weEnabled, 0, 0, 1, 2);
    weLayout->addWidget(new QLabel(tr("Script Name:")), 1, 0);
    m_weScript = new QLineEdit();
    weLayout->addWidget(m_weScript, 1, 1);
    m_weTimeMult = new QDoubleSpinBox();
    m_weTimeMult->setRange(0.0, 100.0);
    m_weTimeMult->setSingleStep(0.1);
    weLayout->addWidget(new QLabel(tr("Time Multiplier:")), 2, 0);
    weLayout->addWidget(m_weTimeMult, 2, 1);
    m_weRealWeather = new QCheckBox(tr("Use Real Weather"));
    weLayout->addWidget(m_weRealWeather, 3, 0, 1, 2);
    m_tabWidget->addTab(weWidget, tr("WeatherFX"));

    // ── Tab 3: LightingFX ───────────────────────────────────────────
    auto* lfWidget = new QWidget();
    auto* lfLayout = new QGridLayout(lfWidget);
    m_lfEnabled = new QCheckBox(tr("Enabled"));
    lfLayout->addWidget(m_lfEnabled, 0, 0, 1, 2);
    m_lfDynamicLights = new QCheckBox(tr("Dynamic Lights"));
    lfLayout->addWidget(m_lfDynamicLights, 1, 0, 1, 2);
    m_lfOcclusion = new QCheckBox(tr("Enable Occlusion"));
    lfLayout->addWidget(m_lfOcclusion, 2, 0, 1, 2);
    m_lfAmbient = new QDoubleSpinBox();
    m_lfAmbient->setRange(0.0, 10.0);
    m_lfAmbient->setSingleStep(0.1);
    lfLayout->addWidget(new QLabel(tr("Ambient Multiplier:")), 3, 0);
    lfLayout->addWidget(m_lfAmbient, 3, 1);
    m_lfSun = new QDoubleSpinBox();
    m_lfSun->setRange(0.0, 10.0);
    m_lfSun->setSingleStep(0.1);
    lfLayout->addWidget(new QLabel(tr("Sun Multiplier:")), 4, 0);
    lfLayout->addWidget(m_lfSun, 4, 1);
    m_tabWidget->addTab(lfWidget, tr("LightingFX"));

    // ── Tab 4: ParticlesFX ──────────────────────────────────────────
    auto* pfWidget = new QWidget();
    auto* pfLayout = new QGridLayout(pfWidget);
    m_pfEnabled = new QCheckBox(tr("Enabled"));
    pfLayout->addWidget(m_pfEnabled, 0, 0, 1, 2);
    m_pfSmoke = new QCheckBox(tr("Enable Smoke"));
    pfLayout->addWidget(m_pfSmoke, 1, 0, 1, 2);
    m_pfSparks = new QCheckBox(tr("Enable Sparks"));
    pfLayout->addWidget(m_pfSparks, 2, 0, 1, 2);
    m_pfGrass = new QCheckBox(tr("Enable Grass"));
    pfLayout->addWidget(m_pfGrass, 3, 0, 1, 2);
    m_pfSmokeIntensity = new QDoubleSpinBox();
    m_pfSmokeIntensity->setRange(0.0, 10.0);
    m_pfSmokeIntensity->setSingleStep(0.1);
    pfLayout->addWidget(new QLabel(tr("Smoke Intensity:")), 4, 0);
    pfLayout->addWidget(m_pfSmokeIntensity, 4, 1);
    m_pfSparkIntensity = new QDoubleSpinBox();
    m_pfSparkIntensity->setRange(0.0, 10.0);
    m_pfSparkIntensity->setSingleStep(0.1);
    pfLayout->addWidget(new QLabel(tr("Spark Intensity:")), 5, 0);
    pfLayout->addWidget(m_pfSparkIntensity, 5, 1);
    m_tabWidget->addTab(pfWidget, tr("ParticlesFX"));

    // ── Tab 5: Physics Extensions ───────────────────────────────────
    auto* phWidget = new QWidget();
    auto* phLayout = new QGridLayout(phWidget);
    m_phEnabled = new QCheckBox(tr("Enabled"));
    phLayout->addWidget(m_phEnabled, 0, 0, 1, 2);
    m_phAero = new QCheckBox(tr("Enable Aero"));
    phLayout->addWidget(m_phAero, 1, 0, 1, 2);
    m_phSuspension = new QCheckBox(tr("Enable Suspension"));
    phLayout->addWidget(m_phSuspension, 2, 0, 1, 2);
    m_phTires = new QCheckBox(tr("Enable Tires"));
    phLayout->addWidget(m_phTires, 3, 0, 1, 2);
    m_phAeroMult = new QDoubleSpinBox();
    m_phAeroMult->setRange(0.0, 10.0);
    m_phAeroMult->setSingleStep(0.1);
    phLayout->addWidget(new QLabel(tr("Aero Multiplier:")), 4, 0);
    phLayout->addWidget(m_phAeroMult, 4, 1);
    m_tabWidget->addTab(phWidget, tr("Physics"));

    // ── Tab 6: Car Extensions ───────────────────────────────────────
    auto* ceWidget = new QWidget();
    auto* ceLayout = new QGridLayout(ceWidget);
    m_ceEnabled = new QCheckBox(tr("Enabled"));
    ceLayout->addWidget(m_ceEnabled, 0, 0, 1, 2);
    m_ceReverseLights = new QCheckBox(tr("Reverse Lights"));
    ceLayout->addWidget(m_ceReverseLights, 1, 0, 1, 2);
    m_ceTurnSignals = new QCheckBox(tr("Turn Signals"));
    ceLayout->addWidget(m_ceTurnSignals, 2, 0, 1, 2);
    m_ceOdometer = new QCheckBox(tr("Odometer"));
    ceLayout->addWidget(m_ceOdometer, 3, 0, 1, 2);
    m_ceWipers = new QCheckBox(tr("Working Wipers"));
    ceLayout->addWidget(m_ceWipers, 4, 0, 1, 2);
    m_tabWidget->addTab(ceWidget, tr("Car Extensions"));

    // ── Tab 7: Track Extensions ─────────────────────────────────────
    auto* teWidget = new QWidget();
    auto* teLayout = new QGridLayout(teWidget);
    m_teEnabled = new QCheckBox(tr("Enabled"));
    teLayout->addWidget(m_teEnabled, 0, 0, 1, 2);
    m_teGrassFx = new QCheckBox(tr("Grass FX"));
    teLayout->addWidget(m_teGrassFx, 1, 0, 1, 2);
    m_teParticles = new QCheckBox(tr("Particles"));
    teLayout->addWidget(m_teParticles, 2, 0, 1, 2);
    m_teGrassDistance = new QDoubleSpinBox();
    m_teGrassDistance->setRange(1.0, 1000.0);
    m_teGrassDistance->setSingleStep(10.0);
    teLayout->addWidget(new QLabel(tr("Grass Distance:")), 3, 0);
    teLayout->addWidget(m_teGrassDistance, 3, 1);
    m_tabWidget->addTab(teWidget, tr("Track Extensions"));

    // ── Tab 8: Extensions Browser ───────────────────────────────────
    auto* extWidget = new QWidget();
    auto* extLayout = new QVBoxLayout(extWidget);
    auto* extTopLayout = new QHBoxLayout();
    m_extList = new QListWidget();
    extTopLayout->addWidget(m_extList);
    m_extConfigView = new QTextEdit();
    m_extConfigView->setReadOnly(true);
    extTopLayout->addWidget(m_extConfigView);
    extLayout->addLayout(extTopLayout);
    auto* extRefreshBtn = new QPushButton(tr("Refresh Extension List"));
    extLayout->addWidget(extRefreshBtn);
    m_tabWidget->addTab(extWidget, tr("Extensions"));

    // ── Tab 9: Shader Compiler ──────────────────────────────────────
    auto* scWidget = new QWidget();
    auto* scLayout = new QVBoxLayout(scWidget);

    auto* scHeader = new QLabel(tr("GLSL Shader Compiler & Profiler"));
    scHeader->setStyleSheet("font-weight: bold; font-size: 13px;");
    scLayout->addWidget(scHeader);

    m_compilerStatusLabel = new QLabel(tr("Compiler: checking..."));
    m_compilerStatusLabel->setStyleSheet("color: #888; font-size: 11px;");
    scLayout->addWidget(m_compilerStatusLabel);

    auto* scSourceLabel = new QLabel(tr("Shader Source (GLSL):"));
    scLayout->addWidget(scSourceLabel);

    m_shaderSourceEdit = new QPlainTextEdit();
    m_shaderSourceEdit->setPlaceholderText(tr("#version 450\n\nlayout(location = 0) in vec3 inPosition;\nlayout(location = 0) out vec4 outColor;\n\nvoid main() {\n    outColor = vec4(inPosition, 1.0);\n}"));
    m_shaderSourceEdit->setTabStopDistance(20);
    m_shaderSourceEdit->setMinimumHeight(150);
    scLayout->addWidget(m_shaderSourceEdit);

    auto* scBtnLayout = new QHBoxLayout();
    auto* validateBtn = new QPushButton(tr("Validate"));
    validateBtn->setStyleSheet("background: #555; color: white;");
    scBtnLayout->addWidget(validateBtn);

    auto* compileBtn = new QPushButton(tr("Compile to SPIR-V"));
    compileBtn->setStyleSheet("background: #E10600; color: white; font-weight: bold;");
    scBtnLayout->addWidget(compileBtn);

    auto* profileBtn = new QPushButton(tr("Profile"));
    profileBtn->setStyleSheet("background: #2a7a2a; color: white;");
    scBtnLayout->addWidget(profileBtn);
    scLayout->addLayout(scBtnLayout);

    auto* scResultLabel = new QLabel(tr("Results:"));
    scLayout->addWidget(scResultLabel);

    m_shaderResultView = new QTextEdit();
    m_shaderResultView->setReadOnly(true);
    m_shaderResultView->setMaximumHeight(180);
    scLayout->addWidget(m_shaderResultView);

    m_tabWidget->addTab(scWidget, tr("Shader Compiler"));

    // Check available compilers
    QStringList compilers = CspShaderCompiler::findAvailableCompilers();
    if (!compilers.isEmpty()) {
        m_compilerStatusLabel->setText(tr("Compiler: %1 (found %2)").arg(compilers.first()).arg(compilers.size()));
        m_compilerStatusLabel->setStyleSheet("color: #2a7a2a; font-size: 11px;");
    } else {
        m_compilerStatusLabel->setText(tr("Compiler: none found (install glslangValidator or glslc)"));
        m_compilerStatusLabel->setStyleSheet("color: #E10600; font-size: 11px;");
    }

    connect(validateBtn, &QPushButton::clicked, this, [this]() {
        QString src = m_shaderSourceEdit->toPlainText();
        QStringList errors;
        bool valid = CspShaderCompiler::validateSource(src, &errors);
        if (valid) {
            m_shaderResultView->setHtml("<span style='color: #2a7a2a;'>" + tr("Validation passed. Source is valid GLSL.") + "</span>");
        } else {
            QString html = "<span style='color: #E10600;'>" + tr("Validation failed:") + "</span><br/>";
            for (const QString& e : errors) html += e.toHtmlEscaped() + "<br/>";
            m_shaderResultView->setHtml(html);
        }
    });

    connect(compileBtn, &QPushButton::clicked, this, [this]() {
        QString src = m_shaderSourceEdit->toPlainText();
        if (src.isEmpty()) {
            m_shaderResultView->setPlainText(tr("No shader source to compile."));
            return;
        }
        ShaderCompileResult result = CspShaderCompiler::compileGLSLToSPIRV(src, true);
        QString html;
        if (result.success) {
            html += QString("<span style='color: #2a7a2a;'>%1</span><br/>").arg(tr("Compilation successful!"));
            html += tr("SPIR-V size: %1 bytes<br/>").arg(result.spirv.size());
            html += tr("Compiler: %1<br/>").arg(result.compilerUsed);
        } else {
            html += "<span style='color: #E10600;'>" + tr("Compilation failed:") + "</span><br/>";
            for (const QString& e : result.errors) html += e.toHtmlEscaped() + "<br/>";
        }
        if (!result.warnings.isEmpty()) {
            html += "<br/><b>" + tr("Warnings:") + "</b><br/>";
            for (const QString& w : result.warnings) html += w.toHtmlEscaped() + "<br/>";
        }
        m_shaderResultView->setHtml(html);
    });

    connect(profileBtn, &QPushButton::clicked, this, [this]() {
        QString src = m_shaderSourceEdit->toPlainText();
        if (src.isEmpty()) {
            m_shaderResultView->setPlainText(tr("No shader source to profile."));
            return;
        }
        ShaderProfile profile = CspShaderCompiler::profileShader(src);
        QString html = QString(
            tr("<b>Shader Profile</b><br/>"
            "<table style='font-size: 12px;'>"
            "<tr><td>Estimated Instructions:</td><td><b>%1</b></td></tr>"
            "<tr><td>Texture Samples:</td><td>%2</td></tr>"
            "<tr><td>Uniforms:</td><td>%3</td></tr>"
            "<tr><td>Branches:</td><td>%4</td></tr>"
            "<tr><td>Loops:</td><td>%5</td></tr>"
            "<tr><td>Function Calls:</td><td>%6</td></tr>"
            "<tr><td>Uses discard:</td><td>%7</td></tr>"
            "<tr><td>Uses derivatives:</td><td>%8</td></tr>"
            "<tr><td>Estimated Complexity:</td><td><b>%9</b></td></tr>"
            "</table>")
        ).arg(profile.estimatedInstructions)
         .arg(profile.textureSamples)
         .arg(profile.uniformCount)
         .arg(profile.branchCount)
         .arg(profile.loopCount)
         .arg(profile.functionCalls)
         .arg(profile.usesDiscard ? tr("Yes") : tr("No"))
         .arg(profile.usesDerivatives ? tr("Yes") : tr("No"))
         .arg(profile.estimatedComplexity, 0, 'f', 2);
        m_shaderResultView->setHtml(html);
    });

    mainLayout->addWidget(m_tabWidget);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load"));
    m_saveBtn = new QPushButton(tr("Save"));
    m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &CspConfigEditorModule::onLoad);
    connect(m_saveBtn, &QPushButton::clicked, this, &CspConfigEditorModule::onSave);
    connect(m_resetBtn, &QPushButton::clicked, this, &CspConfigEditorModule::onReset);
    connect(m_browseBtn, &QPushButton::clicked, this, &CspConfigEditorModule::onBrowseAcPath);
    connect(m_refreshBtn, &QPushButton::clicked, this, &CspConfigEditorModule::onRefreshStatus);
    connect(extRefreshBtn, &QPushButton::clicked, this, &CspConfigEditorModule::refreshExtList);
    connect(m_extList, &QListWidget::currentRowChanged, this, &CspConfigEditorModule::onExtSelectionChanged);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void CspConfigEditorModule::importFile(const QString& filePath)
{
    if (!m_cspManager) {
        QString acPath = m_acPathEdit ? m_acPathEdit->text() : QString();
        if (acPath.isEmpty()) acPath = filePath;
        m_cspManager = new CspConfigManager(acPath);
    }
    onLoad();
}

void CspConfigEditorModule::exportFile(const QString& filePath)
{
    if (!m_cspManager) return;
    saveToManager();
    m_cspManager->saveGlobalConfig();
    m_statusLabel->setText(tr("Saved: %1").arg(filePath));
}

void CspConfigEditorModule::onActivation()
{
    if (m_statusLabel) m_statusLabel->setText(tr("Active"));
}

void CspConfigEditorModule::onDeactivation()
{
    if (m_statusLabel) m_statusLabel->setText(tr("Inactive"));
}

// ── Slots ──────────────────────────────────────────────────────────

void CspConfigEditorModule::onLoad()
{
    QString acPath = m_acPathEdit ? m_acPathEdit->text() : QString();
    if (acPath.isEmpty()) {
        acPath = QFileDialog::getExistingDirectory(this, tr("Select Assetto Corsa Installation"));
        if (acPath.isEmpty()) return;
        m_acPathEdit->setText(acPath);
    }

    if (!m_cspManager || m_cspManager->getCspPath() != acPath + "/extension") {
        delete m_cspManager;
        m_cspManager = new CspConfigManager(acPath);
    }

    loadFromManager();
    onRefreshStatus();
    refreshExtList();
    m_statusLabel->setText(tr("Loaded CSP config from: %1").arg(acPath));
}

void CspConfigEditorModule::onSave()
{
    if (!m_cspManager) {
        m_statusLabel->setText(tr("No CSP config loaded. Click Load first."));
        return;
    }
    saveToManager();
    m_cspManager->saveGlobalConfig();
    m_statusLabel->setText(tr("Saved CSP config to: %1").arg(m_cspManager->getCspPath()));
}

void CspConfigEditorModule::onReset()
{
    m_weEnabled->setChecked(false);
    m_weScript->clear();
    m_weTimeMult->setValue(1.0);
    m_weRealWeather->setChecked(false);

    m_lfEnabled->setChecked(false);
    m_lfDynamicLights->setChecked(true);
    m_lfOcclusion->setChecked(true);
    m_lfAmbient->setValue(1.0);
    m_lfSun->setValue(1.0);

    m_pfEnabled->setChecked(false);
    m_pfSmoke->setChecked(true);
    m_pfSparks->setChecked(true);
    m_pfGrass->setChecked(true);
    m_pfSmokeIntensity->setValue(1.0);
    m_pfSparkIntensity->setValue(1.0);

    m_phEnabled->setChecked(false);
    m_phAero->setChecked(true);
    m_phSuspension->setChecked(true);
    m_phTires->setChecked(true);
    m_phAeroMult->setValue(1.0);

    m_ceEnabled->setChecked(false);
    m_ceReverseLights->setChecked(true);
    m_ceTurnSignals->setChecked(true);
    m_ceOdometer->setChecked(true);
    m_ceWipers->setChecked(true);

    m_teEnabled->setChecked(false);
    m_teGrassFx->setChecked(true);
    m_teParticles->setChecked(true);
    m_teGrassDistance->setValue(100.0);

    m_statusLabel->setText(tr("Reset to defaults"));
}

void CspConfigEditorModule::onBrowseAcPath()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Assetto Corsa Installation"));
    if (!dir.isEmpty()) {
        m_acPathEdit->setText(dir);
    }
}

void CspConfigEditorModule::onRefreshStatus()
{
    if (!m_cspManager) {
        QString acPath = m_acPathEdit ? m_acPathEdit->text() : QString();
        if (acPath.isEmpty()) {
            m_cspStatusLabel->setText(tr("CSP Status: No AC path set"));
            m_cspVersionLabel->setText("CSP Version: --");
            return;
        }
        m_cspManager = new CspConfigManager(acPath);
    }
    m_cspStatusLabel->setText(m_cspManager->isCspInstalled()
        ? tr("CSP Status: Installed") : tr("CSP Status: Not found"));
    m_cspVersionLabel->setText(tr("CSP Version: %1").arg(m_cspManager->getCspVersion()));
}

void CspConfigEditorModule::onExtSelectionChanged()
{
    if (!m_cspManager) return;
    int row = m_extList->currentRow();
    if (row < 0) { m_extConfigView->clear(); return; }

    QString extName = m_extList->item(row)->text();
    QJsonObject config = m_cspManager->getExtensionConfig(extName);
    QJsonDocument doc(config);
    m_extConfigView->setPlainText(doc.toJson(QJsonDocument::Indented));
}

// ── Private helpers ────────────────────────────────────────────────

void CspConfigEditorModule::loadFromManager()
{
    if (!m_cspManager) return;
    if (!m_cspManager->isCspInstalled()) {
        m_statusLabel->setText(tr("CSP not installed at this path"));
        return;
    }

    m_cspManager->loadGlobalConfig();

    // WeatherFX
    auto wf = m_cspManager->getWeatherFx();
    m_weEnabled->setChecked(wf.enabled);
    m_weScript->setText(wf.scriptName);
    m_weTimeMult->setValue(wf.timeMultiplier);
    m_weRealWeather->setChecked(wf.useRealWeather);

    // LightingFX
    auto lf = m_cspManager->getLightingFx();
    m_lfEnabled->setChecked(lf.enabled);
    m_lfDynamicLights->setChecked(lf.dynamicLights);
    m_lfOcclusion->setChecked(lf.enableOcclusion);
    m_lfAmbient->setValue(lf.ambientMultiplier);
    m_lfSun->setValue(lf.sunMultiplier);

    // ParticlesFX
    auto pf = m_cspManager->getParticlesFx();
    m_pfEnabled->setChecked(pf.enabled);
    m_pfSmoke->setChecked(pf.enableSmoke);
    m_pfSparks->setChecked(pf.enableSparks);
    m_pfGrass->setChecked(pf.enableGrass);
    m_pfSmokeIntensity->setValue(pf.smokeIntensity);
    m_pfSparkIntensity->setValue(pf.sparkIntensity);

    // Physics
    auto ph = m_cspManager->getPhysicsExtensions();
    m_phEnabled->setChecked(ph.enabled);
    m_phAero->setChecked(ph.enableAero);
    m_phSuspension->setChecked(ph.enableSuspension);
    m_phTires->setChecked(ph.enableTires);
    m_phAeroMult->setValue(ph.aeroMultiplier);

    // Car Extensions
    QString cePath = m_cspManager->getCspPath() + "/common/car_extensions.ini";
    auto ce = CspConfigParser::parseCarExtensions(cePath);
    m_ceEnabled->setChecked(ce.enabled);
    m_ceReverseLights->setChecked(ce.enableReverseLights);
    m_ceTurnSignals->setChecked(ce.enableTurnSignals);
    m_ceOdometer->setChecked(ce.enableOdometer);
    m_ceWipers->setChecked(ce.enableWorkingWipers);

    // Track
    auto te = m_cspManager->getTrackExtensions();
    m_teEnabled->setChecked(te.enabled);
    m_teGrassFx->setChecked(te.enableGrassFx);
    m_teParticles->setChecked(te.enableParticles);
    m_teGrassDistance->setValue(te.grassDistance);
}

void CspConfigEditorModule::saveToManager()
{
    if (!m_cspManager) return;

    // WeatherFX
    CspConfigParser::CspWeatherFx wf;
    wf.enabled = m_weEnabled->isChecked();
    wf.scriptName = m_weScript->text();
    wf.timeMultiplier = static_cast<float>(m_weTimeMult->value());
    wf.useRealWeather = m_weRealWeather->isChecked();
    m_cspManager->setWeatherFx(wf);

    // LightingFX
    CspConfigParser::CspLightingFx lf;
    lf.enabled = m_lfEnabled->isChecked();
    lf.dynamicLights = m_lfDynamicLights->isChecked();
    lf.enableOcclusion = m_lfOcclusion->isChecked();
    lf.ambientMultiplier = static_cast<float>(m_lfAmbient->value());
    lf.sunMultiplier = static_cast<float>(m_lfSun->value());
    m_cspManager->setLightingFx(lf);

    // ParticlesFX
    CspConfigParser::CspParticlesFx pf;
    pf.enabled = m_pfEnabled->isChecked();
    pf.enableSmoke = m_pfSmoke->isChecked();
    pf.enableSparks = m_pfSparks->isChecked();
    pf.enableGrass = m_pfGrass->isChecked();
    pf.smokeIntensity = static_cast<float>(m_pfSmokeIntensity->value());
    pf.sparkIntensity = static_cast<float>(m_pfSparkIntensity->value());
    m_cspManager->setParticlesFx(pf);

    // Physics
    CspConfigParser::CspPhysicsExtensions ph;
    ph.enabled = m_phEnabled->isChecked();
    ph.enableAero = m_phAero->isChecked();
    ph.enableSuspension = m_phSuspension->isChecked();
    ph.enableTires = m_phTires->isChecked();
    ph.aeroMultiplier = static_cast<float>(m_phAeroMult->value());
    m_cspManager->setPhysicsExtensions(ph);

    // Car Extensions
    CspConfigParser::CspCarExtensions ce;
    ce.enabled = m_ceEnabled->isChecked();
    ce.enableReverseLights = m_ceReverseLights->isChecked();
    ce.enableTurnSignals = m_ceTurnSignals->isChecked();
    ce.enableOdometer = m_ceOdometer->isChecked();
    ce.enableWorkingWipers = m_ceWipers->isChecked();
    m_cspManager->setCarExtensions(ce);

    // Track
    CspConfigParser::CspTrackExtensions te;
    te.enabled = m_teEnabled->isChecked();
    te.enableGrassFx = m_teGrassFx->isChecked();
    te.enableParticles = m_teParticles->isChecked();
    te.grassDistance = static_cast<float>(m_teGrassDistance->value());
    m_cspManager->setTrackExtensions(te);
}

void CspConfigEditorModule::refreshExtList()
{
    if (!m_cspManager) return;
    m_extList->clear();
    QStringList exts = m_cspManager->getAvailableExtensions();
    for (const QString& ext : exts) {
        auto* item = new QListWidgetItem(ext, m_extList);
        item->setCheckState(m_cspManager->isExtensionEnabled(ext) ? Qt::Checked : Qt::Unchecked);
    }
}

QJsonObject CspConfigEditorModule::serializeProject() const
{
    QJsonObject data;
    data["acPath"] = m_acPathEdit ? m_acPathEdit->text() : QString();
    if (m_cspManager) {
        data["cspPath"] = m_cspManager->getCspPath();
    }
    return data;
}

void CspConfigEditorModule::deserializeProject(const QJsonObject& data)
{
    QString acPath = data["acPath"].toString();
    if (!acPath.isEmpty()) {
        m_acPathEdit->setText(acPath);
    }
}

} // namespace ks
