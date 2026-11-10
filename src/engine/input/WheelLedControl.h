#pragma once
#include "../EngineModule.h"
#include "../KsExport.h"
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <functional>

namespace ks {
namespace engine {
namespace input {

/*
 * WheelLedControl - Steering Wheel LED Support
 *
 * Feature Status: PARTIAL
 *
 * Stato attuale:
 * - State/callback container funzionale: raccolte RPM/marcia/warn dal
 *   runtime (SimulationLoop le alimenta dai dati del veicolo già tickati),
 *   invio al protocollo hardware ancora da integrare (Fanatec/Logitech/Moza).
 * - setWheelLedState e i setter singoli sono operativi (stato interno +
 *   logging); sendLedCommandToWheel è il punto dove agganciare l'output
 *   reale del dispositivo.
 *
 * Da fare:
 * - protocolli LED per dispositivo (enumerazione + scrittura)
 * - pattern animati e notifiche TC/ABS/fuel reali
 */

enum class LedType {
	None,
	RPMIndicator,
	GearIndicator,
	TemperatureWarning,
	FuelLevelWarning,
	TrackingLights,
	CustomPattern
};

enum class LedColor : uint32_t {
	Off = 0x000000,
	Red = 0xFF0000,
	Green = 0x00FF00,
	Blue = 0x0000FF,
	Yellow = 0xFFFF00,
	Cyan = 0x00FFFF,
	Magenta = 0xFF00FF,
	White = 0xFFFFFF,
	Orange = 0xFF8000,
	Purple = 0x800080
};

struct WheelLedState {
	uint8_t rpmPercentage = 0;        // 0-100
	uint8_t gearNumber = 0;           // 1-6+
	LedColor statusColor = LedColor::Off;
	bool brakeTempWarning = false;
	bool trackingActive = false;
	bool absActive = false;
	float fuelPercentage = 100.0f;
};

class KSENGINE_API WheelLedControl : public EngineModule {
public:
	static WheelLedControl& instance() {
		static WheelLedControl s;
		return s;
	}

	// EngineModule interface
	std::string moduleName() const override { return "WheelLedControl"; }
	std::string moduleId() const override { return "ks.input.wheel_leds"; }
	bool initialize() override;
	void shutdown() override;

	// Device discovery
	bool detectWheelDevices();
	std::vector<std::string> getConnectedWheels() const;

	// LED Control
	void setWheelLedState(const WheelLedState& state);
	void setRpmIndicator(uint8_t percentage);
	void setGearIndicator(uint8_t gearNumber);
	void setStatusColor(LedColor color);
	void setTemperatureWarning(bool active);
	void setTrackingIndicator(bool active);
	void setAbsIndicator(bool active);
	void setFuelLevelWarning(float percentage);

	// Custom patterns
	void setCustomLedPattern(const std::vector<LedColor>& pattern);
	void animateLedPattern(float animationSpeed);
	void clearAllLeds();

	// State queries
	const WheelLedState& getCurrentState() const { return m_currentState; }
	bool isWheelConnected() const { return m_wheelConnected; }
	std::string getWheelDeviceName() const { return m_wheelDeviceName; }

	// Callbacks
	std::function<void()> onWheelConnected;
	std::function<void()> onWheelDisconnected;
	std::function<void(const std::string&)> onLedUpdateFailed;

private:
	WheelLedControl() = default;
	~WheelLedControl() = default;

	// Internal implementation
	void updateWheelLeds();
	void sendLedCommandToWheel(const WheelLedState& state);

	// State
	WheelLedState m_currentState;
	bool m_wheelConnected = false;
	std::string m_wheelDeviceName;
	void* m_wheelHandle = nullptr;  // Platform-specific device handle
};

} // namespace input
} // namespace engine
} // namespace ks
