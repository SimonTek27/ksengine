#include "MainWindow.h"
#include "sys/LogManager.h"
#include "sys/ProjectSerializer.h"
#include "sdk/kseditor/engine/assets/ProjectBuilder.h"
#include "sdk/kseditor/engine/Graphics/VulkanIntegration.h"
#include "sdk/kseditor/engine/Config/ConfigIntegration.h"
#include "sdk/kseditor/engine/Config/EditorConfig.h"
#include "modules/editor/textEditor/TextEditorModule.h"
#include "modules/tools/AutoSave.h"
#include "modules/tools/ScientificCalculator.h"
#include "modules/modellingEditor/wizard/CarWizard.h"
#include "modules/modellingEditor/wizard/TrackWizard.h"
#include "modules/modellingEditor/wizard/CharacterWizard.h"
#include "modules/modellingEditor/3DModeling_Module.h"
#include "modules/PhysicsEditor/PhysicsEditor.h"
#include "modules/sound/editor/AudioEditorModule.h"
#include "modules/PaintEditor/PaintEditorModule.h"
#include "modules/PaintEditor/PaintEditorWidget.h"
#include "modules/modellingEditor/TrackBuilder/TrackBuilderModule.h"
#include "sdk/kseditor/engine/Audio/AudioStudioTypes.h"
#include "plugins/simulators/kunos/assettocorsa/acFiles/AudioBankParser.h"
#include "modules/fontEditor/FontCreatorQmlBridge.h"
#include "modules/cockpitInstruments/CockpitInstruments.h"
#include "modules/cockpitInstruments/CockpitInstrumentsQmlBridge.h"
#include "modules/modellingEditor/TrackBuilder/TrackSurfaceEditorModule.h"
#include "modules/modellingEditor/CharacterBuilder/DriverEditorModule.h"
#include "modules/ShowroomEditor/ShowroomEditorModule.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QLineEdit>
#include <QUndoView>
#include <QStandardPaths>
#include <QFontComboBox>
#include <QLibraryInfo>
#include <QProcess>
#include <QPushButton>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>

#include <QSvgRenderer>
#include <QPainter>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winreg.h>

#include <QWindow>
#include <QOperatingSystemVersion>

#endif

void MainWindow::applyWindowFrameTheme(const QString& themeKey) {
    const auto& theme = ks::editor::RibbonThemeManager::instance().theme(themeKey);
    
#ifdef Q_OS_WIN
    // Windows: Use DWM for colored title bar
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10) {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        
        // Set title bar color
        COLORREF color = RGB(theme.windowBorder.red(), 
                             theme.windowBorder.green(), 
                             theme.windowBorder.blue());
        
        // Dark mode title bar
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, 
                              &darkMode, sizeof(darkMode));
        
        // Set border color
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, 
                              &color, sizeof(color));
        
        // Set title bar background color
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, 
                              &color, sizeof(color));
    }
#endif
    
    // Qt-side styling
    setStyleSheet(QString(R"(
        QMainWindow {
            background: %1;
            border: 2px solid %2;
            border-radius: 0px;
        }
        
        QMainWindow::title {
            background: %3;
            color: %4;
            padding: 4px;
        }
    )")
    .arg(theme.centralBg.name())
    .arg(theme.windowBorder.name())
    .arg(theme.titleBarBg.name())
    .arg(theme.titleBarText.name()));
}

void MainWindow::setupRibbon() {
    m_ribbonBar = new ks::editor::RibbonBar(this);
    
    // Setup all 7 tabs
    setupCarTab();
    setupTrackTab();
    setupCharacterTab();
    setupShowroomTab();
    setupSoundTab();
    setupFontTab();
    setupPaintTab();
    
    // Add ribbon bar to central widget layout (below title bar)
    QWidget* central = centralWidget();
    if (central) {
        QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(central->layout());
        if (mainLayout) {
            mainLayout->insertWidget(1, m_ribbonBar);
        }
    }
    
    // Connect theme changes
    connect(m_ribbonBar, &ks::editor::RibbonBar::currentChanged,
            this, [this](int index) {
        // Map tab index to theme key
        static const QStringList themeKeys = {
            "car", "track", "character", "showroom", "sound", "font"
        };
        
        if (index >= 0 && index < themeKeys.size()) {
            QString themeKey = themeKeys[index];
            
            // Apply theme to ribbon
            m_ribbonBar->applyTheme(themeKey);
            
            // Apply theme to window frame
            ks::editor::RibbonThemeManager::instance().applyWindowFrame(this, themeKey);
            
            // Apply theme to custom title bar
            const auto& theme = ks::editor::RibbonThemeManager::instance().theme(themeKey);
            if (m_customTitleBar) {
                m_customTitleBar->applyTheme(theme.background, theme.borderColor, theme.titleBarText,
                                             theme.buttonHover, theme.buttonPressed, QColor("#E81123"));
            }
            
            // Update window title color
            updateWindowTitle();
        }
    });
    
    // Initial theme (CAR = index 0)
    m_ribbonBar->applyTheme("car");
    ks::editor::RibbonThemeManager::instance().applyWindowFrame(this, "car");
    
    // Apply theme to custom title bar
    const auto& initialTheme = ks::editor::RibbonThemeManager::instance().theme("car");
    if (m_customTitleBar) {
        m_customTitleBar->applyTheme(initialTheme.background, initialTheme.borderColor, initialTheme.titleBarText,
                                     initialTheme.buttonHover, initialTheme.buttonPressed, QColor("#E81123"));
    }
}

void MainWindow::setupCarTab() {
    auto* carTab = new ks::editor::RibbonTab("CAR", QIcon(":/icons/modeler.svg"), this);
    
    // === SUB-TAB: MODEL ===
    auto* modelSubTab = carTab->addSubTab("Model", QIcon(":/icons/modeler.svg"));
    auto* importPanel = modelSubTab->addPanel("Import/Export");
    auto* importGroup = importPanel->addGroup("Model");
    auto* importBtn = importGroup->addButton(QIcon(":/icons/import.svg"), "Import FBX");
    importBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    connect(importBtn, &QToolButton::clicked, this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(this, tr("Import 3D models"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("FBX Files (*.fbx);;All Files (*)"));
        if (!files.isEmpty()) {
            for (const QString &f : files) m_moduleManager->importFile(f);
        }
    });
    auto* exportBtn = importGroup->addButton(QIcon(":/icons/export.svg"), "Export KN5");
    connect(exportBtn, &QToolButton::clicked, this, [this]() {
        QString folder = QFileDialog::getExistingDirectory(this, tr("Select export folder"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        if (!folder.isEmpty()) m_moduleManager->exportFile(folder);
    });
    
    auto* meshPanel = modelSubTab->addPanel("Mesh");
    auto* meshGroup = meshPanel->addGroup("Operations");
    auto* weldBtn = meshGroup->addButton(QIcon(":/icons/mesh.svg"), "Weld Vertices");
    auto* splitBtn = meshGroup->addButton(QIcon(":/icons/modifier.svg"), "Split Mesh");
    auto* uvBtn = meshGroup->addButton(QIcon(":/icons/uv.svg"), "UV Unwrap");
    
    auto* lodPanel = modelSubTab->addPanel("LOD");
    auto* lodGroup = lodPanel->addGroup("Levels");
    auto* genLodBtn = lodGroup->addButton(QIcon(":/icons/lod.svg"), "Generate LODs");
    auto* lodSettingsBtn = lodGroup->addButton(QIcon(":/icons/settings.svg"), "LOD Settings");
    
    // === SUB-TAB: TEXTURE ===
    auto* textureSubTab = carTab->addSubTab("Texture", QIcon(":/icons/texture.svg"));
    auto* matPanel = textureSubTab->addPanel("Materials");
    auto* matGroup = matPanel->addGroup("Shaders");
    auto* bodyMatBtn = matGroup->addButton(QIcon(":/icons/material.svg"), "Body Paint");
    auto* glassMatBtn = matGroup->addButton(QIcon(":/icons/glass.svg"), "Glass");
    auto* chromeMatBtn = matGroup->addButton(QIcon(":/icons/chrome.svg"), "Chrome");
    auto* carbonMatBtn = matGroup->addButton(QIcon(":/icons/carbon.svg"), "Carbon Fiber");
    
    auto* texPanel = textureSubTab->addPanel("Textures");
    auto* texGroup = texPanel->addGroup("Maps");
    auto* diffBtn = texGroup->addButton(QIcon(":/icons/texture.svg"), "Diffuse");
    auto* normBtn = texGroup->addButton(QIcon(":/icons/normal.svg"), "Normal");
    auto* specBtn = texGroup->addButton(QIcon(":/icons/specular.svg"), "Specular");
    auto* emissBtn = texGroup->addButton(QIcon(":/icons/emissive.svg"), "Emissive");
    
    auto* paintPanel2 = textureSubTab->addPanel("Paint");
    auto* paintGroup2 = paintPanel2->addGroup("Skins");
    auto* newSkinBtn2 = paintGroup2->addButton(QIcon(":/icons/skin.svg"), "New Skin");
    newSkinBtn2->setStyle(ks::editor::RibbonButton::Style::Success);
    auto* skinEditorBtn2 = paintGroup2->addButton(QIcon(":/icons/edit.svg"), "Paint Editor");
    connect(newSkinBtn2, &QToolButton::clicked, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, tr("New Skin"), tr("Skin name:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.isEmpty()) {
            ks::PaintEditor::instance()->createSkin(name);
        }
    });
    connect(skinEditorBtn2, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* paintModule = qobject_cast<ks::PaintEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(paintModule->getModuleName()));
                return;
            }
        }
    });
    
    // === SUB-TAB: PHYSICS ===
    auto* physicsSubTab = carTab->addSubTab("Physics", QIcon(":/icons/physics.svg"));
    auto* suspPanel = physicsSubTab->addPanel("Suspension");
    auto* suspGroup = suspPanel->addGroup("Geometry");
    auto* suspGeoBtn = suspGroup->addButton(QIcon(":/icons/suspension.svg"), "Suspension Geometry");
    auto* wheelBtn = suspGroup->addButton(QIcon(":/icons/wheel.svg"), "Wheels");
    auto* springBtn = suspGroup->addButton(QIcon(":/icons/spring.svg"), "Springs/Dampers");
    connect(suspGeoBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* phys = qobject_cast<ks::PhysicsEditorModule*>(mod)) {
                phys->onShowSuspGeometry();
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(phys->moduleName()));
                return;
            }
        }
    });
    
    auto* aeroPanel = physicsSubTab->addPanel("Aerodynamics");
    auto* aeroGroup = aeroPanel->addGroup("Aero");
    auto* wingBtn = aeroGroup->addButton(QIcon(":/icons/aero.svg"), "Wings");
    auto* bodyAeroBtn = aeroGroup->addButton(QIcon(":/icons/bodyaero.svg"), "Body Aero");
    auto* dragBtn = aeroGroup->addButton(QIcon(":/icons/drag.svg"), "Drag/Downforce");
    connect(wingBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* phys = qobject_cast<ks::PhysicsEditorModule*>(mod)) {
                phys->onShowFfbPreview();
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(phys->moduleName()));
                return;
            }
        }
    });
    
    auto* enginePanel = physicsSubTab->addPanel("Powertrain");
    auto* engineGroup = enginePanel->addGroup("Engine");
    auto* curveBtn = engineGroup->addButton(QIcon(":/icons/engine.svg"), "Torque Curve");
    auto* gearboxBtn = engineGroup->addButton(QIcon(":/icons/gearbox.svg"), "Gearbox");
    auto* differentialBtn = engineGroup->addButton(QIcon(":/icons/diff.svg"), "Differential");
    connect(curveBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* phys = qobject_cast<ks::PhysicsEditorModule*>(mod)) {
                phys->onShowEngineCurve();
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(phys->moduleName()));
                return;
            }
        }
    });
    
    auto* brakePanel = physicsSubTab->addPanel("Brakes");
    auto* brakeGroup = brakePanel->addGroup("System");
    auto* brakeBiasBtn = brakeGroup->addButton(QIcon(":/icons/brake.svg"), "Brake Bias");
    auto* brakeTempBtn = brakeGroup->addButton(QIcon(":/icons/temperature.svg"), "Thermals");
    
    auto* tyrePanel = physicsSubTab->addPanel("Tyres");
    auto* tyreGroup = tyrePanel->addGroup("Model");
    auto* pacejkaBtn = tyreGroup->addButton(QIcon(":/icons/tire.svg"), "Pacejka Editor");
    auto* tyrePressBtn = tyreGroup->addButton(QIcon(":/icons/pressure.svg"), "Pressure");
    auto* tyreTempBtn = tyreGroup->addButton(QIcon(":/icons/temperature.svg"), "Temperature");
    
    // === SUB-TAB: DISPLAY ===
    auto* displaySubTab = carTab->addSubTab("Display", QIcon(":/icons/ui.svg"));
    auto* dashPanel = displaySubTab->addPanel("Dashboard");
    auto* dashGroup = dashPanel->addGroup("Instruments");
    auto* rpmBtn = dashGroup->addButton(QIcon(":/icons/rpm.svg"), "RPM Gauge");
    auto* speedBtn = dashGroup->addButton(QIcon(":/icons/speed.svg"), "Speedometer");
    auto* gearIndicatorBtn = dashGroup->addButton(QIcon(":/icons/gear.svg"), "Gear Indicator");
    auto* fuelBtn = dashGroup->addButton(QIcon(":/icons/fuel.svg"), "Fuel Gauge");
    
    auto* lcdPanel = displaySubTab->addPanel("LCD");
    auto* lcdGroup = lcdPanel->addGroup("Pages");
    auto* lapBtn = lcdGroup->addButton(QIcon(":/icons/lap.svg"), "Lap Times");
    auto* deltaBtn = lcdGroup->addButton(QIcon(":/icons/delta.svg"), "Delta");
    auto* tyreInfoBtn = lcdGroup->addButton(QIcon(":/icons/tyreinfo.svg"), "Tyre Info");
    auto* engineInfoBtn = lcdGroup->addButton(QIcon(":/icons/engineinfo.svg"), "Engine Data");
    
    auto* mirrorPanel = displaySubTab->addPanel("Mirrors");
    auto* mirrorGroup = mirrorPanel->addGroup("Setup");
    auto* rearBtn = mirrorGroup->addButton(QIcon(":/icons/mirror.svg"), "Rear View");
    auto* sideBtn = mirrorGroup->addButton(QIcon(":/icons/sidemirror.svg"), "Side Mirrors");
    auto* virtualBtn = mirrorGroup->addButton(QIcon(":/icons/virtualmirror.svg"), "Virtual Mirror");
    
    // === SUB-TAB: SOUND ===
    auto* soundSubTab = carTab->addSubTab("Sound", QIcon(":/icons/sound.svg"));
    auto* engSoundPanel = soundSubTab->addPanel("Engine");
    auto* engSoundGroup = engSoundPanel->addGroup("Audio");
    auto* intakBtn = engSoundGroup->addButton(QIcon(":/icons/intake.svg"), "Intake");
    auto* exhaustBtn = engSoundGroup->addButton(QIcon(":/icons/exhaust.svg"), "Exhaust");
    auto* backfireBtn = engSoundGroup->addButton(QIcon(":/icons/backfire.svg"), "Backfire");
    auto* limiterBtn = engSoundGroup->addButton(QIcon(":/icons/limiter.svg"), "Limiter");
    
    auto* transSoundPanel = soundSubTab->addPanel("Transmission");
    auto* transSoundGroup = transSoundPanel->addGroup("Sounds");
    auto* shiftSndBtn = transSoundGroup->addButton(QIcon(":/icons/shift.svg"), "Shift");
    auto* clutchSndBtn = transSoundGroup->addButton(QIcon(":/icons/clutch.svg"), "Clutch");
    auto* whineBtn = transSoundGroup->addButton(QIcon(":/icons/whine.svg"), "Gear Whine");
    
    auto* tyreSoundPanel = soundSubTab->addPanel("Tyres");
    auto* tyreSoundGroup = tyreSoundPanel->addGroup("Surfaces");
    auto* scrubBtn = tyreSoundGroup->addButton(QIcon(":/icons/tire.svg"), "Scrub");
    auto* gravelSndBtn = tyreSoundGroup->addButton(QIcon(":/icons/gravel.svg"), "Gravel");
    auto* curbSndBtn = tyreSoundGroup->addButton(QIcon(":/icons/curb.svg"), "Curbs");
    
    // Set default active sub-tab
    carTab->setCurrentSubTabIndex(0);
    
    m_ribbonBar->addTab(carTab);
}

