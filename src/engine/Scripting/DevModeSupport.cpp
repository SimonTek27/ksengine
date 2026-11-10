#include "DevModeSupport.h"
#include "../sys/LogManager.h"

namespace ks {
namespace engine {
namespace scripting {

bool DevModeSupport::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Initializing Dev Mode Support (STUB)");
	logger.info("DevModeSupport", "Missing: Scripting, ModuleLoader, AppPause, VirtualTyres, TimeLimit");
	return true;
}

void DevModeSupport::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Shutting down Dev Mode Support");
	m_devModeActive = false;
}

bool DevModeSupport::enableDevMode(const std::string& password) {
	auto& logger = sys::LogManager::instance();
	logger.warn("DevModeSupport", "Dev Mode enabled (STUB - password check disabled)");
	m_devModeActive = true;
	return true;
}

void DevModeSupport::disableDevMode() {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Dev Mode disabled");
	m_devModeActive = false;
}

bool DevModeSupport::loadScript(const std::string& scriptPath) {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Loading script: " + scriptPath + " (STUB)");
	return false;
}

bool DevModeSupport::executeScript(const std::string& scriptCode) {
	auto& logger = sys::LogManager::instance();
	logger.debug("DevModeSupport", "Executing script (STUB)");
	return false;
}

bool DevModeSupport::registerScriptCallback(const std::string& name, const std::function<void()>& callback) {
	m_scriptCallbacks[name] = callback;
	return true;
}

bool DevModeSupport::loadModule(const std::string& modulePath) {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Loading module: " + modulePath + " (STUB)");
	return false;
}

bool DevModeSupport::unloadModule(const std::string& moduleName) {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Unloading module: " + moduleName);
	return false;
}

bool DevModeSupport::reloadModule(const std::string& moduleName) {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Reloading module: " + moduleName  + " (STUB)");
	return false;
}

void DevModeSupport::setPauseOnBackgroundFocus(bool pause) {
	m_pauseOnBackground = pause;
}

void DevModeSupport::setVirtualTyreMode(bool enabled) {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Virtual tyre mode: " + std::string(enabled ? "ON" : "OFF") + " (STUB)");
	m_virtualTyresEnabled = enabled;
}

bool DevModeSupport::isVirtualTyreModeActive() const {
	return m_virtualTyresEnabled;
}

void DevModeSupport::setSessionTimeLimit(float seconds) {
	auto& logger = sys::LogManager::instance();
	m_sessionTimeLimit = seconds;
	logger.info("DevModeSupport", "Session time limit: " + std::to_string(seconds) + "s");
}

void DevModeSupport::enableAutoSessionEnd(bool enable) {
	auto& logger = sys::LogManager::instance();
	m_autoSessionEnd = enable;
	logger.info("DevModeSupport", "Auto session end: " + std::string(enable ? "ON" : "OFF"));
}

void DevModeSupport::logEngineDiagnostics() {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Logging engine diagnostics (STUB)");
}

void DevModeSupport::dumpMemoryUsage() {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Memory usage dump (STUB)");
}

void DevModeSupport::profileNextFrame() {
	auto& logger = sys::LogManager::instance();
	logger.info("DevModeSupport", "Profiling next frame (STUB)");
}

} // namespace scripting
} // namespace engine
} // namespace ks
