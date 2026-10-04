#pragma once

#include "interfaces/IVehicleSimulator.h"
#include "HybridSystem.h"
#include "BrakeThermalModel.h"
#include "phys_LapTimer.h"

namespace ks {
namespace physics {

class VehicleSimulator : public IVehicleSimulator {
    Q_OBJECT
public:
    explicit VehicleSimulator(QObject* parent = nullptr);
    ~VehicleSimulator() override;

    static VehicleSimulator* instance();
    ::HybridSystem& hybridSystem() { return m_hybridSystem; }
    const ::HybridSystem& hybridSystem() const { return m_hybridSystem; }
    BrakeModelManager& brakeModel() { return m_brakeModel; }
    const BrakeModelManager& brakeModel() const { return m_brakeModel; }
    ::ks::phys_LapTimer* lapTimer() { return &m_lapTimer; }

    void startSimulation() override;
    void stopSimulation() override;
    void reset() override;

    void setThrottle(double value) override;
    void setBrake(double value) override;
    void setSteering(double value) override;

    SimulationState getState() const override { return m_state; }
    bool isRunning() const override { return m_running; }

    void setTireModel(const TireSlipCurve& curve) override;
    TireSlipCurve tireModel() const override { return m_tireModel; }

    LapTimeEstimate estimateLapTime() const override;

    void setMass(double kg) override { m_mass = kg; }
    void setEnginePower(double kw) override { m_enginePowerKw = kw; }
    void setMaxRpm(double rpm) override { m_maxRpm = rpm; }
    void setDragCoeff(double cd) override { m_cd = cd; }
    void setFrontalArea(double area) override { m_frontalArea = area; }
    void setWheelBase(double wb) override { m_wheelBase = wb; }
    void setTrackWidth(double tw) override { m_trackWidth = tw; }

    double mass() const override { return m_mass; }
    double enginePower() const override { return m_enginePowerKw; }
    double maxRpm() const override { return m_maxRpm; }

    void setAbsEnabled(bool enabled) override { m_absEnabled = enabled; }
    bool absEnabled() const override { return m_absEnabled; }
    void setTractionControlEnabled(bool enabled) override { m_tcEnabled = enabled; }
    bool tractionControlEnabled() const override { return m_tcEnabled; }
    void setAbsThreshold(double slipRatio) override { m_absSlipThreshold = slipRatio; }
    void setTcThreshold(double slipRatio) override { m_tcSlipThreshold = slipRatio; }
    double absThreshold() const override { return m_absSlipThreshold; }
    double tcThreshold() const override { return m_tcSlipThreshold; }

    float getBrakeDiscTemp(int wheel) const override;
    float getBrakePadTemp(int wheel) const override;
    float getBrakeFade(int wheel) const override;

    void setErsEnabled(bool enabled) override;
    bool ersEnabled() const override { return m_ersEnabled; }
    void setErsMode(int mode) override;
    void activateErsAttackMode() override;

    void setDriveLayout(DriveLayout layout) override { m_driveLayout = layout; }
    DriveLayout driveLayout() const override { return m_driveLayout; }
    void setCenterDiffPreload(double nm) override { m_centerDiffPreload = nm; }
    double centerDiffPreload() const override { return m_centerDiffPreload; }
    void setCenterDiffPower(double power) override { m_centerDiffPower = power; }
    double centerDiffPower() const override { return m_centerDiffPower; }
    void setFrontRearTorqueSplit(double frontRatio) override;
    float getErsDeployTorque() const override { return m_ersDeployTorque; }
    float getErsRegenTorque() const override { return m_ersRegenTorque; }
    float getErsBatterySoc() const override { return m_ersBatterySoc; }
    float getErsBatteryTemp() const override { return m_ersBatteryTemp; }

