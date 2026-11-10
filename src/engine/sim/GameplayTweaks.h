#pragma once
#include "../EngineModule.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace ks {
namespace engine {
namespace sim {

/*
 * GameplayTweaks - Session Gameplay Configuration
 * 
 * Feature Status: MISSING (Stub)
 * 
 * Planned: Difficulty modifiers, race rule tweaks, AI behavior
 */

struct GameplayTweakSet {
	float difficultyMultiplier = 1.0f;
	float aiAggression = 1.0f;
	float tireDamageRate = 1.0f;
	float fuelConsumption = 1.0f;
	bool mechanicalFailures = false;
	bool racingRulesStrict = false;
	bool damageAffectsDriving = true;
};

class GameplayTweaks : public EngineModule {
public:
	static GameplayTweaks& instance() {
		static GameplayTweaks s;
		return s;
	}

	std::string moduleName() const override { return "GameplayTweaks"; }
	std::string moduleId() const override { return "ks.sim.tweaks"; }
	bool initialize() override;
	void shutdown() override;

	void setTweakSet(const GameplayTweakSet& tweaks);
	const GameplayTweakSet& getTweakSet() const { return m_tweaks; }
	void resetToDefaults();

	std::function<void(const GameplayTweakSet&)> onTweaksChanged;

private:
	GameplayTweaks() = default;
	GameplayTweakSet m_tweaks;
};

} // namespace sim
} // namespace engine
} // namespace ks
