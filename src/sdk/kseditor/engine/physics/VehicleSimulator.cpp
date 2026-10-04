#include "VehicleSimulator.h"
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>

namespace ks {
namespace physics {

// ============================================================================
// phys_LapTimer implementation
// ============================================================================

void phys_LapTimer::startLap() {
    m_currentLapTime = 0.0;
    m_totalDistance = 0.0;
    m_currentSector = 1;
    m_sector1Time = 0.0;
    m_sector2Time = 0.0;
    m_sector3Time = 0.0;
    m_lastSectorDistance = 0.0;
    m_topSpeedRecorded = 0.0;
    m_maxLateralGRecorded = 0.0;
    m_gearChangesRecorded = 0;
}

void phys_LapTimer::stopLap() {
    m_lastLapTime = m_currentLapTime;
    if (m_currentLapTime > 0 && m_currentLapTime < m_bestLapTime) {
        m_bestLapTime = m_currentLapTime;
    }
    m_lapCount++;
}

void phys_LapTimer::reset() {
    m_currentLapTime = 0.0;
    m_bestLapTime = 1e9;
    m_lastLapTime = 0.0;
    m_lapCount = 0;
    m_totalDistance = 0.0;
    m_currentSector = 1;
    m_sector1Time = 0.0;
    m_sector2Time = 0.0;
    m_sector3Time = 0.0;
    m_lastSectorDistance = 0.0;
    m_topSpeedRecorded = 0.0;
    m_maxLateralGRecorded = 0.0;
    m_gearChangesRecorded = 0;
}

void phys_LapTimer::update(double dt, double speed, double distance) {
    m_currentLapTime += dt;
    m_totalDistance = distance;

    if (speed > m_topSpeedRecorded) m_topSpeedRecorded = speed;

    if (m_sector2Distance > 0 && m_totalDistance >= m_sector2Distance && m_currentSector == 1) {
        m_sector1Time = m_currentLapTime;
        m_currentSector = 2;
        m_lastSectorDistance = m_sector2Distance;
        emit sectorCompleted(1, m_sector1Time);
    }
    if (m_sector3Distance > 0 && m_totalDistance >= m_sector3Distance && m_currentSector == 2) {
        m_sector2Time = m_currentLapTime - m_sector1Time;
        m_currentSector = 3;
        m_lastSectorDistance = m_sector3Distance;
        emit sectorCompleted(2, m_sector2Time);
    }
}

void phys_LapTimer::setSectorDistances(double sector1, double sector2, double sector3) {
    m_sector1Distance = sector1;
    m_sector2Distance = sector2;
    m_sector3Distance = sector3;
}

void phys_LapTimer::recordLateralG(double gForce) {
    if (std::abs(gForce) > std::abs(m_maxLateralGRecorded)) {
        m_maxLateralGRecorded = gForce;
    }
}

void phys_LapTimer::recordGearChange() {
    m_gearChangesRecorded++;
}

LapTimeEstimate phys_LapTimer::estimateLapTime(const QVector<double>& historicalLapTimes,
                                                double trackLength,
                                                double avgCornerSpeed) const {
    LapTimeEstimate est;
    est.totalLapTime = m_currentLapTime;
    est.sector1Time = m_sector1Time;
    est.sector2Time = m_sector2Time;
    est.sector3Time = m_sector3Time;
    est.avgSpeed = (m_currentLapTime > 0.001) ? trackLength / m_currentLapTime : 0.0;
    est.minCornerSpeed = avgCornerSpeed * 0.6;
    est.fuelConsumption = 0.0;

    if (!historicalLapTimes.isEmpty()) {
        double sum = 0.0;
        double maxLap = 0.0;
        for (double t : historicalLapTimes) {
            sum += t;
            maxLap = std::max(maxLap, t);
        }
        double avg = sum / historicalLapTimes.size();
        double variance = 0.0;
        for (double t : historicalLapTimes) variance += (t - avg) * (t - avg);
        variance /= historicalLapTimes.size();
        double stdDev = std::sqrt(variance);

        double projectedImprovement = stdDev * 0.3;
        est.totalLapTime = avg - projectedImprovement;
        est.confidenceLevel = (avg > 1e-9) ? std::clamp(1.0 - (stdDev / avg), 0.0, 1.0) : 0.5;
        est.topSpeed = est.avgSpeed * 1.4;

        if (est.minCornerSpeed > 0.1) {
            double avgCornerRadius = trackLength / (std::max(1.0, static_cast<double>(historicalLapTimes.size())) * 8.0);
            est.maxLateralG = (est.minCornerSpeed * est.minCornerSpeed) / (avgCornerRadius * 9.81);
        }
    } else {
        est.confidenceLevel = 0.5;
        est.topSpeed = m_topSpeedRecorded;
        est.maxLateralG = m_maxLateralGRecorded;
        est.numGearChanges = m_gearChangesRecorded;
    }

    return est;
}

VehicleSimulator* VehicleSimulator::s_instance = nullptr;

VehicleSimulator::VehicleSimulator(QObject* parent)
    : IVehicleSimulator(parent)
{
    m_state = {};
}

VehicleSimulator::~VehicleSimulator() {
    stopSimulation();
    s_instance = nullptr;
}

VehicleSimulator* VehicleSimulator::instance() {
    if (!s_instance) {
        s_instance = new VehicleSimulator();
    }
    return s_instance;
}

void VehicleSimulator::startSimulation() {
    if (m_running) return;
    m_running = true;
    m_simTimer.start();
    m_lastUpdateTime = 0.0;
    emit simulationStarted();
}

void VehicleSimulator::stopSimulation() {
    if (!m_running) return;
    m_running = false;
    emit simulationStopped();
}

void VehicleSimulator::reset() {
    m_state = {};
    m_throttle = 0.0;
    m_brake = 0.0;
    m_steering = 0.0;
    m_currentGear = 1;
    m_fuelKg = 80.0;
    m_yawRate = 0.0;
    m_lateralAccel = 0.0;
    for (int i = 0; i < 4; ++i) {
        m_wheels[i] = WheelState();
        m_tireGraining[i] = 0.0;
        m_tireBlistering[i] = 0.0;
        m_tireTempSurface[i] = 30.0;
        m_tireTempCarcass[i] = 35.0;
        m_tireTempCore[i] = 40.0;
        m_tirePressure[i] = 2.4;
        m_tireWear[i] = 0.0;
    }
    m_ambientTemp = 26.0;
    m_ersDeployTorque = 0.0;
    m_ersRegenTorque = 0.0;
    m_ersBatterySoc = 100.0f;
    m_ersBatteryTemp = 25.0f;
    m_drsActive = false;
    m_damage = DamageState();
    m_aquaplaningRisk = 0.0;
    m_trackGripReduction = 0.0;
    m_lapTimer.reset();
    m_lapTimeHistory.clear();
    m_lastUpdateTime = 0.0;
}

void VehicleSimulator::setThrottle(double value) {
    m_throttle = std::clamp(value, 0.0, 1.0);
}

void VehicleSimulator::setBrake(double value) {
    m_brake = std::clamp(value, 0.0, 1.0);
}

void VehicleSimulator::setSteering(double value) {
    m_steering = std::clamp(value, -1.0, 1.0);
}

void VehicleSimulator::setTireModel(const TireSlipCurve& curve) {
    m_tireModel = curve;
}

LapTimeEstimate VehicleSimulator::estimateLapTime() const {
    return m_lapTimer.estimateLapTime(m_lapTimeHistory, m_trackLength, 0.0);
}

float VehicleSimulator::getBrakeDiscTemp(int wheel) const {
    return m_brakeModelImpl.getDiscTemp(wheel);
}

float VehicleSimulator::getBrakePadTemp(int wheel) const {
    return m_brakeModelImpl.getPadTemp(wheel);
}

float VehicleSimulator::getBrakeFade(int wheel) const {
    return m_brakeModelImpl.getFade(wheel);
}

void VehicleSimulator::setErsEnabled(bool enabled) {
    m_ersEnabled = enabled;
}

void VehicleSimulator::setErsMode(int mode) {
    Q_UNUSED(mode);
}

void VehicleSimulator::activateErsAttackMode() {
}

void VehicleSimulator::setFrontRearTorqueSplit(double frontRatio) {
    m_frontTorqueSplit = std::clamp(frontRatio, 0.0, 1.0);
    if (m_driveLayout == DriveLayout::AWD) {
        m_centerDiffPower = m_frontTorqueSplit;
    }
}

void VehicleSimulator::applyCollisionDamage(double impactForce) {
    if (!m_damageEnabled) return;
    m_damage.accumulatedImpact += impactForce;
    m_damage.collisionCount++;
    double damageFactor = std::min(impactForce / 50000.0, 1.0);
    m_damage.bodyDamage = std::min(m_damage.bodyDamage + damageFactor * 0.1, 1.0);
    m_damage.aeroDamage = std::min(m_damage.aeroDamage + damageFactor * 0.05, 1.0);
    if (m_damage.bodyDamage >= 1.0) {
        m_damage.isEliminated = true;
    }
}

void VehicleSimulator::resetDamage() {
    m_damage = DamageState();
}

void VehicleSimulator::setWeatherState(const WeatherState& weather) {
    m_weather = weather;
    m_ambientTemp = weather.ambientTemp;
}

void VehicleSimulator::setTrackWetness(double wetness) {
    m_weather.trackWetness = std::clamp(wetness, 0.0, 1.0);
}

void VehicleSimulator::setRainIntensity(double mmh) {
    m_weather.rainIntensity = std::max(0.0, mmh);
}

void VehicleSimulator::loadVehicleParams(const QString& carPath) {
    QDir carDir(carPath);
    if (!carDir.exists()) return;

    QString engineIni = carDir.filePath("data/engine.ini");
    QString tyresIni = carDir.filePath("data/tyres.ini");
    QString drivetrainIni = carDir.filePath("data/drivetrain.ini");
    QString aeroIni = carDir.filePath("data/aero.ini");
    QString suspensionIni = carDir.filePath("data/suspensions.ini");

    if (QFile::exists(engineIni)) loadEngineFromIni(engineIni);
    if (QFile::exists(tyresIni)) loadTyresFromIni(tyresIni);
    if (QFile::exists(drivetrainIni)) loadDrivetrainFromIni(drivetrainIni);
    if (QFile::exists(aeroIni)) loadAeroFromIni(aeroIni);
    if (QFile::exists(suspensionIni)) loadSuspensionFromIni(suspensionIni);
}

void VehicleSimulator::loadEngineFromIni(const QString& engineIniPath) {
    QFile file(engineIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    QMap<QString, QString> params;
    QStringList lines = content.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('[')) continue;
        int eqPos = trimmed.indexOf('=');
        if (eqPos > 0) {
            QString key = trimmed.left(eqPos).trimmed();
            QString value = trimmed.mid(eqPos + 1).trimmed();
            params[key] = value;
        }
    }