void MainWindow::setupTrackTab() {
    auto* trackTab = new ks::editor::RibbonTab("TRACK", QIcon(":/icons/track.svg"), this);
    
    // === SUB-TAB: LAYOUT ===
    auto* layoutSubTab = trackTab->addSubTab("Layout", QIcon(":/icons/track.svg"));
    auto* createPanel = layoutSubTab->addPanel("Creation");
    auto* createGroup = createPanel->addGroup("Project");
    auto* newTrackBtn = createGroup->addButton(QIcon(":/icons/track.svg"), "New Track");
    newTrackBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    auto* importTrackBtn = createGroup->addButton(QIcon(":/icons/import.svg"), "Import");
    auto* exportTrackBtn = createGroup->addButton(QIcon(":/icons/export.svg"), "Export");
    
    connect(newTrackBtn, &QToolButton::clicked, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, tr("New Track"), tr("Track name:"), QLineEdit::Normal, "New Track", &ok);
        if (ok && !name.isEmpty()) {
            auto* trackModule = ks::track::TrackBuilderModule::instance();
            if (trackModule) trackModule->newProject(name);
        }
    });
    connect(importTrackBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Track Project"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Track Project (*.json);;All Files (*)"));
        if (!path.isEmpty()) {
            auto* trackModule = ks::track::TrackBuilderModule::instance();
            if (trackModule) trackModule->loadProject(path);
        }
    });
    connect(exportTrackBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Export Track Project"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Track Project (*.json);;All Files (*)"));
        if (!path.isEmpty()) {
            auto* trackModule = ks::track::TrackBuilderModule::instance();
            if (trackModule) trackModule->saveProject(path);
        }
    });
    
    auto* roadPanel = layoutSubTab->addPanel("Road");
    auto* roadGroup = roadPanel->addGroup("Mesh");
    auto* addRoadBtn = roadGroup->addButton(QIcon(":/icons/road.svg"), "Add Road");
    auto* editRoadBtn = roadGroup->addButton(QIcon(":/icons/edit.svg"), "Edit Spline");
    auto* roadPropsBtn = roadGroup->addButton(QIcon(":/icons/properties.svg"), "Properties");
    connect(addRoadBtn, &QToolButton::clicked, this, [this]() {
        auto* trackModule = ks::track::TrackBuilderModule::instance();
        if (trackModule) trackModule->addRoad();
    });
    
    auto* sectorPanel = layoutSubTab->addPanel("Sectors");
    auto* sectorGroup = sectorPanel->addGroup("Timing");
    auto* splitBtn = sectorGroup->addButton(QIcon(":/icons/split.svg"), "Split Points");
    auto* sectorBtn = sectorGroup->addButton(QIcon(":/icons/sector.svg"), "Sectors");
    auto* startFinishBtn = sectorGroup->addButton(QIcon(":/icons/startfinish.svg"), "Start/Finish");
    
    // === SUB-TAB: TERRAIN ===
    auto* terrainSubTab = trackTab->addSubTab("Terrain", QIcon(":/icons/terrain.svg"));
    auto* surfacePanel = terrainSubTab->addPanel("Surfaces");
    auto* surfaceGroup = surfacePanel->addGroup("Materials");
    auto* asphaltBtn = surfaceGroup->addButton(QIcon(":/icons/surface.svg"), "Asphalt");
    auto* grassBtn = surfaceGroup->addButton(QIcon(":/icons/grass.svg"), "Grass");
    auto* gravelBtn = surfaceGroup->addButton(QIcon(":/icons/gravel.svg"), "Gravel");
    auto* sandBtn = surfaceGroup->addButton(QIcon(":/icons/sand.svg"), "Sand");
    auto* curbBtn = surfaceGroup->addButton(QIcon(":/icons/curb.svg"), "Curbs");
    connect(asphaltBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (qobject_cast<ks::TrackSurfaceEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(mod->getModuleName()));
                return;
            }
        }
    });
    
    auto* terrainPanel = terrainSubTab->addPanel("Terrain");
    auto* terrainGroup = terrainPanel->addGroup("Tools");
    auto* sculptBtn = terrainGroup->addButton(QIcon(":/icons/sculpt.svg"), "Sculpt");
    auto* flattenBtn = terrainGroup->addButton(QIcon(":/icons/flatten.svg"), "Flatten");
    auto* smoothBtn = terrainGroup->addButton(QIcon(":/icons/smooth.svg"), "Smooth");
    auto* noiseBtn = terrainGroup->addButton(QIcon(":/icons/noise.svg"), "Noise");
    
    auto* objectPanel = terrainSubTab->addPanel("Objects");
    auto* objectGroup = objectPanel->addGroup("Placement");
    auto* treeBtn = objectGroup->addButton(QIcon(":/icons/tree.svg"), "Trees");
    auto* rockBtn = objectGroup->addButton(QIcon(":/icons/rock.svg"), "Rocks");
    auto* grassPlaceBtn = objectGroup->addButton(QIcon(":/icons/grass2.svg"), "Grass Patches");
    
    // === SUB-TAB: AI ===
    auto* aiSubTab = trackTab->addSubTab("AI", QIcon(":/icons/ai.svg"));
    auto* aiLinePanel = aiSubTab->addPanel("AI Lines");
    auto* aiLineGroup = aiLinePanel->addGroup("Paths");
    auto* genLineBtn = aiLineGroup->addButton(QIcon(":/icons/ai.svg"), "Generate Line");
    auto* editLineBtn = aiLineGroup->addButton(QIcon(":/icons/edit.svg"), "Edit Line");
    auto* hintBtn = aiLineGroup->addButton(QIcon(":/icons/hint.svg"), "Hints");
    connect(genLineBtn, &QToolButton::clicked, this, [this]() {
        auto* trackModule = ks::track::TrackBuilderModule::instance();
        if (trackModule) trackModule->autoGenerateAILine();
    });
    
    auto* pitPanel = aiSubTab->addPanel("Pits");
    auto* pitGroup = pitPanel->addGroup("Positions");
    auto* pitLaneBtn = pitGroup->addButton(QIcon(":/icons/pit.svg"), "Pit Lane");
    auto* pitEntryBtn = pitGroup->addButton(QIcon(":/icons/pitentry.svg"), "Entry");
    auto* pitExitBtn = pitGroup->addButton(QIcon(":/icons/pitexit.svg"), "Exit");
    auto* pitBoxBtn = pitGroup->addButton(QIcon(":/icons/pitbox.svg"), "Pit Boxes");
    connect(pitLaneBtn, &QToolButton::clicked, this, [this]() {
        auto* trackModule = ks::track::TrackBuilderModule::instance();
        if (trackModule) trackModule->addPitPosition(0, 0, 0, 0);
    });
    
    auto* camPanel = aiSubTab->addPanel("Cameras");
    auto* camGroup = camPanel->addGroup("Track Cameras");
    auto* addCamBtn = camGroup->addButton(QIcon(":/icons/camera.svg"), "Add Camera");
    auto* camTrackBtn = camGroup->addButton(QIcon(":/icons/camtrack.svg"), "Camera Track");
    
    // === SUB-TAB: LIGHTING ===
    auto* lightingSubTab = trackTab->addSubTab("Lighting", QIcon(":/icons/lighting.svg"));
    auto* lightPanel = lightingSubTab->addPanel("Lights");
    auto* lightGroup = lightPanel->addGroup("Setup");
    auto* sunBtn = lightGroup->addButton(QIcon(":/icons/sun.svg"), "Sun");
    auto* ambientBtn = lightGroup->addButton(QIcon(":/icons/ambient.svg"), "Ambient");
    auto* fogBtn = lightGroup->addButton(QIcon(":/icons/fog.svg"), "Fog");
    auto* shadowBtn = lightGroup->addButton(QIcon(":/icons/shadow.svg"), "Shadows");
    
    auto* timePanel = lightingSubTab->addPanel("Time");
    auto* timeGroup = timePanel->addGroup("Cycle");
    auto* timeBtn = timeGroup->addButton(QIcon(":/icons/time.svg"), "Time of Day");
    auto* weatherBtn = timeGroup->addButton(QIcon(":/icons/weather.svg"), "Weather");
    
    // === SUB-TAB: OBJECTS ===
    auto* objectsSubTab = trackTab->addSubTab("Objects", QIcon(":/icons/object.svg"));
    auto* staticPanel = objectsSubTab->addPanel("Static");
    auto* staticGroup = staticPanel->addGroup("Meshes");
    auto* buildingBtn = staticGroup->addButton(QIcon(":/icons/building.svg"), "Buildings");
    auto* fenceBtn = staticGroup->addButton(QIcon(":/icons/fence.svg"), "Fences");
    auto* barrierBtn = staticGroup->addButton(QIcon(":/icons/barrier.svg"), "Barriers");
    auto* bridgeBtn = staticGroup->addButton(QIcon(":/icons/bridge.svg"), "Bridges");
    
    auto* dynamicPanel = objectsSubTab->addPanel("Dynamic");
    auto* dynamicGroup = dynamicPanel->addGroup("Animated");
    auto* flagBtn = dynamicGroup->addButton(QIcon(":/icons/flag.svg"), "Flags");
    auto* bannerBtn = dynamicGroup->addButton(QIcon(":/icons/banner.svg"), "Banners");
    auto* crowdBtn = dynamicGroup->addButton(QIcon(":/icons/crowd.svg"), "Crowd");
    auto* smokeBtn = dynamicGroup->addButton(QIcon(":/icons/smoke.svg"), "Smoke/Fire");
    
    trackTab->setCurrentSubTabIndex(0);
    m_ribbonBar->addTab(trackTab);
}