    void setDrsEnabled(bool enabled) override { m_drsEnabled = enabled; }
    bool drsEnabled() const override { return m_drsEnabled; }
    void setDrsAutoActivate(bool autoActivate) override { m_drsAutoActivate = autoActivate; }
    bool drsAutoActivate() const override { return m_drsAutoActivate; }
    void setDrsSpeedThreshold(double kmh) override { m_drsSpeedThreshold = kmh; }
    double drsSpeedThreshold() const override { return m_drsSpeedThreshold; }
    void setDrsZoneStart(double dist) override { m_drsZoneStart = dist; }
    void setDrsZoneEnd(double dist) override { m_drsZoneEnd = dist; }
    bool isDrsActive() const override { return m_drsActive; }
    double getDrsDragReduction() const override { return m_drsDragReduction; }
    void setDrsDragReduction(double factor) override { m_drsDragReduction = std::clamp(factor, 0.0, 1.0); }

    const DamageState& damageState() const override { return m_damage; }
    DamageState& damageState() override { return m_damage; }
    void applyCollisionDamage(double impactForce) override;
    void resetDamage() override;
    void enableDamageModel(bool enabled) override { m_damageEnabled = enabled; }
    bool isDamageModelEnabled() const override { return m_damageEnabled; }

    void setWeatherState(const WeatherState& weather) override;
    const WeatherState& weatherState() const override { return m_weather; }
    WeatherState& weatherState() override { return m_weather; }
    void setTrackWetness(double wetness) override;
    void setRainIntensity(double mmh) override;
    double getAquaplaningRisk() const override { return m_aquaplaningRisk; }
    double getTrackGripReduction() const override { return m_weather.gripReduction(); }
    void setAirDensity(double density) override { m_weather.airDensity = std::max(0.8, density); }

    void setFuelConsumptionEnabled(bool enabled) override { m_fuelConsumptionEnabled = enabled; }
    bool isFuelConsumptionEnabled() const override { return m_fuelConsumptionEnabled; }
    double getFuelKg() const override { return m_fuelKg; }
    void setFuelKg(double kg) override { m_fuelKg = std::max(0.0, kg); }
    double getFuelCapacity() const override { return m_fuelCapacity; }
    void setFuelCapacity(double liters) override { m_fuelCapacity = liters; }
    double getEffectiveMass() const override { return m_mass + m_fuelKg; }

    void loadVehicleParams(const QString& carPath) override;
    void loadEngineFromIni(const QString& engineIniPath) override;
    void loadTyresFromIni(const QString& tyresIniPath) override;
    void loadDrivetrainFromIni(const QString& drivetrainIniPath) override;
    void loadAeroFromIni(const QString& aeroIniPath) override;
    void loadSuspensionFromIni(const QString& suspensionIniPath) override;

    WheelState wheelState(int wheel) const override { return m_wheels[wheel]; }

    ValidationMetrics validateAgainstTelemetry(
        const QVector<double>& timestamps,
        const QVector<double>& refSpeed,
        const QVector<double>& refLateralG,
        const QVector<double>& refLongG,
        const QVector<double>& refRPM,
        const QVector<double>& refThrottle,
        const QVector<double>& refBrake,
        const QVector<double>& refSteering) const override;

    virtual void updatePhysics(double dt);

signals:
    void stateUpdated(const SimulationState& state);
    void simulationStarted();
    void simulationStopped();
    void tireDataUpdated(double slipAngle, double lateralForce, double slipRatio, double longitudinalForce);

private:
    void updateTireModel(double dt);
    void updateWeightTransfer(double longitudinalAccel, double lateralAccel);
    void updatePerWheelForces(double dt);
    void updateErsAndDrs(double dt);
    void updateDamageModel(double dt);
    void updateWeatherEffects(double dt);
    void updateFuelWeight(double dt);
    double calculateSlipAngle(int wheel, double steeringAngle, double speed, double yawRate) const;
    double calculateSlipRatio(int wheel) const;

    SimulationState m_state;
    double m_throttle = 0.0;
    double m_brake = 0.0;
    double m_steering = 0.0;
    bool m_running = false;

    double m_mass = 1500.0;
    double m_enginePowerKw = 350.0;
    double m_maxRpm = 7500.0;
    double m_cd = 0.35;
    double m_frontalArea = 2.0;
    double m_wheelBase = 2.7;
    double m_trackWidth = 1.6;
    int m_currentGear = 1;
    QVector<double> m_gearRatios = {3.5, 2.5, 1.8, 1.4, 1.1, 0.9};
    double m_finalDriveRatio = 3.8;
    double m_wheelRadius = 0.33;
    double m_fuelKg = 80.0;

