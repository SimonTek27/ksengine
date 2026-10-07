#include "SimulationLoop.h"
#include "UdpTelemetryBridge.h"
#include "TcpTelemetryBridge.h"
#include "adapters/assetto_corsa/AcSharedMemoryPublisher.h"
#if HAS_VEHICLE_SIM
#include "engine/physics/VehicleSimulator.h"
#endif
#include <cstdio>
#include <cmath>

namespace {
float fin(float v, float fb = 0.f) { return std::isfinite(v) ? v : fb; }
}

namespace ks::sim {

void SimulationLoop::publishSharedMemory() {
    if (!m_shm || !m_shmEnabled) return;
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    const auto& ffb = m_vehicle->ffbSample();
    ks::ac::AcLiveInput live;
    live.throttle = fin(static_cast<float>(st.throttle));
    live.brake = fin(static_cast<float>(st.brake));
    live.steer = fin(static_cast<float>(st.steering));
    live.speedMs = fin(static_cast<float>(st.speed));
    live.rpm = fin(static_cast<float>(m_vehicle->rpm()));
    live.gear = m_vehicle->currentGear();
    live.fuel = fin(static_cast<float>(st.fuel), 0.f);
    live.velocity[0] = fin(st.velocity.x); live.velocity[1] = fin(st.velocity.y); live.velocity[2] = fin(st.velocity.z);
    live.accG[0] = fin(st.acceleration.x / 9.81f); live.accG[1] = fin(st.acceleration.y / 9.81f); live.accG[2] = fin(st.acceleration.z / 9.81f);
    live.heading = fin(st.heading);
    live.wheelSlip[0] = fin(ffb.slipAngleFL); live.wheelSlip[1] = fin(ffb.slipAngleFR);
    live.wheelLoad[0] = fin(ffb.loadFL); live.wheelLoad[1] = fin(ffb.loadFR);
    live.finalFF = fin(ffb.aligningMomentNm);
    for (int i = 0; i < 4; ++i) {
        live.tyreTemp[i] = fin(static_cast<float>(st.tyreTemp[i]));
        live.tyreWear[i] = fin(static_cast<float>(st.tyreWear[i]));
        live.tyrePressure[i] = fin(static_cast<float>(st.tyrePressure[i]));
    }
    live.carX = fin(st.position.x); live.carY = fin(st.position.y); live.carZ = fin(st.position.z);
    live.normalizedSpline = fin(m_normalizedSpline);
    live.distanceTraveled = fin(static_cast<float>(m_lapDistance));
    live.airTemp = fin(m_weather.ambientTemp); live.roadTemp = fin(m_weather.trackTemp);
    live.completedLaps = m_lapTimer.completedLaps();
    live.currentSector = m_lapTimer.sectorIndex();
    live.iCurrentTimeMs = m_lapTimer.currentTimeMs();
    live.iLastTimeMs = m_lapTimer.lastTimeMs();
    live.iBestTimeMs = m_lapTimer.bestTimeMs();
    live.sessionType = static_cast<int>(m_sessionType);
    live.status = m_running ? 2 : 0;
    live.inPit = st.inPitLane; live.pitLimiter = st.pitLimiterActive;
    live.carModel = m_carName; live.trackName = m_trackData.name;
    live.maxRpm = 8500; live.totalLaps = m_totalLaps;
    live.sectorCount = m_lapTimer.sectorCount();
    live.sessionTimeLeft = fin(static_cast<float>(m_timeRemaining));
    live.trackSplineLength = fin(m_trackData.splineLength, 5000.f);
    m_shm->publish(live);
#endif
}

void SimulationLoop::publishUdpTelemetry() {
    if (!m_udp || !m_udpEnabled) return;
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    UdpTelemSample s;
    s.timeSec = m_simTime;
    s.speedMs = fin(static_cast<float>(st.speed));
    s.rpm = fin(static_cast<float>(m_vehicle->rpm()));
    s.throttle = fin(static_cast<float>(st.throttle));
    s.brake = fin(static_cast<float>(st.brake));
    s.steer = fin(static_cast<float>(st.steering));
    s.gear = m_vehicle->currentGear();
    s.fuelL = fin(static_cast<float>(st.fuel));
    s.posX = fin(st.position.x); s.posY = fin(st.position.y); s.posZ = fin(st.position.z);
    s.velX = fin(st.velocity.x); s.velY = fin(st.velocity.y); s.velZ = fin(st.velocity.z);
    s.heading = fin(st.heading);
    s.completedLaps = m_lapTimer.completedLaps();
    s.currentSector = m_lapTimer.sectorIndex();
    s.currentTimeMs = m_lapTimer.currentTimeMs();
    s.lastTimeMs = m_lapTimer.lastTimeMs();
    s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.sessionType = static_cast<int>(m_sessionType);
    s.status = m_running ? 2 : 0;
    s.normalizedSpline = fin(m_normalizedSpline);
    s.airTemp = fin(m_weather.ambientTemp);
    s.roadTemp = fin(m_weather.trackTemp);
    s.inPit = st.inPitLane;
    s.pitLimiter = st.pitLimiterActive;
    m_udp->publish(s);
#endif
}

void SimulationLoop::publishTcpTelemetry() {
    if (!m_tcp || !m_tcpEnabled) return;
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    UdpTelemSample s;
    s.timeSec = m_simTime;
    s.speedMs = fin(static_cast<float>(st.speed));
    s.rpm = fin(static_cast<float>(m_vehicle->rpm()));
    s.throttle = fin(static_cast<float>(st.throttle));
    s.brake = fin(static_cast<float>(st.brake));
    s.steer = fin(static_cast<float>(st.steering));
    s.gear = m_vehicle->currentGear();
    s.fuelL = fin(static_cast<float>(st.fuel));
    s.posX = fin(st.position.x); s.posY = fin(st.position.y); s.posZ = fin(st.position.z);
    s.completedLaps = m_lapTimer.completedLaps();
    s.currentTimeMs = m_lapTimer.currentTimeMs();
    s.lastTimeMs = m_lapTimer.lastTimeMs();
    s.bestTimeMs = m_lapTimer.bestTimeMs();
    s.sessionType = static_cast<int>(m_sessionType);
    s.status = m_running ? 2 : 0;
    s.normalizedSpline = fin(m_normalizedSpline);
    m_tcp->publish(s);
#endif
}

} // namespace ks::sim
