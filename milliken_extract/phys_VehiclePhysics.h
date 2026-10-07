#pragma once

#include "PhysicsEngine.h"
#include "ErsDrsController.h"
#include "WeatherPhysics.h"
#include "EngineSimulator.h"
#include "DamageSystem.h"
#include "TireWearSystem.h"
#include "BrakeWearSystem.h"
#include <QObject>
#include <QVector3D>
#include <QVector>
#include <QElapsedTimer>
#include <memory>
#include <vector>
#include <array>

// Forward declarations for AC models (global namespace)
class KsTireModel;
class EngineModel;
class AeroModel;
class DifferentialModel;
class SuspensionModel;
class BrakeThermalModel;
class BrakeModelManager;
class HybridSystem;

namespace ks::physics {

// ============================================================================
// Generic Model Implementations (fallback when AC models not loaded)
// ============================================================================

struct GenericTireModel {
    float corneringStiffness = 80000.0f;
    float longitudinalStiffness = 60000.0f;
    float peakSlipAngle = 8.0f;
    float peakSlipRatio = 0.08f;
    float peakLateralForce = 12000.0f;
    float peakLongitudinalForce = 10000.0f;
    double calculateLateralForce(double slipAngle, double normalLoad, double friction) const;
    double calculateLongitudinalForce(double slipRatio, double normalLoad, double friction) const;
};

struct GenericEngineModel {
    float maxTorque = 400.0f;
    float maxPower = 350.0f;
    float peakTorqueRpm = 4000.0f;
    float maxRpm = 7500.0f;
    double calculateTorque(double rpm, double throttle) const;
    double calculatePower(double rpm) const;
};

struct GenericAeroModel {
    float dragCoefficient = 0.35f;
    float frontalArea = 2.0f;
    float downforceCoefficient = 0.5f;
    float liftCoefficient = 0.1f;
    double calculateDrag(double speed, double cd, double area) const;
    double calculateLift(double speed) const;
};

struct GenericDiffModel {
    float coastDrag = 10.0f;
    float preload = 20.0f;
    float rampAngle = 30.0f;
    float initialTorque = 50.0f;
    double calculateTorqueSplit(double inputTorque, double slipRatio) const;
};

// ============================================================================
// Vehicle Axis System (Ch.4)
// ============================================================================

enum class CoordinateSystem { SAE, ISO, OpenXR, OpenGL, Custom };

struct VehicleAxes { QVector3D forward, left, up, origin; };
struct WorldFrame { QVector3D north, east, down; };
struct BodyFrame { QVector3D position, velocity, angularVelocity; QMatrix4x4 rotation; };

struct MotionState {
    float x = 0, y = 0, z = 0;
    float roll = 0, pitch = 0, yaw = 0;
    float vx = 0, vy = 0, vz = 0;
    float rollRate = 0, pitchRate = 0, yawRate = 0;
    float ax = 0, ay = 0, az = 0;
};

struct TireContactFrame {
    QVector3D contactPoint, normal, forward, lateral;
    float slipAngle = 0, slipRatio = 0, normalLoad = 0;
};

class VehicleAxisSystem {
public:
    VehicleAxisSystem(CoordinateSystem system = CoordinateSystem::SAE);
    QVector3D toWorld(const QVector3D& body, const BodyFrame& frame) const;
    QVector3D toBody(const QVector3D& world, const BodyFrame& frame) const;
    MotionState fromAccelerations(float latAccel, float yawRate, float speed, float wb, float steer) const;
    float normalizeAngle(float a) const;
    float wrapToPi(float a) const;
    float wrapTo2Pi(float a) const;
    QMatrix4x4 rotationFromEuler(float r, float p, float y) const;
    QMatrix4x4 translationMatrix(const QVector3D& translation) const;
private:
    CoordinateSystem m_system;
    VehicleAxes m_axes;
    WorldFrame m_worldFrame;
    void initializeAxes();
};

// ============================================================================
// Tire Transient Model (Ch.2)
// ============================================================================

struct TireTransientState {
    float lateralForceFilter = 0, longitudinalForceFilter = 0;
    float slipAngleFiltered = 0, slipRatioFiltered = 0;
    float relaxationLengthLateral = 0.04f, relaxationLengthLongitudinal = 0.04f;
    float tireWarmUpFactor = 0, carcassDeflection = 0;
};

class TireTransientModel {
public:
    void update(float dt, float speed, float wheelAngVel, float radius, float slipAngle, float slipRatio, float temp, float load);
    TireTransientState getState() const { return m_state; }
    float calculateRelaxationLengthLateral(float load) const;
    float calculateRelaxationLengthLongitudinal(float load) const;
    float calculateWarmUpFactor(float temp, float optimal) const;
private:
    TireTransientState m_state;
    float m_relaxationBase = 0.04f, m_warmUpTC = 5.0f, m_optimalTemp = 80.0f;
};

// ============================================================================
// Vehicle Dynamics Model (Ch.5-6, Ch.8)
// ============================================================================

struct StabilityDerivatives {
    float dFy_dAlpha = 0, dFy_dKappa = 0, dFy_dGamma = 0;
    float dMz_dAlpha = 0, dMz_dR = 0;
    float understeerGradient = 0, yawVelocityGain = 0, sideSlipGain = 0;
    float yawInertia = 0, yawTimeConstant = 0, lateralTimeConstant = 0;
    float controlAuthority = 0, corneringStiffnessFront = 0, corneringStiffnessRear = 0;
};

struct MomentMethodResult { float sideForce = 0, yawMoment = 0, staticMargin = 0; bool isStable = false; };

class VehicleDynamicsModel {
public:
    StabilityDerivatives calculateStabilityDerivatives(float mass, float wb, float fDist, float rDist, float fCf, float rCf, float cgH, float fRoll, float rRoll) const;
    MomentMethodResult calculateMomentMethod(float slipAngle, float yawRate, float speed, const StabilityDerivatives& d) const;
    std::vector<MomentMethodResult> generateMomentMethodDiagram(float speed, float maxSlip, int n, const StabilityDerivatives& d) const;
    float calculateUndersteerGradient(float fCf, float rCf, float mass, float wb, float fDist) const;
    float calculateYawVelocityGain(float speed, const StabilityDerivatives& d) const;
    float calculateCriticalSpeed(const StabilityDerivatives& d) const;
};

// ============================================================================
// Weight Transfer Model (Ch.18)
// ============================================================================

struct SuspensionGeometry {
    float frontRollCenterHeight = 0, rearRollCenterHeight = 0;
    float antiDivePercent = 0, antiSquatPercent = 0;
    float frontInstantCenterHeight = 0, rearInstantCenterHeight = 0;
};

struct WeightTransferResult {
    float frontLeftLoad = 0, frontRightLoad = 0, rearLeftLoad = 0, rearRightLoad = 0;
    float lateralLoadTransferFront = 0, lateralLoadTransferRear = 0;
    float longitudinalLoadTransfer = 0, rollAngle = 0, rollGradient = 0;
};

class WeightTransferModel {
public:
    WeightTransferResult calculate(float mass, float wb, float tw, float fDist, float rDist, float cgH, float latAccel, float longAccel, const SuspensionGeometry& geo, float fRoll, float rRoll, float totalRoll) const;
    float calculateRollCenterHeight(float armLen, float armAngle) const;
    float calculateAntiDivePercent(float icHeight, float cgH, float fDist, float wb) const;
    float calculateAntiSquatPercent(float icHeight, float cgH, float rDist, float wb) const;
    float calculateRollGradient(float mass, float cgH, float tw, float totalRoll) const;
private:
    float calculateLateralTransfer(float mass, float latAccel, float cgH, float tw, float axleDist, float wb, float rollStiff, float rcHeight, float totalRoll) const;
};

// ============================================================================
// GG Diagram (Ch.9)
// ============================================================================

struct GGPoint { float lateralG = 0, longitudinalG = 0, speed = 0; };
struct GGLimits { float maxLateralG = 1.5f, maxBrakingG = 1.2f, maxAccelerationG = 0.8f, frictionEllipseRatio = 0.9f; };

class GGDiagram {
public:
    void calculate(float speed, float downforce, float mass, float latMax, float brkMax, float accMax);
    std::vector<GGPoint> getEnvelope(int n = 64) const;
    bool isInEnvelope(float latG, float longG) const;
    float getMaximumLateralG(float longG) const;
    float getMaximumLongitudinalG(float latG) const;
    GGLimits getLimits() const { return m_limits; }
private:
    GGLimits m_limits;
    std::vector<GGPoint> m_envelope;
    float calculateEllipseRadius(float angle) const;
};

// ============================================================================
// Suspension Geometry Model (Ch.17)
// ============================================================================

struct CamberResult { float staticCamber = 0, camberGain = 0, dynamicCamber = 0, rollCamber = 0; };
struct RollCenterMigration { float frontRollCenterHeight = 0, rearRollCenterHeight = 0, rollCenterLateralOffset = 0; };

class SuspensionGeometryModel {
public:
    CamberResult calculateCamber(float staticCamber, float compression, float camberGain, float rollAngle, float rollCamberCoeff) const;
    RollCenterMigration calculateRollCenterMigration(float fComp, float rComp, float fRCBase, float rRCBase, float fLatRate, float rLatRate) const;
    float calculateMotionRatio(float wheelTravel, float springTravel) const;
    float calculateRollCenterHeight(float upperLen, float lowerLen, float upperAngle, float lowerAngle, float tw) const;
    float calculateCamberGainFromGeometry(float upperLen, float lowerLen, float upperAngle, float lowerAngle) const;
};

// ============================================================================
// Advanced Aero Model (Ch.3, Ch.13-15)
// ============================================================================

struct AeroComponent { float area = 1, cl = 0, cd = 0, efficiency = 0.95f, posX = 0, posZ = 0; };
struct DiffuserConfig { float exitAngle = 10, throatArea = 0.1f, exitArea = 0.3f, length = 0.5f, expansionRatio = 3.0f; };
struct WingConfig { float span = 1.5f, chord = 0.3f, angleOfAttack = 10, flapAngle = 0, endplateSize = 0.02f; bool hasGurneyFlap = false; float gurneyHeight = 0.005f; };
struct GroundEffectState { float rideHeightFront = 0.05f, rideHeightRear = 0.07f, porpoisingAmplitude = 0, porpoisingFrequency = 0, venturiPressure = 0, downforceGain = 1; };
struct AeroForcesAdvanced { float totalDownforce = 0, totalDrag = 0, frontDownforce = 0, rearDownforce = 0, aeroBalance = 0.5f, ldRatio = 0, inducedDrag = 0, parasiticDrag = 0, groundEffectBonus = 0, diffuserContribution = 0, wingContribution = 0; };

class AdvancedAeroModel {
public:
    AeroForcesAdvanced calculate(float speed, float airDensity, const GroundEffectState& ge, const DiffuserConfig& diff, const WingConfig& frontWing, const WingConfig& rearWing) const;
    float calculateWingDownforce(const WingConfig& w, float speed, float rho) const;
    float calculateWingDrag(const WingConfig& w, float speed, float rho) const;
    float calculateDiffuserDownforce(const DiffuserConfig& d, float speed, float rho, float rideH) const;
    float calculateGroundEffectBonus(float rideH, float speed, float rho) const;
    float calculateInducedDrag(float downforce, float span, float rho, float speed) const;
    float calculatePorpoisingThreshold(const DiffuserConfig& d, float speed) const;
private:
    float calculateDynamicPressure(float speed, float rho) const;
    float calculateReynoldsCorrection(float speed, float chord) const;
};

// ============================================================================
// Pair Analysis Model (Ch.7)
// ============================================================================

struct AxlePair { float wheelBase = 2.4f; float frontLateralForce = 0, rearLateralForce = 0, frontSlipAngle = 0, rearSlipAngle = 0, frontCorneringStiffness = 80000, rearCorneringStiffness = 80000, frontNormalLoad = 5000, rearNormalLoad = 5000; };
struct PairAnalysisResult { float understeerGradient = 0, balanceFactor = 0, frontSlipRatio = 0, rearSlipRatio = 0, yawMoment = 0, sideForce = 0; bool isOversteer = false, isUndersteer = false, isNeutral = false; float criticalSlipAngle = 0, peakLateralG = 0; };
struct PairSensitivity { float dFyFront_dAlpha = 0, dFyRear_dAlpha = 0, dMz_dAlpha = 0, dBeta_dAlpha = 0, dR_dAlpha = 0; };

class PairAnalysisModel {
public:
    PairAnalysisResult analyze(const AxlePair& pair, float wb, float mass, float speed) const;
    PairSensitivity calculateSensitivity(const AxlePair& pair, float wb, float mass) const;
    float calculateSlipAngleDistribution(float fSlip, float rSlip, float fCf, float rCf) const;
    float calculateStabilityIndex(float fCf, float rCf, float fDist, float rDist) const;
    float calculateNeutralSteerSpeed(float fCf, float rCf, float wb) const;
    float calculateMaximumCorneringSpeed(float radius, float maxG) const;
private:
    float calculateYawMomentFromSlip(float fSlip, float rSlip, float fForce, float rForce, float fDist, float rDist) const;
};

// ============================================================================
// Driving Conditions Model (Ch.10-11)
// ============================================================================

struct ConditionState { DrivingCondition condition = DrivingCondition::Dry; float gripCoefficient = 1, waterDepth = 0, visibility = 1, trackTemperature = 30, ambientTemperature = 20, windSpeed = 0, windAngle = 0; bool hydroplaningRisk = false; };
struct GripModifiers { float surfaceGrip = 1, temperatureEffect = 1, wetnessEffect = 1, tireCompoundEffect = 1, totalGrip = 1, brakingGrip = 1, corneringGrip = 1, accelerationGrip = 1; };
struct ConditionEffect { float speedReduction = 0, brakingDistanceIncrease = 0, corneringSpeedReduction = 0, tireWearIncrease = 0, riskLevel = 0; QString recommendedTireCompound, drivingAdvice; };

class DrivingConditionsModel {
public:
    ConditionState evaluate(float precip, float temp, float windSpeed, float windAngle) const;
    GripModifiers calculateGrip(const ConditionState& s, float tireTemp, float compound) const;
    ConditionEffect calculateEffect(const ConditionState& s, float speed, float refGrip) const;
    float calculateHydroplaningRisk(float waterDepth, float tireDepth, float speed) const;
    float calculateGripFromWaterDepth(float depth) const;
    float calculateGripFromTemperature(float temp) const;
    float calculateWindEffect(float windSpeed, float windAngle, float carSpeed) const;
    QString getConditionName(DrivingCondition c) const;
private:
    float calculateSurfaceGrip(DrivingCondition c) const;
    float calculateTemperatureEffect(float trackTemp, float optimal) const;
};

// ============================================================================
// Chassis Setup Model (Ch.12)
// ============================================================================

struct WeightDistribution { float frontPercent = 50, rearPercent = 50, leftPercent = 50, rightPercent = 50, crossWeight = 0, frontLeftPercent = 25, frontRightPercent = 25, rearLeftPercent = 25, rearRightPercent = 25; };
struct BalanceSetup { float springRateFront = 25000, springRateRear = 28000, damperBumpFront = 2000, damperBumpRear = 2200, damperReboundFront = 4000, damperReboundRear = 4400, antiRollBarFront = 15000, antiRollBarRear = 18000, tirePressureFront = 2.2f, tirePressureRear = 2.0f, camberFront = -3, camberRear = -2, toeFront = 0.1f, toeRear = -0.1f; };
struct ChassisAnalysis { float frontRollStiffness = 0, rearRollStiffness = 0, totalRollStiffness = 0, frontRollPercent = 0, understeerContribution = 0, oversteerContribution = 0, naturalFrequencyFront = 0, naturalFrequencyRear = 0, dampingRatioFront = 0, dampingRatioRear = 0; bool isBalanced = false; QString recommendation; };
struct SetupRecommendation { float springRateChange = 0, damperChange = 0, antiRollBarChange = 0, tirePressureChange = 0, camberChange = 0; QString reason; float expectedImprovement = 0; };

class ChassisSetupModel {
public:
    WeightDistribution calculateWeightDistribution(float mass, float wb, float tw, float fDist, float rDist, float cgH, float latOffset) const;
    ChassisAnalysis analyzeSetup(const BalanceSetup& s, float mass, float wb) const;
    SetupRecommendation recommendSetup(const ChassisAnalysis& a, const BalanceSetup& cur, float targetUndersteer) const;
    float calculateOptimalFrontSpringRate(float mass, float wb, float fDist, float targetFreq) const;
    float calculateOptimalRearSpringRate(float mass, float wb, float rDist, float targetFreq) const;
    float calculateNaturalFrequency(float springRate, float mass, float motionRatio) const;
    float calculateDampingRatio(float damperRate, float springRate, float mass) const;
    float calculateCrossWeight(float fl, float fr, float rl, float rr) const;
private:
    float calculateSpringRateForFrequency(float mass, float freq, float motionRatio) const;
};

// ============================================================================
// Braking Model (Ch.16)
// ============================================================================

struct BrakeConfig { float frontBrakeBias = 60, rearBrakeBias = 40, maxBrakePressure = 100, brakeResponseTime = 0.05f, frontBrakeArea = 0.015f, rearBrakeArea = 0.012f, frontPadFriction = 0.4f, rearPadFriction = 0.4f; bool hasABS = false; float absThreshold = 0.15f; };
struct BrakeState { float frontBrakeTorque = 0, rearBrakeTorque = 0, totalBrakeTorque = 0, brakeBias = 60, deceleration = 0, brakeTemperature = 300, brakeFade = 0; bool absActive = false; float slipRatioFront = 0, slipRatioRear = 0; };
struct BrakeTransition { float responseDelay = 0, buildUpTime = 0, peakDeceleration = 0, stableDeceleration = 0, fadeRate = 0, temperatureSensitivity = 0; };
struct DynamicBrakeBias { float staticBias = 60, dynamicBias = 60, loadTransferEffect = 0, speedEffect = 0, temperatureEffect = 0, optimalBias = 62, biasWindow = 3; };

class BrakingModel {
public:
    BrakeState calculate(const BrakeConfig& c, float input, float speed, float loadFront, float loadRear, float dt) const;
    BrakeTransition analyzeTransition(const BrakeConfig& c, float targetDecel) const;
    DynamicBrakeBias calculateDynamicBias(const BrakeConfig& c, float speed, float loadFront, float loadRear, float temp) const;
    float calculateOptimalBrakeBias(float fDist, float wb, float cgH, float maxDecel) const;
    float calculateBrakeTemperature(float curTemp, float torque, float speed, float ambient, float dt) const;
    float calculateBrakeFade(float temp) const;
    float calculateBrakeResponse(float input, float responseTime, float dt) const;
    float calculateLockupThreshold(float load, float grip) const;
    float calculateABSIntervention(float slip, float threshold) const;
private:
    float calculateBrakeTorque(float pressure, float area, float friction, float radius) const;
};

// ============================================================================
// Traction Control Model (Ch.19+)
// ============================================================================

enum class TractionControlMode { Off, Conservative, Balanced, Aggressive, Custom };

struct TractionControlConfig { TractionControlMode mode = TractionControlMode::Balanced; float slipThreshold = 0.15f, slipTarget = 0.10f, interventionRate = 0.3f, maxTorqueReduction = 0.5f, temperatureThreshold = 100; bool perWheelControl = true, launchControlEnabled = false; float launchRpm = 4000, launchSlipTarget = 0.20f; };
struct TractionControlState { float slipRatio[4] = {0}, torqueReduction[4] = {0}; bool interventionActive = false; float interventionLevel = 0, wheelSpeed[4] = {0}, vehicleSpeed = 0; bool isLimited = false; };
struct LaunchControlState { bool active = false; float targetRpm = 4000, currentRpm = 0; bool launchReady = false; float countdown = 0, launchTorque = 0; bool launchComplete = false; };
struct TractionControlResult { float throttleMultiplier = 1, brakeMultiplier = 1; bool tcActive = false; float tcLevel = 0; QString statusMessage; };

class TractionControlModel {
public:
    TractionControlResult calculate(const TractionControlConfig& c, const TractionControlState& s, float throttle, float brake) const;
    LaunchControlState calculateLaunch(const TractionControlConfig& c, const LaunchControlState& ls, float throttle, float rpm, float speed) const;
    float calculateSlipRatio(float wheelSpeed, float vehicleSpeed) const;
    float calculateTorqueReduction(float slip, float threshold, float target, float rate) const;
    float calculateThrottleMultiplier(float slip, const TractionControlConfig& c) const;
    bool checkWheelSpin(float slip, float threshold) const;
    float calculateOptimalSlip(float speed, float grip) const;
private:
    float calculateSlipControl(float slip, float target, float maxReduction) const;
};

// ============================================================================
// Active Suspension Model (Ch.19+)
// ============================================================================

enum class SuspensionMode { Passive, SemiActive, Active, Hydraulic, Magnetic };

struct ActiveSuspensionConfig { float maxForce = 5000, responseTime = 0.01f, powerConsumption = 100, maxDisplacement = 0.05f, controlFrequency = 50; bool antiRollEnabled = true, loadLevelingEnabled = true, rideHeightControl = true; };
struct SuspensionState { float position = 0, velocity = 0, force = 0, targetPosition = 0, error = 0; bool isActive = false; float powerDraw = 0; };
struct ActiveSuspensionState { SuspensionState frontLeft, frontRight, rearLeft, rearRight; float totalPowerDraw = 0, rideHeightFront = 0.05f, rideHeightRear = 0.07f, rollAngle = 0, pitchAngle = 0; };
struct SuspensionCommand { float targetForceFL = 0, targetForceFR = 0, targetForceRL = 0, targetForceRR = 0; bool enableAntiRoll = false, enableLoadLeveling = false, adjustRideHeight = false; };

class ActiveSuspensionModel {
public:
    ActiveSuspensionState calculate(const ActiveSuspensionConfig& c, const SuspensionCommand& cmd, float speed, float latAccel, float longAccel, float dt) const;
    float calculateActiveForce(float target, float current, float maxForce, float responseTime, float dt) const;
    float calculateAntiRollForce(float left, float right, float maxForce) const;
    float calculateLoadLevelingForce(float curHeight, float targetHeight, float maxForce) const;
    float calculateRideHeightForce(float curHeight, float targetHeight, float maxForce) const;
    float calculatePowerConsumption(float force, float velocity) const;
private:
    float pidController(float error, float kp, float ki, float kd, float dt) const;
};

// ============================================================================
// Four-Wheel Steering Model (Ch.19+)
// ============================================================================

enum class SteeringMode { FrontWheelOnly, FourWheelSteering, RearWheelSteering, CounterSteer, CrabSteering };

struct SteeringConfig { float frontSteerLock = 22, rearSteerLock = 5, steerRatio = 15, rearSteerRatio = 20, speedThresholdLow = 30, speedThresholdHigh = 80, maxRearSteerAngle = 3; bool adaptiveEnabled = false; };
struct SteeringState { float frontSteerAngle = 0, rearSteerAngle = 0, totalSteerAngle = 0, effectiveWheelBase = 2.7f, turningRadius = 10; bool isCounterSteering = false, isCrabSteering = false; };
struct SteeringResponse { float yawRateResponse = 0, lateralAccelResponse = 0, sideslipAngle = 0, responseTime = 0.1f, overshoot = 0; };

class FourWheelSteeringModel {
public:
    SteeringState calculate(const SteeringConfig& c, float frontInput, float speed) const;
    SteeringResponse calculateResponse(const SteeringState& s, float speed, float yawRate) const;
    float calculateRearSteerAngle(const SteeringConfig& c, float frontAngle, float speed) const;
    float calculateTurningRadius(float wb, float frontAngle, float rearAngle) const;
    float calculateEffectiveWheelBase(float wb, float frontAngle, float rearAngle) const;
    SteeringMode determineMode(const SteeringConfig& c, float speed) const;
private:
    float interpolateRearSteer(float speed, float lowAngle, float highAngle, float speedLow, float speedHigh) const;
};

// ============================================================================
// Simulation Methods (Ch.19+)
// ============================================================================

struct SimulationConfig { IntegrationMethod integrationMethod = IntegrationMethod::RungeKutta4; float fixedTimeStep = 0.001f, maxTimeStep = 0.01f; int solverIterations = 10; bool enableAero = true, enableSuspension = true, enableDrivetrain = true, enableBrakes = true, enableTires = true, enableWeather = true, enableDamage = false; float gravity = 9.81f, airDensity = 1.225f; };

struct SimStepState { float time = 0, dt = 0.001f; int stepCount = 0; float cpuTime = 0; bool isRunning = false, isPaused = false; float realtimeFactor = 1; };

struct VehicleSimState { float position[3] = {0}, velocity[3] = {0}, acceleration[3] = {0}, orientation[3] = {0}, angularVelocity[3] = {0}, wheelSpeed[4] = {0}, wheelAngle[4] = {0}; float engineRpm = 0; int gear = 1; };

struct PhysicsForces { float tireForces[4][3] = {{0}}; float aeroForce[3] = {0}; float suspensionForces[4][3] = {{0}}; float brakeForces[4] = {0}, driveForces[4] = {0}; float totalForce[3] = {0}, totalMoment[3] = {0}; };

class SimulationMethodsModel {
public:
    VehicleSimState integrate(const VehicleSimState& cur, const PhysicsForces& f, float mass, float inertia[3], float dt, IntegrationMethod m) const;
    PhysicsForces calculateForces(const VehicleSimState& s, const SimulationConfig& c) const;
    float eulerIntegration(float state, float deriv, float dt) const;
    float rungeKutta2Integration(float state, float derivative, float dt, float (*func)(float)) const;
    float rungeKutta4Integration(float state, float derivative, float dt, float (*func)(float)) const;
    float calculateInertiaComponent(float mass, float length, float width) const;
    void updateStep(VehicleSimState& s, const PhysicsForces& f, float mass, float inertia[3], float dt) const;
    float calculatePhysicsStep(const VehicleSimState& s, const PhysicsForces& f, float mass, float dt) const;
    bool checkCollision(const VehicleSimState& s, float radius) const;
    float calculateEnergy(const VehicleSimState& s, float mass, float inertia[3]) const;
};

// ============================================================================
// IVehicleSimulator (Abstract Interface)
// ============================================================================

class IVehicleSimulator : public ISimulator {
    Q_OBJECT
public:
    explicit IVehicleSimulator(QObject* parent = nullptr) : ISimulator(parent) {}
    virtual void setThrottle(double v) = 0;
    virtual void setBrake(double v) = 0;
    virtual void setSteering(double v) = 0;
    virtual void setTireModel(const TireSlipCurve& c) = 0;
    virtual TireSlipCurve tireModel() const = 0;
    virtual LapTimeEstimate estimateLapTime() const = 0;
    virtual void setMass(double kg) = 0;
    virtual void setEnginePower(double kw) = 0;
    virtual void setMaxRpm(double rpm) = 0;
    virtual void setDragCoeff(double cd) = 0;
    virtual void setFrontalArea(double a) = 0;
    virtual void setWheelBase(double wb) = 0;
    virtual void setTrackWidth(double tw) = 0;
    virtual double mass() const = 0;
    virtual double enginePower() const = 0;
    virtual double maxRpm() const = 0;
    virtual void setAbsEnabled(bool e) = 0;
    virtual bool absEnabled() const = 0;
    virtual void setTractionControlEnabled(bool e) = 0;
    virtual bool tractionControlEnabled() const = 0;
    virtual void setAbsThreshold(double s) = 0;
    virtual void setTcThreshold(double s) = 0;
    virtual double absThreshold() const = 0;
    virtual double tcThreshold() const = 0;
    virtual float getBrakeDiscTemp(int w) const = 0;
    virtual float getBrakePadTemp(int w) const = 0;
    virtual float getBrakeFade(int w) const = 0;
    virtual void setErsEnabled(bool e) = 0;
    virtual bool ersEnabled() const = 0;
    virtual void setErsMode(int m) = 0;
    virtual void activateErsAttackMode() = 0;
    virtual void setDriveLayout(DriveLayout l) = 0;
    virtual DriveLayout driveLayout() const = 0;
    virtual void setCenterDiffPreload(double nm) = 0;
    virtual double centerDiffPreload() const = 0;
    virtual void setCenterDiffPower(double p) = 0;
    virtual double centerDiffPower() const = 0;
    virtual void setFrontRearTorqueSplit(double r) = 0;
    virtual float getErsDeployTorque() const = 0;
    virtual float getErsRegenTorque() const = 0;
    virtual float getErsBatterySoc() const = 0;
    virtual float getErsBatteryTemp() const = 0;
    virtual void setDrsEnabled(bool e) = 0;
    virtual bool drsEnabled() const = 0;
    virtual void setDrsAutoActivate(bool a) = 0;
    virtual bool drsAutoActivate() const = 0;
    virtual void setDrsSpeedThreshold(double kph) = 0;
    virtual double drsSpeedThreshold() const = 0;
    virtual void setDrsZoneStart(double d) = 0;
    virtual void setDrsZoneEnd(double d) = 0;
    virtual bool isDrsActive() const = 0;
    virtual double getDrsDragReduction() const = 0;
    virtual void setDrsDragReduction(double f) = 0;
    virtual const DamageState& damageState() const = 0;
    virtual DamageState& damageState() = 0;
    virtual void applyCollisionDamage(double f) = 0;
    virtual void resetDamage() = 0;
    virtual void enableDamageModel(bool e) = 0;
    virtual bool isDamageModelEnabled() const = 0;
    virtual void setWeatherState(const WeatherState& w) = 0;
    virtual const WeatherState& weatherState() const = 0;
    virtual WeatherState& weatherState() = 0;
    virtual void setTrackWetness(double w) = 0;
    virtual void setRainIntensity(double mmh) = 0;
    virtual double getAquaplaningRisk() const = 0;
    virtual double getTrackGripReduction() const = 0;
    virtual void setAirDensity(double d) = 0;
    virtual void setFuelConsumptionEnabled(bool e) = 0;
    virtual bool isFuelConsumptionEnabled() const = 0;
    virtual double getFuelKg() const = 0;
    virtual void setFuelKg(double kg) = 0;
    virtual double getFuelCapacity() const = 0;
    virtual void setFuelCapacity(double liters) = 0;
    virtual double getEffectiveMass() const = 0;
    virtual void loadVehicleParams(const QString& path) = 0;
    virtual void loadEngineFromIni(const QString& path) = 0;
    virtual void loadTyresFromIni(const QString& path) = 0;
    virtual void loadDrivetrainFromIni(const QString& path) = 0;
    virtual void loadAeroFromIni(const QString& path) = 0;
    virtual void loadSuspensionFromIni(const QString& path) = 0;
    virtual WheelState wheelState(int w) const = 0;
    virtual ValidationMetrics validateAgainstTelemetry(
        const QVector<double>& timestamps,
        const QVector<double>& refSpeed,
        const QVector<double>& refLateralG,
        const QVector<double>& refLongG,
        const QVector<double>& refRPM,
        const QVector<double>& refThrottle,
        const QVector<double>& refBrake,
        const QVector<double>& refSteering) const = 0;
};

} // namespace ks::physics

