#include "AeroModel.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <numeric>

// ============================================================================
// AeroModel implementation
// ============================================================================

AeroModel::AeroForces AeroModel::calculate(const AeroState& state) const {
    AeroForces forces;

    float q = calculateDynamicPressure(state.speed, state.airDensity);

    // Calculate forces for each wing
    float totalDownforce = 0;
    float totalDrag = 0;
    float frontDownforce = 0;
    float rearDownforce = 0;

    for (const Wing& wing : m_config.wings) {
        float wingForce = calculateWingForce(wing, state);

        // Split into lift (downforce) and drag components
        float wingDf = wingForce * wing.cl;
        float wingDrag = wingForce * wing.cd;

        totalDownforce += wingDf;
        totalDrag += wingDrag;

        // Front vs rear classification based on position
        if (wing.position[2] > 0) { // Front (positive z = forward)
            frontDownforce += wingDf;
        } else { // Rear (negative z = backward)
            rearDownforce += wingDf;
        }
    }

    // Add body lift/drag
    float bodyDf = q * m_config.frontalArea * m_config.liftCoefficient;
    float bodyDrag = q * m_config.frontalArea * m_config.dragCoefficient;

    totalDownforce += bodyDf;
    totalDrag += bodyDrag;

    // Apply ground effect
    float groundEffect = 1.0f;
    float avgRideHeight = (state.rideHeightFront + state.rideHeightRear) / 2.0f;
    if (avgRideHeight < 0.1f) {
        groundEffect = 1.0f + m_config.groundEffectFactor * (0.1f - avgRideHeight) / 0.1f;
    }
    totalDownforce *= groundEffect;

    forces.downforce = totalDownforce;
    forces.drag = totalDrag;
    forces.frontDownforce = frontDownforce;
    forces.rearDownforce = rearDownforce;

    // Calculate aero balance
    float totalDf = frontDownforce + rearDownforce;
    forces.aeroBalance = (totalDf > 0) ? frontDownforce / totalDf : 0.5f;

    // L/D ratio
    forces.ldRatio = (totalDrag > 0) ? totalDownforce / totalDrag : 0;

    return forces;
}

float AeroModel::calculateWingForce(const Wing& wing, const AeroState& state) const {
    float q = calculateDynamicPressure(state.speed, state.airDensity);

    // Calculate angle of attack
    float aoa = wing.angle + state.pitchAngle * 180.0f / 3.14159f;

    // Look up coefficients
    float cl = interpolateCl(wing, aoa) * wing.clGain;
    float cd = interpolateCd(wing, aoa) * wing.cdGain;

    // Apply height correction
    float height = (wing.position[2] > 0) ? state.rideHeightFront : state.rideHeightRear;
    cl *= interpolateHeightCl(wing, height);
    cd *= interpolateHeightCd(wing, height);

    // Calculate force: F = 0.5 * rho * V^2 * S * CL
    float force = q * wing.area() * cl;

    return force;
}

float AeroModel::interpolateCl(const Wing& wing, float aoa) const {
    return interpolateLut(wing.aoaClLut, aoa);
}

float AeroModel::interpolateCd(const Wing& wing, float aoa) const {
    return interpolateLut(wing.aoaCdLut, aoa);
}

float AeroModel::interpolateHeightCl(const Wing& wing, float height) const {
    return interpolateLut(wing.heightClLut, height);
}

float AeroModel::interpolateHeightCd(const Wing& wing, float height) const {
    return interpolateLut(wing.heightCdLut, height);
}

float AeroModel::interpolateLut(const QVector<QPair<float, float>>& lut, float x) const {
    if (lut.isEmpty()) return 0;

    // Find surrounding points
    for (int i = 0; i < lut.size() - 1; ++i) {
        if (x >= lut[i].first && x <= lut[i+1].first) {
            float t = (x - lut[i].first) / (lut[i+1].first - lut[i].first);
            return lut[i].second + (lut[i+1].second - lut[i].second) * t;
        }
    }

    // Extrapolate
    if (x < lut.first().first) {
        return lut.first().second;
    }
    return lut.last().second;
}