    double m_cgHeight = 0.45;
    double m_frontAxleDist = 1.35;
    double m_rearAxleDist = 1.35;
    double m_rollStiffness = 15000.0;
    double m_steerLock = 22.0;
    double m_steerRatio = 15.0;

    TireSlipCurve m_tireModel;

    // Generic vehicle models (simulator-agnostic)
    struct GenericTireModel {
        double calculateLateralForce(double slipAngle, double normalLoad, double friction) const;
        double calculateLongitudinalForce(double slipRatio, double normalLoad, double friction) const;
    } m_tireModelImpl;

    struct GenericEngineModel {
        double calculateTorque(double rpm, double throttle) const;
        double calculatePower(double rpm) const;
    } m_engineModelImpl;

    struct GenericAeroModel {
        double calculateDrag(double speed, double cd, double area) const;
        double calculateLift(double speed) const;
    } m_aeroModelImpl;

    struct GenericDiffModel {
        double calculateTorqueSplit(double inputTorque, double slipRatio) const;
    } m_diffModelImpl;

    struct GenericBrakeModel {
        float getDiscTemp(int wheel) const { return 300.0f; }
        float getPadTemp(int wheel) const { return 200.0f; }
        float getFade(int wheel) const { return 0.0f; }
    } m_brakeModelImpl;

    struct GenericHybridSystem {
        bool isEnabled() const { return false; }
        float getSoc() const { return 100.0f; }
        float getBatteryTemp() const { return 25.0f; }
    } m_hybridSystemImpl;

    bool m_absEnabled = false;
    bool m_tcEnabled = false;
    double m_absSlipThreshold = 0.15;
    double m_tcSlipThreshold = 0.12;

    WheelState m_wheels[4];
    double m_yawRate = 0.0;
    double m_lateralAccel = 0.0;

    double m_tireGraining[4] = {0.0, 0.0, 0.0, 0.0};
    double m_tireBlistering[4] = {0.0, 0.0, 0.0, 0.0};

    double m_tireTempSurface[4] = {30.0, 30.0, 30.0, 30.0};
    double m_tireTempCarcass[4] = {35.0, 35.0, 35.0, 35.0};
    double m_tireTempCore[4] = {40.0, 40.0, 40.0, 40.0};
    double m_tirePressure[4] = {2.4, 2.4, 2.4, 2.4};
    double m_tireWear[4] = {0.0, 0.0, 0.0, 0.0};
    double m_ambientTemp = 26.0;

    bool m_ersEnabled = false;
    double m_ersDeployTorque = 0.0;
    double m_ersRegenTorque = 0.0;
    float m_ersBatterySoc = 100.0f;
    float m_ersBatteryTemp = 25.0f;

    bool m_drsEnabled = false;
    bool m_drsAutoActivate = false;
    bool m_drsActive = false;
    double m_drsSpeedThreshold = 80.0;
    double m_drsZoneStart = 0.0;
    double m_drsZoneEnd = 0.0;
    double m_drsDragReduction = 0.25;
    double m_drsBaseCd = 0.35;

    bool m_damageEnabled = false;
    DamageState m_damage;

    WeatherState m_weather;
    double m_aquaplaningRisk = 0.0;
    double m_trackGripReduction = 0.0;

    DriveLayout m_driveLayout = DriveLayout::RWD;
    double m_centerDiffPreload = 0.0;
    double m_centerDiffPower = 0.5;
    double m_frontTorqueSplit = 0.0;

    bool m_fuelConsumptionEnabled = true;
    double m_fuelCapacity = 80.0;

    ::HybridSystem m_hybridSystem;
    BrakeModelManager m_brakeModel;
    ::ks::phys_LapTimer m_lapTimer;
    QElapsedTimer m_simTimer;
    double m_trackLength = 5000.0;
    double m_lastUpdateTime = 0.0;

    QVector<double> m_lapTimeHistory;

    static VehicleSimulator* s_instance;
};

} // namespace physics
} // namespace ks