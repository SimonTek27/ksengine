#pragma once
#include "../EngineModule.h"
#include "../KsExport.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace ks {
namespace engine {
namespace sim {

/*
 * GameplayTweaks - Session Gameplay Configuration
 *
 * Feature Status: PARTIAL
 *
 * The set, its defaults and the change callback are here. SimulationLoop owns
 * the application: it registers this module, pushes fuelConsumption into the
 * car's burn rate on every setup apply, hands difficultyMultiplier and
 * aiAggression to the field's AI, and sets the track-limits ladder from
 * racingRulesStrict. The set is re-applied whenever the callback fires.
 * mechanicalFailures is wired (setDamageEnabled on the car); damageAffectsDriving
 * is stored and reported, not wired yet.
 */

struct GameplayTweakSet {
	float difficultyMultiplier = 1.0f;
	float aiAggression = 0.5f; // the AIController's own default: today's AI
	float tireDamageRate = 1.0f;
	float fuelConsumption = 1.0f;
	bool mechanicalFailures = false;
	/** Strict track limits: every cut costs time and the pit penalties climb
	 *  faster (TrackLimitsMonitor's config ladder). The LIMITS on/off switch
	 *  stays the director's; this only chooses how hard the rule bites. */
	bool racingRulesStrict = false;
	bool damageAffectsDriving = true;
};

class KSENGINE_API GameplayTweaks : public EngineModule {
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
