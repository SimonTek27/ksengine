#pragma once

#include "PhysicsEngine.h"
#include "WeatherPhysics.h"
#include <QObject>
#include <QVector3D>
#include <QVector>
#include <QPair>
#include <QPointF>
#include <QElapsedTimer>

namespace ks::physics {

// ============================================================================
// Track Surface Model
// ============================================================================

struct SurfaceProperties { float gripCoefficient = 1, rollingResistance = 0.015f, surfaceRoughness = 0, surfaceTemperature = 30, abrasiveness = 0.1f; bool isWet = false, isCurb = false, isGravel = false, isGrass = false; };
struct SurfaceGripModifier { float temperatureEffect = 1, wetnessReduction = 0, roughnessEffect = 1, curbBump = 0, totalGripModifier = 1; };

class TrackSurfaceModel {
public:
    SurfaceProperties getSurface(float trackPosition) const;
    void addSurface(float start, float end, const SurfaceProperties& props);
    SurfaceGripModifier calculateGripModifier(const SurfaceProperties& s, float tireTemp, float ambient, float trackTemp) const;
    float calculateWetGripReduction(float wetness, float waterDepth) const;
    float calculateCurbEffect(float curbHeight, float suspensionCompression) const;
    float calculateRollingResistance(const SurfaceProperties& s, float speed) const;
private:
    struct SurfaceSection { float startDist = 0, endDist = 0; SurfaceProperties properties; };
    std::vector<SurfaceSection> m_surfaceSections;
};

// ============================================================================
// Banking Model
// ============================================================================

struct BankingState { float bankingAngle = 0, normalForceGain = 0, corneringEnhancement = 0, lateralAccelContribution = 0; };

class BankingModel {
public:
    BankingState calculate(float bankingAngle, float speed, float mass, float cornerRadius) const;
    float calculateNormalForceContribution(float bankingAngle, float mass, float lateralAccel) const;
    float calculateCorneringEnhancement(float bankingAngle, float speed, float cornerRadius) const;
    float calculateSpeedLimit(float bankingAngle, float cornerRadius, float maxGrip) const;
};

// ============================================================================
// Elevation Model
// ============================================================================

struct ElevationState { float gradient = 0, verticalAcceleration = 0, loadVariation = 0, groundEffectModifier = 1; };

class ElevationModel {
public:
    ElevationState calculate(float trackGradient, float speed, float lateralAccel, float rideHeightFront, float rideHeightRear) const;
    float calculateVerticalAcceleration(float gradientChange, float speed) const;
    float calculateLoadVariation(float gradient, float mass, float longAccel) const;
    float calculateGroundEffectModifier(float rideHeight, float speed, float groundEffectFactor) const;
};

// ============================================================================
// Track Simulator
// ============================================================================

class VehicleSimulator;

class TrackSimulator : public QObject {
    Q_OBJECT
public:
    explicit TrackSimulator(QObject* parent = nullptr);
    ~TrackSimulator();
    bool loadTrack(const QString& path);
    bool loadTrackFromData(const TrackLayout& layout);
    TrackLayout currentTrack() const { return m_trackLayout; }
    QString trackName() const { return m_trackLayout.name; }
    bool setVehicle(VehicleSimulator* v);
    VehicleSimulator* vehicle() const { return m_vehicle; }
    QString vehicleName() const { return m_vehicleName; }
    void setSessionType(TrackSessionState::SessionType t);
    void startSession(double duration = 0);
    void stopSession();
    void pauseSession();
    void resumeSession();
    void resetSession();
    bool isSessionActive() const { return m_sessionState.sessionActive; }
    double sessionTimeRemaining() const { return m_sessionState.sessionTimeRemaining; }
    int currentLap() const { return m_sessionState.currentLap; }
    double currentLapTime() const { return m_sessionState.currentLapTime; }
    double bestLapTime() const { return m_sessionState.bestLapTime; }
    double lastLapTime() const { return m_sessionState.lastLapTime; }
    double currentSpeed() const { return m_sessionState.speed; }
    void update(double dt);
    void setWeatherState(const WeatherState& w);
    WeatherState weatherState() const { return m_weatherSim.weatherState(); }
signals:
    void sessionStarted();
    void sessionStopped();
    void sessionStateChanged(TrackSessionState::SessionType type);
    void sessionTimeChanged(double remaining);
    void sessionPaused();
    void sessionResumed();
    void trackChanged(const QString& n);
    void vehicleChanged(const QString& n);
    void lapCompleted(int lap, double time, double best);
    void sectorCompleted(int sector, double time);
    void lapTimeChanged(double time);
    void lapChanged(int lap);
    void bestLapUpdated(double time);
    void bestSectorUpdated(int sector, double time);
    void speedChanged(double speed);
    void positionUpdated(QVector3D pos, double trackDist, double lateral);
    void drsZoneChanged(bool active);
    void drsStateChanged(bool active);
    void pitLaneStateChanged(bool inPit);
    void trackLimitWarning(QVector3D position);
    void recordingStarted();
    void recordingStopped();
    void sessionTypeChanged(TrackSessionState::SessionType type);
private:
    void updateSession(double dt);
    void updateLapTiming(double dt);
    void updateSectorTiming(double dt);
    void updateVehicleTelemetry(double dt);
    void updateTrackPosition(double dt);
    void updateFlags();
    void checkSectorCrossings();
    void checkStartFinishCrossing();
    void checkPitLaneEntryExit();
    void checkDrsZones();
    void checkTrackLimits();
    void updateAiTargets();
    void recordReplayFrame();
    const TrackCorner* cornerAtDistance(double distance) const;
    const TrackSector* sectorAtDistance(double distance) const;
    const TrackLayout::DrsZone* drsZoneAtDistance(double distance) const;
    const TrackLayout::SurfaceSection* surfaceAtDistance(double distance) const;
    double racingLineLateralOffset(const QVector3D& position) const;
    double targetSpeedAtDistance(double distance) const;
    TrackLayout m_trackLayout;
    VehicleSimulator* m_vehicle = nullptr;
    QString m_vehicleName;
    TrackSessionState m_sessionState;
    WeatherSimulator m_weatherSim;
    TrackSurfaceModel m_surfaceModel;
    BankingModel m_bankingModel;
    ElevationModel m_elevationModel;
    QElapsedTimer m_sessionTimer;
    double m_realTimeMultiplier = 1;
    bool m_recording = false;
    double m_lastReplayTime = 0.0;
    QVector<QVariantMap> m_replayData;
    static TrackSimulator* s_instance;
};

} // namespace ks::physics
