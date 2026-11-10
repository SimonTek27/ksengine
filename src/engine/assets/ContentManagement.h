#pragma once
#include "../EngineModule.h"
#include <string>
#include <vector>
#include <functional>

namespace ks {
namespace engine {
namespace content {

/* ContentManagement - Unified Content System
 * Status: STUB - Combines: SetupCatalog, DriverSuits, Helmets, Skins, MaterialMapping
 */

struct SetupEntry {
	std::string name;
	std::string carModel;
	std::string filePath;
	bool isLocked;
};

struct DriverCustomization {
	std::string suitPath;
	std::string helmetPath;
	std::vector<std::string> skinPaths;
};

class ContentManagement : public EngineModule {
public:
	static ContentManagement& instance() {
		static ContentManagement s;
		return s;
	}

	std::string moduleName() const override { return "ContentManagement"; }
	std::string moduleId() const override { return "ks.content"; }
	bool initialize() override;
	void shutdown() override;

	// SetupCatalog
	std::vector<SetupEntry> getAvailableSetups(const std::string& carModel);
	bool loadSetup(const std::string& setupName);
	bool saveSetup(const std::string& setupName);

	// Driver Customization
	void setDriverSuit(const std::string& suitPath);
	void setDriverHelmet(const std::string& helmetPath);
	void applyCarSkin(const std::string& skinPath);

	// Material Mapping  
	bool loadMaterialMap(const std::string& mapPath);
	bool saveMaterialMap(const std::string& mapPath);

	// Content discovery
	std::vector<std::string> getAvailableSuits() const;
	std::vector<std::string> getAvailableHelmets() const;
	std::vector<std::string> getAvailableSkins(const std::string& carModel) const;

	std::function<void(const std::string&)> onSetupLoaded;
	std::function<void(const std::string&)> onCustomizationChanged;

private:
	ContentManagement() = default;
	DriverCustomization m_customization;
};

} // namespace content
} // namespace engine
} // namespace ks
