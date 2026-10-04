#include "TrackSimulator.h"
#include "VehicleSimulator.h"

#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace ks {
namespace physics {

TrackSimulator* TrackSimulator::s_instance = nullptr;

TrackSimulator::TrackSimulator(QObject* parent)
    : QObject(parent)
    , m_sessionTimer()
{
    // Initialize default session state
    m_sessionState.type = TrackSessionState::SessionType::Practice;
    m_sessionState.sessionActive = false;
    
    // Default weather
    m_weather.ambientTemp = 26.0;
    m_weather.trackTemp = 30.0;
    m_weather.airDensity = 1.225;
    m_weather.trackWetness = 0.0;
    m_weather.rainIntensity = 0.0;
    m_weather.windSpeed = 0.0;
    m_weather.windDirection = 0.0;
}

TrackSimulator::~TrackSimulator() {
    stopSession();
    s_instance = nullptr;
}

// ============================================================================
// Track Management
// ============================================================================

bool TrackSimulator::loadTrack(const QString& trackPath) {
    QFile file(trackPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "TrackSimulator: Cannot open track file:" << trackPath;
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        qWarning() << "TrackSimulator: Invalid JSON in track file:" << error.errorString();
        return false;
    }

    QJsonObject obj = doc.object();
    
    m_trackLayout.name = obj["name"].toString();
    m_trackLayout.config = obj["config"].toString();
    m_trackLayout.length = obj["length"].toDouble();
    
    // Start/finish
    QJsonObject sf = obj["startFinish"].toObject();
    m_trackLayout.startFinishPosition = QVector3D(
        sf["x"].toDouble(), sf["y"].toDouble(), sf["z"].toDouble());
    m_trackLayout.startFinishDirection = QVector3D(
        sf["dirX"].toDouble(), sf["dirY"].toDouble(), sf["dirZ"].toDouble());

    // Sectors
    QJsonArray sectorsArray = obj["sectors"].toArray();
    for (const QJsonValue& val : sectorsArray) {
        QJsonObject s = val.toObject();
        TrackSector sector;
        sector.name = s["name"].toString();
        sector.startDistance = s["startDistance"].toDouble();
        sector.endDistance = s["endDistance"].toDouble();
        m_trackLayout.sectors.append(sector);
    }

    // Corners
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

    // Racing line
    QJsonArray rlArray = obj["racingLine"].toArray();
    for (const QJsonValue& val : rlArray) {
        QJsonObject p = val.toObject();
        m_trackLayout.racingLine.append(QVector3D(
            p["x"].toDouble(), p["y"].toDouble(), p["z"].toDouble()));
        m_trackLayout.racingLineDistances.append(p["distance"].toDouble());
    }

    // Elevation
    QJsonArray elevArray = obj["elevation"].toArray();
    for (const QJsonValue& val : elevArray) {
        QJsonObject e = val.toObject();
        m_trackLayout.elevationProfile.append(
            qMakePair(e["distance"].toDouble(), e["height"].toDouble()));
    }

    // Surface sections
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
    }

    // Pit lane
    m_trackLayout.pitEntryDistance = obj["pitEntryDistance"].toDouble();
    m_trackLayout.pitExitDistance = obj["pitExitDistance"].toDouble();
    m_trackLayout.pitLaneLength = obj["pitLaneLength"].toDouble();
    m_trackLayout.pitSpeedLimit = obj["pitSpeedLimit"].toDouble();

    // DRS zones
    QJsonArray drsArray = obj["drsZones"].toArray();
    for (const QJsonValue& val : drsArray) {
        QJsonObject d = val.toObject();
        TrackLayout::DrsZone zone;
        zone.detectionPoint = d["detectionPoint"].toDouble();
        zone.activationPoint = d["activationPoint"].toDouble();
        zone.endPoint = d["endPoint"].toDouble();
        m_trackLayout.drsZones.append(zone);
    }

    qDebug() << "TrackSimulator: Loaded track:" << m_trackLayout.name 
             << "(" << m_trackLayout.config << ") Length:" << m_trackLayout.length;
    