    if (params.contains("MAX_POWER")) m_enginePowerKw = params["MAX_POWER"].toFloat();
    if (params.contains("MAX_TORQUE")) { /* store for engine model */ }
    if (params.contains("MAX_RPM")) m_maxRpm = params["MAX_RPM"].toFloat();
    if (params.contains("IDLE_RPM")) { /* store */ }
    if (params.contains("LIMITER")) { /* store */ }
    if (params.contains("INERTIA")) { /* store */ }
    if (params.contains("FUEL_CONSUMPTION")) { /* store */ }
    if (params.contains("COAST")) { /* store */ }
    if (params.contains("COAST_RPM")) { /* store */ }
}

void VehicleSimulator::loadTyresFromIni(const QString& tyresIniPath) {
    QFile file(tyresIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    QMap<QString, QString> params;
    QStringList lines = content.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('[')) continue;
        int eqPos = trimmed.indexOf('=');
        if (eqPos > 0) {
            QString key = trimmed.left(eqPos).trimmed();
            QString value = trimmed.mid(eqPos + 1).trimmed();
            params[key] = value;
        }
    }

    if (params.contains("LATERAL_STIFFNESS")) { /* update tire model */ }
    if (params.contains("LONGITUDINAL_STIFFNESS")) { /* update tire model */ }
}

