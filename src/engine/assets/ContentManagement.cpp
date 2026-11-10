#include "ContentManagement.h"
#include "../sys/LogManager.h"
#include <filesystem>
#include <fstream>

namespace ks {
namespace engine {
namespace content {

namespace fs = std::filesystem;

bool ContentManagement::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("ContentManagement", "Initializing Content Management");
	return true;
}

void ContentManagement::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("ContentManagement", "Shutting down Content Management");
}

std::vector<SetupEntry> ContentManagement::getAvailableSetups(const std::string& carModel) {
	std::vector<SetupEntry> out;
	fs::path dir = fs::path("content") / "cars" / carModel / "setups";
	std::error_code ec;
	if (!fs::exists(dir, ec)) return out;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (!entry.is_regular_file()) continue;
		SetupEntry s;
		s.name = entry.path().stem().string();
		s.carModel = carModel;
		s.filePath = entry.path().string();
		s.isLocked = false;
		out.push_back(s);
	}
	return out;
}

bool ContentManagement::loadSetup(const std::string& setupName) {
	auto& logger = sys::LogManager::instance();
	logger.info("ContentManagement", "Loading setup: " + setupName);
	if (onSetupLoaded) onSetupLoaded(setupName);
	return true;
}

bool ContentManagement::saveSetup(const std::string& setupName) {
	auto& logger = sys::LogManager::instance();
	logger.info("ContentManagement", "Saving setup: " + setupName);
	return true;
}

void ContentManagement::setDriverSuit(const std::string& suitPath) {
	m_customization.suitPath = suitPath;
	if (onCustomizationChanged) onCustomizationChanged(suitPath);
}

void ContentManagement::setDriverHelmet(const std::string& helmetPath) {
	m_customization.helmetPath = helmetPath;
	if (onCustomizationChanged) onCustomizationChanged(helmetPath);
}

void ContentManagement::applyCarSkin(const std::string& skinPath) {
	m_customization.skinPaths.push_back(skinPath);
	if (onCustomizationChanged) onCustomizationChanged(skinPath);
}

bool ContentManagement::loadMaterialMap(const std::string& mapPath) {
	std::ifstream in(mapPath);
	if (!in) return false;
	return true;
}

bool ContentManagement::saveMaterialMap(const std::string& mapPath) {
	std::ofstream out(mapPath);
	if (!out) return false;
	return true;
}

std::vector<std::string> ContentManagement::getAvailableSuits() const {
	std::vector<std::string> out;
	fs::path dir = "content/drivers/suits";
	std::error_code ec;
	if (!fs::exists(dir, ec)) return out;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (entry.is_directory()) out.push_back(entry.path().filename().string());
	}
	return out;
}

std::vector<std::string> ContentManagement::getAvailableHelmets() const {
	std::vector<std::string> out;
	fs::path dir = "content/drivers/helmets";
	std::error_code ec;
	if (!fs::exists(dir, ec)) return out;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (entry.is_directory()) out.push_back(entry.path().filename().string());
	}
	return out;
}

std::vector<std::string> ContentManagement::getAvailableSkins(const std::string& carModel) const {
	std::vector<std::string> out;
	fs::path dir = fs::path("content") / "cars" / carModel / "skins";
	std::error_code ec;
	if (!fs::exists(dir, ec)) return out;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (entry.is_directory()) out.push_back(entry.path().filename().string());
	}
	return out;
}

} // namespace content
} // namespace engine
} // namespace ks