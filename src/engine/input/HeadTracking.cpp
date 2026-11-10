#include "HeadTracking.h"
#include "../sys/LogManager.h"
#include "../devices/vr/XrManager.h"
#include <algorithm>
#include <cmath>

namespace ks {
namespace engine {
namespace input {

bool HeadTracking::initialize() {
	auto& logger = sys::LogManager::instance();
	logger.info("HeadTracking", "Initializing head tracking module");

	m_trackingActive = false;
	m_deviceConnected = false;
	m_activeDevice = TrackingDevice::None;

	if (ks::device::XrManager::instance()->isInitialized())
		connectToDevice(TrackingDevice::SteamVR);

	return true;
}

void HeadTracking::update(double) {
	if (m_trackingActive && m_deviceConnected) updatePose();
}

void HeadTracking::shutdown() {
	auto& logger = sys::LogManager::instance();
	logger.info("HeadTracking", "Shutting down Head Tracking module");

	if (m_deviceConnected) {
		disconnectDevice();
	}

	m_trackingActive = false;
}

bool HeadTracking::detectTrackingDevice() {
	return ks::device::XrManager::instance()->isInitialized() ||
		ks::device::XrManager::instance()->isSessionRunning();
}

std::vector<TrackingDevice> HeadTracking::getAvailableDevices() const {
	if (ks::device::XrManager::instance()->isInitialized())
		return {TrackingDevice::SteamVR};
	return {};
}

bool HeadTracking::connectToDevice(TrackingDevice device) {
	auto& logger = sys::LogManager::instance();
	logger.info("HeadTracking", "Connecting to tracking device: " + std::to_string(static_cast<int>(device)));

	if (device == TrackingDevice::None) {
		logger.warn("HeadTracking", "Cannot connect to null device");
		return false;
	}

	if (device != TrackingDevice::SteamVR || !detectTrackingDevice()) return false;
	m_activeDevice = device;
	m_deviceConnected = true;
	m_trackingActive = true;
	if (onDeviceConnected) onDeviceConnected();
	return true;
}

void HeadTracking::disconnectDevice() {
	auto& logger = sys::LogManager::instance();
	logger.info("HeadTracking", "Disconnecting tracking device");

	// TODO: Clean up device resources

	m_deviceConnected = false;
	m_trackingActive = false;

	if (onDeviceDisconnected) {
		onDeviceDisconnected();
	}
}

void HeadTracking::setTrackingSettings(const TrackingSettings& settings) {
	m_settings = settings;

	auto& logger = sys::LogManager::instance();
	logger.info("HeadTracking", "Tracking settings updated");
	logger.debug("HeadTracking", "Sensitivity X/Y/Z: " + 
				 std::to_string(settings.sensitivityX) + "/" +
				 std::to_string(settings.sensitivityY) + "/" +
				 std::to_string(settings.sensitivityZ));
	logger.debug("HeadTracking", "Smoothing: " + std::to_string(settings.smoothing));
}

void HeadTracking::calibrateCenter() {
	m_settings.centerX = m_currentPose.position.x;
	m_settings.centerY = m_currentPose.position.y;
	m_settings.centerZ = m_currentPose.position.z;
}

void HeadTracking::resetTracking() {
	auto& logger = sys::LogManager::instance();
	logger.info("HeadTracking", "Resetting head tracking");

	m_currentPose = HeadPose{};
	m_previousPose = HeadPose{};
}

void HeadTracking::setSmoothing(float value) {
	m_settings.smoothing = std::clamp(value, 0.0f, 1.0f);
	auto& logger = sys::LogManager::instance();
	logger.debug("HeadTracking", "Smoothing set to " + std::to_string(m_settings.smoothing));
}

void HeadTracking::setSensitivity(float x, float y, float z) {
	m_settings.sensitivityX = x;
	m_settings.sensitivityY = y;
	m_settings.sensitivityZ = z;
}

void HeadTracking::setDeadZone(const ks::math::vec3& deadZone) {
	m_settings.deadZone = deadZone;
}

void HeadTracking::updatePose() {
	queryTrackingDevice();
	if (!m_currentPose.valid) return;

	// Apply dead zone
	applyDeadZone();

	// Apply smoothing
	applySmoothing();

	// Notify listeners
	if (m_currentPose.valid && onPoseUpdated) {
		onPoseUpdated(m_currentPose);
	}
}

void HeadTracking::applySmoothing() {
	if (m_previousPose.valid && m_settings.smoothing > 0.0f) {
		const float alpha = 1.0f - std::clamp(m_settings.smoothing, 0.0f, 1.0f);
		m_currentPose.position = m_previousPose.position * (1.0f - alpha) +
			m_currentPose.position * alpha;
		const auto& a = m_previousPose.orientation;
		const auto& b = m_currentPose.orientation;
		float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
		const float sign = dot < 0.0f ? -1.0f : 1.0f;
		dot = std::fabs(dot);
		if (dot > 0.9995f) {
			m_currentPose.orientation = ks::math::quat(
				a.x + alpha*(sign*b.x-a.x), a.y + alpha*(sign*b.y-a.y),
				a.z + alpha*(sign*b.z-a.z), a.w + alpha*(sign*b.w-a.w));
		} else {
			const float angle = std::acos(std::clamp(dot, 0.0f, 1.0f));
			const float denominator = std::sin(angle);
			const float wa = std::sin((1.0f-alpha)*angle) / denominator;
			const float wb = std::sin(alpha*angle) / denominator * sign;
			m_currentPose.orientation = ks::math::quat(
				wa*a.x+wb*b.x, wa*a.y+wb*b.y, wa*a.z+wb*b.z, wa*a.w+wb*b.w);
		}
	}
	m_previousPose = m_currentPose;
}

void HeadTracking::queryTrackingDevice() {
	if (!m_deviceConnected) {
		m_currentPose.valid = false;
		return;
	}

	const auto* xr = ks::device::XrManager::instance();
	if (!xr->headPoseValid()) {
		m_currentPose.valid = false;
		return;
	}
	const auto pose = xr->headPose();
	m_currentPose.position = ks::math::vec3(
		(pose.position.x - m_settings.centerX * 0.001f) * 1000.0f * m_settings.sensitivityX,
		(pose.position.y - m_settings.centerY * 0.001f) * 1000.0f * m_settings.sensitivityY,
		(pose.position.z - m_settings.centerZ * 0.001f) * 1000.0f * m_settings.sensitivityZ);
	m_currentPose.orientation = ks::math::quat(pose.orientation.x, pose.orientation.y,
		pose.orientation.z, pose.orientation.w);
	m_currentPose.confidence = 1.0f;
	m_currentPose.valid = true;
	applyDeadZone();
}

void HeadTracking::applyDeadZone() {
	if (std::fabs(m_currentPose.position.x) < m_settings.deadZone.x) m_currentPose.position.x = 0;
	if (std::fabs(m_currentPose.position.y) < m_settings.deadZone.y) m_currentPose.position.y = 0;
	if (std::fabs(m_currentPose.position.z) < m_settings.deadZone.z) m_currentPose.position.z = 0;
}

} // namespace input
} // namespace engine
} // namespace ks
