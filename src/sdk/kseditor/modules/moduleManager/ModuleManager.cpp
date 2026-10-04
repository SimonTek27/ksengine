#include "ModuleManager.h"
#include "editor/EditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include "modellingEditor/3DModeling_Module.h"
#include "PhysicsEditor/PhysicsEditor.h"
#include "assetsLibraryEditor/AssetsLibraryModule.h"
#include "workshop/WorkshopEditorQmlBridge.h"
#include "workshop/WorkshopEditorModule.h"
#include "modmanager/ModManager.h"
#include "LicensePlatesEditor/LicensePlateEditorModule.h"
#include "modmanager/ContentRepair.h"
#include "tools/FormatToolsQmlBridge.h"
#include "editor/FfbEditor/FfbEditorQmlBridge.h"
#include "ShowroomEditor/ShowroomEditorModule.h"
#include "PaintEditor/PaintEditorModule.h"
#include "editor/AIEditor/AIEditorModule.h"
#include "editor/eventEditor/championshipEditor/ChampionshipEditorModule.h"
#include "editor/textEditor/TextEditorModule.h"
#include "PhysicsEditor/SetupEditor/SetupEditorQmlBridge.h"
#include "PhysicsEditor/telemetry/TelemetryViewerQmlBridge.h"
#include "engine/physics/WeatherPhysics.h"
#include "modellingEditor/TrackBuilder/TrackSurfaceEditorModule.h"
#include "modellingEditor/TrackBuilder/TrackMapEditorModule.h"
#include "modellingEditor/TrackBuilder/TrackLightingEditorModule.h"
#include "modellingEditor/TrackBuilder/DRSZoneEditorModule.h"
#include "PaintEditor/SkinIniEditorModule.h"
#include "PaintEditor/GUISkinEditorModule.h"
#include "sound/editor/SoundEditorModule.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/FormatToolsEditorModule.h"
#include "luaScriptEditor/LuaScriptEditorModule.h"
#include "editor/eventEditor/specialEventsEditor/SpecialEventsEditorModule.h"
#include "editor/eventEditor/raceConfigEditor/RaceConfigEditorModule.h"
#include "editor/eventEditor/careerEditor/CareerEditorModule.h"
#include "ShowroomEditor/ShowroomPPEditorModule.h"
#include "modellingEditor/TrackBuilder/cameratrackEditor/TrackCameraEditorModule.h"
#include "modellingEditor/CarBuilder/cameracarEditor/CameraEditorModule.h"
#include "modellingEditor/CharacterBuilder/DriverEditorModule.h"
#include "configEditor/CspConfigEditorModule.h"
#include "ideeditor/IdeEditorModule.h"
#include "fontEditor/FontCreatorQmlBridge.h"
#include "fontEditor/FontCreatorEditorModule.h"
#include "cockpitInstruments/CockpitInstrumentsModule.h"
#include "editor/ppfiltersEditor/PPFiltersQmlBridge.h"
#include "editor/ServerConfigEditor/ServerConfigEditorModule.h"
#include "editor/documentpad/DocumentPadModule.h"
#include "editor/sheetpad/SheetPadModule.h"
#include "vcs/VcsEditorModule.h"
#include "archiveEditor/ArchiveEditorModule.h"
#include "vrEditor/VREditorModule.h"
#include "animationEditor/AnimationEditorModule.h"
#include "sequencerEditor/SequencerEditorModule.h"
#include "blueprintEditor/BlueprintEditorModule.h"
#include "threeDPrintEditor/ThreeDPrintEditorModule.h"
#include "audioEditor/AudioEditorModule.h"
#include "materialEditor/MaterialEditorModule.h"
#include "networkEditor/NetworkEditorModule.h"
#include "meshEditor/MeshEditorModule.h"
#include "graphicsEditor/GraphicsEditorModule.h"
#include "assetEditor/AssetEditorModule.h"
#include "fileFormatEditor/FileFormatEditorModule.h"
#include "help/HelpEditorModule.h"
#include "modmanager/ModManagerEditorModule.h"
#include "systemEditor/SystemEditorModule.h"
#include "tools/ToolsEditorModule.h"
#include "editor/DocumentPrinter.h"
#include "editor/DocumentScanner.h"
#include "resources/ui/UIEditorModule.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <algorithm>

ModuleManager::ModuleManager(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    loadModules();

    LOG_INFO("ModuleManager", QString("Loaded %1 modules").arg(m_modules.size()));
}

