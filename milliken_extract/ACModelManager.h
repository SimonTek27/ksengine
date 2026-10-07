#pragma once

/**
 * @file ACModelManager.h
 * @brief Lifetime / ownership of AC physics model instances — Qt-free
 */

#include <memory>
#include <string>
#include <unordered_map>

class KsTireModel;
class EngineModel;
class AeroModel;
class DifferentialModel;
class SuspensionModel;
class BrakeThermalModel;

namespace ks {
namespace physics {

class HybridSystem;

class ACModelManager {
public:
    ACModelManager();
    ~ACModelManager();

    ACModelManager(const ACModelManager&) = delete;
    ACModelManager& operator=(const ACModelManager&) = delete;
    ACModelManager(ACModelManager&&) = default;
    ACModelManager& operator=(ACModelManager&&) = default;

    KsTireModel* tireModel();
    EngineModel* engineModel();
    AeroModel* aeroModel();
    DifferentialModel* differentialModel();
    SuspensionModel* suspensionModel();
    BrakeThermalModel* brakeModel();
    HybridSystem* hybridSystem();

    const KsTireModel* tireModel() const;
    const EngineModel* engineModel() const;
    const AeroModel* aeroModel() const;
    const DifferentialModel* differentialModel() const;
    const SuspensionModel* suspensionModel() const;
    const BrakeThermalModel* brakeModel() const;
    const HybridSystem* hybridSystem() const;

    bool areModelsLoaded() const { return m_modelsLoaded; }

    /** Resolve optional *.ini under basePath; returns true if at least one file was found. */
    bool loadModels(const std::string& basePath);

    void reset();
    void clear();

private:
    std::unique_ptr<KsTireModel> m_tireModel;
    std::unique_ptr<EngineModel> m_engineModel;
    std::unique_ptr<AeroModel> m_aeroModel;
    std::unique_ptr<DifferentialModel> m_differentialModel;
    std::unique_ptr<SuspensionModel> m_suspensionModel;
    std::unique_ptr<BrakeThermalModel> m_brakeModel;
    std::unique_ptr<HybridSystem> m_hybridSystem;
    bool m_modelsLoaded = false;
};

class ACModelRegistry {
public:
    static ACModelRegistry& instance();

    void registerManager(const std::string& name, ACModelManager* manager);
    void unregisterManager(const std::string& name);
    ACModelManager* getManager(const std::string& name);
    ACModelManager* defaultManager();
    void setDefaultManager(const std::string& name);
    void clear();

private:
    ACModelRegistry() = default;
    std::unordered_map<std::string, ACModelManager*> m_managers;
    std::string m_defaultManagerName;
};

} // namespace physics
} // namespace ks