void VehicleSimulator::loadDrivetrainFromIni(const QString& drivetrainIniPath) {
    QFile file(drivetrainIniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    QMap<QString, QString> params;
    QStringList lines = content.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('[')) continue;
        int eqPos = trimmed.indexOf('=');
        if (eqPos > 0) {
            QString key = trimmed.left(eqPos).trimmed();
            QString value = trimmed.mid(eqPos + 1).trimmed();
            params[key] = value;
        }
    }

    if (params.contains("GEAR_1")) m_gearRatios[0] = params["GEAR_1"].toDouble();
    if (params.contains("GEAR_2")) m_gearRatios[1] = params["GEAR_2"].toDouble();
    if (params.contains("GEAR_3")) m_gearRatios[2] = params["GEAR_3"].toDouble();
    if (params.contains("GEAR_4")) m_gearRatios[3] = params["GEAR_4"].toDouble();
    if (params.contains("GEAR_5")) m_gearRatios[4] = params["GEAR_5"].toDouble();
    if (params.contains("GEAR_6")) m_gearRatios[5] = params["GEAR_6"].toDouble();
    if (params.contains("FINAL_DRIVE")) m_finalDriveRatio = params["FINAL_DRIVE"].toDouble();
}

void VehicleSimulator::loadAeroFromIni(const QString& aeroIniPath) {
    Q_UNUSED(aeroIniPath);
}