// Simulator headers included outside namespace to avoid double-nesting
#include "TireSimulator.h"
#include "AeroSimulator.h"
#include "ChassisSimulator.h"
#include "DriverSimulator.h"
#include "StrategySimulator.h"
#include "phys_LapTimer.h"
#include "VehiclePhysicsModels.h"

namespace ks::physics {

// ============================================================================
// VehicleSimulator (Main Orchestrator)
// ============================================================================

class VehicleSimulator : public IVehicleSimulator {
    Q_OBJECT
public:
    explicit VehicleSimulator(QObject* parent = nullptr);
    ~VehicleSimulator() override;
    static VehicleSimulator* instance();
    static void cleanup();

    void startSimulation() override;
    void stopSimulation() override;
    void reset() override;
    void setThrottle(double v) override;
    void setBrake(double v) override;
    void setSteering(double v) override;
    SimulationState getState() const override { return m_state; }
    bool isRunning() const override { return m_running; }
    void setTireModel(const TireSlipCurve& c) override;
    TireSlipCurve tireModel() const override { return m_tireModel; }
    LapTimeEstimate estimateLapTime() const override;
    void setMass(double kg) override { m_mass = kg; }
    void setEnginePower(double kw) override { m_enginePowerKw = kw; }
    void setMaxRpm(double rpm) override { m_maxRpm = rpm; }
    void setDragCoeff(double cd) override { m_cd = cd; }
    void setFrontalArea(double a) override { m_frontalArea = a; }
    void setWheelBase(double wb) override { m_wheelBase = wb; }
    void setTrackWidth(double tw) override { m_trackWidth = tw; }
    double mass() const override { return m_mass; }
    double enginePower() const override { return m_enginePowerKw; }
    double maxRpm() const override { return m_maxRpm; }
    void setAbsEnabled(bool e) override { m_absEnabled = e; }
    bool absEnabled() const override { return m_absEnabled; }
    void setTractionControlEnabled(bool e) override { m_tcEnabled = e; }
    bool tractionControlEnabled() const override { return m_tcEnabled; }
    void setAbsThreshold(double s) override { m_absSlipThreshold = s; }
    void setTcThreshold(double s) override { m_tcSlipThreshold = s; }
    double absThreshold() const override { return m_absSlipThreshold; }
    double tcThreshold() const override { return m_tcSlipThreshold; }
    float getBrakeDiscTemp(int w) const override;
    float getBrakePadTemp(int w) const override;
    float getBrakeFade(int w) const override;
    void setErsEnabled(bool e) override { m_ersDrs.setErsEnabled(e); }
    bool ersEnabled() const override { return m_ersDrs.ersEnabled(); }
    void setErsMode(int m) override { m_ersDrs.setErsMode(m); }
    void activateErsAttackMode() override { m_ersDrs.activateAttackMode(); }
    void setDriveLayout(DriveLayout l) override { m_driveLayout = l; }
    DriveLayout driveLayout() const override { return m_driveLayout; }
    void setCenterDiffPreload(double nm) override { m_centerDiffPreload = nm; }
    double centerDiffPreload() const override { return m_centerDiffPreload; }
    void setCenterDiffPower(double p) override { m_centerDiffPower = p; }
    double centerDiffPower() const override { return m_centerDiffPower; }
    void setFrontRearTorqueSplit(double r) override;
    float getErsDeployTorque() const override { return m_ersDrs.getErsDeployTorque(); }
    float getErsRegenTorque() const override { return m_ersDrs.getErsRegenTorque(); }
    float getErsBatterySoc() const override { return m_ersDrs.getErsBatterySoc(); }
    float getErsBatteryTemp() const override { return m_ersDrs.getErsBatteryTemp(); }
    void setDrsEnabled(bool e) override { m_ersDrs.setDrsEnabled(e); }
    bool drsEnabled() const override { return m_ersDrs.drsEnabled(); }
    void setDrsAutoActivate(bool a) override { m_ersDrs.setDrsAutoActivate(a); }
    bool drsAutoActivate() const override { return m_ersDrs.drsAutoActivate(); }
    void setDrsSpeedThreshold(double kph) override { m_ersDrs.setDrsSpeedThreshold(kph); }
    double drsSpeedThreshold() const override { return m_ersDrs.drsSpeedThreshold(); }
    void setDrsZoneStart(double d) override { m_ersDrs.setDrsZoneStart(d); }
    void setDrsZoneEnd(double d) override { m_ersDrs.setDrsZoneEnd(d); }
    bool isDrsActive() const override { return m_ersDrs.isDrsActive(); }
    double getDrsDragReduction() const override { return m_ersDrs.getDrsDragReduction(); }
    void setDrsDragReduction(double f) override { m_ersDrs.setDrsDragReduction(f); }
    const DamageState& damageState() const override { return m_damage; }
    DamageState& damageState() override { return m_damage; }
    void applyCollisionDamage(double f) override;
    void resetDamage() override;
    void enableDamageModel(bool e) override { m_damageEnabled = e; }
    bool isDamageModelEnabled() const override { return m_damageEnabled; }
    void setWeatherState(const WeatherState& w) override;
    const WeatherState& weatherState() const override;
    WeatherState& weatherState() override;
    void setTrackWetness(double w) override;
    void setRainIntensity(double mmh) override;
    double getAquaplaningRisk() const override;
    double getTrackGripReduction() const override;
    void setAirDensity(double d) override;
    void setFuelConsumptionEnabled(bool e) override { m_fuelConsumptionEnabled = e; }
    bool isFuelConsumptionEnabled() const override { return m_fuelConsumptionEnabled; }
    double getFuelKg() const override { return m_fuelKg; }
    void setFuelKg(double kg) override { m_fuelKg = std::max(0.0, kg); }
    double getFuelCapacity() const override { return m_fuelCapacity; }
    void setFuelCapacity(double liters) override { m_fuelCapacity = liters; }
    double getEffectiveMass() const override { return m_mass + m_fuelKg; }
    void loadVehicleParams(const QString& path) override;
    void loadEngineFromIni(const QString& path) override;
    void loadTyresFromIni(const QString& path) override;
    void loadDrivetrainFromIni(const QString& path) override;
    void loadAeroFromIni(const QString& path) override;
    void loadSuspensionFromIni(const QString& path) override;
    WheelState wheelState(int w) const override { return m_wheels[w]; }
    ValidationMetrics validateAgainstTelemetry(
        const QVector<double>& timestamps,
        const QVector<double>& refSpeed,
        const QVector<double>& refLateralG,
        const QVector<double>& refLongG,
        const QVector<double>& refRPM,
        const QVector<double>& refThrottle,
        const QVector<double>& refBrake,
        const QVector<double>& refSteering) const override;
    void updatePhysics(double dt);
    bool areAcModelsLoaded() const { return m_acModelsLoaded; }

