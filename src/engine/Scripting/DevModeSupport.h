#pragma once
#include "../EngineModule.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace ks {
namespace engine {
namespace scripting {

/* DevModeSupport - Development Mode Features
 * Status: STUB - Scripting, Module Loader, App Pause Policy, Virtual Tyres, Time Limits
 */

class DevModeSupport : public EngineModule {
public:
	static DevModeSupport& instance() {
		static DevModeSupport s;
		return s;
	}

	std::string moduleName() const override { return "DevModeSupport"; }
	std::string moduleId() const override { return "ks.devmode"; }
	bool initialize() override;
	void shutdown() override;

	// Dev mode control
	bool enableDevMode(const std::string& password = "");
	bool isDevModeActive() const { return m_devModeActive; }
	void disableDevMode();

	// Scripting (Lua/Python)
	bool loadScript(const std::string& scriptPath);
	bool executeScript(const std::string& scriptCode);
	bool registerScriptCallback(const std::string& name, const std::function<void()>& callback);

	// Module loader
	bool loadModule(const std::string& modulePath);
	bool unloadModule(const std::string& moduleName);
	bool reloadModule(const std::string& moduleName);

	// App pause policy
	void setPauseOnBackgroundFocus(bool pause);
	bool shouldPauseOnBackground() const { return m_pauseOnBackground; }

	// Virtual tyres (debug)
	void setVirtualTyreMode(bool enabled);
	bool isVirtualTyreModeActive() const;

	// Time-limited sessions
	void setSessionTimeLimit(float seconds);
	void enableAutoSessionEnd(bool enable);

	// Debug features
	void logEngineDiagnostics();
	void dumpMemoryUsage();
	void profileNextFrame();

private:
	DevModeSupport() = default;
	bool m_devModeActive = false;
	bool m_pauseOnBackground = false;
	bool m_virtualTyresEnabled = false;
	float m_sessionTimeLimit = 0.0f;
	bool m_autoSessionEnd = false;
	std::unordered_map<std::string, std::function<void()>> m_scriptCallbacks;
};

} // namespace scripting
} // namespace engine
} // namespace ks
