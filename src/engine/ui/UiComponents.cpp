#include "UiComponents.h"
#include "../sys/LogManager.h"

namespace ks {
namespace engine {
namespace ui {

bool UiComponents::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("UiComponents", "Initializing UI Components module (STUB)");
	logger.info("UiComponents", "Missing: Splash screen, Credits list, Core switches");
	return true;
}

void UiComponents::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("UiComponents", "Shutting down UI Components module");
}

void UiComponents::showSplash(const std::string& imagePath) {
	auto& logger = sys::LogManager::instance();
	logger.info("UiComponents", "Showing splash screen: " + imagePath + " (STUB)");
	m_splashVisible = true;
}

void UiComponents::hideSplash() {
	m_splashVisible = false;
}

bool UiComponents::isSplashVisible() const {
	return m_splashVisible;
}

void UiComponents::showCredits() {
	auto& logger = sys::LogManager::instance();
	logger.info("UiComponents", "Showing credits (STUB)");
	m_creditsShowing = true;
}

void UiComponents::hideCredits() {
	m_creditsShowing = false;
}

bool UiComponents::areCreditsShowing() const {
	return m_creditsShowing;
}

void UiComponents::setCreditsText(const std::string& creditsFile) {
	auto& logger = sys::LogManager::instance();
	logger.info("UiComponents", "Loading credits from: " + creditsFile + " (STUB)");
}

void UiComponents::showCoreSwitches() {
	auto& logger = sys::LogManager::instance();
	logger.info("UiComponents", "Showing core switches UI (STUB)");
}

void UiComponents::toggleCoreSwitch(const std::string& switchName) {
	auto& logger = sys::LogManager::instance();
	logger.debug("UiComponents", "Toggle core switch: " + switchName);
}

bool UiComponents::getCoreSwitch(const std::string& switchName) const {
	return false;
}

} // namespace ui
} // namespace engine
} // namespace ks