ModuleManager::~ModuleManager()
{
    // Cleanup modules
    qDeleteAll(m_modules);
}

void ModuleManager::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stackedWidget = new QStackedWidget(this);
    layout->addWidget(m_stackedWidget);

    setLayout(layout);
}

void ModuleManager::loadModules()
{
    // ── Real modules ────────────────────────────────────────────────
    registerModule(new ks::ContentRepairModule(this));
    registerModule(new ks::KSModelerModule(this));
    registerModule(new ks::PhysicsEditorModule(this));
    registerModule(new ks::AssetsLibraryModule(this));
    registerModule(new ks::WorkshopEditorModule(this));
    registerModule(new ks::AIEditorModule(this));
    registerModule(new ks::ModManagerModule(this));
    registerModule(new ks::LicensePlateEditorModule(this));
    registerModule(new ks::FormatToolsModule(this));
    registerModule(new ks::FfbEditorModule(this));
    registerModule(new ks::ShowroomEditorModule(this));
    registerModule(new ks::PaintEditorModule(this));
    registerModule(new ks::ChampionshipEditorModule(this));
    registerModule(new ks::TextEditorModule(this));
    registerModule(new ks::SetupEditorModule(this));
    registerModule(new ks::TelemetryViewerModule(this));
    // WeatherModule excluded (old WeatherConfig API)
    registerModule(new ks::TrackSurfaceEditorModule(this));
    registerModule(new ks::TrackMapEditorModule(this));
    registerModule(new ks::TrackLightingEditorModule(this));
    registerModule(new ks::DRSZoneEditorModule(this));
    registerModule(new ks::SkinIniEditorModule(this));
    registerModule(new ks::GUISkinEditorModule(this));
    registerModule(new ks::TrackCameraEditorModule(this));
    registerModule(new ks::CameraEditorModule(this));
    registerModule(new ks::DriverEditorModule(this));
    registerModule(new ks::SoundEditorModule(this));
    registerModule(new ks::FormatToolsEditorModule(this));
    registerModule(new ks::LuaScriptEditorModule(this));
    registerModule(new ks::SpecialEventsEditorModule(this));
    registerModule(new ks::RaceConfigEditorModule(this));
    registerModule(new ks::CspConfigEditorModule(this));
    registerModule(new ks::FontCreatorEditorModule(this));
    registerModule(new ks::IdeEditorModule(this));
    registerModule(new ks::CockpitInstrumentsModule(this));
    registerModule(new ks::DocumentPadModule(this));
    registerModule(new ks::SheetPadModule(this));
    registerModule(new ks::PPFiltersEditorModule(this));
    registerModule(new ks::CareerEditorModule(this));
    registerModule(new ks::ServerConfigEditorModule(this));
    registerModule(new ks::ShowroomPPEditorModule(this));
    registerModule(new ks::VcsEditorModule(this));
    registerModule(new ks::ArchiveEditorModule(this));
    registerModule(new ks::VREditorModule(this));
    registerModule(new ks::AnimationEditorModule(this));
    registerModule(new ks::SequencerEditorModule(this));
    registerModule(new ks::blueprint::BlueprintEditorModule(this));
    registerModule(new ks::device::ThreeDPrintEditorModule(this));
    registerModule(new ks::audio::AudioEditorModule(this));
    registerModule(new ks::graphics::MaterialEditorModule(this));
    registerModule(new ks::network::NetworkEditorModule(this));
    registerModule(new ks::graphics::MeshEditorModule(this));
    registerModule(new ks::GraphicsEditorModule(this));
    registerModule(new ks::assets::AssetEditorModule(this));
    registerModule(new ks::FileFormatEditorModule(this));
    registerModule(new ks::help::HelpEditorModule(this));
    registerModule(new ks::modmanager::ModManagerEditorModule(this));
    registerModule(new ks::sys::SystemEditorModule(this));
    registerModule(new ks::tools::ToolsEditorModule(this));
    registerModule(new ks::ui::UIEditorModule(this));
#if HAS_QPRINTER
    registerModule(new ks::DocumentPrinterModule(this));
#endif
#if 0 // DocumentScannerModule disabled
    registerModule(new ks::DocumentScannerModule(this));
#endif

    // Sort modules by priority (higher priority first)
    std::sort(m_modules.begin(), m_modules.end(), [](ks::EditorModule* a, ks::EditorModule* b) {
        return a->getModulePriority() > b->getModulePriority();
    });

    // Rebuild stacked widget in sorted order
    while (m_stackedWidget->count() > 0) {
        m_stackedWidget->removeWidget(m_stackedWidget->widget(0));
    }
    for (auto* mod : m_modules) {
        m_stackedWidget->addWidget(mod);
    }
}

