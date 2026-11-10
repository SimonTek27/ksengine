#include "VRInput.h"
#include "../sys/LogManager.h"
#include "../devices/vr/XrManager.h"

namespace ks {
namespace engine {
namespace input {

bool VRInput::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("VRInput", "Initializing VR input module");

	m_vrInputActive = false;
	m_controllersDetected = false;
	m_leftController = VRControllerState{};
	m_rightController = VRControllerState{};

	detectVRControllers();

	return true;
}

void VRInput::update(double) {
	if (m_vrInputActive) updateControllerStates();
}

void VRInput::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("VRInput", "Shutting down VR Input module");

	m_vrInputActive = false;
	m_controllersDetected = false;
}

bool VRInput::detectVRControllers() {
	auto* xr = ks::device::XrManager::instance();
	m_vrInputActive = xr->isInitialized();
	m_controllersDetected = m_vrInputActive;
	return m_vrInputActive;
}

std::vector<VRControllerType> VRInput::getAvailableControllers() const {
	std::vector<VRControllerType> available;
	if (m_leftController.type != VRControllerType::None) available.push_back(m_leftController.type);
	if (m_rightController.type != VRControllerType::None &&
		(m_leftController.type == VRControllerType::None || m_rightController.type != m_leftController.type))
		available.push_back(m_rightController.type);
	return available;
}

const VRControllerState& VRInput::getControllerState(VRControllerHand hand) const {
	return (hand == VRControllerHand::Left) ? m_leftController : m_rightController;
}

VRControllerState& VRInput::getControllerState(VRControllerHand hand) {
	return (hand == VRControllerHand::Left) ? m_leftController : m_rightController;
}

bool VRInput::isControllerConnected(VRControllerHand hand) const {
	const auto& state = (hand == VRControllerHand::Left) ? m_leftController : m_rightController;
	return state.type != VRControllerType::None;
}

float VRInput::getTriggerValue(VRControllerHand hand) const {
	return getControllerState(hand).triggerValue;
}

float VRInput::getGripValue(VRControllerHand hand) const {
	return getControllerState(hand).gripValue;
}

ks::math::vec2 VRInput::getTouchpadAxis(VRControllerHand hand) const {
	return getControllerState(hand).touchpadAxis;
}

ks::math::vec2 VRInput::getStickAxis(VRControllerHand hand) const {
	return getControllerState(hand).stickAxis;
}

bool VRInput::isButtonPressed(VRControllerHand hand, VRButton button) const {
	const auto& state = getControllerState(hand);

	switch (button) {
		case VRButton::Trigger:      return state.triggerValue > 0.5f;
		case VRButton::Grip:         return state.gripValue > 0.5f;
		case VRButton::MenuButton:   return state.menuButtonPressed;
		case VRButton::TouchpadClick: return state.touchpadPressed;
		case VRButton::StickClick:   return state.stickPressed;
		case VRButton::AButton:      return state.buttonA;
		case VRButton::BButton:      return state.buttonB;
		case VRButton::XButton:      return state.buttonX;
		case VRButton::YButton:      return state.buttonY;
	}

	return false;
}

void VRInput::setHapticFeedback(VRControllerHand hand, float intensity, float duration) {
	auto& logger = sys::LogManager::instance();
	logger.debug("VRInput", "Haptic feedback requested for " + 
				std::string(hand == VRControllerHand::Left ? "left" : "right") +
				" controller: intensity=" + std::to_string(intensity) + 
				", duration=" + std::to_string(duration) + "ms");

	(void)intensity;
	(void)duration;
}

void VRInput::stopHapticFeedback(VRControllerHand hand) {
	auto& logger = sys::LogManager::instance();
	logger.debug("VRInput", "Stopping haptic feedback for " + 
				std::string(hand == VRControllerHand::Left ? "left" : "right") + " controller");

	(void)hand;
}

const VRHandSkeleton& VRInput::getHandSkeleton(VRControllerHand hand) const {
	return getControllerState(hand).skeleton;
}

ks::math::vec3 VRInput::getFingerTipPosition(VRControllerHand hand, int fingerIndex) const {
	const auto& skeleton = getHandSkeleton(hand);

	if (fingerIndex < 0 || fingerIndex >= static_cast<int>(skeleton.jointPositions.size())) {
		return {};
	}

	return skeleton.jointPositions[fingerIndex];
}

void VRInput::mapControllerButton(VRControllerHand hand, VRButton button, const std::string& action) {
	auto& logger = sys::LogManager::instance();
	logger.info("VRInput", "Mapping controller button to action: " + action);

	(void)hand;
	(void)button;
}

void VRInput::mapControllerAxis(VRControllerHand hand, const std::string& axisName, float sensitivity) {
	auto& logger = sys::LogManager::instance();
	logger.info("VRInput", "Mapping controller axis '" + axisName + 
			   "' with sensitivity " + std::to_string(sensitivity));

	(void)hand;
	(void)axisName;
	(void)sensitivity;
}

bool VRInput::loadProfile(const std::string& profileName) {
	auto& logger = sys::LogManager::instance();
	logger.info("VRInput", "Loading VR input profile: " + profileName);

	// TODO: Load profile from disk
	// TODO: Apply button and axis mappings

	return false;
}

bool VRInput::saveProfile(const std::string& profileName) {
	auto& logger = sys::LogManager::instance();
	logger.info("VRInput", "Saving VR input profile: " + profileName);

	// TODO: Save current mappings to disk

	return false;
}

int VRInput::getControllersConnected() const {
	int count = 0;
	if (isControllerConnected(VRControllerHand::Left)) count++;
	if (isControllerConnected(VRControllerHand::Right)) count++;
	return count;
}

void VRInput::updateControllerStates() {
	if (!ks::device::XrManager::instance()->isSessionRunning()) return;
	queryControllerPose(VRControllerHand::Left);
	queryControllerPose(VRControllerHand::Right);
}

void VRInput::queryControllerPose(VRControllerHand hand) {
	auto& state = getControllerState(hand);
	const VRControllerState previous = state;
	const auto& source = hand == VRControllerHand::Left
		? ks::device::XrManager::instance()->leftController()
		: ks::device::XrManager::instance()->rightController();
	state.type = source.connected ? VRControllerType::OculusTouch : VRControllerType::None;
	state.hand = hand;
	state.triggerValue = source.triggerValue;
	state.gripValue = source.squeezeValueFloat;
	state.stickAxis = ks::math::vec2(source.thumbstickValue.x, source.thumbstickValue.y);
	state.menuButtonPressed = source.menuClicked;
	state.pose.validity = source.aimValid || source.gripValid ? 1.0f : 0.0f;
	const auto& pose = source.gripValid ? source.gripPoseRaw : source.aimPoseRaw;
	state.pose.position = ks::math::vec3(pose.position.x, pose.position.y, pose.position.z);
	state.pose.orientation = ks::math::quat(pose.orientation.x, pose.orientation.y,
		pose.orientation.z, pose.orientation.w);
	if (previous.type == VRControllerType::None && state.type != VRControllerType::None && onControllerConnected)
		onControllerConnected(hand);
	if (previous.type != VRControllerType::None && state.type == VRControllerType::None && onControllerDisconnected)
		onControllerDisconnected(hand);
	const VRButton buttons[] = {VRButton::Trigger, VRButton::Grip, VRButton::MenuButton};
	for (VRButton button : buttons) {
		const bool wasPressed = button == VRButton::Trigger ? previous.triggerValue > 0.5f :
			button == VRButton::Grip ? previous.gripValue > 0.5f : previous.menuButtonPressed;
		const bool isPressed = button == VRButton::Trigger ? state.triggerValue > 0.5f :
			button == VRButton::Grip ? state.gripValue > 0.5f : state.menuButtonPressed;
		if (wasPressed != isPressed) {
			if (isPressed && onButtonPressed) onButtonPressed(hand, button);
			if (!isPressed && onButtonReleased) onButtonReleased(hand, button);
		}
	}
	if (state.type != VRControllerType::None && onControllerUpdated) onControllerUpdated(hand, state);
}

void VRInput::updateHandSkeleton(VRControllerHand hand) {
	// TODO: Query hand tracking data from VR runtime
	// - Get joint positions for all fingers
	// - Get joint orientations
	// - Update confidence score
}

} // namespace input
} // namespace engine
} // namespace ks