// ============================================================================
// Configuration
// ============================================================================

void AeroModel::addWing(const Wing& wing) {
    m_config.wings.append(wing);
}

void AeroModel::removeWing(int index) {
    if (index >= 0 && index < m_config.wings.size()) {
        m_config.wings.removeAt(index);
    }
}

void AeroModel::clearWings() {
    m_config.wings.clear();
}

// ============================================================================
// Presets
// ============================================================================

AeroModel::AeroConfig AeroModel::getSedanConfig() {
    AeroConfig config;
    config.frontalArea = 2.2f;
    config.dragCoefficient = 0.32f;
    config.liftCoefficient = 0.05f;

    Wing frontWing;
    frontWing.name = "Front Splitter";
    frontWing.chord = 0.3f;
    frontWing.span = 1.6f;
    frontWing.angle = 2.0f;
    frontWing.position[2] = 2.3f;
    frontWing.clGain = 0.3f;
    frontWing.cdGain = 0.5f;

    Wing rearWing;
    rearWing.name = "Rear Wing";
    rearWing.chord = 0.2f;
    rearWing.span = 1.2f;
    rearWing.angle = 8.0f;
    rearWing.position[2] = -1.8f;
    rearWing.clGain = 0.5f;
    rearWing.cdGain = 0.6f;

    config.wings.append(frontWing);
    config.wings.append(rearWing);

    return config;
}

AeroModel::AeroConfig AeroModel::getGT3Config() {
    AeroConfig config;
    config.frontalArea = 2.0f;
    config.dragCoefficient = 0.35f;
    config.liftCoefficient = -0.1f;

    Wing frontWing;
    frontWing.name = "Front Splitter";
    frontWing.chord = 0.4f;
    frontWing.span = 1.8f;
    frontWing.angle = 3.0f;
    frontWing.position[2] = 2.4f;
    frontWing.clGain = 0.6f;
    frontWing.cdGain = 0.7f;

    Wing rearWing;
    rearWing.name = "Rear Wing";
    rearWing.chord = 0.25f;
    rearWing.span = 1.4f;
    rearWing.angle = 12.0f;
    rearWing.position[2] = -1.9f;
    rearWing.clGain = 0.8f;
    rearWing.cdGain = 0.8f;

    Wing diffuser;
    diffuser.name = "Diffuser";
    diffuser.chord = 0.5f;
    diffuser.span = 1.2f;
    diffuser.angle = 10.0f;
    diffuser.position[2] = -2.0f;
    diffuser.position[1] = -0.2f;
    diffuser.clGain = 1.0f;
    diffuser.cdGain = 0.3f;

    config.wings.append(frontWing);
    config.wings.append(rearWing);
    config.wings.append(diffuser);

    return config;
}

AeroModel::AeroConfig AeroModel::getFormulaConfig() {
    AeroConfig config;
    config.frontalArea = 1.8f;
    config.dragCoefficient = 0.30f;
    config.liftCoefficient = -0.2f;

    Wing frontWing;
    frontWing.name = "Front Wing";
    frontWing.chord = 0.35f;
    frontWing.span = 1.8f;
    frontWing.angle = 4.0f;
    frontWing.position[2] = 3.0f;
    frontWing.clGain = 1.0f;
    frontWing.cdGain = 0.8f;

    Wing rearWing;
    rearWing.name = "Rear Wing";
    rearWing.chord = 0.3f;
    rearWing.span = 1.0f;
    rearWing.angle = 15.0f;
    rearWing.position[2] = -2.5f;
    rearWing.clGain = 1.2f;
    rearWing.cdGain = 1.0f;

    Wing floor;
    floor.name = "Floor";
    floor.chord = 3.0f;
    floor.span = 1.8f;
    floor.angle = 0.0f;
    floor.position[2] = 0.0f;
    floor.position[1] = -0.3f;
    floor.clGain = 2.0f;
    floor.cdGain = 0.1f;

    config.wings.append(frontWing);
    config.wings.append(rearWing);
    config.wings.append(floor);

    return config;
}

