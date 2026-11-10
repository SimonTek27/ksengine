#pragma once
#include "../EngineModule.h"
#include <string>
#include <functional>

namespace ks {
namespace engine {
namespace sim {

/*
 * SpectatorMode - Multiplayer Spectator Viewing
 * 
 * Feature Status: MISSING (Stub)
 * 
 * Planned: Free camera, driver POV, replay,  scoreboard
 */

enum class SpectatorViewMode {
	FreeCam,
	Driver,
	Broadcast,
	Replay
};

class SpectatorMode : public EngineModule {
public:
	static SpectatorMode& instance() {
		static SpectatorMode s;
		return s;
	}

	std::string moduleName() const override { return "SpectatorMode"; }
	std::string moduleId() const override { return "ks.sim.spectator"; }
	bool initialize() override;
	void shutdown() override;

	bool enterSpectatorMode();
	void exitSpectatorMode();
	bool isSpectating() const { return m_spectating; }

	void setViewMode(SpectatorViewMode mode);
	SpectatorViewMode getViewMode() const { return m_viewMode; }

	void followDriver(int driverId);
	void showScoreboard(bool show);

	std::function<void()> onSpectatorEnter;
	std::function<void()> onSpectatorExit;
	std::function<void(int)> onDriverFollowed;
	std::function<void(bool)> onScoreboardToggle;

private:
	SpectatorMode() = default;
	bool m_spectating = false;
	SpectatorViewMode m_viewMode = SpectatorViewMode::FreeCam;
	int m_followedDriver = -1;
	bool m_scoreboardVisible = false;
};

} // namespace sim
} // namespace engine
} // namespace ks
