#include "GameplayTweaks.h"
#include "../sys/LogManager.h"

namespace ks {
namespace engine {
namespace sim {

bool GameplayTweaks::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("GameplayTweaks", "Initializing Gameplay Tweaks module");
	logger.info("GameplayTweaks", "The session applies fuelConsumption and tireDamageRate to the car, "
				 "mechanicalFailures to the damage model's enabled flag, "
				 "damageAffectsDriving to whether damage scales engine power, "
				 "the AI pace/aggression pair and racingRulesStrict to the track-limits ladder");
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