AeroModel::AeroConfig AeroModel::getRoadCarConfig() {
    AeroConfig config;
    config.frontalArea = 2.3f;
    config.dragCoefficient = 0.30f;
    config.liftCoefficient = 0.1f; // Slight lift

    return config;
}

// ============================================================================
// INI operations
// ============================================================================

AeroModel::AeroConfig AeroModel::loadFromIni(const QString& iniPath) {
    AeroConfig config;

    QFile file(iniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    QTextStream stream(&file);
    QString currentSection;
    Wing currentWing;
    bool inWing = false;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.startsWith('[') && line.endsWith(']')) {
            if (inWing) {
                config.wings.append(currentWing);
                currentWing = Wing();
            }

            currentSection = line.mid(1, line.length() - 2);
            inWing = currentSection.startsWith("WING_");
        } else if (line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed().toUpper();
            QString value = line.mid(eqPos + 1).trimmed();

            if (inWing) {
                if (key == "NAME") currentWing.name = value;
                else if (key == "CHORD") currentWing.chord = value.toFloat();
                else if (key == "SPAN") currentWing.span = value.toFloat();
                else if (key == "ANGLE") currentWing.angle = value.toFloat();
                else if (key == "CL_GAIN") currentWing.clGain = value.toFloat();
                else if (key == "CD_GAIN") currentWing.cdGain = value.toFloat();
                else if (key == "POSITION") {
                    QStringList pos = value.split(',');
                    if (pos.size() >= 3) {
                        currentWing.position[0] = pos[0].trimmed().toFloat();
                        currentWing.position[1] = pos[1].trimmed().toFloat();
                        currentWing.position[2] = pos[2].trimmed().toFloat();
                    }
                }
            } else {
                if (key == "FRONTAL_AREA") config.frontalArea = value.toFloat();
                else if (key == "DRAG_COEFFICIENT") config.dragCoefficient = value.toFloat();
                else if (key == "LIFT_COEFFICIENT") config.liftCoefficient = value.toFloat();
            }
        }
    }

    if (inWing) {
        config.wings.append(currentWing);
    }

    file.close();
    return config;
}

