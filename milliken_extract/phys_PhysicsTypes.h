#pragma once

/**
 * @file PhysicsTypes.h
 * @brief Additional physics types beyond core types
 * @copyright KS Physics Engine
 */

#include "PhysicsCoreTypes.h"

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

// Pair Analysis Model is defined in VehiclePhysics.h

// ============================================================================
// Track Layout
// ============================================================================

struct TrackSector { QString name; double startDistance, endDistance; double length() const { return endDistance - startDistance; } };

struct TrackCorner {
    int number; QString name; double position, radius, entrySpeed, apexSpeed, exitSpeed, banking;
    enum class Type { Left, Right, Chicane, Hairpin, Sweeper, Straight };
    Type type = Type::Left;
};

struct TrackLayout {
    QString name, config; double length = 0.0f;
    QVector3D startFinishPosition, startFinishDirection;
    QVector<TrackCorner> corners;
    QVector<TrackSector> sectors;
    QVector<QVector3D> racingLine;
    QVector<double> racingLineDistances;
    QVector<QPair<double, double>> elevationProfile;
    double pitEntryDistance = 0, pitExitDistance = 0, pitLaneLength = 0, pitSpeedLimit = 60;

    struct DrsZone {
        int id = 0;
        double startDistance = 0, endDistance = 0;
        double detectionPoint = 0;
    };
    QVector<DrsZone> drsZones;

    struct SurfaceSection {
        enum class Surface { Asphalt, Concrete, Kerb, Gravel, Grass, Dirt, Ice };
        double startDistance = 0, endDistance = 0;
        double gripLevel = 1.0, bumpiness = 0;
        Surface surface = Surface::Asphalt;
    };
    QVector<SurfaceSection> surfaceSections;
};

// ============================================================================
// Track Session State
// ============================================================================

struct TrackSessionState {
    enum class SessionType { Practice, Qualifying, Race, TimeAttack, Test, Hotlap };
    SessionType type = SessionType::Practice;
    QString trackName, vehicleName, driverName;
    bool sessionActive = false;
    double sessionTimeRemaining = 0, sessionTotalTime = 0;
    int currentLap = 0, totalLaps = 0;
    double currentLapTime = 0, bestLapTime = 1e9, lastLapTime = 0;
    double currentSectorTime = 0; int currentSector = 1;
    double sector1Time = 0, sector2Time = 0, sector3Time = 0;
    double bestSector1 = 1e9, bestSector2 = 1e9, bestSector3 = 1e9;
    double speed = 0.0;
    struct LapRecord { int lapNumber = 0; double lapTime = 0, sector1 = 0, sector2 = 0, sector3 = 0, maxSpeed = 0, avgSpeed = 0, fuelUsed = 0; bool valid = false; QString timestamp; };
    QVector<LapRecord> lapHistory;
    double rpm = 0; int gear = 1; double throttle = 0, brake = 0, steering = 0, fuel = 100, fuelPerLap = 0;
    double tyreTemp[4] = {30,30,30,30}, tyrePressure[4] = {2.2,2.2,2.0,2.0}, tyreWear[4] = {0};
    double trackDistance = 0, lateralOffset = 0;
    QVector3D worldPosition, worldRotation;
    bool inPitLane = false, pitLimiterActive = false, drsActive = false, drsAvailable = false, crossedStartFinish = false, cornerCutWarning = false;
};

} // namespace physics
} // namespace ks