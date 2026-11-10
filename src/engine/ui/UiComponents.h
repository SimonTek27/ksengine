#pragma once
#include "../EngineModule.h"
#include <string>

namespace ks {
namespace engine {
namespace ui {

/* UiComponents - Splash Screen, Credits, Core Switches
 * Status: STUB - Missing UI elements
 */

class UiComponents : public EngineModule {
public:
	static UiComponents& instance() {
		static UiComponents s;
		return s;
	}

	std::string moduleName() const override { return "UiComponents"; }
	std::string moduleId() const override { return "ks.ui.components"; }
	bool initialize() override;
	void shutdown() override;

	// Splash screen
	void showSplash(const std::string& imagePath);
	void hideSplash();
	bool isSplashVisible() const;

	// Credits
	void showCredits();
	void hideCredits();
	bool areCreditsShowing() const;
	void setCreditsText(const std::string& creditsFile);

	// Developer mode
	void showCoreSwitches();
	void toggleCoreSwitch(const std::string& switchName);
	bool getCoreSwitch(const std::string& switchName) const;

private:
	UiComponents() = default;
	bool m_splashVisible = false;
	bool m_creditsShowing = false;
};

} // namespace ui
} // namespace engine
} // namespace ks
