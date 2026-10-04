#include "phys_Simulator.h"
#include "VehicleSimulator.h"
#include "HybridSystem.h"
#include "BrakeThermalModel.h"

namespace ks {

phys_Simulator* phys_Simulator::s_instance = nullptr;

phys_Simulator* phys_Simulator::instance() {
    if (!s_instance) {
        s_instance = new phys_Simulator(nullptr);
    }
    return s_instance;
}

phys_Simulator::phys_Simulator(QObject* parent) : QObject(parent) {
}

phys_Simulator::~phys_Simulator() = default;

ks::physics::VehicleSimulator* phys_Simulator::sim() const {
    return ks::physics::VehicleSimulator::instance();
}

HybridSystem& phys_Simulator::hybridSystem() {
    return sim()->hybridSystem();
}

const HybridSystem& phys_Simulator::hybridSystem() const {
    return sim()->hybridSystem();
}

BrakeModelManager& phys_Simulator::brakeModel() {
    return sim()->brakeModel();
}

const BrakeModelManager& phys_Simulator::brakeModel() const {
    return sim()->brakeModel();
}

phys_LapTimer* phys_Simulator::lapTimer() {
    return sim()->lapTimer();
}

void phys_Simulator::startSimulation() {
    sim()->startSimulation();
    connect(sim(), &ks::physics::VehicleSimulator::stateUpdated, this, &phys_Simulator::stateUpdated);
}

void phys_Simulator::stopSimulation() {
    sim()->stopSimulation();
    disconnect(sim(), &ks::physics::VehicleSimulator::stateUpdated, this, &phys_Simulator::stateUpdated);
}

void phys_Simulator::reset() {
    sim()->reset();
}

void phys_Simulator::setThrottle(double value) { sim()->setThrottle(value); }
void phys_Simulator::setBrake(double value) { sim()->setBrake(value); }
void phys_Simulator::setSteering(double value) { sim()->setSteering(value); }

SimulationState phys_Simulator::getState() const { return sim()->getState(); }
bool phys_Simulator::isRunning() const { return sim()->isRunning(); }

void phys_Simulator::setTireModel(const TireSlipCurve& curve) { sim()->setTireModel(curve); }
TireSlipCurve phys_Simulator::tireModel() const { return sim()->tireModel(); }

LapTimeEstimate phys_Simulator::estimateLapTime() const { return sim()->estimateLapTime(); }

void phys_Simulator::setMass(double kg) { sim()->setMass(kg); }
void phys_Simulator::setEnginePower(double kw) { sim()->setEnginePower(kw); }
void phys_Simulator::setMaxRpm(double rpm) { sim()->setMaxRpm(rpm); }
void phys_Simulator::setDragCoeff(double cd) { sim()->setDragCoeff(cd); }
void phys_Simulator::setFrontalArea(double area) { sim()->setFrontalArea(area); }
void phys_Simulator::setWheelBase(double wb) { sim()->setWheelBase(wb); }
void phys_Simulator::setTrackWidth(double tw) { sim()->setTrackWidth(tw); }

double phys_Simulator::mass() const { return sim()->mass(); }
double phys_Simulator::enginePower() const { return sim()->enginePower(); }
double phys_Simulator::maxRpm() const { return sim()->maxRpm(); }

void phys_Simulator::setAbsEnabled(bool enabled) { sim()->setAbsEnabled(enabled); }
bool phys_Simulator::absEnabled() const { return sim()->absEnabled(); }
void phys_Simulator::setTractionControlEnabled(bool enabled) { sim()->setTractionControlEnabled(enabled); }
bool phys_Simulator::tractionControlEnabled() const { return sim()->tractionControlEnabled(); }
void phys_Simulator::setAbsThreshold(double slipRatio) { sim()->setAbsThreshold(slipRatio); }
void phys_Simulator::setTcThreshold(double slipRatio) { sim()->setTcThreshold(slipRatio); }
double phys_Simulator::absThreshold() const { return sim()->absThreshold(); }
double phys_Simulator::tcThreshold() const { return sim()->tcThreshold(); }

float phys_Simulator::getBrakeDiscTemp(int wheel) const { return sim()->getBrakeDiscTemp(wheel); }
float phys_Simulator::getBrakePadTemp(int wheel) const { return sim()->getBrakePadTemp(wheel); }
float phys_Simulator::getBrakeFade(int wheel) const { return sim()->getBrakeFade(wheel); }

void phys_Simulator::setErsEnabled(bool enabled) { sim()->setErsEnabled(enabled); }
bool phys_Simulator::ersEnabled() const { return sim()->ersEnabled(); }
void phys_Simulator::setErsMode(int mode) { sim()->setErsMode(mode); }
void phys_Simulator::activateErsAttackMode() { sim()->activateErsAttackMode(); }

void phys_Simulator::setDriveLayout(DriveLayout layout) { sim()->setDriveLayout(layout); }
DriveLayout phys_Simulator::driveLayout() const { return sim()->driveLayout(); }
void phys_Simulator::setCenterDiffPreload(double nm) { sim()->setCenterDiffPreload(nm); }
double phys_Simulator::centerDiffPreload() const { return sim()->centerDiffPreload(); }
void phys_Simulator::setCenterDiffPower(double power) { sim()->setCenterDiffPower(power); }
double phys_Simulator::centerDiffPower() const { return sim()->centerDiffPower(); }
void phys_Simulator::setFrontRearTorqueSplit(double frontRatio) { sim()->setFrontRearTorqueSplit(frontRatio); }
float phys_Simulator::getErsDeployTorque() const { return sim()->getErsDeployTorque(); }
float phys_Simulator::getErsRegenTorque() const { return sim()->getErsRegenTorque(); }
float phys_Simulator::getErsBatterySoc() const { return sim()->getErsBatterySoc(); }
float phys_Simulator::getErsBatteryTemp() const { return sim()->getErsBatteryTemp(); }

void phys_Simulator::setDrsEnabled(bool enabled) { sim()->setDrsEnabled(enabled); }
bool phys_Simulator::drsEnabled() const { return sim()->drsEnabled(); }
void phys_Simulator::setDrsAutoActivate(bool autoActivate) { sim()->setDrsAutoActivate(autoActivate); }
bool phys_Simulator::drsAutoActivate() const { return sim()->drsAutoActivate(); }
void phys_Simulator::setDrsSpeedThreshold(double kmh) { sim()->setDrsSpeedThreshold(kmh); }
double phys_Simulator::drsSpeedThreshold() const { return sim()->drsSpeedThreshold(); }
void phys_Simulator::setDrsZoneStart(double dist) { sim()->setDrsZoneStart(dist); }
void phys_Simulator::setDrsZoneEnd(double dist) { sim()->setDrsZoneEnd(dist); }
bool phys_Simulator::isDrsActive() const { return sim()->isDrsActive(); }
double phys_Simulator::getDrsDragReduction() const { return sim()->getDrsDragReduction(); }
void phys_Simulator::setDrsDragReduction(double factor) { sim()->setDrsDragReduction(factor); }

const DamageState& phys_Simulator::damageState() const { return sim()->damageState(); }
DamageState& phys_Simulator::damageState() { return sim()->damageState(); }
void phys_Simulator::applyCollisionDamage(double impactForce) { sim()->applyCollisionDamage(impactForce); }
void phys_Simulator::resetDamage() { sim()->resetDamage(); }
void phys_Simulator::enableDamageModel(bool enabled) { sim()->enableDamageModel(enabled); }
bool phys_Simulator::isDamageModelEnabled() const { return sim()->isDamageModelEnabled(); }

void phys_Simulator::setWeatherState(const WeatherState& weather) { sim()->setWeatherState(weather); }
const WeatherState& phys_Simulator::weatherState() const { return sim()->weatherState(); }
WeatherState& phys_Simulator::weatherState() { return sim()->weatherState(); }
void phys_Simulator::setTrackWetness(double wetness) { sim()->setTrackWetness(wetness); }
void phys_Simulator::setRainIntensity(double mmh) { sim()->setRainIntensity(mmh); }
double phys_Simulator::getAquaplaningRisk() const { return sim()->getAquaplaningRisk(); }
double phys_Simulator::getTrackGripReduction() const { return sim()->getTrackGripReduction(); }
void phys_Simulator::setAirDensity(double density) { sim()->setAirDensity(density); }

void phys_Simulator::setFuelConsumptionEnabled(bool enabled) { sim()->setFuelConsumptionEnabled(enabled); }
bool phys_Simulator::isFuelConsumptionEnabled() const { return sim()->isFuelConsumptionEnabled(); }
double phys_Simulator::getFuelKg() const { return sim()->getFuelKg(); }
void phys_Simulator::setFuelKg(double kg) { sim()->setFuelKg(kg); }
double phys_Simulator::getFuelCapacity() const { return sim()->getFuelCapacity(); }
void phys_Simulator::setFuelCapacity(double liters) { sim()->setFuelCapacity(liters); }
double phys_Simulator::getEffectiveMass() const { return sim()->getEffectiveMass(); }

void phys_Simulator::loadVehicleParams(const QString& carPath) { sim()->loadVehicleParams(carPath); }
void phys_Simulator::loadEngineFromIni(const QString& engineIniPath) { sim()->loadEngineFromIni(engineIniPath); }
void phys_Simulator::loadTyresFromIni(const QString& tyresIniPath) { sim()->loadTyresFromIni(tyresIniPath); }
void phys_Simulator::loadDrivetrainFromIni(const QString& drivetrainIniPath) { sim()->loadDrivetrainFromIni(drivetrainIniPath); }
void phys_Simulator::loadAeroFromIni(const QString& aeroIniPath) { sim()->loadAeroFromIni(aeroIniPath); }
void phys_Simulator::loadSuspensionFromIni(const QString& suspensionIniPath) { sim()->loadSuspensionFromIni(suspensionIniPath); }

WheelState phys_Simulator::wheelState(int wheel) const { return sim()->wheelState(wheel); }

ValidationMetrics phys_Simulator::validateAgainstTelemetry(
    const QVector<double>& timestamps,
    const QVector<double>& refSpeed,
    const QVector<double>& refLateralG,
    const QVector<double>& refLongG,
    const QVector<double>& refRPM,
    const QVector<double>& refThrottle,
    const QVector<double>& refBrake,
    const QVector<double>& refSteering) const
{
    return sim()->validateAgainstTelemetry(timestamps, refSpeed, refLateralG, refLongG, refRPM, refThrottle, refBrake, refSteering);
}

} // namespace ks
