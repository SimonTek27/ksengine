#pragma once

#include "interfaces/IVehicleSimulator.h"
#include <QVector3D>
#include <QVector>
#include <QPair>
#include <QPointF>
#include <QElapsedTimer>

namespace ks {
namespace physics {

// Forward declarations
class VehicleSimulator;
class SoftBodySimulator;
class ClothSimulator;
class ParticleSystemSimulator;

// ============================================================================
// Track Definition & Geometry
// ============================================================================

struct TrackSector {
    QString name;
    double startDistance;      // meters from start/finish
    double endDistance;        // meters from start/finish
    double length() const { return endDistance - startDistance; }
};

struct TrackCorner {
    int number;
    QString name;
    double position;           // meters from start/finish
    double radius;             // corner radius in meters
    double entrySpeed;         // target entry speed (m/s)
    double apexSpeed;          // target apex speed (m/s)
    double exitSpeed;          // target exit speed (m/s)
    double banking;            // banking angle in degrees
    enum class Type { Left, Right, Chicane, Hairpin, Sweeper, Straight };
    Type type = Type::Left;
};

struct TrackLayout {
    QString name;
    QString config;            // e.g., "GP", "National", "Short"
    double length;             // total track length in meters
    QVector3D startFinishPosition;
    QVector3D startFinishDirection;
    
    QVector<TrackSector> sectors;  // typically 3 sectors
    QVector<TrackCorner> corners;  // all corners on track
    
    // Racing line (sampled positions along optimal line)
    QVector<QVector3D> racingLine;
    QVector<double> racingLineDistances;  // distance along track for each point
    
    // Elevation profile
    QVector<QPair<double, double>> elevationProfile;  // (distance, height)
    
    // Track surface properties per section
    struct SurfaceSection {
        double startDistance;
        double endDistance;
        double gripLevel;        // 0.0 - 1.0
        double bumpiness;        // 0.0 - 1.0
        enum class Surface { Asphalt, Concrete, Gravel, Grass, Kerb, Dirt };
        Surface surface = Surface::Asphalt;
    };
    QVector<SurfaceSection> surfaceSections;
    
    // Pit lane
    double pitEntryDistance;
    double pitExitDistance;
    double pitLaneLength;
    double pitSpeedLimit;        // m/s
    
    // DRS zones
    struct DrsZone {
        double detectionPoint;
        double activationPoint;
        double endPoint;
    };
    QVector<DrsZone> drsZones;
};

// ============================================================================
// Track Session State
// ============================================================================

struct TrackSessionState {
    enum class SessionType { Practice, Qualifying, Race, TimeAttack, Test };
    SessionType type = SessionType::Practice;
    
    QString trackName;
    QString vehicleName;
    QString driverName;
    
    // Session timing
    double sessionTimeRemaining = 0.0;
    double sessionTotalTime = 0.0;
    bool sessionActive = false;
    
    // Lap timing
    int currentLap = 0;
    int totalLaps = 0;
    double currentLapTime = 0.0;
    double bestLapTime = 1e9;
    double lastLapTime = 0.0;
    double currentSectorTime = 0.0;
    int currentSector = 1;
    
    // Sector times for current lap
    double sector1Time = 0.0;
    double sector2Time = 0.0;
    double sector3Time = 0.0;
    
    // Best sector times
    double bestSector1 = 1e9;
    double bestSector2 = 1e9;
    double bestSector3 = 1e9;
    
    // Lap history
    struct LapRecord {
        int lapNumber;
        double lapTime;
        double sector1;
        double sector2;
        double sector3;
        double maxSpeed;
        double avgSpeed;
        double fuelUsed;
        bool valid;
        QString timestamp;
    };
    QVector<LapRecord> lapHistory;
    
    // Vehicle state
    double speed = 0.0;
    double rpm = 0.0;
    int gear = 1;
    double throttle = 0.0;
    double brake = 0.0;
    double steering = 0.0;
    double fuel = 100.0;
    double fuelPerLap = 0.0;
    
    // Tyre state
    double tyreTemp[4] = {30, 30, 30, 30};
    double tyrePressure[4] = {2.2, 2.2, 2.0, 2.0};
    double tyreWear[4] = {0, 0, 0, 0};
    