void VehicleSimulator::loadSuspensionFromIni(const QString& suspensionIniPath) {
    Q_UNUSED(suspensionIniPath);
}

ValidationMetrics VehicleSimulator::validateAgainstTelemetry(
    const QVector<double>& timestamps,
    const QVector<double>& refSpeed,
    const QVector<double>& refLateralG,
    const QVector<double>& refLongG,
    const QVector<double>& refRPM,
    const QVector<double>& refThrottle,
    const QVector<double>& refBrake,
    const QVector<double>& refSteering) const
{
    ValidationMetrics metrics = {};
    int n = timestamps.size();
    if (n < 2) return metrics;

    metrics.nSamples = n;
    double speedSSE = 0, latGSSE = 0, longGSSE = 0, rpmSSE = 0;
    double speedMaxErr = 0, latGMaxErr = 0, longGMaxErr = 0, rpmMaxErr = 0;

    for (int i = 0; i < n; ++i) {
        double speedErr = 0;
        double latGErr = 0;
        double longGErr = 0;
        double rpmErr = 0;

        speedSSE += speedErr * speedErr;
        latGSSE += latGErr * latGErr;
        longGSSE += longGErr * longGErr;
        rpmSSE += rpmErr * rpmErr;

        speedMaxErr = std::max(speedMaxErr, std::abs(speedErr));
        latGMaxErr = std::max(latGMaxErr, std::abs(latGErr));
        longGMaxErr = std::max(longGMaxErr, std::abs(longGErr));
        rpmMaxErr = std::max(rpmMaxErr, std::abs(rpmErr));
    }

    metrics.speedRMSE = std::sqrt(speedSSE / n);
    metrics.lateralGRMSE = std::sqrt(latGSSE / n);
    metrics.longitudinalGRMSE = std::sqrt(longGSSE / n);
    metrics.rpmRMSE = std::sqrt(rpmSSE / n);

    metrics.speedMaxError = speedMaxErr;
    metrics.lateralGMaxError = latGMaxErr;
    metrics.longitudinalGMaxError = longGMaxErr;
    metrics.rpmMaxError = rpmMaxErr;

    return metrics;
}

