#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace ks::sim {

// ============================================================================
// UdpTelemetryListener - Listens to Kinetika UDP telemetry stream
// Broadcasts telemetry on UDP port 20747
// ============================================================================

struct AcTelemetryData {
    uint64_t timestamp = 0;
    float speed = 0.f;
    float rpm = 0.f;
    float throttle = 0.f;
    float brake = 0.f;
    float steering = 0.f;
    float gear = 0.f;
    float fuel = 0.f;
    float fuelDelta = 0.f;
    float tyreWearFL = 0.f;
    float tyreWearFR = 0.f;
    float tyreWearRL = 0.f;
    float tyreWearRR = 0.f;
    float tyreTemperatureFL = 0.f;
    float tyreTemperatureFR = 0.f;
    float tyreTemperatureRL = 0.f;
    float tyreTemperatureRR = 0.f;
    float powerUnitTemperature = 0.f;
    float brakeTemperature = 0.f;
    float localSpeedX = 0.f;
    float localSpeedY = 0.f;
    float localSpeedZ = 0.f;
    float lateralG = 0.f;
    float longitudinalG = 0.f;
    float angularVelocityX = 0.f;
    float angularVelocityY = 0.f;
    float angularVelocityZ = 0.f;
    float pitch = 0.f;
    float roll = 0.f;
    float yaw = 0.f;
    uint8_t sessionType = 0;
    uint8_t trackId = 0;
    uint8_t trackConfiguration = 0;
    float windSpeed = 0.f;
    float windDirection = 0.f;
    float airDensity = 0.f;
    float airTemp = 0.f;
    float grassAmount = 0.f;
    float slipstreamTimeToFinish = 0.f;
    float playerCarState = 0.f;
    float opponentCarRight = 0.f;
    float opponentCarLeft = 0.f;
    int nLaps = 0;
    int racePos = 0;
    float lapTime = 0.f;
    float bestLapTime = 0.f;
    float splitTime = 0.f;
    float trackStatus = 0.f;
    float surfaceType = 0.f;
    bool isInPit = false;
    bool isRacePaused = false;
    bool isTimeAccelerated = false;
    bool isSpeedThresholdCrossed = false;
};

// Callback type for telemetry data received
using TelemetryCallback = std::function<void(const AcTelemetryData&)>;

class UdpTelemetryListener {
public:
    UdpTelemetryListener();
    ~UdpTelemetryListener();

    // Initialize the listener on the specified port (default: 20747)
    bool start(uint16_t port = 20747);

    // Stop the listener
    void stop();

    // Set callback for when telemetry data is received
    void setCallback(TelemetryCallback cb);

    // Check if running
    bool isRunning() const;

    // Get last received data (thread-safe)
    AcTelemetryData getLastData() const;

private:
    // Thread function that receives UDP packets
    void receiveThread();

    // Parse raw telemetry packet
    void parsePacket(const uint8_t* data, size_t size);

    // UDP socket handle (platform-specific)
    int m_socketfd = -1;
    std::atomic<bool> m_running{false};
    std::thread m_receiveThread;
    TelemetryCallback m_callback;
    mutable std::mutex m_dataMutex;
    AcTelemetryData m_lastData;
};

} // namespace ks::sim