    emit trackChanged(m_trackLayout.name);
    return true;
}

bool TrackSimulator::loadTrackFromData(const TrackLayout& layout) {
    m_trackLayout = layout;
    emit trackChanged(m_trackLayout.name);
    return true;
}

// ============================================================================
// Vehicle Management
// ============================================================================

bool TrackSimulator::setVehicle(VehicleSimulator* vehicle) {
    if (!vehicle) return false;
    
    m_vehicle = vehicle;
    m_vehicleName = vehicle->metaObject()->className();  // or use a name property
    
    // Connect vehicle signals
    connect(vehicle, &VehicleSimulator::stateUpdated, this, [this](const SimulationState& state) {
        // Vehicle state is polled in updateVehicleTelemetry
    });
    
    emit vehicleChanged(m_vehicleName);
    return true;
}

// ============================================================================
// Session Control
// ============================================================================

void TrackSimulator::setSessionType(TrackSessionState::SessionType type) {
    m_sessionState.type = type;
    emit sessionTypeChanged(m_sessionState.type);
}

void TrackSimulator::startSession(double durationMinutes) {
    if (m_sessionState.sessionActive) return;
    
    if (!m_vehicle) {
        qWarning() << "TrackSimulator: No vehicle set, cannot start session";
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
    
    // Start vehicle simulation
    m_vehicle->startSimulation();
    
    emit sessionStarted();
    emit sessionStateChanged(true);
    emit recordingStarted();
    
    qDebug() << "TrackSimulator: Session started (" 
             << (durationMinutes > 0 ? QString::number(durationMinutes) + " min" : "untimed") << ")";
}

void TrackSimulator::stopSession() {
    if (!m_sessionState.sessionActive) return;
    
    m_sessionState.sessionActive = false;
    m_sessionTimer.invalidate();
    
    if (m_vehicle) {
        m_vehicle->stopSimulation();
    }
    
    m_recording = false;
    
    emit sessionStopped();
    emit sessionStateChanged(false);
    emit recordingStopped();
    
    qDebug() << "TrackSimulator: Session stopped";
}

void TrackSimulator::pauseSession() {
    if (!m_sessionState.sessionActive) return;
    
    m_sessionState.sessionActive = false;
    if (m_vehicle) m_vehicle->stopSimulation();
    
    emit sessionPaused();
    emit sessionStateChanged(false);
}

void TrackSimulator::resumeSession() {
    if (m_sessionState.sessionActive) return;
    
    m_sessionState.sessionActive = true;
    if (m_vehicle) m_vehicle->startSimulation();
    m_sessionTimer.restart();
    
    emit sessionResumed();
    emit sessionStateChanged(true);
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
    
    // Scale dt by real-time multiplier
    dt *= m_realTimeMultiplier;
    dt = std::min(dt, 0.02);  // Cap at 20ms for stability
    
    // Update session timer
    if (m_sessionState.sessionTotalTime > 0) {
        double elapsed = m_sessionTimer.elapsed() / 1000.0;
        m_sessionState.sessionTimeRemaining = std::max(0.0, m_sessionState.sessionTotalTime - elapsed);
        if (m_sessionState.sessionTimeRemaining <= 0) {
            stopSession();
            return;
        }
        emit sessionTimeChanged(m_sessionState.sessionTimeRemaining);
    }
    
    // Update vehicle physics
    m_vehicle->setThrottle(m_sessionState.throttle);
    m_vehicle->setBrake(m_sessionState.brake);
    m_vehicle->setSteering(m_sessionState.steering);
    
    // Update lap/sector timing
    updateLapTiming(dt);
    updateSectorTiming(dt);
    
    // Update vehicle telemetry
    updateVehicleTelemetry(dt);
    
    // Update track position
    updateTrackPosition(dt);
    
    // Check various track conditions
    updateFlags();
    checkSectorCrossings();
    checkStartFinishCrossing();
    checkPitLaneEntryExit();
    checkDrsZones();
    checkTrackLimits();
    
    // Update AI targets
    updateAiTargets();
    
    // Record replay frame
    recordReplayFrame();
}

void TrackSimulator::updateLapTiming(double dt) {
    m_sessionState.currentLapTime += dt;
    emit lapTimeChanged(m_sessionState.currentLapTime);
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
    
    // Get wheel states
    for (int i = 0; i < 4; ++i) {
        WheelState ws = m_vehicle->wheelState(i);
        m_sessionState.tyreTemp[i] = ws.temperature;
        m_sessionState.tyrePressure[i] = ws.pressure;
        m_sessionState.tyreWear[i] = ws.wear;
    }
    
    emit speedChanged(m_sessionState.speed);
}

void TrackSimulator::updateTrackPosition(double dt) {
    if (!m_vehicle) return;
    
    const SimulationState& state = m_vehicle->getState();
    m_sessionState.trackDistance = state.currentLapDistance;
    m_sessionState.worldPosition = QVector3D(state.position.x, state.position.y, state.position.z);
    
    // Calculate lateral offset from racing line
    m_sessionState.lateralOffset = racingLineLateralOffset(QVector3D(state.position.x, state.position.y, state.position.z));
    
    emit positionUpdated(QVector3D(state.position.x, state.position.y, state.position.z), m_sessionState.trackDistance, m_sessionState.lateralOffset);
}

void TrackSimulator::updateFlags() {
    // Update DRS availability
    bool wasAvailable = m_sessionState.drsAvailable;
    m_sessionState.drsAvailable = false;
    for (const auto& zone : m_trackLayout.drsZones) {
        if (m_sessionState.trackDistance >= zone.detectionPoint && 
            m_sessionState.trackDistance <= zone.activationPoint) {
            m_sessionState.drsAvailable = true;
            break;
        }
    }
    if (m_sessionState.drsAvailable != wasAvailable) {
        emit drsZoneChanged(m_sessionState.drsAvailable);
    }
}

void TrackSimulator::checkSectorCrossings() {
    if (m_trackLayout.sectors.size() < 3) return;
    
    for (int i = 0; i < m_trackLayout.sectors.size(); ++i) {
        const TrackSector& sector = m_trackLayout.sectors[i];
        
        // Check if we crossed the end of this sector
        if (m_sessionState.trackDistance >= sector.endDistance - 1.0 &&
            m_sessionState.trackDistance < sector.endDistance + 1.0) {
            
            if (m_sessionState.currentSector == i + 1) {
                double sectorTime = m_sessionState.currentSectorTime;
                
                // Store sector time
                switch (i) {
                    case 0: m_sessionState.sector1Time = sectorTime; break;
                    case 1: m_sessionState.sector2Time = sectorTime; break;
                    case 2: m_sessionState.sector3Time = sectorTime; break;
                }
                
                // Update best sector
                double& bestSector = (i == 0) ? m_sessionState.bestSector1 : 
                                   (i == 1) ? m_sessionState.bestSector2 : m_sessionState.bestSector3;
                if (sectorTime > 0 && sectorTime < bestSector) {
                    bestSector = sectorTime;
                    emit bestSectorUpdated(i + 1, sectorTime);
                }
                
                emit sectorCompleted(i + 1, sectorTime);
                m_sessionState.currentSectorTime = 0.0;
                m_sessionState.currentSector = i + 2;
            }
        }
    }
}

void TrackSimulator::checkStartFinishCrossing() {
    double trackLength = m_trackLayout.length;
    double prevDist = m_sessionState.trackDistance - m_vehicle->getState().speed * (1.0/60.0);  // approximate
    
    // Check if we crossed start/finish (distance wrapped around)
    if (m_sessionState.trackDistance > trackLength * 0.9 && prevDist < trackLength * 0.1) {
        if (!m_sessionState.crossedStartFinish) {
            m_sessionState.crossedStartFinish = true;
            
            double lapTime = m_sessionState.currentLapTime;
            m_sessionState.lastLapTime = lapTime;
            
            // Record lap
            TrackSessionState::LapRecord record;
            record.lapNumber = m_sessionState.currentLap;
            record.lapTime = lapTime;
            record.sector1 = m_sessionState.sector1Time;
            record.sector2 = m_sessionState.sector2Time;
            record.sector3 = m_sessionState.sector3Time;
            // Track max speed during lap
            const SimulationState& state = m_vehicle->getState();
            if (state.speed > record.maxSpeed || record.lapNumber == 0) {
                record.maxSpeed = state.speed;
            }
            
            record.avgSpeed = m_trackLayout.length / lapTime;
            
            // Track fuel usage (simplified: estimate based on RPM and time)
            // Fuel consumption rate is proportional to RPM above idle
            double fuelRate = 0.0;
            if (state.rpm > 2000) {
                fuelRate = (state.rpm - 2000) * 0.01;  // approximate ml/sec
            }
            record.fuelUsed = fuelRate * (lapTime / 60.0);  // convert to ml per lap
            record.valid = true;
            record.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
            m_sessionState.lapHistory.append(record);
            
            // Update best lap
            if (lapTime > 0 && lapTime < m_sessionState.bestLapTime) {
                m_sessionState.bestLapTime = lapTime;
                emit bestLapUpdated(lapTime);
            }
            
            emit lapCompleted(m_sessionState.currentLap, lapTime, m_sessionState.bestLapTime);
            
            // Reset for next lap
            m_sessionState.currentLap++;
            m_sessionState.currentLapTime = 0.0;
            m_sessionState.sector1Time = 0.0;
            m_sessionState.sector2Time = 0.0;
            m_sessionState.sector3Time = 0.0;
            m_sessionState.currentSector = 1;
            m_sessionState.currentSectorTime = 0.0;
            m_sessionState.crossedStartFinish = false;
            
            emit lapChanged(m_sessionState.currentLap);
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
    
    // Simple pit lane detection
    if (pitEntry > 0 && pitExit > 0) {
        if (pitEntry < pitExit) {
            m_sessionState.inPitLane = (dist >= pitEntry && dist <= pitExit);
        } else {
            // Pit lane crosses start/finish
            m_sessionState.inPitLane = (dist >= pitEntry || dist <= pitExit);
        }
    }
    
    // Pit limiter
    if (m_sessionState.inPitLane && !wasInPit) {
        m_sessionState.pitLimiterActive = true;
    } else if (!m_sessionState.inPitLane && wasInPit) {
        m_sessionState.pitLimiterActive = false;
    }
    
    if (m_sessionState.inPitLane != wasInPit) {
        emit pitLaneStateChanged(m_sessionState.inPitLane);
    }
}

void TrackSimulator::checkDrsZones() {
    bool wasActive = m_sessionState.drsActive;
    m_sessionState.drsActive = false;
    
    if (!m_sessionState.drsAvailable) return;
    
    for (const auto& zone : m_trackLayout.drsZones) {
        if (m_sessionState.trackDistance >= zone.activationPoint && 
            m_sessionState.trackDistance <= zone.endPoint &&
            m_sessionState.speed > zone.detectionPoint / 3.6) {  // speed threshold
            m_sessionState.drsActive = true;
            break;
        }
    }
    
    if (m_sessionState.drsActive != wasActive) {
        emit drsStateChanged(m_sessionState.drsActive);
    }
}

void TrackSimulator::checkTrackLimits() {
    // Check if car is too far from racing line
    double maxOffset = 3.0;  // meters
    bool warning = std::abs(m_sessionState.lateralOffset) > maxOffset;
    
    if (warning != m_sessionState.cornerCutWarning) {
        m_sessionState.cornerCutWarning = warning;
        if (warning) {
            const TrackCorner* corner = cornerAtDistance(m_sessionState.trackDistance);
            QString cornerName = corner ? corner->name : "Unknown";
            emit trackLimitWarning(true, cornerName);
        } else {
            emit trackLimitWarning(false, "");
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
        if (distance >= zone.detectionPoint && distance <= zone.endPoint) {
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

double TrackSimulator::racingLineLateralOffset(const QVector3D& position) const {
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
    
    // Find closest point on racing line and compute lateral offset
    // Simplified: return distance to closest racing line point
    return minDist;
}

double TrackSimulator::targetSpeedAtDistance(double distance) const {
    const TrackCorner* corner = cornerAtDistance(distance);
    if (corner) {
        // Return apex speed when in corner, exit speed when leaving
        double cornerDist = std::abs(distance - corner->position);
        if (cornerDist < 20.0) return corner->apexSpeed;
        else if (cornerDist < 50.0) return corner->exitSpeed;
    }
    return 80.0;  // default target speed
}

// ============================================================================
// AI Target Computation
// ============================================================================

TrackSimulator::TargetState TrackSimulator::computeAiTarget(double lookaheadDistance) const {
    TargetState target;
    
    if (!m_vehicle) {
        target.targetSpeed = 50.0;
        target.targetSteering = 0.0;
        target.targetThrottle = 0.5;
        target.targetBrake = 0.0;
        target.targetGear = 1;
        return target;
    }
    
    double currentDist = m_sessionState.trackDistance;
    double lookaheadDist = currentDist + lookaheadDistance;
    
    // Handle track wrap-around
    if (lookaheadDist > m_trackLayout.length) {
        lookaheadDist -= m_trackLayout.length;
    }
    
    // Find target corner
    TrackCorner* nextCorner = nullptr;
    for (const auto& corner : m_trackLayout.corners) {
        if (corner.position >= currentDist && corner.position <= lookaheadDist) {
            nextCorner = const_cast<TrackCorner*>(&corner);
            break;
        }
    }
    
    if (nextCorner) {
        double distToCorner = nextCorner->position - currentDist;
        if (distToCorner < 0) distToCorner += m_trackLayout.length;
        
        target.currentCorner = nextCorner->name;
        target.targetSpeed = nextCorner->entrySpeed;
        
        // Steering based on corner direction
        target.targetSteering = (nextCorner->type == TrackCorner::Type::Left || 
                                 nextCorner->type == TrackCorner::Type::Hairpin) ? 1.0 : -1.0;
        target.targetSteering *= std::min(1.0, distToCorner / 100.0);
        
        if (distToCorner < 50.0) {
            target.targetThrottle = 0.0;
            target.targetBrake = 0.8;
        } else if (distToCorner < 100.0) {
            target.targetThrottle = 0.3;
            target.targetBrake = 0.4;
        } else {
            target.targetThrottle = 1.0;
            target.targetBrake = 0.0;
        }
        
        // Gear selection based on speed
        target.targetGear = std::max(1, static_cast<int>(target.targetSpeed / 60.0) + 1);
    } else {
        // No corner ahead, full throttle
        target.targetSpeed = 100.0;
        target.targetSteering = 0.0;
        target.targetThrottle = 1.0;
        target.targetBrake = 0.0;
        target.targetGear = 6;
    }
    
    return target;
}

// ============================================================================
// Analysis
// ============================================================================

double TrackSimulator::estimateLapTime() const {
    if (!m_vehicle) return 0.0;
    
    double totalTime = 0.0;
    double currentSpeed = 0.0;
    
    // Simple estimation: straight sections + corners
    double straightLength = m_trackLayout.length * 0.6;
    double cornerLength = m_trackLayout.length * 0.4;
    
    // Average straight speed
    double avgStraightSpeed = m_vehicle->enginePower() / 1000.0 * 50.0;  // rough estimate
    avgStraightSpeed = std::clamp(avgStraightSpeed, 50.0, 90.0);
    
    // Average corner speed
    double avgCornerSpeed = 0.0;
    for (const auto& corner : m_trackLayout.corners) {
        avgCornerSpeed += corner.apexSpeed;
    }
    avgCornerSpeed = m_trackLayout.corners.empty() ? 40.0 : avgCornerSpeed / m_trackLayout.corners.size();
    
    totalTime = straightLength / avgStraightSpeed + cornerLength / avgCornerSpeed;
    return totalTime;
}

double TrackSimulator::estimateFuelForLaps(int laps) const {
    if (!m_vehicle || m_sessionState.fuelPerLap <= 0) return 0.0;
    return m_sessionState.fuelPerLap * laps;
}

int TrackSimulator::estimateLapsRemaining() const {
    if (m_sessionState.fuel <= 0 || m_sessionState.fuelPerLap <= 0) return 0;
    return static_cast<int>(m_sessionState.fuel / m_sessionState.fuelPerLap);
}

// ============================================================================
// Replay
// ============================================================================

void TrackSimulator::startRecording() {
    m_recording = true;
    m_replayData.clear();
    m_lastReplayTime = 0.0;
    emit recordingStarted();
}

void TrackSimulator::stopRecording() {
    m_recording = false;
    emit recordingStopped();
}

void TrackSimulator::clearReplay() {
    m_replayData.clear();
}

void TrackSimulator::recordReplayFrame() {
    if (!m_recording) return;
    
    double currentTime = m_sessionTimer.elapsed() / 1000.0;
    if (currentTime - m_lastReplayTime < 0.1) return;  // 10Hz recording
    
    ReplayFrame frame;
    frame.timestamp = currentTime;
    frame.state = m_sessionState;
    m_replayData.append(frame);
    m_lastReplayTime = currentTime;
}

// ============================================================================
// Weather
// ============================================================================

void TrackSimulator::setWeatherState(const WeatherState& weather) {
    m_weather = weather;
    if (m_vehicle) {
        m_vehicle->setWeatherState(weather);
    }
}

// ============================================================================
// Sector/Time Helpers
// ============================================================================

double TrackSimulator::sectorTime(int sector) const {
    switch (sector) {
        case 1: return m_sessionState.sector1Time;
        case 2: return m_sessionState.sector2Time;
        case 3: return m_sessionState.sector3Time;
        default: return 0.0;
    }
}

double TrackSimulator::bestSectorTime(int sector) const {
    switch (sector) {
        case 1: return m_sessionState.bestSector1;
        case 2: return m_sessionState.bestSector2;
        case 3: return m_sessionState.bestSector3;
        default: return 1e9;
    }
}

int TrackSimulator::findCurrentSector(double distance) const {
    for (int i = 0; i < m_trackLayout.sectors.size(); ++i) {
        if (distance >= m_trackLayout.sectors[i].startDistance && 
            distance <= m_trackLayout.sectors[i].endDistance) {
            return i + 1;
        }
    }
    return 1;
}

int TrackSimulator::findCurrentCorner(double distance) const {
    for (int i = 0; i < m_trackLayout.corners.size(); ++i) {
        const auto& corner = m_trackLayout.corners[i];
        double cornerStart = corner.position - 50.0;
        double cornerEnd = corner.position + 50.0;
        if (distance >= cornerStart && distance <= cornerEnd) {
            return i;
        }
    }
    return -1;
}

double TrackSimulator::distanceToRacingLine(const QVector3D& pos) const {
    if (m_trackLayout.racingLine.empty()) return 0.0;
    
    double minDist = 1e9;
    for (const auto& point : m_trackLayout.racingLine) {
        double dist = (pos - point).length();
        minDist = std::min(minDist, dist);
    }
    return minDist;
}

double TrackSimulator::projectToTrack(const QVector3D& pos, double& outTrackDist) const {
    if (m_trackLayout.racingLine.empty()) return 0.0;
    
    double minDist = 1e9;
    int closestIdx = 0;
    
    for (int i = 0; i < m_trackLayout.racingLine.size(); ++i) {
        double dist = (pos - m_trackLayout.racingLine[i]).length();
        if (dist < minDist) {
            minDist = dist;
            closestIdx = i;
        }
    }
    
    outTrackDist = m_trackLayout.racingLineDistances[closestIdx];
    return minDist;
}

// ============================================================================
// AI Targets
// ============================================================================

void TrackSimulator::updateAiTargets() {
    if (!m_vehicle) return;
    
    m_aiTarget = computeAiTarget(m_aiLookahead);
}

// ============================================================================
// Update Loop (public interface)
// ============================================================================

void TrackSimulator::update(double dt) {
    if (m_sessionState.sessionActive) {
        updateSession(dt);
    }
}

} // namespace physics
} // namespace ks