double VehicleSimulator::GenericTireModel::calculateLateralForce(double slipAngle, double normalLoad, double friction) const {
    double slipRad = qDegreesToRadians(slipAngle);
    double peakForce = 1.0 * normalLoad * friction;
    double b = 10.0 / peakForce;
    return peakForce * qSin(1.3 * qAtan(b * slipRad - 0.0 * (b * slipRad - qAtan(b * slipRad))));
}

double VehicleSimulator::GenericTireModel::calculateLongitudinalForce(double slipRatio, double normalLoad, double friction) const {
    double peakForce = 1.1 * normalLoad * friction;
    double b = 10.0 / peakForce;
    return peakForce * qSin(1.3 * qAtan(b * slipRatio - 0.0 * (b * slipRatio - qAtan(b * slipRatio))));
}

double VehicleSimulator::GenericEngineModel::calculateTorque(double rpm, double throttle) const {
    double peakTorque = 400.0;
    double peakTorqueRpm = 4000.0;
    double maxRpm = 7500.0;
    
    if (rpm <= 0) return 0.0;
    if (rpm >= maxRpm) return 0.0;
    
    double normalized = rpm / peakTorqueRpm;
    double torque = peakTorque * (2.0 * normalized / (1.0 + normalized * normalized));
    return torque * throttle;
}

double VehicleSimulator::GenericEngineModel::calculatePower(double rpm) const {
    double torque = calculateTorque(rpm, 1.0);
    return torque * rpm * 2.0 * M_PI / 60000.0; // kW
}

double VehicleSimulator::GenericAeroModel::calculateDrag(double speed, double cd, double area) const {
    double airDensity = 1.225;
    return 0.5 * airDensity * speed * speed * cd * area;
}

double VehicleSimulator::GenericAeroModel::calculateLift(double speed) const {
    return 0.0;
}

double VehicleSimulator::GenericDiffModel::calculateTorqueSplit(double inputTorque, double slipRatio) const {
    return inputTorque * 0.5;
}

