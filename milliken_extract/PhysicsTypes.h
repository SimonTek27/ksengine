#pragma once

/**
 * @file PhysicsTypes.h
 * @brief Additional physics types beyond core types
 * @copyright KS Physics Engine
 *
 * Qt-free. Depends on PhysicsCoreTypes.h (PhysVec3, etc.).
 */

#include "PhysicsCoreTypes.h"

#include <string>
#include <utility>
#include <vector>

namespace ks {
namespace physics {

// ============================================================================
// Additional Physics Types (beyond core types)
// ============================================================================

// Note: Core types (WeatherState, SimulationState, WheelState, TireForceData,
// DamageState, LapTimeEstimate, ValidationMetrics, DriveLayout, TireModelType,
// TireSlipCurve, IntegrationMethod, DrivingCondition) are defined in
// PhysicsCoreTypes.h. This file contains additional types that are used by
// specific physics modules but are not fundamental enough to be in the core
// types header.

// BrakeConfig is defined in VehiclePhysics.h

// ============================================================================
// Brake State (extended)
// ============================================================================

struct BrakeStateExtended {
    float frontBrakeTorque = 0.0f;
    float rearBrakeTorque = 0.0f;
    float totalBrakeTorque = 0.0f;
    float brakeBias = 60.0f;
    float deceleration = 0.0f;
    float brakeTemperature = 300.0f;
    float brakeFade = 0.0f;
    bool absActive = false;
    float slipRatioFront = 0.0f;
    float slipRatioRear = 0.0f;
};

// AeroForcesAdvanced, WingConfig, DiffuserConfig, SuspensionGeometry, WeightTransferResult
// are defined in VehiclePhysics.h

// ============================================================================
// Track Layout
// ============================================================================

struct TrackSector {
    std::string name;
    double startDistance = 0.0;
    double endDistance = 0.0;
    double length() const { return endDistance - startDistance; }
};

struct TrackCorner {
    int number = 0;
    std::string name;
    double position = 0.0;
    double radius = 0.0;
    double entrySpeed = 0.0;
    double apexSpeed = 0.0;
    double exitSpeed = 0.0;
    double banking = 0.0;
    enum class Type { Left, Right, Chicane, Hairpin, Sweeper, Straight };
    Type type = Type::Left;
};

struct TrackLayout {
    std::string name;
    std::string config;
    double length = 0.0;
    PhysVec3 startFinishPosition;
    PhysVec3 startFinishDirection;
    std::vector<TrackCorner> corners;
    std::vector<TrackSector> sectors;
    std::vector<PhysVec3> racingLine;
    std::vector<double> racingLineDistances;
    std::vector<std::pair<double, double>> elevationProfile;
    double pitEntryDistance = 0.0;
    double pitExitDistance = 0.0;
    double pitLaneLength = 0.0;
    double pitSpeedLimit = 60.0;

    struct DrsZone {
        int id = 0;
        double startDistance = 0.0;
        double endDistance = 0.0;
        double detectionPoint = 0.0;
    };
    std::vector<DrsZone> drsZones;

    struct SurfaceSection {
        enum class Surface { Asphalt, Concrete, Kerb, Gravel, Grass, Dirt, Ice };
        double startDistance = 0.0;
        double endDistance = 0.0;
        double gripLevel = 1.0;
        double bumpiness = 0.0;
        Surface surface = Surface::Asphalt;
    };
    std::vector<SurfaceSection> surfaceSections;
};

// ============================================================================
// Track Session State
// ============================================================================

struct TrackSessionState {
    enum class SessionType { Practice, Qualifying, Race, TimeAttack, Test, Hotlap };
    SessionType type = SessionType::Practice;
    std::string trackName;
    std::string vehicleName;
    std::string driverName;
    bool sessionActive = false;
    double sessionTimeRemaining = 0.0;
    double sessionTotalTime = 0.0;
    int currentLap = 0;
    int totalLaps = 0;
    double currentLapTime = 0.0;
    double bestLapTime = 1e9;
    double lastLapTime = 0.0;
    double currentSectorTime = 0.0;
    int currentSector = 1;
    double sector1Time = 0.0;
    double sector2Time = 0.0;
    double sector3Time = 0.0;
    double bestSector1 = 1e9;
    double bestSector2 = 1e9;
    double bestSector3 = 1e9;
    double speed = 0.0;

    struct LapRecord {
        int lapNumber = 0;
        double lapTime = 0.0;
        double sector1 = 0.0;
        double sector2 = 0.0;
        double sector3 = 0.0;
        double maxSpeed = 0.0;
        double avgSpeed = 0.0;
        double fuelUsed = 0.0;
        bool valid = false;
        std::string timestamp;
    };
    std::vector<LapRecord> lapHistory;

    double rpm = 0.0;
    int gear = 1;
    double throttle = 0.0;
    double brake = 0.0;
    double steering = 0.0;
    double fuel = 100.0;
    double fuelPerLap = 0.0;
    double tyreTemp[4] = {30.0, 30.0, 30.0, 30.0};
    double tyrePressure[4] = {2.2, 2.2, 2.0, 2.0};
    double tyreWear[4] = {0.0, 0.0, 0.0, 0.0};
    double trackDistance = 0.0;
    double lateralOffset = 0.0;
    PhysVec3 worldPosition;
    PhysVec3 worldRotation;
    bool inPitLane = false;
    bool pitLimiterActive = false;
    bool drsActive = false;
    bool drsAvailable = false;
    bool crossedStartFinish = false;
    bool cornerCutWarning = false;
};

} // namespace physics
} // namespace ks