bool AeroModel::saveToIni(const AeroConfig& config, const QString& iniPath) {
    QFile file(iniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "; Aerodynamic Configuration\n";
    stream << "; Generated by ksEditor\n\n";

    stream << "[BODY]\n";
    stream << "FRONTAL_AREA=" << QString::number(config.frontalArea, 'f', 3) << "\n";
    stream << "DRAG_COEFFICIENT=" << QString::number(config.dragCoefficient, 'f', 4) << "\n";
    stream << "LIFT_COEFFICIENT=" << QString::number(config.liftCoefficient, 'f', 4) << "\n\n";

    for (int i = 0; i < config.wings.size(); ++i) {
        const Wing& wing = config.wings[i];
        stream << "[WING_" << i << "]\n";
        stream << "NAME=" << wing.name << "\n";
        stream << "CHORD=" << QString::number(wing.chord, 'f', 3) << "\n";
        stream << "SPAN=" << QString::number(wing.span, 'f', 3) << "\n";
        stream << "ANGLE=" << QString::number(wing.angle, 'f', 2) << "\n";
        stream << "POSITION=" << wing.position[0] << "," << wing.position[1] << "," << wing.position[2] << "\n";
        stream << "CL_GAIN=" << QString::number(wing.clGain, 'f', 3) << "\n";
        stream << "CD_GAIN=" << QString::number(wing.cdGain, 'f', 3) << "\n\n";
    }

    file.close();
    return true;
}

// ============================================================================
// Validation
// ============================================================================

bool AeroModel::validateConfig(const AeroConfig& config, QString* error) {
    if (config.frontalArea <= 0 || config.frontalArea > 5.0f) {
        if (error) *error = "Frontal area out of range (0-5 m^2)";
        return false;
    }

    if (config.dragCoefficient < 0 || config.dragCoefficient > 1.0f) {
        if (error) *error = "Drag coefficient out of range (0-1)";
        return false;
    }

    for (const Wing& wing : config.wings) {
        if (wing.chord <= 0 || wing.span <= 0) {
            if (error) *error = "Wing dimensions must be positive";
            return false;
        }
    }

    return true;
}

// ============================================================================
// Utility
// ============================================================================

float AeroModel::calculateDynamicPressure(float speed, float airDensity) {
    return 0.5f * airDensity * speed * speed;
}

float AeroModel::calculateReynoldsNumber(float speed, float chord, float viscosity) {
    return speed * chord / viscosity;
}

// ============================================================================
// AeroModelManager implementation
// ============================================================================

AeroModelManager::AeroModelManager() {
}

void AeroModelManager::loadFromIni(const QString& carPath) {
    QString iniPath = carPath + "/data/aero.ini";
    m_model = AeroModel();
    AeroModel::AeroConfig config = AeroModel::loadFromIni(iniPath);

    // Load wings into model
    for (const AeroModel::Wing& wing : config.wings) {
        m_model.addWing(wing);
    }
}

void AeroModelManager::saveToIni(const QString& carPath) const {
    QString iniPath = carPath + "/data/aero.ini";

    AeroModel::AeroConfig config;
    config.frontalArea = 2.0f;

    for (int i = 0; i < m_model.wingCount(); ++i) {
        config.wings.append(m_model.wing(i));
    }

    AeroModel::saveToIni(config, iniPath);
}

AeroModel::AeroForces AeroModelManager::calculateForces(
    float speed, float rideHeightFront, float rideHeightRear) const {

    AeroModel::AeroState state;
    state.speed = speed;
    state.rideHeightFront = rideHeightFront;
    state.rideHeightRear = rideHeightRear;

    return m_model.calculate(state);
}

float AeroModelManager::calculateTopSpeed(float enginePower, float weight) const {
    // Simplified top speed calculation
    // Power = Drag * Speed
    // Drag = 0.5 * rho * Cd * A * V^2
    // Solving for V: V = (2 * P / (rho * Cd * A))^(1/3)

    AeroModel::AeroConfig config;
    float rho = 1.225f;
    float CdA = config.dragCoefficient * config.frontalArea;

    if (CdA > 0) {
        float V = std::pow(2.0f * enginePower / (rho * CdA), 1.0f / 3.0f);
        return V * 3.6f; // Convert to km/h
    }

    return 200.0f; // Default
}

float AeroModelManager::calculateCorneringForce(float speed, float cornerRadius) const {
    // F = m * v^2 / r
    float v = speed / 3.6f; // Convert to m/s
    return m_weight * v * v / cornerRadius;
}

float AeroModelManager::calculateBrakeDistance(float speed, float friction) const {
    // d = v^2 / (2 * mu * g)
    float v = speed / 3.6f;
    float g = 9.81f;
    return v * v / (2.0f * friction * g);
}

QMap<QString, QPair<float, float>> AeroModelManager::compareAero(const AeroModelManager& other) const {
    QMap<QString, QPair<float, float>> comparison;

    AeroModel::AeroState state;
    state.speed = 50.0f; // 50 m/s = 180 km/h

    AeroModel::AeroForces forces1 = m_model.calculate(state);
    AeroModel::AeroForces forces2 = other.m_model.calculate(state);

    comparison["Downforce"] = QPair<float,float>(forces1.downforce, forces2.downforce);
    comparison["Drag"] = QPair<float,float>(forces1.drag, forces2.drag);
    comparison["L/D Ratio"] = QPair<float,float>(forces1.ldRatio, forces2.ldRatio);
    comparison["Aero Balance"] = QPair<float,float>(forces1.aeroBalance, forces2.aeroBalance);

    return comparison;
}