    HybridSystem& hybridSystem();
    const HybridSystem& hybridSystem() const;
    BrakeModelManager& brakeModel();
    const BrakeModelManager& brakeModel() const;
    ::ks::phys_LapTimer* lapTimer() { return &m_lapTimer; }

signals:
    void stateUpdated(const SimulationState& state);
    void simulationStarted();
    void simulationStopped();
    void tireDataUpdated(double slipAngle, double latForce, double slipRatio, double longForce);

private:
    void updateTireModel(double dt);
    void updateWeightTransfer(double longAccel, double latAccel);
    void updatePerWheelForces(double dt);
    void updateErsAndDrs(double dt);
    void updateDamageModel(double dt);
    void updateFuelWeight(double dt);
    double calculateSlipAngle(int wheel, double steer, double speed, double yawRate) const;
    double calculateSlipRatio(int wheel) const;

    SimulationState m_state;
    double m_throttle = 0, m_brake = 0, m_steering = 0;
    bool m_running = false;
    double m_mass = 1500, m_enginePowerKw = 350, m_maxRpm = 7500;
    double m_cd = 0.35, m_frontalArea = 2, m_wheelBase = 2.7, m_trackWidth = 1.6;
    int m_currentGear = 1;
    QVector<double> m_gearRatios = {3.5, 2.5, 1.8, 1.4, 1.1, 0.9};
    double m_finalDriveRatio = 3.8, m_wheelRadius = 0.33, m_fuelKg = 80;
    double m_cgHeight = 0.45, m_frontAxleDist = 1.35, m_rearAxleDist = 1.35;
    double m_rollStiffness = 15000, m_steerLock = 22, m_steerRatio = 15;
    TireSlipCurve m_tireModel;