void MainWindow::setupCharacterTab() {
    auto* charTab = new ks::editor::RibbonTab("CHARACTER", QIcon(":/icons/character.svg"), this);
    
    // === SUB-TAB: MODEL ===
    auto* modelSubTab = charTab->addSubTab("Model", QIcon(":/icons/character.svg"));
    auto* importPanel = modelSubTab->addPanel("Import/Export");
    auto* importGroup = importPanel->addGroup("Model");
    auto* importBtn = importGroup->addButton(QIcon(":/icons/import.svg"), "Import Model");
    importBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    connect(importBtn, &QToolButton::clicked, this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Character Model"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("3D Files (*.fbx *.obj *.glb *.kn5);;All Files (*)"));
        if (!files.isEmpty()) {
            for (const QString &f : files) m_moduleManager->importFile(f);
        }
    });
    auto* exportBtn = importGroup->addButton(QIcon(":/icons/export.svg"), "Export");
    
    auto* rigPanel = modelSubTab->addPanel("Rigging");
    auto* rigGroup = rigPanel->addGroup("Skeleton");
    auto* rigBtn = rigGroup->addButton(QIcon(":/icons/rig.svg"), "Auto Rig");
    auto* boneBtn = rigGroup->addButton(QIcon(":/icons/bone.svg"), "Edit Bones");
    auto* weightBtn = rigGroup->addButton(QIcon(":/icons/weight.svg"), "Weight Paint");
    auto* ikBtn = rigGroup->addButton(QIcon(":/icons/ik.svg"), "IK Setup");
    connect(rigBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (qobject_cast<ks::DriverEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(mod->getModuleName()));
                return;
            }
        }
    });
    
    auto* lodPanel = modelSubTab->addPanel("LOD");
    auto* lodGroup = lodPanel->addGroup("Levels");
    auto* genLodBtn = lodGroup->addButton(QIcon(":/icons/lod.svg"), "Generate LODs");
    auto* lodSettingsBtn = lodGroup->addButton(QIcon(":/icons/settings.svg"), "LOD Settings");
    
    // === SUB-TAB: ANIMATION ===
    auto* animSubTab = charTab->addSubTab("Animation", QIcon(":/icons/animation.svg"));
    auto* motionPanel = animSubTab->addPanel("Motions");
    auto* motionGroup = motionPanel->addGroup("Driving");
    auto* steeringBtn = motionGroup->addButton(QIcon(":/icons/steering.svg"), "Steering");
    auto* shiftingBtn = motionGroup->addButton(QIcon(":/icons/shift.svg"), "Shifting");
    auto* idleBtn = motionGroup->addButton(QIcon(":/icons/idle.svg"), "Idle");
    auto* lookBtn = motionGroup->addButton(QIcon(":/icons/look.svg"), "Head Look");
    connect(steeringBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Steering Animation"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Animation Files (*.fbx *.bvh *.ksanim);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(shiftingBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Shifting Animation"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Animation Files (*.fbx *.bvh *.ksanim);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(idleBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Idle Animation"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Animation Files (*.fbx *.bvh *.ksanim);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* extraPanel = animSubTab->addPanel("Extra");
    auto* extraGroup = extraPanel->addGroup("Animations");
    auto* enterBtn = extraGroup->addButton(QIcon(":/icons/enter.svg"), "Enter Car");
    auto* exitBtn = extraGroup->addButton(QIcon(":/icons/exit.svg"), "Exit Car");
    auto* crashBtn = extraGroup->addButton(QIcon(":/icons/crash.svg"), "Crash");
    auto* victoryBtn = extraGroup->addButton(QIcon(":/icons/victory.svg"), "Victory");
    
    auto* timelinePanel = animSubTab->addPanel("Timeline");
    auto* timelineGroup = timelinePanel->addGroup("Editor");
    auto* openTimelineBtn = timelineGroup->addButton(QIcon(":/icons/timeline.svg"), "Open Timeline");
    auto* blendBtn = timelineGroup->addButton(QIcon(":/icons/blend.svg"), "Blend Tree");
    
    // === SUB-TAB: TEXTURES ===
    auto* texSubTab = charTab->addSubTab("Textures", QIcon(":/icons/texture.svg"));
    auto* suitPanel = texSubTab->addPanel("Suit");
    auto* suitGroup = suitPanel->addGroup("Materials");
    auto* suitMatBtn = suitGroup->addButton(QIcon(":/icons/suit.svg"), "Suit Material");
    auto* suitTexBtn = suitGroup->addButton(QIcon(":/icons/texture.svg"), "Suit Texture");
    auto* logoBtn = suitGroup->addButton(QIcon(":/icons/logo.svg"), "Logos");
    connect(suitTexBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Suit Texture"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Image Files (*.png *.jpg *.dds);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* helmetPanel = texSubTab->addPanel("Helmet");
    auto* helmetGroup = helmetPanel->addGroup("Design");
    auto* helmetMatBtn = helmetGroup->addButton(QIcon(":/icons/helmet.svg"), "Helmet Material");
    auto* helmetTexBtn = helmetGroup->addButton(QIcon(":/icons/texture.svg"), "Helmet Texture");
    auto* visorBtn = helmetGroup->addButton(QIcon(":/icons/visor.svg"), "Visor");
    connect(helmetTexBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Helmet Texture"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Image Files (*.png *.jpg *.dds);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* glovesPanel = texSubTab->addPanel("Gloves");
    auto* glovesGroup = glovesPanel->addGroup("Materials");
    auto* glovesMatBtn = glovesGroup->addButton(QIcon(":/icons/gloves.svg"), "Gloves Material");
    auto* glovesTexBtn = glovesGroup->addButton(QIcon(":/icons/texture.svg"), "Gloves Texture");
    connect(glovesTexBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Gloves Texture"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Image Files (*.png *.jpg *.dds);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* bootsPanel = texSubTab->addPanel("Boots");
    auto* bootsGroup = bootsPanel->addGroup("Materials");
    auto* bootsMatBtn = bootsGroup->addButton(QIcon(":/icons/boots.svg"), "Boots Material");
    auto* bootsTexBtn = bootsGroup->addButton(QIcon(":/icons/texture.svg"), "Boots Texture");
    
    // === SUB-TAB: PHYSICS ===
    auto* physSubTab = charTab->addSubTab("Physics", QIcon(":/icons/physics.svg"));
    auto* ragdollPanel = physSubTab->addPanel("Ragdoll");
    auto* ragdollGroup = ragdollPanel->addGroup("Setup");
    auto* ragdollBtn = ragdollGroup->addButton(QIcon(":/icons/ragdoll.svg"), "Configure Ragdoll");
    auto* collisionBtn = ragdollGroup->addButton(QIcon(":/icons/collision.svg"), "Collision Capsules");
    auto* jointBtn = ragdollGroup->addButton(QIcon(":/icons/joint.svg"), "Joint Limits");
    
    auto* massPanel = physSubTab->addPanel("Mass");
    auto* massGroup = massPanel->addGroup("Distribution");
    auto* massBtn = massGroup->addButton(QIcon(":/icons/mass.svg"), "Mass Centers");
    auto* inertiaBtn = massGroup->addButton(QIcon(":/icons/inertia.svg"), "Inertia Tensor");
    
    charTab->setCurrentSubTabIndex(0);
    m_ribbonBar->addTab(charTab);
}

void MainWindow::setupShowroomTab() {
    auto* showroomTab = new ks::editor::RibbonTab("SHOWROOM", QIcon(":/icons/scene.svg"), this);
    
    // === SUB-TAB: SCENE ===
    auto* sceneSubTab = showroomTab->addSubTab("Scene", QIcon(":/icons/scene.svg"));
    auto* setupPanel = sceneSubTab->addPanel("Setup");
    auto* setupGroup = setupPanel->addGroup("Environment");
    auto* newSceneBtn = setupGroup->addButton(QIcon(":/icons/scene.svg"), "New Scene");
    newSceneBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    auto* bgBtn = setupGroup->addButton(QIcon(":/icons/background.svg"), "Background");
    auto* floorBtn = setupGroup->addButton(QIcon(":/icons/floor.svg"), "Floor");
    auto* skyBtn = setupGroup->addButton(QIcon(":/icons/sky.svg"), "Skybox");
    connect(newSceneBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(mod->getModuleName()));
                return;
            }
        }
    });
    connect(bgBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    connect(floorBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    
    auto* objectPanel = sceneSubTab->addPanel("Objects");
    auto* objectGroup = objectPanel->addGroup("Placement");
    auto* carBtn = objectGroup->addButton(QIcon(":/icons/car.svg"), "Car");
    auto* propBtn = objectGroup->addButton(QIcon(":/icons/prop.svg"), "Props");
    auto* decalBtn = objectGroup->addButton(QIcon(":/icons/decal.svg"), "Decals");
    
    // === SUB-TAB: LIGHTING ===
    auto* lightingSubTab = showroomTab->addSubTab("Lighting", QIcon(":/icons/lighting.svg"));
    auto* lightPanel = lightingSubTab->addPanel("Lights");
    auto* lightGroup = lightPanel->addGroup("Setup");
    auto* ambientBtn = lightGroup->addButton(QIcon(":/icons/ambient.svg"), "Ambient");
    auto* spotBtn = lightGroup->addButton(QIcon(":/icons/spotlight.svg"), "Spot");
    auto* hdriBtn = lightGroup->addButton(QIcon(":/icons/hdri.svg"), "HDRI");
    auto* sunBtn = lightGroup->addButton(QIcon(":/icons/sun.svg"), "Sun");
    auto* areaBtn = lightGroup->addButton(QIcon(":/icons/arealight.svg"), "Area");
    connect(ambientBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    connect(spotBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    connect(hdriBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    
    auto* shadowPanel = lightingSubTab->addPanel("Shadows");
    auto* shadowGroup = shadowPanel->addGroup("Quality");
    auto* cascadeBtn = shadowGroup->addButton(QIcon(":/icons/cascade.svg"), "Cascades");
    auto* contactBtn = shadowGroup->addButton(QIcon(":/icons/contact.svg"), "Contact");
    auto* filterBtn = shadowGroup->addButton(QIcon(":/icons/filter.svg"), "Filtering");
    
    auto* ppPanel = lightingSubTab->addPanel("Post Process");
    auto* ppGroup = ppPanel->addGroup("Effects");
    auto* bloomBtn = ppGroup->addButton(QIcon(":/icons/bloom.svg"), "Bloom");
    auto* tonemapBtn = ppGroup->addButton(QIcon(":/icons/tonemap.svg"), "Tone Mapping");
    auto* colorBtn = ppGroup->addButton(QIcon(":/icons/colorgrading.svg"), "Color Grading");
    auto* vignetteBtn = ppGroup->addButton(QIcon(":/icons/vignette.svg"), "Vignette");
    
    // === SUB-TAB: CAMERA ===
    auto* cameraSubTab = showroomTab->addSubTab("Camera", QIcon(":/icons/camera.svg"));
    auto* viewPanel = cameraSubTab->addPanel("Views");
    auto* viewGroup = viewPanel->addGroup("Presets");
    auto* orbitBtn = viewGroup->addButton(QIcon(":/icons/orbit.svg"), "Orbit");
    auto* turntableBtn = viewGroup->addButton(QIcon(":/icons/turntable.svg"), "Turntable");
    auto* freeBtn = viewGroup->addButton(QIcon(":/icons/freecam.svg"), "Free Camera");
    auto* dollyBtn = viewGroup->addButton(QIcon(":/icons/dolly.svg"), "Dolly");
    connect(orbitBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    connect(turntableBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    
    auto* pathPanel = cameraSubTab->addPanel("Paths");
    auto* pathGroup = pathPanel->addGroup("Animation");
    auto* keyframeBtn = pathGroup->addButton(QIcon(":/icons/keyframe.svg"), "Keyframes");
    auto* splineBtn = pathGroup->addButton(QIcon(":/icons/spline.svg"), "Spline");
    auto* speedBtn = pathGroup->addButton(QIcon(":/icons/speed.svg"), "Speed Curve");
    
    auto* dofPanel = cameraSubTab->addPanel("Depth of Field");
    auto* dofGroup = dofPanel->addGroup("Settings");
    auto* enableDofBtn = dofGroup->addButton(QIcon(":/icons/dof.svg"), "Enable DOF");
    auto* focusBtn = dofGroup->addButton(QIcon(":/icons/focus.svg"), "Focus Distance");
    auto* apertureBtn = dofGroup->addButton(QIcon(":/icons/aperture.svg"), "Aperture");
    
    // === SUB-TAB: RENDER ===
    auto* renderSubTab = showroomTab->addSubTab("Render", QIcon(":/icons/render.svg"));
    auto* outputPanel = renderSubTab->addPanel("Output");
    auto* outputGroup = outputPanel->addGroup("Settings");
    auto* resolutionBtn = outputGroup->addButton(QIcon(":/icons/resolution.svg"), "Resolution");
    auto* formatBtn = outputGroup->addButton(QIcon(":/icons/format.svg"), "Format");
    auto* aaBtn = outputGroup->addButton(QIcon(":/icons/aa.svg"), "Anti-Aliasing");
    auto* samplesBtn = outputGroup->addButton(QIcon(":/icons/samples.svg"), "Samples");
    
    auto* renderPanel = renderSubTab->addPanel("Render");
    auto* renderGroup = renderPanel->addGroup("Actions");
    auto* renderBtn = renderGroup->addButton(QIcon(":/icons/render.svg"), "Render Image");
    renderBtn->setStyle(ks::editor::RibbonButton::Style::Success);
    auto* batchBtn = renderGroup->addButton(QIcon(":/icons/batch.svg"), "Batch Render");
    auto* previewBtn = renderGroup->addButton(QIcon(":/icons/preview.svg"), "Preview");
    connect(renderBtn, &QToolButton::clicked, this, [this]() {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* showroom = qobject_cast<ks::ShowroomEditorModule*>(mod)) {
                showroom->onGeneratePreview();
                m_moduleManager->setCurrentModule(m_moduleManager->moduleIndex(showroom->moduleName()));
                return;
            }
        }
    });
    
    auto* composePanel = renderSubTab->addPanel("Compositing");
    auto* composeGroup = composePanel->addGroup("Layers");
    auto* layersBtn = composeGroup->addButton(QIcon(":/icons/layers.svg"), "Layers");
    auto* maskBtn = composeGroup->addButton(QIcon(":/icons/mask.svg"), "Masks");
    auto* exportBtn = composeGroup->addButton(QIcon(":/icons/export.svg"), "Export EXR");
    
    showroomTab->setCurrentSubTabIndex(0);
    m_ribbonBar->addTab(showroomTab);
}

void MainWindow::setupSoundTab() {
    auto* soundTab = new ks::editor::RibbonTab("SOUND", QIcon(":/icons/sound.svg"), this);
    
    // === SUB-TAB: ENGINE ===
    auto* engineSubTab = soundTab->addSubTab("Engine", QIcon(":/icons/engine.svg"));
    auto* enginePanel = engineSubTab->addPanel("Engine");
    auto* engineGroup = enginePanel->addGroup("Samples");
    auto* importEngineBtn = engineGroup->addButton(QIcon(":/icons/engine.svg"), "Engine Sound");
    importEngineBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    auto* exhaustBtn = engineGroup->addButton(QIcon(":/icons/exhaust.svg"), "Exhaust");
    auto* intakeBtn = engineGroup->addButton(QIcon(":/icons/intake.svg"), "Intake");
    auto* turboBtn = engineGroup->addButton(QIcon(":/icons/turbo.svg"), "Turbo");
    auto* backfireBtn = engineGroup->addButton(QIcon(":/icons/fire.svg"), "Backfire");
    auto* limiterBtn = engineGroup->addButton(QIcon(":/icons/limiter.svg"), "Limiter");
    connect(importEngineBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Engine Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(exhaustBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Exhaust Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(intakeBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Intake Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* engineTunePanel = engineSubTab->addPanel("Tuning");
    auto* engineTuneGroup = engineTunePanel->addGroup("Parameters");
    auto* rpmRangeBtn = engineTuneGroup->addButton(QIcon(":/icons/rpm.svg"), "RPM Range");
    auto* loadBtn = engineTuneGroup->addButton(QIcon(":/icons/load.svg"), "Load Curves");
    auto* volumeBtn = engineTuneGroup->addButton(QIcon(":/icons/volume.svg"), "Volume Curves");
    auto* pitchBtn = engineTuneGroup->addButton(QIcon(":/icons/pitch.svg"), "Pitch Curves");
    
    // === SUB-TAB: TRANSMISSION ===
    auto* transSubTab = soundTab->addSubTab("Transmission", QIcon(":/icons/gearbox.svg"));
    auto* gearPanel = transSubTab->addPanel("Gearbox");
    auto* gearGroup = gearPanel->addGroup("Sounds");
    auto* shiftBtn = gearGroup->addButton(QIcon(":/icons/shift.svg"), "Shift");
    auto* clutchBtn = gearGroup->addButton(QIcon(":/icons/clutch.svg"), "Clutch");
    auto* whineBtn = gearGroup->addButton(QIcon(":/icons/whine.svg"), "Gear Whine");
    auto* neutralBtn = gearGroup->addButton(QIcon(":/icons/neutral.svg"), "Neutral");
    connect(shiftBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Shift Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(clutchBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Clutch Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(backfireBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Backfire Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* diffPanel = transSubTab->addPanel("Differential");
    auto* diffGroup = diffPanel->addGroup("Sounds");
    auto* lsdBtn = diffGroup->addButton(QIcon(":/icons/diff.svg"), "LSD Whine");
    auto* lockBtn = diffGroup->addButton(QIcon(":/icons/lock.svg"), "Lock");
    
    // === SUB-TAB: SURFACES ===
    auto* surfSubTab = soundTab->addSubTab("Surfaces", QIcon(":/icons/tire.svg"));
    auto* tyrePanel = surfSubTab->addPanel("Tyres");
    auto* tyreGroup = tyrePanel->addGroup("Road");
    auto* tireBtn = tyreGroup->addButton(QIcon(":/icons/tire.svg"), "Tire Scrub");
    auto* gravelBtn = tyreGroup->addButton(QIcon(":/icons/gravel.svg"), "Gravel");
    auto* curbBtn = tyreGroup->addButton(QIcon(":/icons/curb.svg"), "Curbs");
    auto* wetBtn = tyreGroup->addButton(QIcon(":/icons/wet.svg"), "Wet");
    connect(tireBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Tire Scrub Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(gravelBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Gravel Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    connect(curbBtn, &QToolButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, tr("Import Curb Sound"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Audio Files (*.wav *.ogg *.mp3 *.flac);;All Files (*)"));
        if (!path.isEmpty()) m_moduleManager->importFile(path);
    });
    
    auto* skidPanel = surfSubTab->addPanel("Skidmarks");
    auto* skidGroup = skidPanel->addGroup("Audio");
    auto* skidBtn = skidGroup->addButton(QIcon(":/icons/skid.svg"), "Skid Sound");
    auto* smokeBtn = skidGroup->addButton(QIcon(":/icons/smoke.svg"), "Smoke Puff");
    
    // === SUB-TAB: STUDIO ===
    auto* studioSubTab = soundTab->addSubTab("Studio", QIcon(":/icons/studio.svg"));
    auto* studioPanel = studioSubTab->addPanel("ksAudioStudio");
    auto* studioGroup = studioPanel->addGroup("Tools");
    auto* bankBtn = studioGroup->addButton(QIcon(":/icons/bank.svg"), "Build Audio Bank");
    bankBtn->setStyle(ks::editor::RibbonButton::Style::Warning);
    auto* eventsBtn = studioGroup->addButton(QIcon(":/icons/events.svg"), "Events");
    auto* paramsBtn = studioGroup->addButton(QIcon(":/icons/params.svg"), "Parameters");
    auto* recordStudioBtn = studioGroup->addButton(QIcon(":/icons/record.svg"), "Recording Studio");
    recordStudioBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    connect(bankBtn, &QToolButton::clicked, this, [this]() {
        auto* audio = ks::AudioEditorModule::instance();
        if (audio) audio->onBuildBanks();
    });
    connect(eventsBtn, &QToolButton::clicked, this, [this]() {
        auto* audio = ks::AudioEditorModule::instance();
        if (audio) audio->onImportAsset();
    });
    connect(recordStudioBtn, &QToolButton::clicked, this, [this]() {
        auto* audio = ks::AudioEditorModule::instance();
        if (audio) audio->onNewProject();
    });
    
    auto* mixerPanel = studioSubTab->addPanel("Mixer");
    auto* mixerGroup = mixerPanel->addGroup("Channels");
    auto* mixerBtn = mixerGroup->addButton(QIcon(":/icons/mixer.svg"), "Open Mixer");
    auto* routeBtn = mixerGroup->addButton(QIcon(":/icons/route.svg"), "Routing");
    auto* fxBtn = mixerGroup->addButton(QIcon(":/icons/fx.svg"), "Effects");
    
    soundTab->setCurrentSubTabIndex(0);
    m_ribbonBar->addTab(soundTab);
}

void MainWindow::setupFontTab() {
    auto* fontTab = new ks::editor::RibbonTab("FONT", QIcon(":/icons/font.svg"), this);
    
    // === SUB-TAB: TYPEFACE ===
    auto* typefaceSubTab = fontTab->addSubTab("Typeface", QIcon(":/icons/font.svg"));
    auto* typePanel = typefaceSubTab->addPanel("Typeface");
    auto* typeGroup = typePanel->addGroup("Font");
    auto* newFontBtn = typeGroup->addButton(QIcon(":/icons/font.svg"), "New Font");
    newFontBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    auto* importFontBtn = typeGroup->addButton(QIcon(":/icons/import.svg"), "Import");
    auto* exportFontBtn = typeGroup->addButton(QIcon(":/icons/export.svg"), "Export");
    auto* familyBtn = typeGroup->addButton(QIcon(":/icons/family.svg"), "Family");
    connect(newFontBtn, &QToolButton::clicked, this, [this]() {
        auto* fontBridge = ks::FontCreatorQmlBridge::instance();
        if (fontBridge) {
            QStringList fonts = fontBridge->getSystemFonts();
            if (!fonts.isEmpty()) fontBridge->setFontFamily(fonts.first());
        }
    });
    connect(importFontBtn, &QToolButton::clicked, this, [this]() {
        auto* fontBridge = ks::FontCreatorQmlBridge::instance();
        if (!fontBridge) return;
        QString path = QFileDialog::getOpenFileName(this, tr("Import Font Config"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Font Config (*.json *.acf);;All Files (*)"));
        if (!path.isEmpty()) {
            if (path.endsWith(".json"))
                fontBridge->importFromJSON(path);
            else
                fontBridge->loadPreset(path);
        }
    });
    connect(exportFontBtn, &QToolButton::clicked, this, [this]() {
        auto* fontBridge = ks::FontCreatorQmlBridge::instance();
        if (!fontBridge) return;
        QString path = QFileDialog::getSaveFileName(this, tr("Export Font"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("Font Atlas (*.png);;AC Font (*.acf);;All Files (*)"));
        if (!path.isEmpty()) {
            if (path.endsWith(".acf"))
                fontBridge->savePreset(path);
            else
                fontBridge->generateAtlas(path);
        }
    });
    
    auto* stylePanel = typefaceSubTab->addPanel("Styles");
    auto* styleGroup = stylePanel->addGroup("Variants");
    auto* boldBtn = styleGroup->addButton(QIcon(":/icons/bold.svg"), "Bold");
    auto* italicBtn = styleGroup->addButton(QIcon(":/icons/italic.svg"), "Italic");
    auto* weightBtn = styleGroup->addButton(QIcon(":/icons/weight.svg"), "Weight");
    auto* widthBtn = styleGroup->addButton(QIcon(":/icons/width.svg"), "Width");
    
    // === SUB-TAB: GLYPHS ===
    auto* glyphSubTab = fontTab->addSubTab("Glyphs", QIcon(":/icons/glyph.svg"));
    auto* glyphPanel = glyphSubTab->addPanel("Glyphs");
    auto* glyphGroup = glyphPanel->addGroup("Editor");
    auto* editGlyphBtn = glyphGroup->addButton(QIcon(":/icons/glyph.svg"), "Edit Glyph");
    auto* metricsBtn = glyphGroup->addButton(QIcon(":/icons/metrics.svg"), "Metrics");
    auto* kerningBtn = glyphGroup->addButton(QIcon(":/icons/kerning.svg"), "Kerning");
    auto* ligatureBtn = glyphGroup->addButton(QIcon(":/icons/ligature.svg"), "Ligatures");
    connect(editGlyphBtn, &QToolButton::clicked, this, [this]() {
        // Toggle glyph editor mode - the QML UI handles this
    });
    connect(metricsBtn, &QToolButton::clicked, this, [this]() {
        auto* fontBridge = ks::FontCreatorQmlBridge::instance();
        if (fontBridge) {
            QVariantList glyphs = fontBridge->getGlyphs();
            setStatusMessage(tr("Loaded %1 glyphs with metrics").arg(glyphs.size()));
        }
    });
    connect(kerningBtn, &QToolButton::clicked, this, [this]() {
        auto* fontBridge = ks::FontCreatorQmlBridge::instance();
        if (fontBridge) {
            fontBridge->extractKerning();
            setStatusMessage(tr("Extracted kerning pairs from current font"));
        }
    });
    
    auto* setPanel = glyphSubTab->addPanel("Character Sets");
    auto* setGroup = setPanel->addGroup("Unicode");
    auto* latinBtn = setGroup->addButton(QIcon(":/icons/latin.svg"), "Basic Latin");
    auto* extBtn = setGroup->addButton(QIcon(":/icons/extended.svg"), "Extended");
    auto* cyrillicBtn = setGroup->addButton(QIcon(":/icons/cyrillic.svg"), "Cyrillic");
    auto* customBtn = setGroup->addButton(QIcon(":/icons/custom.svg"), "Custom Range");
    
    // === SUB-TAB: PREVIEW ===
    auto* previewSubTab = fontTab->addSubTab("Preview", QIcon(":/icons/preview.svg"));
    auto* previewPanel = previewSubTab->addPanel("Preview");
    auto* previewGroup = previewPanel->addGroup("View");
    auto* previewBtn = previewGroup->addButton(QIcon(":/icons/preview.svg"), "Preview");
    previewBtn->setStyle(ks::editor::RibbonButton::Style::Success);
    auto* textBtn = previewGroup->addButton(QIcon(":/icons/text.svg"), "Sample Text");
    auto* sizeBtn = previewGroup->addButton(QIcon(":/icons/size.svg"), "Sizes");
    auto* colorBtn = previewGroup->addButton(QIcon(":/icons/color.svg"), "Colors");
    connect(previewBtn, &QToolButton::clicked, this, [this]() {
        auto* fontBridge = ks::FontCreatorQmlBridge::instance();
        if (fontBridge) {
            QString preview = fontBridge->getPreviewText();
            setStatusMessage(tr("Font preview: %1").arg(preview.isEmpty() ? "No preview text set" : preview.left(50)));
        }
    });
    
    auto* exportPanel = previewSubTab->addPanel("Export");
    auto* exportGroup = exportPanel->addGroup("Output");
    auto* atlasBtn = exportGroup->addButton(QIcon(":/icons/atlas.svg"), "Texture Atlas");
    auto* sdfBtn = exportGroup->addButton(QIcon(":/icons/sdf.svg"), "SDF");
    auto* msdfBtn = exportGroup->addButton(QIcon(":/icons/msdf.svg"), "MSDF");
    auto* jsonBtn = exportGroup->addButton(QIcon(":/icons/json.svg"), "JSON Metadata");
    
    fontTab->setCurrentSubTabIndex(0);
    m_ribbonBar->addTab(fontTab);
}

void MainWindow::setupPaintTab() {
    auto* paintTab = new ks::editor::RibbonTab("PAINT", QIcon(":/icons/livery.svg"), this);

    // === SUB-TAB: BRUSH ===
    auto* brushSubTab = paintTab->addSubTab("Brush", QIcon(":/icons/livery.svg"));
    auto* brushPanel = brushSubTab->addPanel("Tools");
    auto* brushGroup = brushPanel->addGroup("Brush");
    m_paintBrushBtn = brushGroup->addButton(QIcon(":/icons/livery.svg"), "Brush");
    m_paintBrushBtn->setShortcut(QKeySequence("B"));
    m_paintBrushBtn->setStyle(ks::editor::RibbonButton::Style::Primary);
    m_paintPencilBtn = brushGroup->addButton(QIcon(":/icons/livery.svg"), "Pencil");
    m_paintPencilBtn->setShortcut(QKeySequence("Shift+B"));
    m_paintEraserBtn = brushGroup->addButton(QIcon(":/icons/livery.svg"), "Eraser");
    m_paintEraserBtn->setShortcut(QKeySequence("E"));
    m_paintAirbrushBtn = brushGroup->addButton(QIcon(":/icons/livery.svg"), "Airbrush");

    auto* fillPanel = brushSubTab->addPanel("Fill");
    auto* fillGroup = fillPanel->addGroup("Paint");
    m_paintFillBtn = fillGroup->addButton(QIcon(":/icons/livery.svg"), "Fill");
    m_paintFillBtn->setShortcut(QKeySequence("Shift+G"));
    m_paintGradientBtn = fillGroup->addButton(QIcon(":/icons/livery.svg"), "Gradient");
    m_paintGradientBtn->setShortcut(QKeySequence("G"));
    m_paintCloneBtn = fillGroup->addButton(QIcon(":/icons/livery.svg"), "Clone Stamp");
    m_paintCloneBtn->setShortcut(QKeySequence("S"));

    auto* colorPanel = brushSubTab->addPanel("Colors");
    auto* colorGroup = colorPanel->addGroup("Swatch");
    m_paintFgColorBtn = colorGroup->addButton(QIcon(":/icons/livery.svg"), "FG Color");
    m_paintBgColorBtn = colorGroup->addButton(QIcon(":/icons/livery.svg"), "BG Color");
    m_paintSwapColorsBtn = colorGroup->addButton(QIcon(":/icons/livery.svg"), "Swap");
    m_paintSwapColorsBtn->setShortcut(QKeySequence("X"));
    m_paintDefaultColorsBtn = colorGroup->addButton(QIcon(":/icons/livery.svg"), "Default");
    m_paintDefaultColorsBtn->setShortcut(QKeySequence("D"));

    // === SUB-TAB: SELECT ===
    auto* selectSubTab = paintTab->addSubTab("Select", QIcon(":/icons/livery.svg"));
    auto* marqueePanel = selectSubTab->addPanel("Marquee");
    auto* marqueeGroup = marqueePanel->addGroup("Shape");
    m_paintRectSelectBtn = marqueeGroup->addButton(QIcon(":/icons/livery.svg"), "Rect Sel");
    m_paintRectSelectBtn->setShortcut(QKeySequence("M"));
    m_paintEllipseSelectBtn = marqueeGroup->addButton(QIcon(":/icons/livery.svg"), "Ellipse Sel");
    m_paintEllipseSelectBtn->setShortcut(QKeySequence("Shift+M"));
    m_paintLassoBtn = marqueeGroup->addButton(QIcon(":/icons/livery.svg"), "Lasso");
    m_paintLassoBtn->setShortcut(QKeySequence("L"));
    m_paintMagicWandBtn = marqueeGroup->addButton(QIcon(":/icons/livery.svg"), "Magic Wand");
    m_paintMagicWandBtn->setShortcut(QKeySequence("W"));

    auto* modPanel = selectSubTab->addPanel("Modify");
    auto* modGroup = modPanel->addGroup("Actions");
    m_paintDeselectBtn = modGroup->addButton(QIcon(":/icons/livery.svg"), "Deselect");
    m_paintDeselectBtn->setShortcut(QKeySequence("Ctrl+D"));
    m_paintInvertBtn = modGroup->addButton(QIcon(":/icons/livery.svg"), "Invert");
    m_paintInvertBtn->setShortcut(QKeySequence("Ctrl+I"));

    // === SUB-TAB: VIEW ===
    auto* viewSubTab = paintTab->addSubTab("View", QIcon(":/icons/livery.svg"));
    auto* zoomPanel = viewSubTab->addPanel("Zoom");
    auto* zoomGroup = zoomPanel->addGroup("Navigate");
    m_paintZoomInBtn = zoomGroup->addButton(QIcon(":/icons/zoom-in.svg"), "Zoom In");
    m_paintZoomInBtn->setShortcut(QKeySequence("Ctrl++"));
    m_paintZoomOutBtn = zoomGroup->addButton(QIcon(":/icons/zoom-out.svg"), "Zoom Out");
    m_paintZoomOutBtn->setShortcut(QKeySequence("Ctrl+-"));
    m_paintFitBtn = zoomGroup->addButton(QIcon(":/icons/livery.svg"), "Fit");
    m_paintFitBtn->setShortcut(QKeySequence("Ctrl+0"));
    m_paintZoomToolBtn = zoomGroup->addButton(QIcon(":/icons/livery.svg"), "Zoom Tool");
    m_paintZoomToolBtn->setShortcut(QKeySequence("Z"));

    auto* viewPanel = viewSubTab->addPanel("Display");
    auto* viewGroup = viewPanel->addGroup("Toggles");
    m_paintFullscreenBtn = viewGroup->addButton(QIcon(":/icons/livery.svg"), "Fullscreen");
    m_paintFullscreenBtn->setShortcut(QKeySequence("F"));
    m_paintRulersBtn = viewGroup->addButton(QIcon(":/icons/livery.svg"), "Rulers");
    m_paintRulersBtn->setShortcut(QKeySequence("Ctrl+Shift+R"));

    // === SUB-TAB: LAYER ===
    auto* layerSubTab = paintTab->addSubTab("Layer", QIcon(":/icons/layers.svg"));
    auto* layerPanel = layerSubTab->addPanel("Ops");
    auto* layerGroup = layerPanel->addGroup("Actions");
    m_paintNewLayerBtn = layerGroup->addButton(QIcon(":/icons/layers.svg"), "New Layer");
    m_paintNewLayerBtn->setShortcut(QKeySequence("Ctrl+Shift+N"));
    m_paintDupLayerBtn = layerGroup->addButton(QIcon(":/icons/layers.svg"), "Duplicate");
    m_paintDupLayerBtn->setShortcut(QKeySequence("Ctrl+J"));
    m_paintMergeLayerBtn = layerGroup->addButton(QIcon(":/icons/layers.svg"), "Merge Down");
    m_paintMergeLayerBtn->setShortcut(QKeySequence("Ctrl+E"));
    m_paintFlattenBtn = layerGroup->addButton(QIcon(":/icons/layers.svg"), "Flatten");

    paintTab->setCurrentSubTabIndex(0);
    m_ribbonBar->addTab(paintTab);
}

// ==================== Constructor / Destructor ====================

MainWindow::MainWindow(const QString& projectPath, QWidget* parent)
    : QMainWindow(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_moduleManager(new ModuleManager(this))
    , m_settings(new SettingsManager(this))
    , m_undoStack(new QUndoStack(this))
    , m_currentProjectPath(projectPath)
    , m_projectBuilder(new ProjectBuilder(this))
{
    LOG_INFO("MainWindow", "Initializing main window");

    // Initialize help system
    if (ks::HelpSystem::instance()) {
        ks::HelpSystem::instance()->initialize(this);
        ks::HelpSystem::instance()->enableHelp(true);
        // ks::HelpSystem::instance()->startQuickStartGuide();  // Disabled - may cause crash on startup
    }

    // Set window properties
    setWindowTitle(Constants::APP_NAME);
    resize(Constants::DEFAULT_WINDOW_SIZE);
    setMinimumSize(Constants::MIN_WINDOW_SIZE);
    setAcceptDrops(true);

    // Setup UI
    setupUI();
    setupCustomTitleBar();
    setupRibbon();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupDockWidgets();
    setupConnections();
    createActions();
    updateActions();

    // Detect environment
    detectSimulator();
    detectCSPVersion();
    updateRecentProjectsMenu();
    updateWindowTitle();

    // Setup auto-recovery timer
    m_sessionRecoveryTimer = new QTimer(this);
    connect(m_sessionRecoveryTimer, &QTimer::timeout, this, &MainWindow::saveSessionBackup);
    m_sessionRecoveryTimer->start(30000); // Save every 30 seconds

    // Setup crash recovery
    m_crashRecovery = new ks::CrashRecovery(this);
    connect(m_crashRecovery, &ks::CrashRecovery::recoveryNeeded,
            this, &MainWindow::showRecoveryDialog);
    m_crashRecovery->startSession();
    if (!m_currentProjectPath.isEmpty()) {
        m_crashRecovery->addOpenDocument(m_currentProjectPath);
        m_crashRecovery->setActiveDocument(m_currentProjectPath);
    }

    // Offer to recover any session left behind by a previous crash once the
    // main window is visible (recoveryNeeded -> showRecoveryDialog).
    QTimer::singleShot(0, this, [this]() {
        if (m_crashRecovery)
            m_crashRecovery->recover();
    });

    // Setup template system
    m_templateManager = ks::TemplateManager::instance();
    m_templateManager->initialize();

    // Setup file comparison system
    m_diffEngine = ks::FileComparisonEngine::instance();
    m_diffEngine->initialize();

    // Restore window geometry
    QSettings settings(Constants::APP_NAME, Constants::APP_NAME);
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());

    LOG_INFO("MainWindow", "Main window initialized");
}

MainWindow::~MainWindow()
{
    LOG_INFO("MainWindow", "Shutting down main window");

    // End crash recovery session
    if (m_crashRecovery) {
        m_crashRecovery->endSession();
    }

    // Cancel any running build
    if (m_projectBuilder) {
        m_projectBuilder->cancel();
    }

    // Save window state
    QSettings settings(Constants::APP_NAME, Constants::APP_NAME);
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
}

// ==================== Setup Methods ====================

void MainWindow::setupCustomTitleBar()
{
    // Create custom title bar with KDE-style menu button
    m_customTitleBar = new CustomTitleBar(this);
    connect(m_customTitleBar, &CustomTitleBar::menuRequested, this, [this]() {
        if (m_helpBrowser) {
            m_helpBrowser->show();
            m_helpBrowser->raise();
            m_helpBrowser->activateWindow();
        } else {
            ks::HelpSystem::instance()->showHelp();
        }
    });
    connect(m_customTitleBar, &CustomTitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_customTitleBar, &CustomTitleBar::maximizeRequested, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    connect(m_customTitleBar, &CustomTitleBar::closeRequested, this, &QWidget::close);
    
    m_customTitleBar->setTitle(windowTitle());
    m_customTitleBar->setWindowIcon(windowIcon());
    
    // Add to the central widget layout at position 0 (top)
    QWidget* central = centralWidget();
    if (central) {
        QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(central->layout());
        if (mainLayout) {
            mainLayout->insertWidget(0, m_customTitleBar);
        }
    }
}

void MainWindow::setupUI()
{
    // Create a central widget with vertical layout: title bar -> ribbon -> module manager
    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Title bar will be added by setupCustomTitleBar
    // Ribbon bar will be added by setupRibbon
    // Module manager goes at the bottom
    mainLayout->addWidget(m_moduleManager, 1);
    
    setCentralWidget(central);
}

void MainWindow::setupMenuBar()
{
    m_menuBar = menuBar();

    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupModulesMenu();
    setupToolsMenu();
    setupSettingsMenu();
    setupHelpMenu();
}

void MainWindow::setupFileMenu()
{
    QMenu* fileMenu = m_menuBar->addMenu(tr("&File"));

    QAction* newAct = new QAction(tr("&New Project..."), this);
    newAct->setShortcut(QKeySequence::New);
    newAct->setStatusTip(tr("Create a new ksEditor project"));
    connect(newAct, &QAction::triggered, this, &MainWindow::newProject);
    fileMenu->addAction(newAct);

    QAction* openAct = new QAction(tr("&Open Project..."), this);
    openAct->setShortcut(QKeySequence::Open);
    openAct->setStatusTip(tr("Open an existing project"));
    connect(openAct, &QAction::triggered, this, &MainWindow::openProject);
    QAction* import3DAct = new QAction(tr("&Import 3D..."), this);
    import3DAct->setStatusTip(tr("Import 3D model(s) into the current module"));
    connect(import3DAct, &QAction::triggered, this, [this]() {
        QStringList files = QFileDialog::getOpenFileNames(this, tr("Import 3D files"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            tr("3D Files (*.fbx *.obj *.glb *.gltf *.dae *.kn5);;All Files (*)"));
        if (!files.isEmpty()) {
            for (const QString &f : files) m_moduleManager->importFile(f);
        }
    });
    fileMenu->addAction(import3DAct);

    QAction* export3DAct = new QAction(tr("&Export 3D..."), this);
    export3DAct->setStatusTip(tr("Export selected object(s) or project to supported 3D formats"));
    connect(export3DAct, &QAction::triggered, this, [this]() {
        QString folder = QFileDialog::getExistingDirectory(this, tr("Select export folder"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        if (!folder.isEmpty()) m_moduleManager->exportFile(folder);
    });
    fileMenu->addAction(export3DAct);

    fileMenu->addSeparator();
    QMenu* recentMenu = fileMenu->addMenu(tr("Recent Projects"));
    for (int i = 0; i < Constants::MAX_RECENT_PROJECTS; ++i) {
        QAction* recentAct = new QAction(tr("Empty"), this);
        recentAct->setVisible(false);
        connect(recentAct, &QAction::triggered, this, [this, recentAct]() {
            openRecentProject(recentAct->data().toString());
        });
        m_recentProjectsActions.append(recentAct);
        recentMenu->addAction(recentAct);
    }
    fileMenu->addMenu(recentMenu);
    fileMenu->addSeparator();

    m_saveAction = new QAction(tr("&Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(tr("Save the current project"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    fileMenu->addAction(m_saveAction);

    m_saveAsAction = new QAction(tr("Save &As..."), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setStatusTip(tr("Save the project with a new name"));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();

    m_buildAction = new QAction(tr("&Build"), this);
    m_buildAction->setShortcut(QKeySequence(tr("Ctrl+B")));
    m_buildAction->setStatusTip(tr("Build the current project"));
    connect(m_buildAction, &QAction::triggered, this, &MainWindow::buildProject);
    fileMenu->addAction(m_buildAction);
    fileMenu->addSeparator();

    QAction* closeAct = new QAction(tr("&Close Project"), this);
    closeAct->setShortcut(QKeySequence(tr("Ctrl+W")));
    closeAct->setStatusTip(tr("Close the current project"));
    connect(closeAct, &QAction::triggered, this, &MainWindow::closeProject);
    fileMenu->addAction(closeAct);
    fileMenu->addSeparator();

    QAction* exitAct = new QAction(tr("E&xit"), this);
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAct);
}

void MainWindow::setupEditMenu()
{
    QMenu* editMenu = m_menuBar->addMenu(tr("&Edit"));

    m_undoAction = new QAction(tr("&Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);
    editMenu->addAction(m_undoAction);

    m_redoAction = new QAction(tr("&Redo"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addAction(m_redoAction);

    editMenu->addSeparator();

    m_cutAction = new QAction(tr("Cu&t"), this);
    m_cutAction->setShortcut(QKeySequence::Cut);
    connect(m_cutAction, &QAction::triggered, this, &MainWindow::cut);
    editMenu->addAction(m_cutAction);

    m_copyAction = new QAction(tr("&Copy"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copy);
    editMenu->addAction(m_copyAction);

    m_pasteAction = new QAction(tr("&Paste"), this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::paste);
    editMenu->addAction(m_pasteAction);

    m_deleteAction = new QAction(tr("&Delete"), this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteSelected);
    editMenu->addAction(m_deleteAction);
}

void MainWindow::setupViewMenu()
{
    QMenu* viewMenu = m_menuBar->addMenu(tr("&View"));

    QAction* fullscreenAct = new QAction(tr("&Fullscreen"), this);
    fullscreenAct->setShortcut(Qt::Key_F11);
    connect(fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);
    viewMenu->addAction(fullscreenAct);
    viewMenu->addSeparator();

    QAction* sidebarAct = new QAction(tr("&Sidebar"), this);
    sidebarAct->setCheckable(true);
    sidebarAct->setChecked(true);
    connect(sidebarAct, &QAction::triggered, this, &MainWindow::toggleSidebar);
    viewMenu->addAction(sidebarAct);

    QAction* propertiesAct = new QAction(tr("&Properties"), this);
    propertiesAct->setCheckable(true);
    propertiesAct->setChecked(true);
    connect(propertiesAct, &QAction::triggered, this, &MainWindow::toggleProperties);
    viewMenu->addAction(propertiesAct);

    QAction* outputAct = new QAction(tr("&Output"), this);
    outputAct->setCheckable(true);
    outputAct->setChecked(true);
    connect(outputAct, &QAction::toggled, [this](bool checked) {
        if (m_outputDock) m_outputDock->setVisible(checked);
    });
    viewMenu->addAction(outputAct);
    viewMenu->addSeparator();

    QAction* statusbarAct = new QAction(tr("&Status Bar"), this);
    statusbarAct->setCheckable(true);
    statusbarAct->setChecked(true);
    connect(statusbarAct, &QAction::triggered, this, &MainWindow::toggleStatusBar);
    viewMenu->addAction(statusbarAct);
    viewMenu->addSeparator();

    QAction* resetLayoutAct = new QAction(tr("&Reset Layout"), this);
    connect(resetLayoutAct, &QAction::triggered, this, &MainWindow::resetLayout);
    viewMenu->addAction(resetLayoutAct);
    viewMenu->addSeparator();

    QAction* scriptConsoleAct = new QAction(tr("Script Console"), this);
    scriptConsoleAct->setShortcut(Qt::Key_F12);
    connect(scriptConsoleAct, &QAction::triggered, this, &MainWindow::toggleScriptConsole);
    viewMenu->addAction(scriptConsoleAct);

    QAction* terminalAct = new QAction(tr("Terminal"), this);
    terminalAct->setShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::Key_AsciiTilde)));
    connect(terminalAct, &QAction::triggered, this, &MainWindow::toggleTerminal);
    viewMenu->addAction(terminalAct);

    QAction* sourceControlAct = new QAction(tr("Source Control"), this);
    sourceControlAct->setCheckable(true);
    sourceControlAct->setChecked(false);
    connect(sourceControlAct, &QAction::triggered, this, &MainWindow::toggleGit);
    connect(m_gitDock, &QDockWidget::visibilityChanged, [sourceControlAct](bool visible) {
        sourceControlAct->setChecked(visible);
    });
    viewMenu->addAction(sourceControlAct);

    viewMenu->addSeparator();

    QAction* searchResultsAct = new QAction(tr("Find in Files..."), this);
    searchResultsAct->setCheckable(true);
    searchResultsAct->setChecked(false);
    searchResultsAct->setShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::SHIFT) | static_cast<int>(Qt::Key_F)));
    connect(searchResultsAct, &QAction::triggered, [this]() {
        if (m_searchDock) {
            m_searchDock->setVisible(!m_searchDock->isVisible());
            if (m_searchDock->isVisible() && m_projectSearch) {
                m_projectSearch->setFocus();
            }
        }
    });
    connect(m_searchDock, &QDockWidget::visibilityChanged, [this, searchResultsAct](bool visible) {
        searchResultsAct->setChecked(visible);
        if (visible && m_projectSearch) m_projectSearch->focusSearchInput();
    });
    viewMenu->addAction(searchResultsAct);
}

void MainWindow::setupModulesMenu()
{
    QMenu* modulesMenu = m_menuBar->addMenu(tr("&Modules"));

    m_moduleGroup = new QActionGroup(this);
    m_moduleGroup->setExclusive(true);

    for (int i = 0; i < m_moduleManager->moduleCount(); ++i) {
        QString name = m_moduleManager->moduleName(i);
        QAction* moduleAct = new QAction(name, this);
        moduleAct->setCheckable(true);
        moduleAct->setChecked(i == 0);
        moduleAct->setData(i);
        if (i < 9) moduleAct->setShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | (static_cast<int>(Qt::Key_1) + i)));
        m_moduleGroup->addAction(moduleAct);
        modulesMenu->addAction(moduleAct);
    }

    connect(m_moduleGroup, &QActionGroup::triggered, [this](QAction* action) {
        switchToModule(action->data().toInt());
    });
}

void MainWindow::setupToolsMenu()
{
    QMenu* toolsMenu = m_menuBar->addMenu(tr("&Tools"));

    QAction* calcAct = new QAction(tr("&Calculator"), this);
    calcAct->setShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::SHIFT) | static_cast<int>(Qt::Key_C)));
    connect(calcAct, &QAction::triggered, this, []() {
        ks::ScientificCalculator::launch();
    });
    toolsMenu->addAction(calcAct);
}

void MainWindow::setupSettingsMenu()
{
    QMenu* settingsMenu = m_menuBar->addMenu(tr("Se&ttings"));

    QAction* prefsAct = new QAction(tr("&Preferences..."), this);
    prefsAct->setShortcut(Qt::CTRL + Qt::Key_P);
    connect(prefsAct, &QAction::triggered, this, &MainWindow::showSettings);
    settingsMenu->addAction(prefsAct);
    settingsMenu->addSeparator();

    QAction* runInAC = new QAction(tr("&Launch in Simulator"), this);
    connect(runInAC, &QAction::triggered, this, &MainWindow::runInSimulator);
    settingsMenu->addAction(runInAC);
}

void MainWindow::setupHelpMenu()
{
    QMenu* helpMenu = m_menuBar->addMenu(tr("&Help"));

    QAction* aboutAct = new QAction(tr("&About ksEditor"), this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);
    helpMenu->addAction(aboutAct);

    QAction* docsAct = new QAction(tr("&Documentation"), this);
    docsAct->setShortcut(Qt::Key_F1);
    connect(docsAct, &QAction::triggered, this, &MainWindow::showDocumentation);
    helpMenu->addAction(docsAct);

    helpMenu->addSeparator();

    QAction* quickStartAct = new QAction(tr("&Quick Start Guide"), this);
    connect(quickStartAct, &QAction::triggered, this, [this]() {
        ks::HelpSystem::instance()->startQuickStartGuide();
    });
    helpMenu->addAction(quickStartAct);

    QAction* tutorialAct = new QAction(tr("&Tutorials"), this);
    QMenu* tutorialSubMenu = new QMenu(tr("&Tutorials"), this);
    tutorialAct->setMenu(tutorialSubMenu);

    auto addTutorial = [this, tutorialSubMenu](const QString& name, ks::TutorialSystem::TutorialPage page) {
        QAction* act = new QAction(name, this);
        connect(act, &QAction::triggered, this, [this, page]() {
            ks::HelpSystem::instance()->startTutorial(page);
        });
        tutorialSubMenu->addAction(act);
    };

    addTutorial(tr("Welcome"), ks::TutorialSystem::Welcome);
    addTutorial(tr("Quick Start"), ks::TutorialSystem::QuickStart);
    tutorialSubMenu->addSeparator();
    addTutorial(tr("Creating a Project"), ks::TutorialSystem::CreatingProject);
    addTutorial(tr("Importing 3D Models"), ks::TutorialSystem::Importing3D);
    addTutorial(tr("Working with 3D"), ks::TutorialSystem::WorkingWith3D);
    addTutorial(tr("Exporting"), ks::TutorialSystem::Exporting);
    addTutorial(tr("Settings"), ks::TutorialSystem::Settings);
    addTutorial(tr("Advanced Features"), ks::TutorialSystem::AdvancedFeatures);
    addTutorial(tr("Troubleshooting"), ks::TutorialSystem::Troubleshooting);

    helpMenu->addAction(tutorialAct);
}

void MainWindow::setupToolBar()
{
    m_mainToolBar = addToolBar(tr("Main Toolbar"));
    m_mainToolBar->setObjectName("MainToolBar");

    QAction* newAct = m_mainToolBar->addAction(tr("New"), this, &MainWindow::newProject);
    newAct->setToolTip(tr("New Project"));

    QAction* openAct = m_mainToolBar->addAction(tr("Open"), this, &MainWindow::openProject);
    openAct->setToolTip(tr("Open Project"));

    m_mainToolBar->addAction(m_saveAction);

    m_mainToolBar->addSeparator();

    m_mainToolBar->addAction(m_buildAction);

    m_mainToolBar->addSeparator();

    m_mainToolBar->addAction(tr("Undo"), this, &MainWindow::undo)->setToolTip(tr("Undo"));
    m_mainToolBar->addAction(tr("Redo"), this, &MainWindow::redo)->setToolTip(tr("Redo"));

    m_mainToolBar->addSeparator();

    QAction* calcToolAct = m_mainToolBar->addAction(tr("Calc"), this, []() {
        ks::ScientificCalculator::launch();
    });
    calcToolAct->setToolTip(tr("Calculator (Ctrl+Shift+C)"));

    m_moduleToolBar = addToolBar(tr("Module Toolbar"));
    m_moduleToolBar->setObjectName("ModuleToolBar");
}

void MainWindow::setupStatusBar()
{
    m_statusBar = statusBar();

    m_simPathLabel = new QLabel(tr("Sim Path: Not detected"), this);
    m_simPathLabel->setTextFormat(Qt::PlainText);
    m_simPathLabel->setStyleSheet("color: #888; font-size: 11px; font-family: 'Consolas', monospace; padding: 0 8px;");
    m_statusBar->addPermanentWidget(m_simPathLabel);

    m_cspVersionLabel = new QLabel(tr("CSP: N/A"), this);
    m_cspVersionLabel->setTextFormat(Qt::PlainText);
    m_cspVersionLabel->setStyleSheet("color: #888; font-size: 11px; font-family: 'Consolas', monospace; padding: 0 8px;");
    m_statusBar->addPermanentWidget(m_cspVersionLabel);

    m_navSpeedLabel = new QLabel(tr("Speed: 1.0x"), this);
    m_navSpeedLabel->setStyleSheet("color: #888; font-size: 11px; font-family: 'Consolas', monospace; padding: 0 8px;");
    m_statusBar->addPermanentWidget(m_navSpeedLabel);

    // Snap status with SVG icon
    m_snapStatusLabel = new QLabel(this);
    m_snapStatusLabel->setTextFormat(Qt::PlainText);
    m_snapStatusLabel->setStyleSheet("color: #888; font-size: 11px; font-family: 'Consolas', monospace; padding: 0 8px;");
    m_statusBar->addPermanentWidget(m_snapStatusLabel);

    // Placement status with SVG icon
    m_placementStatusLabel = new QLabel(this);
    m_placementStatusLabel->setTextFormat(Qt::PlainText);
    m_placementStatusLabel->setStyleSheet("color: #888; font-size: 11px; font-family: 'Consolas', monospace; padding: 0 8px;");
    m_statusBar->addPermanentWidget(m_placementStatusLabel);

    // Load SVG icons
    QPixmap snapIcon = loadSvgIcon(":/icons/snap.svg", QSize(16, 16));
    QPixmap placementIcon = loadSvgIcon(":/icons/placement.svg", QSize(16, 16));

    setStatusSnap(tr("Snap: Off"));
    setStatusPlacement(tr("Place: Off"));

    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setStyleSheet("color: #aaa; padding: 0 4px;");
    m_statusBar->addWidget(m_statusLabel, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(Constants::PROGRESS_BAR_WIDTH);
    m_progressBar->setVisible(false);
    m_statusBar->addPermanentWidget(m_progressBar);
}

QPixmap MainWindow::loadSvgIcon(const QString& path, const QSize& size)
{
    QSvgRenderer renderer(path);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    return pixmap;
}

void MainWindow::setStatusNavSpeed(const QString& text)
{
    if (m_navSpeedLabel) m_navSpeedLabel->setText(text);
}

void MainWindow::setStatusSnap(const QString& text)
{
    if (m_snapStatusLabel) {
        QPixmap snapIcon = loadSvgIcon(":/icons/snap.svg", QSize(14, 14));
        m_snapStatusLabel->setPixmap(snapIcon);
        m_snapStatusLabel->setText(" " + text);
    }
}

void MainWindow::setStatusPlacement(const QString& text)
{
    if (m_placementStatusLabel) {
        QPixmap placementIcon = loadSvgIcon(":/icons/placement.svg", QSize(14, 14));
        m_placementStatusLabel->setPixmap(placementIcon);
        m_placementStatusLabel->setText(" " + text);
    }
}

void MainWindow::setupDockWidgets()
{
    m_sidebarDock = new QDockWidget(tr("Sidebar"), this);
    m_sidebarDock->setObjectName("SidebarDock");
    m_sidebarDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarDock->setMinimumWidth(Constants::MIN_SIDEBAR_WIDTH);

    m_fileTree = new FileTreeWidget(this);
    m_sidebarDock->setWidget(m_fileTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
    connect(m_fileTree, &FileTreeWidget::fileActivated,
            this, &MainWindow::onFileTreeActivated);

    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->setObjectName("PropertiesDock");
    m_propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget* propertiesWidget = new QWidget(this);
    propertiesWidget->setMinimumWidth(Constants::MIN_PROPERTIES_WIDTH);
    m_propertiesDock->setWidget(propertiesWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    m_outputDock = new QDockWidget(tr("Output"), this);
    m_outputDock->setObjectName("OutputDock");
    m_outputDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_outputDock->setMinimumHeight(Constants::MIN_OUTPUT_HEIGHT);

    QWidget* outputWidget = new QWidget(this);
    m_outputDock->setWidget(outputWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    // Script console dock
    m_scriptConsoleDock = createScriptConsoleDock();
    addDockWidget(Qt::BottomDockWidgetArea, m_scriptConsoleDock);
    m_scriptConsoleDock->hide();

    // Search results dock
    m_projectSearch = new ProjectSearchWidget(this);
    m_searchDock = new QDockWidget(tr("Search Results"), this);
    m_searchDock->setObjectName("SearchResultsDock");
    m_searchDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_searchDock->setWidget(m_projectSearch);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);
    m_searchDock->hide();

    connect(m_projectSearch, &ProjectSearchWidget::resultActivated,
            this, &MainWindow::onSearchResultActivated);

    // Terminal dock
    m_terminal = new TerminalWidget(this);
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName("TerminalDock");
    m_terminalDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_terminalDock->setMinimumHeight(Constants::MIN_OUTPUT_HEIGHT);
    m_terminalDock->setWidget(m_terminal);
    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);
    m_terminalDock->hide();

    // Git status dock
    m_gitStatus = new GitStatusWidget(this);
    m_gitDock = new QDockWidget(tr("Source Control"), this);
    m_gitDock->setObjectName("GitDock");
    m_gitDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_gitDock->setWidget(m_gitStatus);
    addDockWidget(Qt::RightDockWidgetArea, m_gitDock);
    m_gitDock->hide();

    connect(m_gitStatus, &GitStatusWidget::openFileRequested, this, [this](const QString& filePath, int line) {
        if (filePath.isEmpty()) return;
        m_moduleManager->setCurrentModule("Text Editor");
        if (line > 0) {
            ks::TextEditorModule* te = qobject_cast<ks::TextEditorModule*>(m_moduleManager->currentEditorModule());
            if (te) te->openFileAtLine(filePath, line);
        } else {
            m_moduleManager->importFile(filePath);
        }
    });

    tabifyDockWidget(m_sidebarDock, m_propertiesDock);
    tabifyDockWidget(m_sidebarDock, m_gitDock);
}

QDockWidget* MainWindow::createScriptConsoleDock()
{
    QDockWidget* dock = new QDockWidget(tr("Script Console"), this);
    dock->setObjectName("ScriptConsoleDock");

    QWidget* consoleWidget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(consoleWidget);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_scriptOutput = new QPlainTextEdit(consoleWidget);
    m_scriptOutput->setReadOnly(true);
    m_scriptOutput->setStyleSheet(
        "QPlainTextEdit { background: #1e1e1e; color: #d4d4d4; font-family: 'Consolas', monospace; "
        "font-size: 12px; border: 1px solid #333; }"
    );
    m_scriptOutput->setMaximumBlockCount(1000);
    layout->addWidget(m_scriptOutput, 1);

    QHBoxLayout* inputLayout = new QHBoxLayout();
    m_scriptInput = new QLineEdit(consoleWidget);
    m_scriptInput->setStyleSheet(
        "QLineEdit { background: #2d2d2d; color: #d4d4d4; font-family: 'Consolas', monospace; "
        "font-size: 12px; border: 1px solid #555; padding: 4px; }"
    );
    m_scriptInput->setPlaceholderText(tr("Type JavaScript and press Enter..."));
    inputLayout->addWidget(m_scriptInput, 1);

    QPushButton* runBtn = new QPushButton(tr("Run"), consoleWidget);
    runBtn->setStyleSheet(
        "QPushButton { background: #0078d4; color: white; border: none; padding: 4px 12px; "
        "border-radius: 3px; } QPushButton:hover { background: #1a8ae8; }"
    );
    inputLayout->addWidget(runBtn);

    layout->addLayout(inputLayout);

    dock->setWidget(consoleWidget);
    dock->resize(400, 200);

    m_scriptEngine = new QJSEngine(this);

    connect(m_scriptInput, &QLineEdit::returnPressed, this, &MainWindow::executeScript);
    connect(runBtn, &QPushButton::clicked, this, &MainWindow::executeScript);

    return dock;
}

void MainWindow::toggleScriptConsole()
{
    if (m_scriptConsoleDock) {
        m_scriptConsoleDock->setVisible(!m_scriptConsoleDock->isVisible());
        if (m_scriptConsoleDock->isVisible()) {
            m_scriptInput->setFocus();
        }
    }
}

void MainWindow::toggleTerminal()
{
    if (m_terminalDock) {
        m_terminalDock->setVisible(!m_terminalDock->isVisible());
        if (m_terminalDock->isVisible() && m_terminal) {
            m_terminal->setFocus();
        }
    }
}

void MainWindow::toggleGit()
{
    if (m_gitDock) {
        m_gitDock->setVisible(!m_gitDock->isVisible());
        if (m_gitDock->isVisible() && m_gitStatus) {
            m_gitStatus->refresh();
        }
    }
}

void MainWindow::executeScript()
{
    QString code = m_scriptInput->text().trimmed();
    if (code.isEmpty()) return;

    m_scriptOutput->appendPlainText("> " + code);

    if (code == "clear" || code == "cls") {
        m_scriptOutput->clear();
        m_scriptInput->clear();
        return;
    }

    QJSValue result = m_scriptEngine->evaluate(code);
    if (result.isError()) {
        m_scriptOutput->appendPlainText("Error: " + result.toString());
    } else if (!result.isUndefined()) {
        m_scriptOutput->appendPlainText(result.toString());
    }

    m_scriptInput->clear();
}

void MainWindow::setupConnections()
{
    connect(m_moduleManager, &ModuleManager::moduleChanged, this, &MainWindow::onModuleChanged);
    connect(m_undoStack, &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);
    connect(m_undoStack, &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);
    connect(m_undoStack, &QUndoStack::cleanChanged, [this](bool clean) {
        updateWindowTitle();
    });

    connect(m_projectBuilder, &ProjectBuilder::progressUpdated, this, &MainWindow::onBuildProgress);
    connect(m_projectBuilder, &ProjectBuilder::buildComplete, this, &MainWindow::onBuildComplete);

    // Register help contexts for main modules
    auto* help = ks::HelpSystem::instance();
    help->registerHelp("3DModeler", "viewport", "Main 3D viewport for modeling and preview. Orbit: left-drag, Pan: right-drag, Zoom: scroll", "F1");
    help->registerHelp("3DModeler", "import", "Import 3D models (FBX, OBJ, GLB, KN5, DAE)", "Ctrl+I");
    help->registerHelp("3DModeler", "export", "Export models to game-ready formats (KN5, FBX, OBJ)", "Ctrl+E");
    help->registerHelp("3DModeler", "sculpt", "Sculpting mode with brushes for organic modeling", "S");
    help->registerHelp("3DModeler", "boolean", "CSG boolean operations: union, difference, intersection");
    help->registerHelp("AudioEditor", "mixer", "Multi-track audio mixer with channel strips and effects", "F2");
    help->registerHelp("AudioEditor", "effects", "Audio effects rack: EQ, Compressor, Reverb, Delay and more");
    help->registerHelp("AudioEditor", "spectrum", "Real-time spectrum analyzer and oscilloscope");
    help->registerHelp("PhysicsEditor", "simulator", "Vehicle dynamics simulator with 14-DOF physics", "F3");
    help->registerHelp("PhysicsEditor", "tires", "Pacejka tire model editor with temperature and wear simulation");
    help->registerHelp("PhysicsEditor", "aero", "Aerodynamics editor: downforce, drag, DRS, active aero");
    help->registerHelp("PhysicsEditor", "ers", "ERS/Hybrid system: MGU-K, MGU-H, battery deployment");
    help->registerHelp("ShowroomEditor", "viewport", "3D showroom preview with configurable lighting and cameras");
    help->registerHelp("ShowroomEditor", "config", "Showroom configuration: camera, lighting, background");
    help->registerHelp("PaintEditor", "painting", "Car paint editing with DDS export and decal import");
    help->registerHelp("CockpitInstruments", "segments", "7/14/16-segment display editor for AC dashboards");
    help->registerHelp("FontCreator", "glyphs", "Bitmap glyph editor for font atlas generation");
    help->registerHelp("ScriptConsole", "console", "JavaScript console with auto-complete and history", "F12");
    help->registerHelp("ModManager", "manager", "Content organization, installation, and conflict resolution");
    help->registerHelp("General", "ribbon", "Context-sensitive ribbon toolbar for current module");
    help->registerHelp("General", "statusbar", "Status bar showing current module, file info, and operations");
}

void MainWindow::createActions()
{
    // Actions are created in their respective setup methods
    updateActions();

    // Connect undo/redo signals
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
}

void MainWindow::updateActions()
{
    const bool hasProject = !m_currentProjectPath.isEmpty();

    m_saveAction->setEnabled(hasProject);
    m_saveAsAction->setEnabled(hasProject);
    m_buildAction->setEnabled(hasProject);

    // Edit actions depend on focus widget or current module
    QWidget* focusWidget = QApplication::focusWidget();
    bool hasEditableText = (qobject_cast<QLineEdit*>(focusWidget) != nullptr);

    m_cutAction->setEnabled(hasProject && (hasEditableText || m_moduleManager->canCut()));
    m_copyAction->setEnabled(hasProject && (hasEditableText || m_moduleManager->canCopy()));
    m_pasteAction->setEnabled(hasProject && (hasEditableText || m_moduleManager->canPaste()));
    m_deleteAction->setEnabled(hasProject && m_moduleManager->canDelete());
}

// ==================== Path Detection ====================

QString MainWindow::detectSimulatorFromRegistry() const
{
#ifdef Q_OS_WIN
    QStringList keys = EditorConfig::instance().registryKeys();
    for (const QString& keyPath : keys) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.toStdWString().c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t path[MAX_PATH];
            DWORD size = sizeof(path);
            DWORD type = REG_SZ;

            if (RegQueryValueExW(hKey, L"InstallLocation", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return QString::fromWCharArray(path);
            }
            RegCloseKey(hKey);
        }
    }
#endif
    return QString();
}

QStringList MainWindow::getDefaultSimulatorPaths() const
{
    return EditorConfig::instance().defaultSearchPaths();
}

void MainWindow::detectSimulator()
{
    QStringList possiblePaths;

    QString registryPath = detectSimulatorFromRegistry();
    if (!registryPath.isEmpty()) {
        possiblePaths << registryPath;
    }

    QString userPath = m_settings->value("ksPath").toString();
    if (!userPath.isEmpty()) {
        possiblePaths.prepend(userPath);
    }

    possiblePaths << getDefaultSimulatorPaths();

    possiblePaths.removeDuplicates();

    for (const QString& path : possiblePaths) {
        QDir dir(path);
        if (dir.exists() && dir.exists(EditorConfig::instance().simExeName())) {
            setSimPath(path);
            return;
        }
    }

    LOG_WARNING("MainWindow", "Simulator not found in standard locations");
    setStatusMessage(tr("Simulator not found. Please set path in Settings."), 5000);
}

void MainWindow::detectCSPVersion()
{
    if (m_simPath.isEmpty()) {
        setCSPVersion(tr("Not installed"));
        return;
    }

    QString extensionPath = m_simPath + "/extension";
    QDir dir(extensionPath);

    QStringList cspFiles = dir.entryList(QStringList() << "*.dll");
    for (const QString& file : cspFiles) {
        if (file.contains("custom_shaders_patch", Qt::CaseInsensitive)) {
            setCSPVersion(tr("Detected"));
            return;
        }
    }

    // Also check in settings.ini
    QSettings settings(m_simPath + "/cfg/settings.ini", QSettings::IniFormat);
    QString cspVersion = settings.value("CUSTOM_SHADERS_PATCH_VERSION", tr("Not installed")).toString();
    setCSPVersion(cspVersion);
}

// ==================== File Operations ====================

bool MainWindow::createProjectFile(const QString& path, const QString& name)
{
    ks::ProjectData data;
    data.name = name;
    data.version = "1.0";
    data.created = QDateTime::currentDateTime();
    data.modified = data.created;
    data.filePath = path;
    data.activeModule = m_moduleManager->moduleName(0);
    data.activeModuleIndex = 0;

    if (!ks::ProjectSerializer::instance().save(path, data)) {
        LOG_ERROR("MainWindow", "Failed to create project file: " + path);
        return false;
    }

    LOG_INFO("MainWindow", "Created project: " + path);
    return true;
}

bool MainWindow::loadProjectFile(const QString& path)
{
    ks::ProjectData data;
    if (!ks::ProjectSerializer::instance().load(path, data)) {
        LOG_ERROR("MainWindow", "Failed to load project: " + path);
        return false;
    }

    LOG_INFO("MainWindow", "Loaded project: " + data.name + " v" + data.version);

    for (auto* mod : m_moduleManager->modules()) {
        QString modId = mod->moduleId();
        if (data.modelerState.contains(modId)) {
            QJsonObject modState = QJsonObject::fromVariantMap(data.modelerState[modId].toMap());
            mod->deserializeProject(modState);
        }
    }

    if (data.modelerState.contains("scene")) {
        for (auto* mod : m_moduleManager->modules()) {
            if (auto* modeler = qobject_cast<ks::KSModelerModule*>(mod)) {
                QJsonObject sceneJson = QJsonObject::fromVariantMap(data.modelerState["scene"].toMap());
                ks::ProjectSerializer::deserializeScene(modeler->sceneGraph(), sceneJson);
                break;
            }
        }
    }

    if (data.activeModuleIndex >= 0 && data.activeModuleIndex < m_moduleManager->moduleCount()) {
        m_moduleManager->setCurrentModule(data.activeModuleIndex);
    }

    return true;
}

void MainWindow::newProject()
{
    QString projectName = QInputDialog::getText(this, tr("New Project"),
                                                tr("Project name:"));
    if (projectName.isEmpty()) return;

    QString projectDir = QFileDialog::getExistingDirectory(this, tr("Project Location"));
    if (projectDir.isEmpty()) return;

    QString projectPath = QString("%1/%2%3").arg(projectDir, projectName, Constants::PROJECT_EXTENSION);

    if (!createProjectFile(projectPath, projectName)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to create project file"));
        return;
    }

    m_currentProjectPath = projectPath;
    addToRecentProjects(projectPath);
    updateWindowTitle();
    updateActions();

    if (m_fileTree) {
        QFileInfo projectFile(projectPath);
        m_fileTree->setRootPath(projectFile.absolutePath());
    }
    if (m_projectSearch) {
        QFileInfo projectFile(projectPath);
        m_projectSearch->setSearchRoot(projectFile.absolutePath());
    }
    if (m_gitStatus) {
        QFileInfo projectFile(projectPath);
        m_gitStatus->setRepoPath(projectFile.absolutePath());
    }

    emit projectOpened(projectPath);
    LOG_INFO("MainWindow", "Created new project: " + projectPath);
}

void MainWindow::openProject()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Project"),
        QDir::homePath(),
        tr("ksEditor Project (*%1);;All Files (*)").arg(Constants::PROJECT_EXTENSION)
    );

    if (!filePath.isEmpty()) {
        openRecentProject(filePath);
    }
}

void MainWindow::openRecentProject(const QString& path)
{
    // Close current project first
    if (!closeProject()) {
        return;
    }

    if (!loadProjectFile(path)) {
        QMessageBox::warning(this, tr("Error"),
                           tr("Failed to open project: %1").arg(path));
        return;
    }

    m_currentProjectPath = path;
    addToRecentProjects(path);
    updateWindowTitle();
    updateActions();

    if (m_crashRecovery) {
        m_crashRecovery->addOpenDocument(path);
        m_crashRecovery->setActiveDocument(path);
    }

    // Set file tree root and search root to project directory
    QFileInfo projectFile(path);
    if (m_fileTree && projectFile.exists()) {
        m_fileTree->setRootPath(projectFile.absolutePath());
    }
    if (m_projectSearch && projectFile.exists()) {
        m_projectSearch->setSearchRoot(projectFile.absolutePath());
    }
    if (m_gitStatus && projectFile.exists()) {
        m_gitStatus->setRepoPath(projectFile.absolutePath());
    }

    emit projectOpened(path);
    LOG_INFO("MainWindow", "Opened project: " + path);
}

bool MainWindow::saveProject()
{
    if (m_currentProjectPath.isEmpty()) {
        return saveProjectAs();
    }

    return saveProjectFile(m_currentProjectPath);
}

bool MainWindow::saveProjectAs()
{
    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save Project As"),
        QDir::homePath(),
        tr("ksEditor Project (*%1)").arg(Constants::PROJECT_EXTENSION)
    );

    if (filePath.isEmpty()) return false;

    // Ensure extension
    if (!filePath.endsWith(Constants::PROJECT_EXTENSION, Qt::CaseInsensitive)) {
        filePath += Constants::PROJECT_EXTENSION;
    }

    if (saveProjectFile(filePath)) {
        m_currentProjectPath = filePath;
        addToRecentProjects(filePath);
        updateWindowTitle();
        return true;
    }

    return false;
}

bool MainWindow::saveProjectFile(const QString& path)
{
    LOG_INFO("MainWindow", "Saving project: " + path);

    ks::ProjectData data;
    data.filePath = path;
    data.modified = QDateTime::currentDateTime();
    data.activeModuleIndex = m_moduleManager->currentModule();

    QFileInfo info(path);
    data.name = info.completeBaseName();

    for (auto* mod : m_moduleManager->modules()) {
        QJsonObject modState = mod->serializeProject();
        if (!modState.isEmpty()) {
            data.modelerState[mod->moduleId()] = modState.toVariantMap();
        }
    }

    for (auto* mod : m_moduleManager->modules()) {
        if (auto* modeler = qobject_cast<ks::KSModelerModule*>(mod)) {
            data.modelerState["scene"] = ks::ProjectSerializer::serializeScene(modeler->sceneGraph());
            break;
        }
    }

    if (!ks::ProjectSerializer::instance().save(path, data)) {
        QMessageBox::warning(this, tr("Save Failed"),
                           tr("Could not save project to: %1").arg(path));
        return false;
    }

    m_undoStack->setClean();
    updateWindowTitle();
    setStatusMessage(tr("Project saved"), 2000);
    return true;
}

void MainWindow::buildProject()
{
    if (m_currentProjectPath.isEmpty()) {
        QMessageBox::information(this, tr("No Project"),
                               tr("Please create or open a project first."));
        return;
    }

    if (m_simPath.isEmpty()) {
        QMessageBox::warning(this, tr("Simulator Not Found"),
                           tr("Please set the simulator path in Settings."));
        return;
    }

    setStatusMessage(tr("Building project..."));
    setProgress(0);

    m_projectBuilder->build(m_currentProjectPath, m_simPath);
}

bool MainWindow::closeProject()
{
    if (m_currentProjectPath.isEmpty()) {
        return true;
    }

    if (!promptForUnsavedChanges()) {
        return false;
    }

    // Clear undo stack
    m_undoStack->clear();

    m_currentProjectPath.clear();
    updateWindowTitle();
    updateActions();

    if (m_fileTree) {
        m_fileTree->setRootPath(QDir::homePath());
    }
    if (m_projectSearch) {
        m_projectSearch->setSearchRoot(QString());
    }
    if (m_gitStatus) {
        m_gitStatus->setRepoPath(QString());
    }

    emit projectClosed();
    LOG_INFO("MainWindow", "Closed project");

    return true;
}

bool MainWindow::promptForUnsavedChanges()
{
    if (m_undoStack->isClean()) {
        return true;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("Save changes before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );

    switch (reply) {
        case QMessageBox::Save:
            return saveProject();
        case QMessageBox::Discard:
            return true;
        default:
            return false;
    }
}

void MainWindow::addToRecentProjects(const QString& path)
{
    QStringList recent = m_settings->value("recentProjects").toStringList();
    recent.removeAll(path);
    recent.prepend(path);

    while (recent.size() > Constants::MAX_RECENT_PROJECTS) {
        recent.removeLast();
    }

    m_settings->setValue("recentProjects", recent);
    updateRecentProjectsMenu();
}

void MainWindow::updateRecentProjectsMenu()
{
    QStringList recent = m_settings->value("recentProjects").toStringList();

    for (int i = 0; i < m_recentProjectsActions.size(); ++i) {
        if (i < recent.size()) {
            QFileInfo info(recent[i]);
            QString displayText = QString("%1. %2").arg(i + 1).arg(info.fileName());
            m_recentProjectsActions[i]->setText(displayText);
            m_recentProjectsActions[i]->setData(recent[i]);
            m_recentProjectsActions[i]->setVisible(true);
            m_recentProjectsActions[i]->setToolTip(recent[i]);
        } else {
            m_recentProjectsActions[i]->setVisible(false);
        }
    }
}

// ==================== Module Switching ====================

bool MainWindow::switchToModule(int index)
{
    if (index < 0 || index >= m_moduleManager->moduleCount()) {
        LOG_ERROR("MainWindow", "Invalid module index: " + QString::number(index));
        return false;
    }

    m_moduleManager->setCurrentModule(index);
    updateWindowTitle();
    updateActions();
    return true;
}

bool MainWindow::switchToModule(const QString& moduleName)
{
    int index = m_moduleManager->moduleIndex(moduleName);
    if (index >= 0) {
        return switchToModule(index);
    }

    LOG_WARNING("MainWindow", "Module not found: " + moduleName);
    return false;
}

void MainWindow::onModuleChanged(int index)
{
    const QList<QAction*>& actions = m_moduleGroup->actions();
    if (index >= 0 && index < actions.size()) {
        actions[index]->setChecked(true);
    }

    emit moduleChanged(index);
    updateActions();
}

// ==================== Edit Operations ====================

void MainWindow::undo()
{
    if (m_undoStack->canUndo()) {
        m_undoStack->undo();
        setStatusMessage(tr("Undo"), 1000);
    }
}

void MainWindow::redo()
{
    if (m_undoStack->canRedo()) {
        m_undoStack->redo();
        setStatusMessage(tr("Redo"), 1000);
    }
}

void MainWindow::cut()
{
    QWidget* focusWidget = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focusWidget)) {
        edit->cut();
    } else if (m_moduleManager->currentEditorModule()) {
        m_moduleManager->currentEditorModule()->cut();
    }
}

void MainWindow::copy()
{
    QWidget* focusWidget = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focusWidget)) {
        edit->copy();
    } else if (m_moduleManager->currentEditorModule()) {
        m_moduleManager->currentEditorModule()->copy();
    }
}

void MainWindow::paste()
{
    QWidget* focusWidget = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focusWidget)) {
        edit->paste();
    } else if (m_moduleManager->currentEditorModule()) {
        m_moduleManager->currentEditorModule()->paste();
    }
}

void MainWindow::deleteSelected()
{
    if (m_moduleManager->currentEditorModule()) {
        m_moduleManager->currentEditorModule()->deleteSelected();
    }
}

// ==================== View Operations ====================

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::toggleSidebar()
{
    if (m_sidebarDock) {
        m_sidebarDock->setVisible(!m_sidebarDock->isVisible());
    }
}

void MainWindow::onFileTreeActivated(const QString& filePath)
{
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();

    if (suffix == "ksep") {
        openRecentProject(filePath);
        return;
    }

    QStringList textExtensions = {"txt", "md", "log", "ini", "cfg", "conf",
        "json", "xml", "yaml", "yml", "toml", "csv", "env",
        "cpp", "c", "h", "hpp", "hxx", "cxx", "cs", "java", "kt",
        "py", "lua", "js", "ts", "rs", "go", "swift",
        "glsl", "vert", "frag", "comp", "css", "html", "htm"};

    if (textExtensions.contains(suffix)) {
        if (m_moduleManager->currentEditorModule() &&
            m_moduleManager->currentEditorModule()->moduleId() != "textEditor") {
            m_moduleManager->setCurrentModule("Text Editor");
        }
        m_moduleManager->importFile(filePath);
    } else if (suffix == "kn5" || suffix == "fbx" || suffix == "obj" ||
               suffix == "glb" || suffix == "gltf" || suffix == "dae" || suffix == "stl") {
        m_moduleManager->importFile(filePath);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    }
}

void MainWindow::onSearchResultActivated(const QString& filePath, int lineNumber)
{
    if (filePath.isEmpty()) return;

    ks::TextEditorModule* textEditor = nullptr;
    for (auto* mod : m_moduleManager->modules()) {
        if (mod->moduleId() == "textEditor") {
            textEditor = qobject_cast<ks::TextEditorModule*>(mod);
            break;
        }
    }

    if (textEditor) {
        m_moduleManager->setCurrentModule("Text Editor");
        textEditor->openFileAtLine(filePath, lineNumber);
    }
}

void MainWindow::toggleStatusBar()
{
    if (m_statusBar) {
        m_statusBar->setVisible(!m_statusBar->isVisible());
    }
}

void MainWindow::toggleProperties()
{
    if (m_propertiesDock) {
        m_propertiesDock->setVisible(!m_propertiesDock->isVisible());
    }
}

void MainWindow::resetLayout()
{
    QSettings settings(Constants::APP_NAME, Constants::APP_NAME);
    settings.remove("geometry");
    settings.remove("windowState");

    QMessageBox::information(this, tr("Reset Layout"),
                           tr("Please restart the application to reset the layout."));
}

void MainWindow::saveWindowLayout()
{
    QSettings settings(Constants::APP_NAME, Constants::APP_NAME);
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("windowState", saveState());
    setStatusMessage(tr("Window layout saved"), 2000);
}

void MainWindow::loadWindowLayout()
{
    QSettings settings(Constants::APP_NAME, Constants::APP_NAME);
    QByteArray geometry = settings.value("windowGeometry").toByteArray();
    QByteArray state = settings.value("windowState").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    if (!state.isEmpty()) restoreState(state);
}

void MainWindow::resetWindowLayout()
{
    resetLayout();
}

void MainWindow::showRecoveryDialog(const QVector<ks::CrashRecovery::Session>& sessions)
{
    if (sessions.isEmpty()) return;

    // Sessions are in file-system order; pick the most recent one as target.
    const auto& session = sessions.first();

    QStringList documents = session.openDocuments;
    documents.removeAll(QString());
    QString target = !session.lastActiveDocument.isEmpty()
        ? session.lastActiveDocument
        : (documents.isEmpty() ? QString() : documents.first());

    QString message = tr("Crash recovery data found for %1 session(s):\n\n").arg(sessions.size());
    for (const auto& s : sessions) {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(s.timestamp);
        message += tr("- %1: %2 document(s)\n")
                       .arg(dt.toString(Qt::ISODate))
                       .arg(s.openDocuments.size());
    }
    message += tr("\nThe following project will be reopened:\n%1\n\nWould you like to recover?")
                   .arg(target.isEmpty() ? tr("(none)") : target);

    const auto reply = QMessageBox::question(this, tr("Crash Recovery"), message,
                                             QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_crashRecovery->recoverSession(session);
        if (target.isEmpty()) {
            setStatusMessage(tr("Session recovered"), 3000);
        } else if (QFileInfo::exists(target)) {
            openRecentProject(target);
            setStatusMessage(tr("Session recovered"), 3000);
        } else {
            setStatusMessage(tr("Recovered session, but project file no longer exists"), 4000);
        }
    }

    // The user has been informed; drop the abandoned session markers so they
    // are not offered again on the next launch.
    for (const auto& s : sessions)
        m_crashRecovery->dismissSession(s.id);
}

// ==================== Settings ====================

void MainWindow::showSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    dlg.setMinimumWidth(500);
    dlg.setMinimumHeight(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(&dlg);

    QTabWidget* tabs = new QTabWidget(&dlg);

    // General tab
    QWidget* generalTab = new QWidget;
    QFormLayout* generalLayout = new QFormLayout(generalTab);
    QLineEdit* simPathEdit = new QLineEdit(m_simPath, generalTab);
    QPushButton* browseBtn = new QPushButton("...", generalTab);
    browseBtn->setMaximumWidth(30);
    QHBoxLayout* simPathLayout = new QHBoxLayout;
    simPathLayout->addWidget(simPathEdit);
    simPathLayout->addWidget(browseBtn);
    generalLayout->addRow(tr("Simulator Path:"), simPathLayout);
    connect(browseBtn, &QPushButton::clicked, [&]() {
        QString dir = QFileDialog::getExistingDirectory(&dlg, tr("Select Simulator Path"), simPathEdit->text());
        if (!dir.isEmpty()) simPathEdit->setText(dir);
    });

    QLineEdit* cspVersionEdit = new QLineEdit(m_cspVersion, generalTab);
    generalLayout->addRow(tr("CSP Version:"), cspVersionEdit);

    QSpinBox* recentCountSpin = new QSpinBox(generalTab);
    recentCountSpin->setRange(3, 30);
    recentCountSpin->setValue(m_settings->integer("ui/recentProjectsMax", 10));
    generalLayout->addRow(tr("Max Recent Projects:"), recentCountSpin);

    QCheckBox* autoSaveCheck = new QCheckBox(generalTab);
    autoSaveCheck->setChecked(m_settings->boolean("editor/autoSave", true));
    generalLayout->addRow(tr("Enable Auto-Save:"), autoSaveCheck);

    QSpinBox* autoSaveInterval = new QSpinBox(generalTab);
    autoSaveInterval->setRange(1, 60);
    autoSaveInterval->setValue(m_settings->integer("editor/autoSaveInterval", 5));
    autoSaveInterval->setSuffix(" min");
    generalLayout->addRow(tr("Auto-Save Interval:"), autoSaveInterval);

    tabs->addTab(generalTab, tr("General"));

    // Appearance tab
    QWidget* appearanceTab = new QWidget;
    QFormLayout* appearanceLayout = new QFormLayout(appearanceTab);

    QComboBox* themeCombo = new QComboBox(appearanceTab);
    themeCombo->addItems({"Dark", "Light", "System"});
    themeCombo->setCurrentIndex(m_settings->integer("ui/theme", 0));
    appearanceLayout->addRow(tr("Theme:"), themeCombo);

    QComboBox* langCombo = new QComboBox(appearanceTab);
    langCombo->addItem(tr("English"), "en");
    langCombo->addItem(tr("German"), "de");
    langCombo->addItem(tr("Italian"), "it");
    langCombo->addItem(tr("Japanese"), "ja");
    langCombo->addItem(tr("System Default"), "system");
    QString currentLang = m_settings->string("ui/language", "system");
    int langIdx = langCombo->findData(currentLang);
    if (langIdx >= 0) langCombo->setCurrentIndex(langIdx);
    appearanceLayout->addRow(tr("Language:"), langCombo);

    QFontComboBox* fontCombo = new QFontComboBox(appearanceTab);
    fontCombo->setCurrentFont(QFont(m_settings->string("ui/font", "Segoe UI")));
    appearanceLayout->addRow(tr("Font:"), fontCombo);

    QSpinBox* fontSizeSpin = new QSpinBox(appearanceTab);
    fontSizeSpin->setRange(8, 24);
    fontSizeSpin->setValue(m_settings->integer("ui/fontSize", 10));
    appearanceLayout->addRow(tr("Font Size:"), fontSizeSpin);

    tabs->addTab(appearanceTab, tr("Appearance"));

    // Editor tab
    QWidget* editorTab = new QWidget;
    QFormLayout* editorLayout = new QFormLayout(editorTab);

    QComboBox* gizmoCombo = new QComboBox(editorTab);
    gizmoCombo->addItems({tr("Translate"), tr("Rotate"), tr("Scale")});
    gizmoCombo->setCurrentIndex(m_settings->integer("editor/defaultGizmo", 0));
    editorLayout->addRow(tr("Default Gizmo:"), gizmoCombo);

    QDoubleSpinBox* snapSpin = new QDoubleSpinBox(editorTab);
    snapSpin->setRange(0.001, 10.0);
    snapSpin->setSingleStep(0.1);
    snapSpin->setDecimals(3);
    snapSpin->setValue(m_settings->real("editor/snapValue", 0.1));
    editorLayout->addRow(tr("Snap Value:"), snapSpin);

    QCheckBox* snapCheck = new QCheckBox(editorTab);
    snapCheck->setChecked(m_settings->boolean("editor/snapEnabled", false));
    editorLayout->addRow(tr("Enable Snapping:"), snapCheck);

    tabs->addTab(editorTab, tr("Editor"));

    mainLayout->addWidget(tabs);

    // Buttons
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, &dlg);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, [&]() {
        m_simPath = simPathEdit->text();
        m_cspVersion = cspVersionEdit->text();
        m_settings->setValue("simulator/path", m_simPath);
        m_settings->setValue("simulator/cspVersion", m_cspVersion);
        m_settings->setValue("ui/recentProjectsMax", recentCountSpin->value());
        m_settings->setValue("editor/autoSave", autoSaveCheck->isChecked());
        m_settings->setValue("editor/autoSaveInterval", autoSaveInterval->value());
        m_settings->setValue("ui/theme", themeCombo->currentIndex());
        m_settings->setValue("ui/language", langCombo->currentData().toString());
        m_settings->setValue("ui/font", fontCombo->currentFont().family());
        m_settings->setValue("ui/fontSize", fontSizeSpin->value());
        m_settings->setValue("editor/defaultGizmo", gizmoCombo->currentIndex());
        m_settings->setValue("editor/snapValue", snapSpin->value());
        m_settings->setValue("editor/snapEnabled", snapCheck->isChecked());
        m_settings->sync();

        // Update autosave timer
        if (m_autoSaveTimer) {
            if (autoSaveCheck->isChecked() && autoSaveInterval->value() > 0) {
                m_autoSaveTimer->start(autoSaveInterval->value() * 60 * 1000);
            } else {
                m_autoSaveTimer->stop();
            }
        }

        emit simPathChanged(m_simPath);
        loadLanguage(m_settings->string("ui/language", "system"));
        dlg.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

void MainWindow::loadLanguage(const QString& langCode)
{
    QString code = langCode;
    if (code == "system") {
        code = QLocale::system().name();
    }
    
    // Remove old translators
    qApp->removeTranslator(&m_appTranslator);
    qApp->removeTranslator(&m_qtTranslator);
    
    // Load Qt translations
    if (m_qtTranslator.load("qt_" + code, QLibraryInfo::path(QLibraryInfo::LibraryLocation::TranslationsPath))) {
        qApp->installTranslator(&m_qtTranslator);
    }
    
    // Load app translations
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/i18n",
        QCoreApplication::applicationDirPath() + "/../i18n",
        ":/i18n"
    };
    for (const QString& path : searchPaths) {
        if (m_appTranslator.load(path + "/kseditor_" + code)) {
            qApp->installTranslator(&m_appTranslator);
            break;
        }
    }
    
    m_settings->setValue("ui/language", code);
    m_settings->sync();
}

void MainWindow::onLanguageChanged(const QString& langCode)
{
    loadLanguage(langCode);
}

void MainWindow::showDocumentation()
{
    if (!m_helpBrowser) {
        m_helpBrowser = new ks::HelpBrowser(this);
    }
    m_helpBrowser->show();
    m_helpBrowser->raise();
    m_helpBrowser->activateWindow();
}

void MainWindow::showAbout()
{
    QString aboutText = tr(
        "<h3>ksEditor Qt</h3>"
        "<p>Version 0.9</p>"
        "<p>%1</p>"
        "<p>Built with Qt %2</p>"
        "<p>&copy; 2024 ksEditor Team</p>"
    ).arg(EditorConfig::instance().aboutDescription()).arg(qVersion());

    QMessageBox::about(this, tr("About ksEditor Qt"), aboutText);
}

void MainWindow::runInSimulator()
{
    if (m_simPath.isEmpty()) {
        QMessageBox::warning(this, tr("Simulator Not Found"),
                           tr("Please set the simulator path in Settings."));
        return;
    }

    QString exeName = EditorConfig::instance().simExeName();
    QString exePath = m_simPath + "/" + exeName;
    if (!QFile::exists(exePath)) {
        QMessageBox::warning(this, tr("Error"),
                           tr("Simulator executable not found in the specified path."));
        return;
    }

    bool started = QProcess::startDetached(exePath, QStringList(), m_simPath);
    if (started) {
        setStatusMessage(tr("Launched simulator"), 3000);
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Failed to launch simulator."));
    }
}

// ==================== Update Methods ====================

void MainWindow::updateWindowTitle()
{
    QString title = Constants::APP_NAME;

    if (!m_currentProjectPath.isEmpty()) {
        QFileInfo info(m_currentProjectPath);
        title += " - " + info.baseName();
    }

    int currentModule = m_moduleManager->currentModule();
    if (currentModule >= 0) {
        title += " [" + m_moduleManager->moduleName(currentModule) + "]";
    }

    if (!m_undoStack->isClean()) {
        title += " *";
    }

    setWindowTitle(title);
    
    if (m_customTitleBar) {
        m_customTitleBar->setTitle(title);
    }
}

// ==================== Status Bar ====================

void MainWindow::setStatusMessage(const QString& message, int timeoutMs)
{
    m_statusLabel->setText(message);

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this]() {
            if (m_statusLabel->text() != tr("Ready")) {
                m_statusLabel->setText(tr("Ready"));
            }
        });
    }
}

void MainWindow::setProgress(int value, int maximum)
{
    if (value < 0) {
        clearProgress();
        return;
    }

    m_progressBar->setVisible(true);
    m_progressBar->setMaximum(maximum);
    m_progressBar->setValue(value);
}

void MainWindow::clearProgress()
{
    m_progressBar->setVisible(false);
    m_progressBar->setValue(0);
}

// ==================== Build Progress ====================

void MainWindow::onBuildProgress(int percent)
{
    setProgress(percent);
    setStatusMessage(tr("Building... %1%").arg(percent));
}

void MainWindow::onBuildComplete(bool success, const QString& message)
{
    clearProgress();

    if (success) {
        setStatusMessage(tr("Build completed successfully"), 3000);
        QMessageBox::information(this, tr("Build Complete"), message);
    } else {
        setStatusMessage(tr("Build failed"), 5000);
        QMessageBox::warning(this, tr("Build Failed"), message);
    }
}

void MainWindow::performAutoSave()
{
    if (m_currentProjectPath.isEmpty()) return;

    if (!m_undoStack->isClean()) {
        LOG_INFO("MainWindow", "Auto-saving project...");
        saveProjectFile(m_currentProjectPath);
        setStatusMessage(tr("Project auto-saved"), 2000);
    }
}

void MainWindow::saveSessionBackup()
{
    if (m_crashRecovery) {
        m_crashRecovery->saveSession();
    }
}

void MainWindow::createProjectFromTemplate(const QString& templateId)
{
    if (!m_templateManager) return;

    if (!m_templateManager->hasTemplate(templateId)) {
        QMessageBox::warning(this, tr("Template Not Found"),
                           tr("Template '%1' not found.").arg(templateId));
        return;
    }

    QString outputDir = QFileDialog::getExistingDirectory(this, tr("Select Project Directory"));
    if (outputDir.isEmpty()) return;

    QString projectPath = m_templateManager->getProjectPathFromTemplate(templateId, outputDir);
    if (projectPath.isEmpty()) return;

    if (!m_templateManager->initializeTemplateProject(templateId, projectPath)) {
        QMessageBox::warning(this, tr("Error"),
                           tr("Failed to create project from template."));
        return;
    }

    openRecentProject(projectPath);
    setStatusMessage(tr("Project created from template"), 3000);
}

void MainWindow::showTemplateBrowser()
{
    if (!m_templateManager || !m_templateManager->isInitialized()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Template Browser"));
    dlg.setMinimumSize(600, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);

    QListWidget* list = new QListWidget(&dlg);
    QVector<ks::TemplateManager::TemplateInfo> infos = m_templateManager->getAllTemplateInfos();
    for (const auto& info : infos) {
        QListWidgetItem* item = new QListWidgetItem(info.name, list);
        item->setData(Qt::UserRole, info.id);
        item->setToolTip(info.description);
    }
    layout->addWidget(list);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

    if (dlg.exec() == QDialog::Accepted && list->currentItem()) {
        QString templateId = list->currentItem()->data(Qt::UserRole).toString();
        createProjectFromTemplate(templateId);
    }
}

void MainWindow::showFileDiff(const QString& filePath)
{
    if (!m_diffEngine || !m_diffEngine->isInitialized()) return;

    QString backupPath = filePath + ".bak";
    if (!QFile::exists(backupPath)) {
        QMessageBox::information(this, tr("No Backup"),
                               tr("No backup file found for comparison."));
        return;
    }

    ks::FileDiffResult result = m_diffEngine->compareFiles(backupPath, filePath);

    QDialog dlg(this);
    dlg.setWindowTitle(tr("File Diff: %1").arg(QFileInfo(filePath).fileName()));
    dlg.setMinimumSize(700, 500);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QPlainTextEdit* output = new QPlainTextEdit(&dlg);
    output->setReadOnly(true);
    output->setPlainText(m_diffEngine->generateHumanReadableReport({result}));
    layout->addWidget(output);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

void MainWindow::compareProjectFiles()
{
    if (!m_diffEngine || !m_diffEngine->isInitialized()) return;

    QString dir1 = QFileDialog::getExistingDirectory(this, tr("Select First Directory"));
    if (dir1.isEmpty()) return;

    QString dir2 = QFileDialog::getExistingDirectory(this, tr("Select Second Directory"));
    if (dir2.isEmpty()) return;

    QVector<ks::FileDiffResult> diffs = m_diffEngine->compareDirectories(dir1, dir2);

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Directory Comparison"));
    dlg.setMinimumSize(700, 500);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QPlainTextEdit* output = new QPlainTextEdit(&dlg);
    output->setReadOnly(true);
    output->setPlainText(m_diffEngine->generateHumanReadableReport(diffs));
    layout->addWidget(output);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

void MainWindow::saveProjectLayout()
{
    saveWindowLayout();
}

void MainWindow::loadProjectLayout()
{
    loadWindowLayout();
}

void MainWindow::resetProjectLayout()
{
    resetWindowLayout();
}

// ==================== Simulator Path ====================

bool MainWindow::setSimPath(const QString& path)
{
    QDir dir(path);
    if (!dir.exists()) {
        LOG_ERROR("MainWindow", "Invalid simulator path: " + path);
        return false;
    }

    if (!dir.exists(EditorConfig::instance().simExeName())) {
        LOG_ERROR("MainWindow", "Simulator executable not found in: " + path);
        return false;
    }

    m_simPath = path;
    m_simPathLabel->setText(tr("Sim: %1").arg(QFileInfo(path).fileName()));

    m_settings->setValue("ksPath", path);
    emit simPathChanged(path);

    LOG_INFO("MainWindow", "Simulator path set: " + path);

    // Initialize simulator Vulkan Integration using KsPlugin
    if (KsIntegration::initialize(path + "/system")) {
        KsVulkanIntegration* ksVulkan = KsVulkanIntegration::instance();
        if (ksVulkan) {
            if (!ksVulkan->initialize(path + "/system")) {
                LOG_WARNING("MainWindow", "KS Vulkan Integration initialization returned false");
            } else {
                LOG_INFO("MainWindow", "KS Vulkan Integration initialized successfully");
                ksVulkan->applyGraphicsSettings();
                ksVulkan->applyLightingSettings();
            }
        }
    } else {
        LOG_ERROR("MainWindow", "Failed to initialize KS Config system");
    }

    // Re-detect CSP after path is set
    detectCSPVersion();

    return true;
}

void MainWindow::setPaintMode(bool enabled)
{
    m_paintMode = enabled;
    if (!enabled) return;

    // Apply paint theme
    ks::editor::RibbonThemeManager::instance().applyTheme("paint");
    ks::editor::RibbonThemeManager::instance().applyWindowFrame(this, "paint");

    // Apply paint QSS theme (PhotoGIMP-inspired; mirrors ksModeler theme files)
    QFile qssFile(":/ui/styles/paint-dark.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }

// Set window title for paint mode
    setWindowTitle(tr("PaintEditor PhotoGIMP Paint Mode"));

    // Switch to PaintEditor module
    int paintIdx = m_moduleManager->moduleIndex("Paint Editor");
    if (paintIdx >= 0) {
        switchToModule(paintIdx);
    }

    // Show ribbon paint tab (index 6 since we have 6 existing tabs)
    if (m_ribbonBar) {
        m_ribbonBar->applyTheme("paint");
        m_ribbonBar->setCurrentIndex(6);
    }

    // Connect paint tab ribbon buttons to PaintEditorWidget
    connectPaintTabButtons();

    LOG_INFO("MainWindow", "Paint mode activated (PhotoGIMP-inspired)");
}

void MainWindow::connectPaintTabButtons()
{
    // Get the PaintEditorModule and its widget
    ks::PaintEditorModule* paintModule = qobject_cast<ks::PaintEditorModule*>(m_moduleManager->editorModule(m_moduleManager->moduleIndex("Paint Editor")));
    if (!paintModule) return;
    ks::PaintEditorWidget* editorWidget = paintModule->editorWidget();
    if (!editorWidget) return;

    // Connect brush tool buttons
    auto connectTool = [editorWidget](ks::editor::RibbonButton* btn, int brushTypeIndex) {
        if (btn) connect(btn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, [editorWidget, brushTypeIndex](bool) {
            editorWidget->onBrushTypeChanged(brushTypeIndex);
        });
    };

    connectTool(m_paintBrushBtn, 0);      // Brush
    connectTool(m_paintPencilBtn, 1);     // Pencil
    connectTool(m_paintEraserBtn, 2);     // Eraser
    connectTool(m_paintAirbrushBtn, 3);   // Airbrush

    // Connect fill/gradient/clone (map to brush types)
    if (m_paintFillBtn) connect(m_paintFillBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, [editorWidget](bool) {
        editorWidget->onBrushTypeChanged(6); // Bucket Fill
    });
    if (m_paintGradientBtn) connect(m_paintGradientBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, [editorWidget](bool) {
        editorWidget->onBrushTypeChanged(7); // Gradient
    });
    if (m_paintCloneBtn) connect(m_paintCloneBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, [editorWidget](bool) {
        editorWidget->onBrushTypeChanged(8); // Clone
    });

    // Connect color buttons
    if (m_paintFgColorBtn) connect(m_paintFgColorBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onColorSelected);
    if (m_paintBgColorBtn) connect(m_paintBgColorBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onSecondaryColorSelected);
    if (m_paintSwapColorsBtn) connect(m_paintSwapColorsBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onSwapColors);
    if (m_paintDefaultColorsBtn) connect(m_paintDefaultColorsBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onResetColors);

    // Connect selection buttons
    auto connectSelect = [editorWidget](ks::editor::RibbonButton* btn, int selectTypeIndex) {
        if (btn) connect(btn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, [editorWidget, selectTypeIndex](bool) {
            editorWidget->onBrushTypeChanged(selectTypeIndex);
        });
    };
    connectSelect(m_paintRectSelectBtn, 4);    // Rect Select
    connectSelect(m_paintEllipseSelectBtn, 5); // Ellipse Select
    connectSelect(m_paintLassoBtn, 9);         // Free Select (Lasso)
    connectSelect(m_paintMagicWandBtn, 10);    // Fuzzy Select (Magic Wand)

    if (m_paintDeselectBtn) connect(m_paintDeselectBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onSelectNone);
    if (m_paintInvertBtn) connect(m_paintInvertBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onSelectInvert);

    // Connect view buttons
    if (m_paintZoomInBtn) connect(m_paintZoomInBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onZoomIn);
    if (m_paintZoomOutBtn) connect(m_paintZoomOutBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onZoomOut);
    if (m_paintFitBtn) connect(m_paintFitBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onZoomFit);
    if (m_paintZoomToolBtn) connect(m_paintZoomToolBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onZoomTool);

    // Connect layer buttons
    if (m_paintNewLayerBtn) connect(m_paintNewLayerBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onAddLayer);
    if (m_paintDupLayerBtn) connect(m_paintDupLayerBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onDuplicateSkin);
    if (m_paintMergeLayerBtn) connect(m_paintMergeLayerBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onMergeLayerDown);
    if (m_paintFlattenBtn) connect(m_paintFlattenBtn, QOverload<bool>::of(&ks::editor::RibbonButton::clicked), editorWidget, &ks::PaintEditorWidget::onFlattenImage);
}

void MainWindow::setCSPVersion(const QString& version)
{
    m_cspVersion = version;
    m_cspVersionLabel->setText(tr("CSP: %1").arg(version));
    emit cspVersionChanged(version);
}

// ==================== Protected Methods ====================

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Cancel any running build
    if (m_projectBuilder && m_projectBuilder->isRunning()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("Build in Progress"),
            tr("A build is currently in progress. Cancel and exit?"),
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply == QMessageBox::Yes) {
            m_projectBuilder->cancel();
        } else {
            event->ignore();
            return;
        }
    }

    if (!promptForUnsavedChanges()) {
        event->ignore();
        return;
    }

    // Save window state
    QSettings settings(Constants::APP_NAME, Constants::APP_NAME);
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // Ctrl+Tab for module switching
    if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_Tab) {
        int next = (m_moduleManager->currentModule() + 1) % m_moduleManager->moduleCount();
        switchToModule(next);
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

bool MainWindow::event(QEvent* event)
{
    if (event->type() == QEvent::EnterWhatsThisMode) {
        // Show help browser when ? button is clicked
        if (m_helpBrowser) {
            m_helpBrowser->show();
            m_helpBrowser->raise();
            m_helpBrowser->activateWindow();
        } else {
            ks::HelpSystem::instance()->showHelp();
        }
        return true;
    }
    return QMainWindow::event(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (!mimeData->hasUrls()) {
        return;
    }

    QList<QUrl> urlList = mimeData->urls();

    for (const QUrl& url : urlList) {
        QString filePath = url.toLocalFile();
        QFileInfo fi(filePath);
        QString suffix = fi.suffix().toLower();

        if (suffix == "ksep") {
            openRecentProject(filePath);
        } else if (suffix == "fbx" || suffix == "kn5") {
            if (!hasOpenProject()) {
                QMessageBox::warning(this, tr("No Project"),
                                   tr("Please open or create a project before importing files."));
                return;
            }
            m_moduleManager->importFile(filePath);
        } else if (suffix == "png" || suffix == "jpg" || suffix == "jpeg" ||
                   suffix == "bmp" || suffix == "tga" || suffix == "tiff") {
            QMessageBox::warning(this, tr("Unsupported Format"),
                               tr("Cannot read \"%1\" - this editor does not support image input.")
                               .arg(fi.fileName()));
        } else {
            QMessageBox::warning(this, tr("Unsupported Format"),
                               tr("Cannot read \"%1\" - unsupported file format.")
                               .arg(fi.fileName()));
        }
    }

    event->acceptProposedAction();
}