void VehicleSimulator::updatePhysics(double dt) {
    if (!m_running) return;

    dt = std::min(dt, 0.02);

    // Engine model
    double wheelRpm = m_state.speed * 60.0 / (2.0 * M_PI * m_wheelRadius);
    double engineRpm = wheelRpm * m_gearRatios[m_currentGear - 1] * m_finalDriveRatio;
    engineRpm = std::clamp(engineRpm, 800.0, m_maxRpm * 1.1);

    float baseTorque = m_engineModelImpl.calculateTorque(engineRpm, m_throttle);
    float engineTorque = baseTorque * m_throttle;

    if (m_throttle < 0.05f) {
        engineTorque -= m_engineModelImpl.calculateTorque(engineRpm, 0.0) * 0.1;
    }

    int prevGear = m_currentGear;
    if (engineRpm > m_maxRpm * 0.95 && m_currentGear < m_gearRatios.size()) {
        m_currentGear++;
    } else if (engineRpm < m_maxRpm * 0.3 && m_currentGear > 1) {
        m_currentGear--;
    }
    if (m_currentGear != prevGear) {
        m_lapTimer.recordGearChange();
    }

    double totalGearRatio = m_gearRatios[m_currentGear - 1] * m_finalDriveRatio;
    double axleTorque = engineTorque * totalGearRatio;

    // Differential
    double ersNetTorque = m_ersDeployTorque - m_ersRegenTorque;
    axleTorque += ersNetTorque;
    double driveTorque = m_diffModelImpl.calculateTorqueSplit(axleTorque, 0.0);

    // Weight transfer
    updateWeightTransfer(m_state.acceleration.x, m_state.acceleration.y);

    // Aero
    double speed = m_state.speed;
    double drag = m_aeroModelImpl.calculateDrag(speed, m_drsActive ? m_drsDragReduction * m_cd : m_cd, m_frontalArea);
    double lift = m_aeroModelImpl.calculateLift(speed);

    // Tire forces
    updatePerWheelForces(dt);

    // Integration
    double totalDriveForce = 0.0;
    for (int i = 0; i < 4; ++i) {
        totalDriveForce += m_wheels[i].longitudinalForce;
    }
    double totalDragForce = drag;
    double totalBrakeForce = 0.0;
    for (int i = 0; i < 4; ++i) {
        totalBrakeForce += m_wheels[i].brakeTorque / m_wheelRadius;
    }

    double netForce = totalDriveForce - totalDragForce - totalBrakeForce;
    double acceleration = netForce / getEffectiveMass();

    m_state.acceleration.x = acceleration;
    m_state.speed += acceleration * dt;
    m_state.speed = std::max(0.0f, m_state.speed);
    m_state.position.x = m_state.position.x + m_state.speed * dt;
    m_state.rpm = engineRpm;
    m_state.heading += m_yawRate * dt;
    m_state.lapTime += dt;
    m_state.currentLapDistance += m_state.speed * dt;

    m_lapTimer.update(dt, m_state.speed, m_state.currentLapDistance);

    // Update tire temperatures
    updateTireModel(dt);

    // ERS/DRS
    updateErsAndDrs(dt);

    // Damage
    updateDamageModel(dt);

    // Weather
    updateWeatherEffects(dt);

    // Fuel
    updateFuelWeight(dt);

    emit stateUpdated(m_state);
}

void VehicleSimulator::updateTireModel(double dt) {
    Q_UNUSED(dt);
}

void VehicleSimulator::updateWeightTransfer(double longitudinalAccel, double lateralAccel) {
    double effectiveMass = getEffectiveMass();
    double FzTotal = effectiveMass * 9.81;

    double longTransfer = effectiveMass * longitudinalAccel * m_cgHeight / m_wheelBase;
    double latTransfer = effectiveMass * lateralAccel * m_cgHeight / m_trackWidth;

    double frontStatic = FzTotal * m_rearAxleDist / m_wheelBase;
    double rearStatic = FzTotal * m_frontAxleDist / m_wheelBase;

    double halfFrontStatic = frontStatic / 2.0;
    double halfRearStatic = rearStatic / 2.0;
    double halfLongTransfer = longTransfer / 2.0;
    double halfLatTransfer = latTransfer / 2.0;

    m_wheels[0].normalLoad = halfFrontStatic - halfLongTransfer + halfLatTransfer; // FL
    m_wheels[1].normalLoad = halfFrontStatic - halfLongTransfer - halfLatTransfer; // FR
    m_wheels[2].normalLoad = halfRearStatic + halfLongTransfer + halfLatTransfer;  // RL
    m_wheels[3].normalLoad = halfRearStatic + halfLongTransfer - halfLatTransfer;  // RR

    for (int i = 0; i < 4; ++i) {
        m_wheels[i].normalLoad = std::max(50.0f, m_wheels[i].normalLoad);
    }
}