    std::unique_ptr<KsTireModel> m_ksTireModel;
    std::unique_ptr<EngineModel> m_engineModel;
    std::unique_ptr<AeroModel> m_aeroModel;
    std::unique_ptr<DifferentialModel> m_differentialModel;
    std::unique_ptr<SuspensionModel> m_suspensionModel;
    std::unique_ptr<BrakeModelManager> m_brakeModel;
    bool m_acModelsLoaded = false;
    
    // Generic model implementations (fallback when AC models not loaded)
    GenericTireModel m_tireModelImpl;
    GenericEngineModel m_engineModelImpl;
    GenericAeroModel m_aeroModelImpl;
    GenericDiffModel m_diffModelImpl;

    double m_tireSlipAngle[4] = {0}, m_tireSlipRatio[4] = {0};
    bool m_absEnabled = false, m_tcEnabled = false;
    double m_absSlipThreshold = 0.15, m_tcSlipThreshold = 0.12;
    WheelState m_wheels[4];
    double m_yawRate = 0, m_lateralAccel = 0;
    double m_tireGraining[4] = {0}, m_tireBlistering[4] = {0};
    double m_tireTempSurface[4] = {30}, m_tireTempCarcass[4] = {35}, m_tireTempCore[4] = {40};
    double m_tirePressure[4] = {2.4}, m_tireWear[4] = {0}, m_ambientTemp = 26;
    ErsDrsController m_ersDrs;
    bool m_damageEnabled = false;
    DamageState m_damage;
    bool m_fuelConsumptionEnabled = true;
    double m_fuelCapacity = 80;
    ::ks::phys_LapTimer m_lapTimer;
    QElapsedTimer m_simTimer;
    double m_trackLength = 5000, m_lastUpdateTime = 0;
    QVector<double> m_lapTimeHistory;
    PhysicsWorld m_physicsWorld;
    static VehicleSimulator* s_instance;
    
