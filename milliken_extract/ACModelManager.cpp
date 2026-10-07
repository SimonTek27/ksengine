#include "ACModelManager.h"
#include "KsTireModel.h"
#include "EngineModel.h"
#include "AeroModel.h"
#include "DifferentialModel.h"
#include "SuspensionModel.h"
#include "BrakeThermalModel.h"
#include "HybridSystem.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>

namespace ks {
namespace physics {

namespace {
bool fileExists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
std::string joinPath(const std::string& base, const char* file) {
    if (base.empty()) return file;
    if (base.back() == '/' || base.back() == '\\') return base + file;
    return base + "/" + file;
}
} // namespace

ACModelManager::ACModelManager() {
    m_tireModel = std::make_unique<KsTireModel>();
    m_engineModel = std::make_unique<EngineModel>();
    m_aeroModel = std::make_unique<AeroModel>();
    m_differentialModel = std::make_unique<DifferentialModel>();
    m_suspensionModel = std::make_unique<SuspensionModel>();
    m_brakeModel = std::make_unique<BrakeThermalModel>();
    m_hybridSystem = std::make_unique<HybridSystem>();
}

ACModelManager::~ACModelManager() = default;

KsTireModel* ACModelManager::tireModel() { return m_tireModel.get(); }
EngineModel* ACModelManager::engineModel() { return m_engineModel.get(); }
AeroModel* ACModelManager::aeroModel() { return m_aeroModel.get(); }
DifferentialModel* ACModelManager::differentialModel() { return m_differentialModel.get(); }
SuspensionModel* ACModelManager::suspensionModel() { return m_suspensionModel.get(); }
BrakeThermalModel* ACModelManager::brakeModel() { return m_brakeModel.get(); }
HybridSystem* ACModelManager::hybridSystem() { return m_hybridSystem.get(); }

const KsTireModel* ACModelManager::tireModel() const { return m_tireModel.get(); }
const EngineModel* ACModelManager::engineModel() const { return m_engineModel.get(); }
const AeroModel* ACModelManager::aeroModel() const { return m_aeroModel.get(); }
const DifferentialModel* ACModelManager::differentialModel() const { return m_differentialModel.get(); }
const SuspensionModel* ACModelManager::suspensionModel() const { return m_suspensionModel.get(); }
const BrakeThermalModel* ACModelManager::brakeModel() const { return m_brakeModel.get(); }
const HybridSystem* ACModelManager::hybridSystem() const { return m_hybridSystem.get(); }

bool ACModelManager::loadModels(const std::string& basePath) {
    auto resolve = [&](const char* name) { return joinPath(basePath, name); };
    bool any = false;
    if (fileExists(resolve("tyres.ini")) || fileExists(resolve("tires.ini"))) any = true;
    if (fileExists(resolve("engine.ini"))) any = true;
    if (fileExists(resolve("aero.ini"))) any = true;
    if (fileExists(resolve("drivetrain.ini"))) any = true;
    if (fileExists(resolve("suspension.ini"))) any = true;
    if (fileExists(resolve("brakes.ini"))) any = true;
    m_modelsLoaded = any;
    if (any)
        std::fprintf(stderr, "ACModelManager: models present under %s\n", basePath.c_str());
    return any;
}

void ACModelManager::reset() {
    // Models keep defaults; hybrid energy full
    if (m_hybridSystem) m_hybridSystem->reset();
    m_modelsLoaded = false;
}

void ACModelManager::clear() {
    m_tireModel.reset();
    m_engineModel.reset();
    m_aeroModel.reset();
    m_differentialModel.reset();
    m_suspensionModel.reset();
    m_brakeModel.reset();
    m_hybridSystem.reset();
    m_modelsLoaded = false;
}

ACModelRegistry& ACModelRegistry::instance() {
    static ACModelRegistry r;
    return r;
}

void ACModelRegistry::registerManager(const std::string& name, ACModelManager* manager) {
    m_managers[name] = manager;
    if (m_defaultManagerName.empty()) m_defaultManagerName = name;
}

void ACModelRegistry::unregisterManager(const std::string& name) {
    m_managers.erase(name);
    if (m_defaultManagerName == name)
        m_defaultManagerName = m_managers.empty() ? std::string() : m_managers.begin()->first;
}

ACModelManager* ACModelRegistry::getManager(const std::string& name) {
    auto it = m_managers.find(name);
    return it == m_managers.end() ? nullptr : it->second;
}

ACModelManager* ACModelRegistry::defaultManager() {
    return getManager(m_defaultManagerName);
}

void ACModelRegistry::setDefaultManager(const std::string& name) {
    m_defaultManagerName = name;
}

void ACModelRegistry::clear() {
    m_managers.clear();
    m_defaultManagerName.clear();
}

} // namespace physics
} // namespace ks