    // Position on track
    double trackDistance = 0.0;     // distance along track from start/finish
    double lateralOffset = 0.0;     // distance from racing line
    QVector3D worldPosition;
    QVector3D worldRotation;
    
    // Flags
    bool inPitLane = false;
    bool pitLimiterActive = false;
    bool drsActive = false;
    bool drsAvailable = false;
    bool crossedStartFinish = false;
    bool cornerCutWarning = false;
};

// ============================================================================
// Track Simulator - Main Class
// ============================================================================

class TrackSimulator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString trackName READ trackName NOTIFY trackChanged)
    Q_PROPERTY(QString vehicleName READ vehicleName NOTIFY vehicleChanged)
    Q_PROPERTY(TrackSessionState::SessionType sessionType READ sessionType WRITE setSessionType NOTIFY sessionTypeChanged)
    Q_PROPERTY(bool sessionActive READ isSessionActive NOTIFY sessionStateChanged)
    Q_PROPERTY(double sessionTimeRemaining READ sessionTimeRemaining NOTIFY sessionTimeChanged)
    Q_PROPERTY(int currentLap READ currentLap NOTIFY lapChanged)
    Q_PROPERTY(double currentLapTime READ currentLapTime NOTIFY lapTimeChanged)
    Q_PROPERTY(double bestLapTime READ bestLapTime NOTIFY bestLapChanged)
    Q_PROPERTY(double lastLapTime READ lastLapTime NOTIFY lapCompleted)
    Q_PROPERTY(double currentSpeed READ currentSpeed NOTIFY speedChanged)
    Q_PROPERTY(double fuelRemaining READ fuelRemaining NOTIFY fuelChanged)
    Q_PROPERTY(bool inPitLane READ inPitLane NOTIFY pitLaneStateChanged)
    Q_PROPERTY(bool drsActive READ drsActive NOTIFY drsStateChanged)
    
public:
    explicit TrackSimulator(QObject* parent = nullptr);
    ~TrackSimulator();
    
    // Track management
    bool loadTrack(const QString& trackPath);
    bool loadTrackFromData(const TrackLayout& layout);
    TrackLayout currentTrack() const { return m_trackLayout; }
    QString trackName() const { return m_trackLayout.name; }
    
    // Vehicle management
    bool setVehicle(VehicleSimulator* vehicle);
    VehicleSimulator* vehicle() const { return m_vehicle; }
    QString vehicleName() const { return m_vehicleName; }
    
    // Session control
    void setSessionType(TrackSessionState::SessionType type);
    TrackSessionState::SessionType sessionType() const { return m_sessionState.type; }
    
    void startSession(double durationMinutes = 0.0);  // 0 = untimed
    void stopSession();
    void pauseSession();
    void resumeSession();
    void resetSession();
    
    bool isSessionActive() const { return m_sessionState.sessionActive; }
    double sessionTimeRemaining() const { return m_sessionState.sessionTimeRemaining; }
    double sessionProgress() const;  // 0.0 - 1.0
    
    // Lap/sector timing
    int currentLap() const { return m_sessionState.currentLap; }
    double currentLapTime() const { return m_sessionState.currentLapTime; }
    double bestLapTime() const { return m_sessionState.bestLapTime; }
    double lastLapTime() const { return m_sessionState.lastLapTime; }
    int currentSector() const { return m_sessionState.currentSector; }
    double sectorTime(int sector) const;
    double bestSectorTime(int sector) const;
    
    // Real-time telemetry
    double currentSpeed() const { return m_sessionState.speed; }
    double currentRpm() const { return m_sessionState.rpm; }
    int currentGear() const { return m_sessionState.gear; }
    double throttle() const { return m_sessionState.throttle; }
    double brake() const { return m_sessionState.brake; }
    double steering() const { return m_sessionState.steering; }
    double fuelRemaining() const { return m_sessionState.fuel; }
    double fuelPerLap() const { return m_sessionState.fuelPerLap; }
    
    double tyreTemp(int wheel) const { return m_sessionState.tyreTemp[wheel]; }
    double tyrePressure(int wheel) const { return m_sessionState.tyrePressure[wheel]; }
    double tyreWear(int wheel) const { return m_sessionState.tyreWear[wheel]; }
    
    double trackDistance() const { return m_sessionState.trackDistance; }
    double lateralOffset() const { return m_sessionState.lateralOffset; }
    QVector3D worldPosition() const { return m_sessionState.worldPosition; }
    
