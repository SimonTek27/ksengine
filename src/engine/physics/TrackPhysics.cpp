#ifndef KSENGINE_QT_FREE
#include <string>
#include "PhysicsCoreTypes.h"
#include "TrackPhysics.h"
#include "VehiclePhysics.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <ctime>

namespace ks::physics {

// ============================================================================
// Track Surface Model
// ============================================================================

SurfaceProperties TrackSurfaceModel::getSurface(float trackPosition) const {
    for (const auto& section : m_surfaceSections) {
        if (trackPosition >= section.startDist && trackPosition < section.endDist) {
            return section.properties;
        }
    }

    SurfaceProperties defaultSurface;
    defaultSurface.gripCoefficient = 1.0f;
    defaultSurface.rollingResistance = 0.015f;
    defaultSurface.surfaceRoughness = 0.0f;
    defaultSurface.surfaceTemperature = 30.0f;
    defaultSurface.abrasiveness = 0.1f;
    return defaultSurface;
}

void TrackSurfaceModel::addSurface(float startDist, float endDist, const SurfaceProperties& props) {
    SurfaceSection section;
    section.startDist = startDist;
    section.endDist = endDist;
    section.properties = props;
    m_surfaceSections.push_back(section);
}

SurfaceGripModifier TrackSurfaceModel::calculateGripModifier(
    const SurfaceProperties& surface,
    float tireTemp, float ambientTemp, float trackTemp
) const {
    SurfaceGripModifier modifier;

    float optimalTireTemp = 80.0f;
    float tempDiff = tireTemp - optimalTireTemp;
    modifier.temperatureEffect = 1.0f - 0.002f * tempDiff * tempDiff;
    modifier.temperatureEffect = std::clamp(modifier.temperatureEffect, 0.5f, 1.0f);

    if (surface.isWet) {
        modifier.wetnessReduction = 0.3f;
    } else {
        modifier.wetnessReduction = 0.0f;
    }

    modifier.roughnessEffect = 1.0f - surface.surfaceRoughness * 0.1f;

    if (surface.isCurb) {
        modifier.curbBump = 0.05f;
    } else {
        modifier.curbBump = 0.0f;
    }

    modifier.totalGripModifier = surface.gripCoefficient *
        modifier.temperatureEffect *
        (1.0f - modifier.wetnessReduction) *
        modifier.roughnessEffect;

    return modifier;
}

float TrackSurfaceModel::calculateWetGripReduction(
    float wetness, float waterDepth
) const {
    float reduction = wetness * 0.3f;

    if (waterDepth > 0.001f) {
        float hydroplaningRisk = std::min(waterDepth / 0.005f, 1.0f);
        reduction += hydroplaningRisk * 0.4f;
    }

    return std::clamp(reduction, 0.0f, 0.8f);
}

float TrackSurfaceModel::calculateCurbEffect(
    float curbHeight, float suspensionCompression
) const {
    if (curbHeight < 0.01f) return 0.0f;

    float compressionRatio = suspensionCompression / 0.1f;
    compressionRatio = std::clamp(compressionRatio, 0.0f, 1.0f);

    float bumpForce = curbHeight * 5000.0f * (1.0f - compressionRatio);

    return bumpForce;
}

float TrackSurfaceModel::calculateRollingResistance(
    const SurfaceProperties& surface, float speed
) const {
    float baseResistance = surface.rollingResistance;

    float speedFactor = 1.0f + speed * 0.001f;

    float roughnessFactor = 1.0f + surface.surfaceRoughness * 0.5f;

    return baseResistance * speedFactor * roughnessFactor;
}

// ============================================================================
// Banking Model
// ============================================================================

BankingState BankingState_calculate(
    float bankingAngle, float speed, float mass,
    float cornerRadius
) {
    BankingState state;
    state.bankingAngle = bankingAngle;

    float gravity = 9.81f;
    float sinAngle = std::sin(bankingAngle);
    float cosAngle = std::cos(bankingAngle);

    state.normalForceGain = mass * gravity * sinAngle;

    if (cornerRadius > 0.0f) {
        float centrifugalForce = mass * speed * speed / cornerRadius;
        state.lateralAccelContribution = centrifugalForce * sinAngle;
    }

    if (cosAngle > 0.0f) {
        state.corneringEnhancement = 1.0f / cosAngle;
    } else {
        state.corneringEnhancement = 1.0f;
    }

    return state;
}

BankingState BankingModel::calculate(
    float bankingAngle, float speed, float mass,
    float cornerRadius
) const {
    return BankingState_calculate(bankingAngle, speed, mass, cornerRadius);
}

float BankingModel::calculateNormalForceContribution(
    float bankingAngle, float mass, float lateralAccel
) const {
    float gravity = 9.81f;
    float sinAngle = std::sin(bankingAngle);
    float cosAngle = std::cos(bankingAngle);

    float normalFromGravity = mass * gravity * cosAngle;
    float normalFromBanking = mass * lateralAccel * sinAngle;

    return normalFromGravity + normalFromBanking;
}

float BankingModel::calculateCorneringEnhancement(
    float bankingAngle, float speed, float cornerRadius
) const {
    float cosAngle = std::cos(bankingAngle);
    if (cosAngle <= 0.0f) return 1.0f;

    float gravity = 9.81f;
    float optimalSpeed = std::sqrt(gravity * cornerRadius * std::tan(bankingAngle));

    float speedRatio = speed / optimalSpeed;
    speedRatio = std::clamp(speedRatio, 0.0f, 2.0f);

    float enhancement = 1.0f + speedRatio * (1.0f / cosAngle - 1.0f);
    return enhancement;
}

float BankingModel::calculateSpeedLimit(
    float bankingAngle, float cornerRadius, float maxGrip
) const {
    float gravity = 9.81f;
    float sinAngle = std::sin(bankingAngle);
    float cosAngle = std::cos(bankingAngle);

    float maxLateralAccel = maxGrip * gravity;
    float bankingContribution = gravity * sinAngle / cosAngle;

    float availableAccel = maxLateralAccel + bankingContribution;
    if (availableAccel <= 0.0f) return 0.0f;

    return std::sqrt(availableAccel * cornerRadius);
}

// ============================================================================
// Elevation Model
// ============================================================================

ElevationState ElevationModel::calculate(
    float trackGradient, float speed, float lateralAccel,
    float rideHeightFront, float rideHeightRear
) const {
    ElevationState state;

    state.gradient = trackGradient;

    state.verticalAcceleration = calculateVerticalAcceleration(trackGradient, speed);

    float mass = 1500.0f;
    state.loadVariation = calculateLoadVariation(trackGradient, mass, 0.0f);

    float avgRideHeight = (rideHeightFront + rideHeightRear) * 0.5f;
    state.groundEffectModifier = calculateGroundEffectModifier(avgRideHeight, speed, 1.0f);

    return state;
}

float ElevationModel::calculateVerticalAcceleration(
    float gradientChange, float speed
) const {
    float gravity = 9.81f;

    float vertAccel = gravity * std::sin(gradientChange);

    return vertAccel;
}

float ElevationModel::calculateLoadVariation(
    float gradient, float mass, float longitudinalAccel
) const {
    float gravity = 9.81f;

    float normalLoad = mass * gravity * std::cos(gradient);

    float gradientEffect = mass * gravity * std::sin(gradient);

    float accelEffect = mass * longitudinalAccel * std::cos(gradient);

    return gradientEffect + accelEffect;
}

float ElevationModel::calculateGroundEffectModifier(
    float rideHeight, float speed, float groundEffectFactor
) const {
    float referenceRideHeight = 0.05f;

    if (rideHeight <= 0.0f) return 1.0f;

    float heightRatio = referenceRideHeight / rideHeight;
    heightRatio = std::clamp(heightRatio, 0.5f, 2.0f);

    float speedFactor = 1.0f;
    if (speed > 10.0f) {
        speedFactor = 1.0f + (speed - 10.0f) * 0.001f;
    }

    float modifier = heightRatio * speedFactor * groundEffectFactor;
    return std::clamp(modifier, 0.3f, 2.0f);
}

// ============================================================================
// Track Simulator
// ============================================================================

TrackSimulator* TrackSimulator::s_instance = nullptr;

TrackSimulator::TrackSimulator()
    , m_sessionTimer()
{
    m_sessionState.type = TrackSessionState::SessionType::Practice;
    m_sessionState.sessionActive = false;

    WeatherState defaultWeather;
    defaultWeather.ambientTemp = 26.0;
    defaultWeather.trackTemp = 30.0;
    defaultWeather.airDensity = 1.225;
    defaultWeather.trackWetness = 0.0;
    defaultWeather.rainIntensity = 0.0;
    defaultWeather.windSpeed = 0.0;
    defaultWeather.windDirection = 0.0;
    m_weatherSim.setWeatherState(defaultWeather);
}

TrackSimulator::~TrackSimulator() {
    stopSession();
    s_instance = nullptr;
}

// ============================================================================
// Track Management
// ============================================================================

bool TrackSimulator::loadTrack(const std::string& trackPath) {
    QFile file(trackPath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "%s
", "TrackSimulator: Cannot open track file:" << trackPath;
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        std::fprintf(stderr, "%s
", "TrackSimulator: Invalid JSON in track file:" << error.errorString();
        return false;
    }

    QJsonObject obj = doc.object();

    m_trackLayout.name = obj["name"].toString();
    m_trackLayout.config = obj["config"].toString();
    m_trackLayout.length = obj["length"].toDouble();

    QJsonObject sf = obj["startFinish"].toObject();
    m_trackLayout.startFinishPosition = PhysVec3(
        sf["x"].toDouble(), sf["y"].toDouble(), sf["z"].toDouble());
    m_trackLayout.startFinishDirection = PhysVec3(
        sf["dirX"].toDouble(), sf["dirY"].toDouble(), sf["dirZ"].toDouble());

    QJsonArray sectorsArray = obj["sectors"].toArray();
    for (const QJsonValue& val : sectorsArray) {
        QJsonObject s = val.toObject();
        TrackSector sector;
        sector.name = s["name"].toString();
        sector.startDistance = s["startDistance"].toDouble();
        sector.endDistance = s["endDistance"].toDouble();
        m_trackLayout.sectors.append(sector);
    }

    QJsonArray cornersArray = obj["corners"].toArray();
    for (const QJsonValue& val : cornersArray) {
        QJsonObject c = val.toObject();
        TrackCorner corner;
        corner.number = c["number"].toInt();
        corner.name = c["name"].toString();
        corner.position = c["position"].toDouble();
        corner.radius = c["radius"].toDouble();
        corner.entrySpeed = c["entrySpeed"].toDouble();
        corner.apexSpeed = c["apexSpeed"].toDouble();
        corner.exitSpeed = c["exitSpeed"].toDouble();
        corner.banking = c["banking"].toDouble();
        corner.type = static_cast<TrackCorner::Type>(c["type"].toInt());
        m_trackLayout.corners.append(corner);
    }

    QJsonArray rlArray = obj["racingLine"].toArray();
    for (const QJsonValue& val : rlArray) {
        QJsonObject p = val.toObject();
        m_trackLayout.racingLine.append(PhysVec3(
            p["x"].toDouble(), p["y"].toDouble(), p["z"].toDouble()));
        m_trackLayout.racingLineDistances.append(p["distance"].toDouble());
    }

    QJsonArray elevArray = obj["elevation"].toArray();
    for (const QJsonValue& val : elevArray) {
        QJsonObject e = val.toObject();
        m_trackLayout.elevationProfile.append(
            qMakePair(e["distance"].toDouble(), e["height"].toDouble()));
    }

    QJsonArray surfArray = obj["surfaceSections"].toArray();
    for (const QJsonValue& val : surfArray) {
        QJsonObject s = val.toObject();
        TrackLayout::SurfaceSection section;
        section.startDistance = s["startDistance"].toDouble();
        section.endDistance = s["endDistance"].toDouble();
        section.gripLevel = s["gripLevel"].toDouble();
        section.bumpiness = s["bumpiness"].toDouble();
        section.surface = static_cast<TrackLayout::SurfaceSection::Surface>(s["surfaceType"].toInt());
        m_trackLayout.surfaceSections.append(section);

        SurfaceProperties props;
        props.gripCoefficient = static_cast<float>(section.gripLevel);
        props.surfaceRoughness = static_cast<float>(section.bumpiness);
        switch (section.surface) {
            case TrackLayout::SurfaceSection::Surface::Kerb:
                props.isCurb = true;
                break;
            case TrackLayout::SurfaceSection::Surface::Gravel:
                props.isGravel = true;
                break;
            case TrackLayout::SurfaceSection::Surface::Grass:
                props.isGrass = true;
                break;
            default:
                break;
        }
        m_surfaceModel.addSurface(
            static_cast<float>(section.startDistance),
            static_cast<float>(section.endDistance),
            props
        );
    }

    m_trackLayout.pitEntryDistance = obj["pitEntryDistance"].toDouble();
    m_trackLayout.pitExitDistance = obj["pitExitDistance"].toDouble();
    m_trackLayout.pitLaneLength = obj["pitLaneLength"].toDouble();
    m_trackLayout.pitSpeedLimit = obj["pitSpeedLimit"].toDouble();

    QJsonArray drsArray = obj["drsZones"].toArray();
    for (const QJsonValue& val : drsArray) {
        QJsonObject d = val.toObject();
        TrackLayout::DrsZone zone;
        zone.detectionPoint = d["detectionPoint"].toDouble();
        zone.startDistance = d["activationPoint"].toDouble();
        zone.endDistance = d["endPoint"].toDouble();
        m_trackLayout.drsZones.append(zone);
    }

    std::fprintf(stderr, "%s
", "TrackSimulator: Loaded track:" << m_trackLayout.name
             << "(" << m_trackLayout.config << ") Length:" << m_trackLayout.length;

    // emit trackChanged(m_trackLayout.name);
    return true;
}

bool TrackSimulator::loadTrackFromData(const TrackLayout& layout) {
    m_trackLayout = layout;
    // emit trackChanged(m_trackLayout.name);
    return true;
}

// ============================================================================
// Vehicle Management
// ============================================================================

bool TrackSimulator::setVehicle(VehicleSimulator* vehicle) {
    if (!vehicle) return false;

    m_vehicle = vehicle;
    m_vehicleName = vehicle->metaObject()->className();

    connect(vehicle, &VehicleSimulator::stateUpdated, this, [this](const SimulationState& state) {
    });

    // emit vehicleChanged(m_vehicleName);
    return true;
}

// ============================================================================
// Session Control
// ============================================================================

void TrackSimulator::setSessionType(TrackSessionState::SessionType type) {
    m_sessionState.type = type;
    // emit sessionTypeChanged(m_sessionState.type);
}

void TrackSimulator::startSession(double durationMinutes) {
    if (m_sessionState.sessionActive) return;

    if (!m_vehicle) {
        std::fprintf(stderr, "%s
", "TrackSimulator: No vehicle set, cannot start session";
        return;
    }

    m_sessionState.sessionActive = true;
    m_sessionState.sessionTotalTime = durationMinutes * 60.0;
    m_sessionState.sessionTimeRemaining = m_sessionState.sessionTotalTime;
    m_sessionState.currentLap = 1;
    m_sessionState.currentLapTime = 0.0;
    m_sessionState.bestLapTime = 1e9;
    m_sessionState.lastLapTime = 0.0;
    m_sessionState.currentSector = 1;
    m_sessionState.sector1Time = 0.0;
    m_sessionState.sector2Time = 0.0;
    m_sessionState.sector3Time = 0.0;
    m_sessionState.bestSector1 = 1e9;
    m_sessionState.bestSector2 = 1e9;
    m_sessionState.bestSector3 = 1e9;
    m_sessionState.lapHistory.clear();
    m_sessionState.trackDistance = 0.0;
    m_sessionState.lateralOffset = 0.0;
    m_sessionState.inPitLane = false;
    m_sessionState.pitLimiterActive = false;
    m_sessionState.drsActive = false;
    m_sessionState.drsAvailable = false;
    m_sessionState.crossedStartFinish = false;
    m_sessionState.cornerCutWarning = false;

    m_sessionTimer.start();
    m_lastReplayTime = 0.0;
    m_recording = true;
    m_replayData.clear();

    m_vehicle->startSimulation();

    // emit sessionStarted();
    // emit sessionStateChanged(m_sessionState.type);
    // emit recordingStarted();

    std::fprintf(stderr, "%s
", "TrackSimulator: Session started ("
             << (durationMinutes > 0 ? std::string::number(durationMinutes) + " min" : "untimed") << ")";
}

void TrackSimulator::stopSession() {
    if (!m_sessionState.sessionActive) return;

    m_sessionState.sessionActive = false;
    m_sessionTimer.invalidate();

    if (m_vehicle) {
        m_vehicle->stopSimulation();
    }

    m_recording = false;

    // emit sessionStopped();
    // emit sessionStateChanged(m_sessionState.type);
    // emit recordingStopped();

    std::fprintf(stderr, "%s
", "TrackSimulator: Session stopped";
}

void TrackSimulator::pauseSession() {
    if (!m_sessionState.sessionActive) return;

    m_sessionState.sessionActive = false;
    if (m_vehicle) m_vehicle->stopSimulation();

    // emit sessionPaused();
    // emit sessionStateChanged(m_sessionState.type);
}

void TrackSimulator::resumeSession() {
    if (m_sessionState.sessionActive) return;

    m_sessionState.sessionActive = true;
    if (m_vehicle) m_vehicle->startSimulation();
    m_sessionTimer.restart();

    // emit sessionResumed();
    // emit sessionStateChanged(m_sessionState.type);
}

void TrackSimulator::resetSession() {
    stopSession();
    m_sessionState = TrackSessionState();
    m_sessionState.type = TrackSessionState::SessionType::Practice;
    m_replayData.clear();
    m_lastReplayTime = 0.0;
}

// ============================================================================
// Main Update Loop
// ============================================================================

void TrackSimulator::updateSession(double dt) {
    if (!m_sessionState.sessionActive || !m_vehicle) return;

    dt *= m_realTimeMultiplier;
    dt = std::min(dt, 0.02);

    if (m_sessionState.sessionTotalTime > 0) {
        double elapsed = m_sessionTimer.elapsed() / 1000.0;
        m_sessionState.sessionTimeRemaining = std::max(0.0, m_sessionState.sessionTotalTime - elapsed);
        if (m_sessionState.sessionTimeRemaining <= 0) {
            stopSession();
            return;
        }
        // emit sessionTimeChanged(m_sessionState.sessionTimeRemaining);
    }

    m_vehicle->setThrottle(m_sessionState.throttle);
    m_vehicle->setBrake(m_sessionState.brake);
    m_vehicle->setSteering(m_sessionState.steering);

    updateLapTiming(dt);
    updateSectorTiming(dt);

    updateVehicleTelemetry(dt);

    updateTrackPosition(dt);

    updateFlags();
    checkSectorCrossings();
    checkStartFinishCrossing();
    checkPitLaneEntryExit();
    checkDrsZones();
    checkTrackLimits();

    updateAiTargets();

    recordReplayFrame();
}

void TrackSimulator::updateLapTiming(double dt) {
    m_sessionState.currentLapTime += dt;
    // emit lapTimeChanged(m_sessionState.currentLapTime);
}

void TrackSimulator::updateSectorTiming(double dt) {
    m_sessionState.currentSectorTime += dt;
}

void TrackSimulator::updateVehicleTelemetry(double dt) {
    if (!m_vehicle) return;

    const SimulationState& state = m_vehicle->getState();

    m_sessionState.speed = state.speed;
    m_sessionState.rpm = state.rpm;
    m_sessionState.gear = state.gear;

    for (int i = 0; i < 4; ++i) {
        WheelState ws = m_vehicle->wheelState(i);
        m_sessionState.tyreTemp[i] = ws.temperature;
        m_sessionState.tyrePressure[i] = ws.pressure;
        m_sessionState.tyreWear[i] = ws.wear;
    }

    // emit speedChanged(m_sessionState.speed);
}

void TrackSimulator::updateTrackPosition(double dt) {
    if (!m_vehicle) return;

    const SimulationState& state = m_vehicle->getState();
    m_sessionState.trackDistance = state.currentLapDistance;
    m_sessionState.worldPosition = state.position;

    m_sessionState.lateralOffset = racingLineLateralOffset(state.position);

    // emit positionUpdated(state.position, m_sessionState.trackDistance, m_sessionState.lateralOffset);
}

void TrackSimulator::updateFlags() {
    bool wasAvailable = m_sessionState.drsAvailable;
    m_sessionState.drsAvailable = false;
    for (const auto& zone : m_trackLayout.drsZones) {
        if (m_sessionState.trackDistance >= zone.detectionPoint &&
            m_sessionState.trackDistance <= zone.startDistance) {
            m_sessionState.drsAvailable = true;
            break;
        }
    }
    if (m_sessionState.drsAvailable != wasAvailable) {
        // emit drsZoneChanged(m_sessionState.drsAvailable);
    }
}

void TrackSimulator::checkSectorCrossings() {
    if (m_trackLayout.sectors.size() < 3) return;

    for (int i = 0; i < m_trackLayout.sectors.size(); ++i) {
        const TrackSector& sector = m_trackLayout.sectors[i];

        if (m_sessionState.trackDistance >= sector.endDistance - 1.0 &&
            m_sessionState.trackDistance < sector.endDistance + 1.0) {

            if (m_sessionState.currentSector == i + 1) {
                double sectorTime = m_sessionState.currentSectorTime;

                switch (i) {
                    case 0: m_sessionState.sector1Time = sectorTime; break;
                    case 1: m_sessionState.sector2Time = sectorTime; break;
                    case 2: m_sessionState.sector3Time = sectorTime; break;
                }

                double& bestSector = (i == 0) ? m_sessionState.bestSector1 :
                                   (i == 1) ? m_sessionState.bestSector2 : m_sessionState.bestSector3;
                if (sectorTime > 0 && sectorTime < bestSector) {
                    bestSector = sectorTime;
                    // emit bestSectorUpdated(i + 1, sectorTime);
                }

                // emit sectorCompleted(i + 1, sectorTime);
                m_sessionState.currentSectorTime = 0.0;
                m_sessionState.currentSector = i + 2;
            }
        }
    }
}

void TrackSimulator::checkStartFinishCrossing() {
    double trackLength = m_trackLayout.length;
    double prevDist = m_sessionState.trackDistance - m_vehicle->getState().speed * (1.0/60.0);

    if (m_sessionState.trackDistance > trackLength * 0.9 && prevDist < trackLength * 0.1) {
        if (!m_sessionState.crossedStartFinish) {
            m_sessionState.crossedStartFinish = true;

            double lapTime = m_sessionState.currentLapTime;
            m_sessionState.lastLapTime = lapTime;

            TrackSessionState::LapRecord record;
            record.lapNumber = m_sessionState.currentLap;
            record.lapTime = lapTime;
            record.sector1 = m_sessionState.sector1Time;
            record.sector2 = m_sessionState.sector2Time;
            record.sector3 = m_sessionState.sector3Time;
            const SimulationState& state = m_vehicle->getState();
            if (state.speed > record.maxSpeed || record.lapNumber == 0) {
                record.maxSpeed = state.speed;
            }

            record.avgSpeed = m_trackLayout.length / lapTime;

            double fuelRate = 0.0;
            if (state.rpm > 2000) {
                fuelRate = (state.rpm - 2000) * 0.01;
            }
            record.fuelUsed = fuelRate * (lapTime / 60.0);
            record.valid = true;
            record.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
            m_sessionState.lapHistory.append(record);

            if (lapTime > 0 && lapTime < m_sessionState.bestLapTime) {
                m_sessionState.bestLapTime = lapTime;
                // emit bestLapUpdated(lapTime);
            }

            // emit lapCompleted(m_sessionState.currentLap, lapTime, m_sessionState.bestLapTime);

            m_sessionState.currentLap++;
            m_sessionState.currentLapTime = 0.0;
            m_sessionState.sector1Time = 0.0;
            m_sessionState.sector2Time = 0.0;
            m_sessionState.sector3Time = 0.0;
            m_sessionState.currentSector = 1;
            m_sessionState.currentSectorTime = 0.0;
            m_sessionState.crossedStartFinish = false;

            // emit lapChanged(m_sessionState.currentLap);
        }
    } else if (m_sessionState.trackDistance < trackLength * 0.5) {
        m_sessionState.crossedStartFinish = false;
    }
}

void TrackSimulator::checkPitLaneEntryExit() {
    double pitEntry = m_trackLayout.pitEntryDistance;
    double pitExit = m_trackLayout.pitExitDistance;
    double dist = m_sessionState.trackDistance;

    bool wasInPit = m_sessionState.inPitLane;

    if (pitEntry > 0 && pitExit > 0) {
        if (pitEntry < pitExit) {
            m_sessionState.inPitLane = (dist >= pitEntry && dist <= pitExit);
        } else {
            m_sessionState.inPitLane = (dist >= pitEntry || dist <= pitExit);
        }
    }

    if (m_sessionState.inPitLane && !wasInPit) {
        m_sessionState.pitLimiterActive = true;
    } else if (!m_sessionState.inPitLane && wasInPit) {
        m_sessionState.pitLimiterActive = false;
    }

    if (m_sessionState.inPitLane != wasInPit) {
        // emit pitLaneStateChanged(m_sessionState.inPitLane);
    }
}

void TrackSimulator::checkDrsZones() {
    bool wasActive = m_sessionState.drsActive;
    m_sessionState.drsActive = false;

    if (!m_sessionState.drsAvailable) return;

    for (const auto& zone : m_trackLayout.drsZones) {
        if (m_sessionState.trackDistance >= zone.startDistance &&
            m_sessionState.trackDistance <= zone.endDistance &&
            m_sessionState.speed > zone.detectionPoint / 3.6) {
            m_sessionState.drsActive = true;
            break;
        }
    }

    if (m_sessionState.drsActive != wasActive) {
        // emit drsStateChanged(m_sessionState.drsActive);
    }
}

void TrackSimulator::checkTrackLimits() {
    double maxOffset = 3.0;
    bool warning = std::abs(m_sessionState.lateralOffset) > maxOffset;

    if (warning != m_sessionState.cornerCutWarning) {
        m_sessionState.cornerCutWarning = warning;
        if (warning) {
            // emit trackLimitWarning(m_sessionState.worldPosition);
        }
    }
}

// ============================================================================
// Track Queries
// ============================================================================

const TrackCorner* TrackSimulator::cornerAtDistance(double distance) const {
    for (const auto& corner : m_trackLayout.corners) {
        double cornerStart = corner.position - 50.0;
        double cornerEnd = corner.position + 50.0;
        if (distance >= cornerStart && distance <= cornerEnd) {
            return &corner;
        }
    }
    return nullptr;
}

const TrackSector* TrackSimulator::sectorAtDistance(double distance) const {
    for (const auto& sector : m_trackLayout.sectors) {
        if (distance >= sector.startDistance && distance <= sector.endDistance) {
            return &sector;
        }
    }
    return nullptr;
}

const TrackLayout::DrsZone* TrackSimulator::drsZoneAtDistance(double distance) const {
    for (const auto& zone : m_trackLayout.drsZones) {
        if (distance >= zone.detectionPoint && distance <= zone.endDistance) {
            return &zone;
        }
    }
    return nullptr;
}

const TrackLayout::SurfaceSection* TrackSimulator::surfaceAtDistance(double distance) const {
    for (const auto& section : m_trackLayout.surfaceSections) {
        if (distance >= section.startDistance && distance <= section.endDistance) {
            return &section;
        }
    }
    return nullptr;
}

double TrackSimulator::racingLineLateralOffset(const PhysVec3& position) const {
    if (m_trackLayout.racingLine.empty()) return 0.0;

    double minDist = 1e9;
    double trackDist = 0.0;

    for (int i = 0; i < m_trackLayout.racingLine.size(); ++i) {
        double dist = (position - m_trackLayout.racingLine[i]).length();
        if (dist < minDist) {
            minDist = dist;
            trackDist = m_trackLayout.racingLineDistances[i];
        }
    }

    return minDist;
}

double TrackSimulator::targetSpeedAtDistance(double distance) const {
    const TrackCorner* corner = cornerAtDistance(distance);
    if (corner) {
        double cornerDist = std::abs(distance - corner->position);
        if (cornerDist < 20.0) return corner->apexSpeed;
        else if (cornerDist < 50.0) return corner->exitSpeed;
    }
    return 80.0;
}

// ============================================================================
// AI Target Computation
// ============================================================================

void TrackSimulator::recordReplayFrame() {
    if (!m_recording) return;

    double currentTime = m_sessionTimer.elapsed() / 1000.0;
    if (currentTime - m_lastReplayTime < 0.1) return;

    QVariantMap frame;
    frame["timestamp"] = currentTime;
    frame["trackDistance"] = m_sessionState.trackDistance;
    frame["speed"] = m_sessionState.speed;
    frame["lapTime"] = m_sessionState.currentLapTime;
    frame["lap"] = m_sessionState.currentLap;
    frame["sector"] = m_sessionState.currentSector;
    m_replayData.append(frame);
    m_lastReplayTime = currentTime;
}

// ============================================================================
// Weather
// ============================================================================

void TrackSimulator::setWeatherState(const WeatherState& weather) {
    m_weatherSim.setWeatherState(weather);
    if (m_vehicle) {
        m_vehicle->setWeatherState(weather);
    }
}

// ============================================================================
// AI Targets
// ============================================================================

void TrackSimulator::updateAiTargets() {
    (void)m_vehicle
}

// ============================================================================
// Update Loop (public interface)
// ============================================================================

void TrackSimulator::update(double dt) {
    if (m_sessionState.sessionActive) {
        updateSession(dt);
    }
}

} // namespace ks::physics

#else
// Qt-free: TrackPhysics I/O uses std::fstream alternatives in VehicleSimulator/TrackLoader
#endif