QString ModuleManager::moduleName(int index) const
{
    if (index >= 0 && index < m_modules.size()) {
        QString name = m_modules[index]->moduleName();
        if (name.isEmpty()) name = m_modules[index]->getModuleName();
        return name;
    }
    return QString();
}

int ModuleManager::moduleIndex(const QString& name) const
{
    for (int i = 0; i < m_modules.size(); ++i) {
        QString modName = m_modules[i]->moduleName();
        if (modName.isEmpty()) modName = m_modules[i]->getModuleName();
        if (modName == name) {
            return i;
        }
    }
    return -1;
}

ks::EditorModule* ModuleManager::currentEditorModule() const
{
    return editorModule(currentModule());
}

ks::EditorModule* ModuleManager::editorModule(int index) const
{
    if (index >= 0 && index < m_modules.size()) {
        return m_modules[index];
    }
    return nullptr;
}

void ModuleManager::setCurrentModule(int index)
{
    if (index < 0 || index >= m_stackedWidget->count()) {
        return;
    }

    int current = currentModule();
    if (current != index) {
        emit moduleAboutToChange(current, index);

        // Deactivate old module
        if (current >= 0 && current < m_modules.size()) {
            m_modules[current]->onDeactivation();
        }

        m_stackedWidget->setCurrentIndex(index);

        // Activate new module
        if (index >= 0 && index < m_modules.size()) {
            m_modules[index]->onActivation();
        }

        emit moduleChanged(index);

        LOG_INFO("ModuleManager", QString("Switched to module: %1").arg(moduleName(index)));
    }
}

void ModuleManager::setCurrentModule(const QString& name)
{
    int index = moduleIndex(name);
    if (index >= 0) {
        setCurrentModule(index);
    }
}

void ModuleManager::registerModule(ks::EditorModule* module)
{
    if (module && !m_modules.contains(module)) {
        m_modules.append(module);
        m_stackedWidget->addWidget(module);
    }
}

void ModuleManager::unregisterModule(ks::EditorModule* module)
{
    if (module) {
        int index = m_modules.indexOf(module);
        if (index >= 0) {
            m_stackedWidget->removeWidget(module);
            m_modules.removeAt(index);
        }
    }
}

void ModuleManager::importFile(const QString& filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif") {
        qWarning() << "Cannot import image file as model:" << filePath;
        return;
    }

    ks::EditorModule* current = currentEditorModule();
    if (current) {
        // Delegate to the current module if it supports import
        current->importFile(filePath);
        emit fileImported(filePath);
    }
}

void ModuleManager::exportFile(const QString& filePath)
{
    ks::EditorModule* current = currentEditorModule();
    if (current) {
        current->exportFile(filePath);
        emit fileExported(filePath);
    }
}

void ModuleManager::newProject(const QString& name, const QString& path)
{
    ks::EditorModule* current = currentEditorModule();
    if (current) {
        current->newProject(name, path);
        emit projectCreated(name, path);
    }
}

void ModuleManager::openProject(const QString& projectPath)
{
    ks::EditorModule* current = currentEditorModule();
    if (current) {
        current->openProject(projectPath);
        emit projectOpened(projectPath);
    }
}

void ModuleManager::saveProject()
{
    ks::EditorModule* current = currentEditorModule();
    if (current) {
        current->saveProject();
    }
}

bool ModuleManager::canCut() const
{
    ks::EditorModule* current = currentEditorModule();
    return current ? current->canCut() : false;
}

bool ModuleManager::canCopy() const
{
    ks::EditorModule* current = currentEditorModule();
    return current ? current->canCopy() : false;
}

bool ModuleManager::canPaste() const
{
    ks::EditorModule* current = currentEditorModule();
    return current ? current->canPaste() : false;
}

bool ModuleManager::canDelete() const
{
    ks::EditorModule* current = currentEditorModule();
    return current ? current->canDelete() : false;
}

void ModuleManager::buildCurrentProject()
{
    ks::EditorModule* current = currentEditorModule();
    if (current) {
        current->saveProject();
    }
    qWarning() << "ModuleManager::buildCurrentProject() is deprecated. Use MainWindow::buildProject() instead.";
}
