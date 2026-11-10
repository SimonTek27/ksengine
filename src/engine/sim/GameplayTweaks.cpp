#include "GameplayTweaks.h"
#include "../sys/LogManager.h"

namespace ks {
namespace engine {
namespace sim {

bool GameplayTweaks::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("GameplayTweaks", "Initializing Gameplay Tweaks module (STUB)");
	resetToDefaults();
	return true;
}

void GameplayTweaks::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("GameplayTweaks", "Shutting down Gameplay Tweaks module");
}

void GameplayTweaks::setTweakSet(const GameplayTweakSet& tweaks) {
	m_tweaks = tweaks;
	if (onTweaksChanged) onTweaksChanged(tweaks);
}

void GameplayTweaks::resetToDefaults() {
	m_tweaks = GameplayTweakSet{};
}

} // namespace sim
} // namespace engine
} // namespace ks