    // Weather integration
    WeatherSimulator m_weatherSim;
    double m_aquaplaningRisk = 0.0;
    double m_trackGripReduction = 0.0;
    
    // Torque split
    double m_frontTorqueSplit = 0.5;
    DriveLayout m_driveLayout = DriveLayout::RWD;
    double m_centerDiffPreload = 0.0;
    double m_centerDiffPower = 0.0;

    // Sub-models
    TireTransientModel m_tireTransientModel;
    VehicleDynamicsModel m_vehicleDynamicsModel;
    WeightTransferModel m_weightTransferModel;
    GGDiagram m_ggDiagram;
    SuspensionGeometryModel m_suspensionGeometryModel;
    AdvancedAeroModel m_advancedAeroModel;
    PairAnalysisModel m_pairAnalysisModel;
    DrivingConditionsModel m_drivingConditionsModel;
    ChassisSetupModel m_chassisSetupModel;
    BrakingModel m_brakingModel;
    StabilityDerivatives m_stabilityDerivs;
    WeightTransferResult m_weightTransferResult;
    SuspensionGeometry m_suspensionGeometry;
    GroundEffectState m_groundEffectState;
    AxlePair m_axlePair;
    BrakeState m_brakeState;
    
    // Advanced models (Milliken book)
    DriverModel m_driverModel;
    TireWearModel m_tireWearModel;
    FuelManagementModel m_fuelManagementModel;
    ThermalModel m_thermalModel;
    MultiBodyDynamics m_multiBodyDynamics;
    TrackConditionsModel m_trackConditionsModel;
    WeatherEffectsModel m_weatherEffectsModel;
    RaceStrategyModel m_raceStrategyModel;
    