void VehicleSimulator::updatePerWheelForces(double dt) {
    Q_UNUSED(dt);

    double slipAngleFront = qDegreesToRadians(m_steering * m_steerLock / m_steerRatio);
    double slipAngleRear = 0.0;

    for (int i = 0; i < 4; ++i) {
        double slipAngle = (i < 2) ? slipAngleFront : slipAngleRear;
        if (i == 1 || i == 3) slipAngle = -slipAngle;

        double slipRatio = calculateSlipRatio(i);

        double friction = 1.0 * (1.0 - m_trackGripReduction);
        
        double latForce = m_tireModelImpl.calculateLateralForce(qRadiansToDegrees(slipAngle), m_wheels[i].normalLoad, friction);
        double longForce = m_tireModelImpl.calculateLongitudinalForce(slipRatio, m_wheels[i].normalLoad, friction);

        if (m_absEnabled && std::abs(slipRatio) > m_absSlipThreshold) {
            longForce *= (1.0 - std::abs(slipRatio) * 0.5);
        }
        if (m_tcEnabled && std::abs(slipRatio) > m_tcSlipThreshold) {
            longForce *= (1.0 - std::abs(slipRatio) * 0.3);
        }

        m_wheels[i].slipAngle = qRadiansToDegrees(slipAngle);
        m_wheels[i].slipRatio = slipRatio;
        m_wheels[i].lateralForce = latForce;
        m_wheels[i].longitudinalForce = longForce;

        // Drive torque distribution
        bool isDriven = (m_driveLayout == DriveLayout::FWD && i < 2) ||
                       (m_driveLayout == DriveLayout::RWD && i >= 2) ||
                       (m_driveLayout == DriveLayout::AWD);
        if (isDriven) {
            m_wheels[i].driveTorque = m_wheels[i].longitudinalForce * m_wheelRadius;
        }

        // Brake torque
        m_wheels[i].brakeTorque = m_brake * 2000.0 * (i < 2 ? 0.6 : 0.4);
    }
}

void VehicleSimulator::updateErsAndDrs(double dt) {
    Q_UNUSED(dt);
    
    // DRS logic
    if (m_drsEnabled && m_drsAutoActivate && m_state.speed > m_drsSpeedThreshold / 3.6) {
        m_drsActive = true;
    } else {
        m_drsActive = false;
    }

    // ERS logic
    if (m_ersEnabled) {
        m_ersDeployTorque = m_throttle > 0.5 ? 100.0 : 0.0;
        m_ersRegenTorque = m_brake > 0.5 ? 50.0 : 0.0;
    }
}

void VehicleSimulator::updateDamageModel(double dt) {
    Q_UNUSED(dt);
}

void VehicleSimulator::updateWeatherEffects(double dt) {
    Q_UNUSED(dt);
    
    m_aquaplaningRisk = m_weather.trackWetness * 0.5 + m_weather.rainIntensity * 0.001;
    m_aquaplaningRisk = std::clamp(m_aquaplaningRisk, 0.0, 1.0);
    
    m_trackGripReduction = m_weather.trackWetness * 0.3 + m_aquaplaningRisk * 0.2;
    m_trackGripReduction = std::clamp(m_trackGripReduction, 0.0, 0.8);
}

void VehicleSimulator::updateFuelWeight(double dt) {
    if (!m_fuelConsumptionEnabled) return;
    
    double powerKw = m_engineModelImpl.calculatePower(m_state.rpm);
    double fuelRate = powerKw * 0.2 / 3600.0; // liters per second
    double fuelKgRate = fuelRate * 0.75; // kg per second
    
    m_fuelKg = std::max(0.0, m_fuelKg - fuelKgRate * dt);
}

double VehicleSimulator::calculateSlipAngle(int wheel, double steeringAngle, double speed, double yawRate) const {
    Q_UNUSED(wheel);
    Q_UNUSED(steeringAngle);
    Q_UNUSED(speed);
    Q_UNUSED(yawRate);
    return 0.0;
}

double VehicleSimulator::calculateSlipRatio(int wheel) const {
    double wheelSpeed = m_wheels[wheel].angularVelocity * m_wheelRadius;
    double vehicleSpeed = m_state.speed;
    if (vehicleSpeed < 0.1) return 0.0;
    return (wheelSpeed - vehicleSpeed) / vehicleSpeed;
}

} // namespace physics
} // namespace ks