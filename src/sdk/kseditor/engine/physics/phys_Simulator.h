#pragma once

#include <QObject>
#include <QVector3D>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QElapsedTimer>

#include "PhysicsCoreTypes.h"

class HybridSystem;
class BrakeModelManager;

namespace ks {
class phys_LapTimer;
namespace physics { class VehicleSimulator; }

using SimulationState = ks::physics::SimulationState;
using TireSlipCurve = ks::physics::TireSlipCurve;
using LapTimeEstimate = ks::physics::LapTimeEstimate;
using WheelState = ks::physics::WheelState;
using ValidationMetrics = ks::physics::ValidationMetrics;
using WeatherState = ks::physics::WeatherState;
using DamageState = ks::physics::DamageState;
using DriveLayout = ks::physics::DriveLayout;

class phys_Simulator : public QObject {
    Q_OBJECT
public:
    static phys_Simulator* instance();

    void startSimulation();
    void stopSimulation();
    void reset();

    void setThrottle(double value);
    void setBrake(double value);
    void setSteering(double value);

    SimulationState getState() const;
    bool isRunning() const;

    void setTireModel(const TireSlipCurve& curve);
    TireSlipCurve tireModel() const;

    LapTimeEstimate estimateLapTime() const;

    HybridSystem& hybridSystem();
    const HybridSystem& hybridSystem() const;
    BrakeModelManager& brakeModel();
    const BrakeModelManager& brakeModel() const;
    phys_LapTimer* lapTimer();

    void setMass(double kg);
    void setEnginePower(double kw);
    void setMaxRpm(double rpm);
    void setDragCoeff(double cd);
    void setFrontalArea(double area);
    void setWheelBase(double wb);
    void setTrackWidth(double tw);

    double mass() const;
    double enginePower() const;
    double maxRpm() const;

    void setAbsEnabled(bool enabled);
    bool absEnabled() const;
    void setTractionControlEnabled(bool enabled);
    bool tractionControlEnabled() const;
    void setAbsThreshold(double slipRatio);
    void setTcThreshold(double slipRatio);
    double absThreshold() const;
    double tcThreshold() const;

    float getBrakeDiscTemp(int wheel) const;
    float getBrakePadTemp(int wheel) const;
    float getBrakeFade(int wheel) const;

    void setErsEnabled(bool enabled);
    bool ersEnabled() const;
    void setErsMode(int mode);
    void activateErsAttackMode();

    void setDriveLayout(DriveLayout layout);
    DriveLayout driveLayout() const;
    void setCenterDiffPreload(double nm);
    double centerDiffPreload() const;
    void setCenterDiffPower(double power);
    double centerDiffPower() const;
    void setFrontRearTorqueSplit(double frontRatio);
    float getErsDeployTorque() const;
    float getErsRegenTorque() const;
    float getErsBatterySoc() const;
    float getErsBatteryTemp() const;

    void setDrsEnabled(bool enabled);
    bool drsEnabled() const;
    void setDrsAutoActivate(bool autoActivate);
    bool drsAutoActivate() const;
    void setDrsSpeedThreshold(double kmh);
    double drsSpeedThreshold() const;
    void setDrsZoneStart(double dist);
    void setDrsZoneEnd(double dist);
    bool isDrsActive() const;
    double getDrsDragReduction() const;
    void setDrsDragReduction(double factor);

    const DamageState& damageState() const;
    DamageState& damageState();
    void applyCollisionDamage(double impactForce);
    void resetDamage();
    void enableDamageModel(bool enabled);
    bool isDamageModelEnabled() const;

    void setWeatherState(const WeatherState& weather);
    const WeatherState& weatherState() const;
    WeatherState& weatherState();
    void setTrackWetness(double wetness);
    void setRainIntensity(double mmh);
    double getAquaplaningRisk() const;
    double getTrackGripReduction() const;
    void setAirDensity(double density);

    void setFuelConsumptionEnabled(bool enabled);
    bool isFuelConsumptionEnabled() const;
    double getFuelKg() const;
    void setFuelKg(double kg);
    double getFuelCapacity() const;
    void setFuelCapacity(double liters);
    double getEffectiveMass() const;

    void loadVehicleParams(const QString& carPath);
    void loadEngineFromIni(const QString& engineIniPath);
    void loadTyresFromIni(const QString& tyresIniPath);
    void loadDrivetrainFromIni(const QString& drivetrainIniPath);
    void loadAeroFromIni(const QString& aeroIniPath);
    void loadSuspensionFromIni(const QString& suspensionIniPath);

    WheelState wheelState(int wheel) const;

    ValidationMetrics validateAgainstTelemetry(
        const QVector<double>& timestamps,
        const QVector<double>& refSpeed,
        const QVector<double>& refLateralG,
        const QVector<double>& refLongG,
        const QVector<double>& refRPM,
        const QVector<double>& refThrottle,
        const QVector<double>& refBrake,
        const QVector<double>& refSteering) const;

signals:
    void stateUpdated(const SimulationState& state);
    void simulationStarted();
    void simulationStopped();
    void tireDataUpdated(double slipAngle, double lateralForce, double slipRatio, double longitudinalForce);

private:
    explicit phys_Simulator(QObject* parent = nullptr);
    ~phys_Simulator() override;
    static phys_Simulator* s_instance;

    ks::physics::VehicleSimulator* sim() const;
};

} // namespace ks