    // New specialized simulator classes
    EngineSimulator m_engineSim;
    TireSimulator m_tireSim;
    AeroSimulator m_aeroSim;
    ChassisSimulator m_chassisSim;
    DriverSimulator m_driverSim;
    StrategySimulator m_strategySim;

    // rFactor2-style damage, tire wear, and brake wear systems
    DamageSystem m_damageSystem;
    std::array<TireWearSystem, 4> m_tireWearSystems;
    BrakeWearSystem m_brakeWearSystem;

    TelemetryProcessingModel m_telemetryProcessingModel;
    VehiclePerformanceAnalysisModel m_performanceAnalysisModel;
    SetupOptimizationModel m_setupOptimizationModel;
    TireTestingModel m_tireTestingModel;
    AerodynamicTestingModel m_aerodynamicTestingModel;
    SuspensionTuningModel m_suspensionTuningModel;
    EngineMappingModel m_engineMappingModel;
    DifferentialTuningModel m_differentialTuningModel;
    GearboxModel m_gearboxModel;
    BrakeSystemModel m_brakeSystemModel;
    DriverTrainingModel m_driverTrainingModel;
    RaceEngineeringModel m_raceEngineeringModel;
    SetupDocumentationModel m_setupDocumentationModel;
};

// Analysis models are in VehiclePhysicsModels.h (included below for backward compatibility)
#include "VehiclePhysicsModels.h"

} // namespace ks::physics