    bool inPitLane() const { return m_sessionState.inPitLane; }
    bool pitLimiterActive() const { return m_sessionState.pitLimiterActive; }
    bool drsActive() const { return m_sessionState.drsActive; }
    bool drsAvailable() const { return m_sessionState.drsAvailable; }
    
    // Lap history
    QVector<TrackSessionState::LapRecord> lapHistory() const { return m_sessionState.lapHistory; }
    
    // Main update
    void update(double dt);

    // Analysis
    double estimateLapTime() const;
    double estimateFuelForLaps(int laps) const;
    int estimateLapsRemaining() const;
    
    // Track queries
    const TrackCorner* cornerAtDistance(double distance) const;
    const TrackSector* sectorAtDistance(double distance) const;
    const TrackLayout::DrsZone* drsZoneAtDistance(double distance) const;
    const TrackLayout::SurfaceSection* surfaceAtDistance(double distance) const;
    double racingLineLateralOffset(const QVector3D& position) const;
    double targetSpeedAtDistance(double distance) const;
    
    // AI/Autonomous driving
    struct TargetState {
        double targetSpeed;
        double targetSteering;
        double targetThrottle;
        double targetBrake;
        int targetGear;
        QString currentCorner;
    };
    TargetState computeAiTarget(double lookaheadDistance = 50.0) const;
    
    // Replay/Analysis
    struct ReplayFrame {
        double timestamp;
        TrackSessionState state;
    };
    void startRecording();
    void stopRecording();
    QVector<ReplayFrame> replayData() const { return m_replayData; }
    void clearReplay();
    
    // Configuration
    void setRealTimeMultiplier(double multiplier) { m_realTimeMultiplier = multiplier; }
    double realTimeMultiplier() const { return m_realTimeMultiplier; }
    
    void setWeatherState(const WeatherState& weather);
    WeatherState weatherState() const { return m_weather; }
    
signals:
    // Session signals
    void sessionStarted();
    void sessionStopped();
    void sessionPaused();
    void sessionResumed();
    void sessionTimeChanged(double remaining);
    void sessionTypeChanged(TrackSessionState::SessionType type);
    
    // Lap/sector signals
    void lapStarted(int lapNumber);
    void lapCompleted(int lapNumber, double lapTime, double bestLapTime);
    void sectorCompleted(int sector, double sectorTime);
    void bestLapUpdated(double lapTime);
    void bestSectorUpdated(int sector, double sectorTime);
    
    // State signals
    void lapChanged(int lap);
    void lapTimeChanged(double time);
    void bestLapChanged(double time);
    void speedChanged(double speed);
    void fuelChanged(double fuel);
    void trackChanged(const QString& name);
    void vehicleChanged(const QString& name);
    void sessionStateChanged(bool active);
    void pitLaneStateChanged(bool inPit);
    void drsStateChanged(bool active);
    
    // Position/tracking
    void positionUpdated(const QVector3D& pos, double trackDist, double lateralOffset);
    void trackLimitWarning(bool warning, const QString& cornerName);
    void drsZoneChanged(bool available);
    
    // Vehicle telemetry
    void tyreTempUpdated(int wheel, double temp);
    void tyrePressureUpdated(int wheel, double pressure);
    void tyreWearUpdated(int wheel, double wear);
    
    // Replay
    void recordingStarted();
    void recordingStopped();
    
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
    
    double distanceToRacingLine(const QVector3D& pos) const;
    double projectToTrack(const QVector3D& pos, double& outTrackDist) const;
    int findCurrentSector(double distance) const;
    int findCurrentCorner(double distance) const;
    
    TrackLayout m_trackLayout;
    VehicleSimulator* m_vehicle = nullptr;
    QString m_vehicleName;
    TrackSessionState m_sessionState;
    WeatherState m_weather;
    QElapsedTimer m_sessionTimer;
    double m_realTimeMultiplier = 1.0;
    
    // Replay
    bool m_recording = false;
    QVector<ReplayFrame> m_replayData;
    double m_lastReplayTime = 0.0;
    
    // AI targets
    mutable TargetState m_aiTarget;
    mutable double m_aiLookahead = 50.0;
    
    static TrackSimulator* s_instance;
};

} // namespace physics
} // namespace ks
