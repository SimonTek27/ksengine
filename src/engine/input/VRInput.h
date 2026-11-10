#pragma once
#include "../KsExport.h"
#include "../EngineModule.h"
#include "../Math/MathTypesFree.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace ks {
namespace engine {
namespace input {

/*
 * VRInput - Virtual Reality Controller and Input Support
 * 
 * Feature Status: MISSING (Stub Implementation)
 * 
 * Planned Features:
 * - VR controller detection (HTC Vive, Oculus Touch, Valve Index)
 * - Hand pose tracking (position, rotation per joint)
 * - Button and trigger mapping
 * - Haptic feedback control
 * - Gesture recognition
 * - Remappable action bindings
 * - Configuration profiles per headset
 * - Fallback to gamepad when VR not available
 * 
 * Current State:
 * - No VR controller support active
 * - All methods are stubs
 * - Returns empty device lists
 * 
 * Dependencies:
 * - VRSupport module (graphics)
 */

enum class VRControllerType {
	None,
	HTCViveController,
	OculusTouch,
	ValveIndexController,
	PlayStation,
	Other
};

enum class VRControllerHand {
	Left,
	Right,
	Both
};

enum class VRButton {
	Trigger,
	Grip,
	MenuButton,
	TouchpadClick,
	StickClick,
	AButton,
	BButton,
	XButton,
	YButton
};

struct VRControllerPose {
	ks::math::vec3 position{};
	ks::math::quat orientation{};
	float validity = 1.0f;            // 0-1
};

struct VRHandSkeleton {
	// 26 joint positions per hand (finger joints)
	std::vector<ks::math::vec3> jointPositions;
	std::vector<ks::math::quat> jointOrientations;
	float confidence = 1.0f;
};

struct VRControllerState {
	VRControllerType type = VRControllerType::None;
	VRControllerHand hand = VRControllerHand::Left;
	VRControllerPose pose;
	VRHandSkeleton skeleton;

	// Input states
	float triggerValue = 0.0f;        // 0-1
	float gripValue = 0.0f;           // 0-1
	ks::math::vec2 touchpadAxis{};
	ks::math::vec2 stickAxis{};
	bool menuButtonPressed = false;
	bool touchpadPressed = false;
	bool stickPressed = false;
	bool buttonA = false;
	bool buttonB = false;
	bool buttonX = false;
	bool buttonY = false;

	// Battery
	float batteryLevel = 100.0f;      // 0-100%
	bool lowBatteryWarning = false;
};

class KSENGINE_API VRInput : public EngineModule {
public:
	static VRInput& instance() {
		static VRInput s;
		return s;
	}

	// EngineModule interface
	std::string moduleName() const override { return "VRInput"; }
	std::string moduleId() const override { return "ks.input.vr"; }
	bool initialize() override;
	void shutdown() override;
	void update(double dt) override;

	// Device discovery
	bool detectVRControllers();
	std::vector<VRControllerType> getAvailableControllers() const;

	// Controller state
	const VRControllerState& getControllerState(VRControllerHand hand) const;
	VRControllerState& getControllerState(VRControllerHand hand);
	bool isControllerConnected(VRControllerHand hand) const;

	// Input queries
	float getTriggerValue(VRControllerHand hand) const;
	float getGripValue(VRControllerHand hand) const;
	ks::math::vec2 getTouchpadAxis(VRControllerHand hand) const;
	ks::math::vec2 getStickAxis(VRControllerHand hand) const;
	bool isButtonPressed(VRControllerHand hand, VRButton button) const;

	// Haptics
	void setHapticFeedback(VRControllerHand hand, float intensity, float duration);
	void stopHapticFeedback(VRControllerHand hand);

	// Hand tracking
	const VRHandSkeleton& getHandSkeleton(VRControllerHand hand) const;
	ks::math::vec3 getFingerTipPosition(VRControllerHand hand, int fingerIndex) const;

	// Mapping
	void mapControllerButton(VRControllerHand hand, VRButton button, const std::string& action);
	void mapControllerAxis(VRControllerHand hand, const std::string& axisName, float sensitivity);

	// Configuration
	bool loadProfile(const std::string& profileName);
	bool saveProfile(const std::string& profileName);

	// Status
	bool isVRInputActive() const { return m_vrInputActive; }
	int getControllersConnected() const;

	// Callbacks
	std::function<void(VRControllerHand, const VRControllerState&)> onControllerUpdated;
	std::function<void(VRControllerHand)> onControllerConnected;
	std::function<void(VRControllerHand)> onControllerDisconnected;
	std::function<void(VRControllerHand, VRButton)> onButtonPressed;
	std::function<void(VRControllerHand, VRButton)> onButtonReleased;

private:
	VRInput() = default;
	~VRInput() = default;

	// Internal implementation
	void updateControllerStates();
	void queryControllerPose(VRControllerHand hand);
	void updateHandSkeleton(VRControllerHand hand);

	// State
	VRControllerState m_leftController;
	VRControllerState m_rightController;
	bool m_vrInputActive = false;
	bool m_controllersDetected = false;
};

} // namespace input
} // namespace engine
} // namespace ks
