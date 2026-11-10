#include "SpectatorMode.h"
#include "../sys/LogManager.h"

namespace ks {
namespace engine {
namespace sim {

bool SpectatorMode::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("SpectatorMode", "Initializing Spectator Mode module (STUB)");
	return true;
}

void SpectatorMode::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("SpectatorMode", "Shutting down Spectator Mode module");
}

bool SpectatorMode::enterSpectatorMode() {
	m_spectating = true;
	if (onSpectatorEnter) onSpectatorEnter();
	return true;
}

void SpectatorMode::exitSpectatorMode() {
	m_spectating = false;
	if (onSpectatorExit) onSpectatorExit();
}

void SpectatorMode::setViewMode(SpectatorViewMode mode) {
	m_viewMode = mode;
}

void SpectatorMode::followDriver(int driverId) {
	m_followedDriver = driverId;
	if (onDriverFollowed) onDriverFollowed(driverId);
}

void SpectatorMode::showScoreboard(bool show) {
	m_scoreboardVisible = show;
	if (onScoreboardToggle) onScoreboardToggle(show);
}

} // namespace sim
} // namespace engine
} // namespace ks
