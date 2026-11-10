#pragma once
#include "../EngineModule.h"
#include "../Math/MathTypesFree.h"
#include <string>
#include <vector>
#include <functional>

namespace ks {
namespace engine {
namespace graphics {

/*
 * TripleScreenSupport - Multi-Monitor Racing Setup
 * 
 * Feature Status: MISSING (Stub Implementation)
 * 
 * Planned Features:
 * - Multi-window rendering across 3+ displays
 * - Bezel compensation for side-angle correction
 * - Independent projection matrices per screen
 * - View frustum culling per display
 * - Synchronized frame timing across windows
 * - Warp distortion correction
 * - Driver position offset per screen configuration
 * 
 * Current State:
 * - Only single window support active
 * - All methods are stubs logging access attempts
 * - Configuration structures defined but not functional
 * 
 * Note: One window, one swapchain architecture prevents
 * immediate implementation. Requires refactoring render
 * loop to support multiple independent swapchains.
 */

enum class ScreenConfiguration {
	SingleScreen,
	TripleScreen,
	QuadScreen,
	PanovisionWide,
	Custom
};

struct ScreenSettings {
	int displayIndex = 0;
	struct Point { int x = 0; int y = 0; };
	Point resolution{1920, 1080};
	Point position{};
	float bezelWidth = 0.0f;           // physical bezel width in mm
	float bezelCompensation = 0.0f;    // angle correction in degrees
	float horizontalFOV = 100.0f;
	float verticalFOV = 56.25f;
	bool active = true;
};

struct TripleScreenConfig {
	ScreenConfiguration mode = ScreenConfiguration::SingleScreen;
	std::vector<ScreenSettings> screens;
	float driverEyeHeight = 1.0f;      // relative to car cockpit
	ks::math::vec3 driverPosition{};    // offset from cockpit
	bool synchronizeFrames = true;
	float masterBezelCompensation = 0.0f;
	bool useWarpDistortion = false;
};

class TripleScreenSupport : public EngineModule {
public:
	static TripleScreenSupport& instance() {
		static TripleScreenSupport s;
		return s;
	}

	// EngineModule interface
	std::string moduleName() const override { return "TripleScreenSupport"; }
	std::string moduleId() const override { return "ks.graphics.triplescreen"; }
	bool initialize() override;
	void shutdown() override;

	// Configuration
	bool setScreenConfiguration(const TripleScreenConfig& config);
	ScreenConfiguration getActiveConfiguration() const { return m_config.mode; }
	const TripleScreenConfig& getConfiguration() const { return m_config; }

	// Screen management
	int getScreenCount() const { return static_cast<int>(m_config.screens.size()); }
	const ScreenSettings& getScreenSettings(int index) const;
	bool addScreen(const ScreenSettings& settings);
	bool removeScreen(int index);

	// Rendering per-screen
	void setupScreenViewMatrix(int screenIndex, ks::math::mat4& viewMatrix);
	void setupScreenProjectionMatrix(int screenIndex, ks::math::mat4& projMatrix);
	ks::math::mat4 getScreenWarpMatrix(int screenIndex) const;

	// Bezel correction
	void applyBezelCompensation(int screenIndex);
	void setBezelWidth(float widthMm);
	void calibrateBezelAngles();

	// Frame synchronization
	void beginFrameSync();
	void endFrameSync();
	void waitForScreenSync();

	// Driver position
	void setDriverPosition(const ks::math::vec3& pos);
	ks::math::vec3 getDriverPosition() const { return m_config.driverPosition; }

	// Status queries
	bool isMultiScreenActive() const { return m_config.mode != ScreenConfiguration::SingleScreen; }
	bool isBezelCorrectionActive() const { return m_config.masterBezelCompensation != 0.0f; }

	// Callbacks
	std::function<void(const TripleScreenConfig&)> onConfigurationChanged;
	std::function<void(int)> onScreenAdded;
	std::function<void(int)> onScreenRemoved;

private:
	TripleScreenSupport() = default;
	~TripleScreenSupport() = default;

	// Internal rendering setup
	void setupMultiWindowRenderLoop();
	void synchronizeWindowFrames();
	void applyWarpDistortion(int screenIndex);

	// State
	TripleScreenConfig m_config;
	bool m_multiScreenActive = false;
	std::vector<void*> m_windowHandles;  // Platform-specific window pointers
};

} // namespace graphics
} // namespace engine
} // namespace ks
