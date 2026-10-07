#include <vector>
#include <string>
#include "PhysicsValidator.h"
#include "VehiclePhysics.h"
#include "TrackPhysics.h"
#include <cmath>
#include <cstdio>
namespace ks {
namespace physics {

// ============================================================================
// ValidationResult Implementation
// ============================================================================

std::string ValidationResult::formatReport() const {
    std::string report;
    
    if (isValid && errors.isEmpty() && warnings.isEmpty()) {
        report = "✓ Validation passed with no issues.\n";
        return report;
    }
    
    report += "=== Validation Report ===\n\n";
    
    if (!errors.isEmpty()) {
        report += "✗ ERRORS (" + std::string::number(errors.size()) + "):\n";
        for (const std::string& error : errors) {
            report += "  - " + error + "\n";
        }
        report += "\n";
    }
    
    if (!warnings.isEmpty()) {
        report += "⚠ WARNINGS (" + std::string::number(warnings.size()) + "):\n";
        for (const std::string& warning : warnings) {
            report += "  - " + warning + "\n";
        }
        report += "\n";
    }
    
    if (!suggestions.isEmpty()) {
        report += "💡 SUGGESTIONS:\n";
        for (const std::string& suggestion : suggestions) {
            report += "  - " + suggestion + "\n";
        }
        report += "\n";
    }
    
    report += "Status: " + std::string(isValid ? "VALID" : "INVALID") + "\n";
    return report;
}

// ============================================================================
// Vehicle Validation
// ============================================================================

ValidationResult PhysicsValidator::validateVehicleParameters(float mass, float power, float wheelbase) {
    ValidationResult result;
    
    if (mass < 400.0f || mass > 2000.0f) {
        result.addError(std::string("Mass out of range: %1 kg (400-2000)").arg(mass));
    }
    
    if (power < 20.0f || power > 1500.0f) {
        result.addError(std::string("Power out of range: %1 kW (20-1500)").arg(power));
    }
    
    if (wheelbase < 1.5f || wheelbase > 4.0f) {
        result.addError(std::string("Wheelbase out of range: %1 m (1.5-4.0)").arg(wheelbase));
    }
    
    checkPowerToWeight(mass, power, result);
    
    if (mass > 1500.0f && power < 150.0f) {
        result.addSuggestion("Heavy vehicle with low power - consider weight reduction");
    }
    
    if (mass < 600.0f && power > 300.0f) {
        result.addWarning("Very light vehicle with high power - stability may be compromised");
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateTireParameters(float stiffness, float friction, float radius) {
    ValidationResult result;
    
    if (stiffness < 1000.0f || stiffness > 100000.0f) {
        result.addWarning(std::string("Tire stiffness out of typical range: %1 N/rad").arg(stiffness));
    }
    
    if (friction < 0.2f || friction > 2.0f) {
        result.addWarning(std::string("Friction coefficient out of range: %1 (0.2-2.0)").arg(friction));
    }
    
    if (radius < 0.15f || radius > 0.6f) {
        result.addError(std::string("Tire radius out of range: %1 m (0.15-0.6)").arg(radius));
    }
    
    if (radius > 0.5f && friction < 0.5f) {
        result.addSuggestion("Large tires with low friction - consider softer compound");
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateSuspensionSetup(const BalanceSetup& setup) {
    ValidationResult result;
    
    if (setup.springRateFront < 5000.0f || setup.springRateFront > 80000.0f) {
        result.addWarning(std::string("Front spring rate unusual: %1 N/m").arg(setup.springRateFront));
    }
    
    if (setup.springRateRear < 5000.0f || setup.springRateRear > 80000.0f) {
        result.addWarning(std::string("Rear spring rate unusual: %1 N/m").arg(setup.springRateRear));
    }
    
    float springRatio = setup.springRateFront / (setup.springRateRear + 1.0f);
    if (springRatio > 2.0f) {
        result.addWarning("Front springs significantly stiffer than rear");
    }
    if (springRatio < 0.5f) {
        result.addWarning("Rear springs significantly stiffer than front");
    }
    
    if (setup.antiRollBarFront > 30000.0f) {
        result.addSuggestion("High front ARB rate - may cause understeer");
    }
    
    if (std::abs(setup.camberFront) > 5.0f) {
        result.addWarning(std::string("High front camber: %1°").arg(setup.camberFront));
    }
    
    if (std::abs(setup.camberRear) > 4.0f) {
        result.addWarning(std::string("High rear camber: %1°").arg(setup.camberRear));
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateWeightDistribution(float frontPercent, float leftPercent) {
    ValidationResult result;
    
    if (frontPercent < 30.0f || frontPercent > 70.0f) {
        result.addError(std::string("Front weight distribution extreme: %1%").arg(frontPercent));
    }
    
    if (leftPercent < 40.0f || leftPercent > 60.0f) {
        result.addWarning(std::string("Lateral weight distribution uneven: %1%").arg(leftPercent));
    }
    
    if (frontPercent > 60.0f) {
        result.addSuggestion("Front-heavy car - consider reducing front weight");
    }
    
    if (frontPercent < 40.0f) {
        result.addSuggestion("Rear-heavy car - consider reducing rear weight");
    }
    
    return result;
}

// ============================================================================
// Track Validation
// ============================================================================

ValidationResult PhysicsValidator::validateTrack(const TrackLayout& layout) {
    ValidationResult result;
    
    if (layout.name.isEmpty()) {
        result.addError("Track name is empty");
    }
    
    ValidationResult lengthResult = validateTrackLength(layout.length);
    if (!lengthResult.isValid) {
        result.errors.append(lengthResult.errors);
    }
    result.warnings.append(lengthResult.warnings);
    
    if (layout.sectors.size() < 3) {
        result.addWarning("Track has fewer than 3 sectors");
    } else {
        ValidationResult sectorResult = validateSectors(layout.sectors);
        if (!sectorResult.isValid) {
            result.errors.append(sectorResult.errors);
        }
        result.warnings.append(sectorResult.warnings);
    }
    
    if (layout.corners.isEmpty()) {
        result.addWarning("No corners defined for track");
    } else {
        ValidationResult cornerResult = validateCorners(layout.corners);
        if (!cornerResult.isValid) {
            result.errors.append(cornerResult.errors);
        }
        result.warnings.append(cornerResult.warnings);
    }
    
    if (layout.pitEntryDistance < 0 || layout.pitExitDistance < 0) {
        result.addWarning("Pit entry or exit distance not set");
    }
    
    if (layout.pitSpeedLimit < 1.0f || layout.pitSpeedLimit > 100.0f) {
        result.addWarning(std::string("Unusual pit speed limit: %1 km/h").arg(layout.pitSpeedLimit));
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateSectors(const std::vector<TrackSector>& sectors) {
    ValidationResult result;
    
    if (sectors.isEmpty()) {
        result.addError("No sectors defined");
        return result;
    }
    
    double prevEnd = 0.0;
    for (int i = 0; i < sectors.size(); ++i) {
        const auto& sector = sectors[i];
        if (sector.name.isEmpty()) {
            result.addWarning(std::string("Sector %1 has no name").arg(i));
        }
        if (sector.startDistance < 0 || sector.endDistance <= sector.startDistance) {
            result.addError(std::string("Invalid sector boundaries for sector %1").arg(i));
        }
        if (i > 0 && sector.startDistance != prevEnd) {
            result.addWarning(std::string("Sector %1 start doesn't match previous sector end").arg(i));
        }
        prevEnd = sector.endDistance;
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateTrackLength(float length) {
    ValidationResult result;
    
    if (length < 500.0f || length > 50000.0f) {
        result.addError(std::string("Track length out of range: %1 m (500-50000)").arg(length));
    }
    
    if (length < 1000.0f) {
        result.addWarning("Very short track - less than 1km");
    }
    
    if (length > 20000.0f) {
        result.addWarning("Very long track - may cause memory issues");
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateCorners(const std::vector<TrackCorner>& corners) {
    ValidationResult result;
    
    if (corners.isEmpty()) return result;
    
    for (int i = 0; i < corners.size(); ++i) {
        const auto& corner = corners[i];
        if (corner.name.isEmpty()) {
            result.addWarning(std::string("Corner %1 has no name").arg(i));
        }
        if (corner.radius < 1.0f || corner.radius > 500.0f) {
            result.addWarning(std::string("Unusual radius for corner %1: %2 m").arg(i).arg(corner.radius));
        }
        checkCornerRadius(corner.radius, result);
        if (i > 0 && std::abs(corner.position - corners[i-1].position) < 10.0f) {
            result.addWarning(std::string("Corners %1 and %2 very close").arg(i-1).arg(i));
        }
    }
    
    return result;
}

// ============================================================================
// Weather Validation
// ============================================================================

ValidationResult PhysicsValidator::validateWeather(const WeatherState& weather) {
    ValidationResult result;
    
    validateTemperature(weather.ambientTemp, -40.0f, 60.0f, result);
    validateTemperature(weather.trackTemp, -40.0f, 80.0f, result);
    
    if (weather.airDensity < 0.8f || weather.airDensity > 1.5f) {
        result.addWarning(std::string("Unusual air density: %1 kg/m³").arg(weather.airDensity));
    }
    
    if (weather.trackWetness < 0.0f || weather.trackWetness > 1.0f) {
        result.addError("Track wetness must be between 0 and 1");
    }
    
    if (weather.rainIntensity < 0.0f || weather.rainIntensity > 100.0f) {
        result.addError("Rain intensity out of range (0-100 mm/h)");
    }
    
    if (weather.windSpeed < 0.0f || weather.windSpeed > 50.0f) {
        result.addWarning(std::string("Unusual wind speed: %1 m/s").arg(weather.windSpeed));
    }
    
    if (weather.humidity < 0.0f || weather.humidity > 1.0f) {
        result.addError("Humidity must be between 0 and 1");
    }
    
    if (weather.cloudCover < 0.0f || weather.cloudCover > 1.0f) {
        result.addError("Cloud cover must be between 0 and 1");
    }
    
    if (weather.rainIntensity > 10.0f && weather.trackWetness < 0.3f) {
        result.addWarning("High rain intensity with low track wetness - inconsistency");
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateTemperature(float temp, float minTemp, float maxTemp) {
    ValidationResult result;
    validateTemperature(temp, minTemp, maxTemp, result);
    return result;
}

void PhysicsValidator::validateTemperature(float temp, float minTemp, float maxTemp, ValidationResult& result) {
    if (temp < minTemp || temp > maxTemp) {
        result.addError(std::string("Temperature out of range: %1°C (%2 to %3)")
                       .arg(temp).arg(minTemp).arg(maxTemp));
    }
}

// ============================================================================
// Simulation Validation
// ============================================================================

ValidationResult PhysicsValidator::validateSimulationConfig(const SimulationConfig& config) {
    ValidationResult result;
    
    if (config.fixedTimeStep <= 0.0f || config.fixedTimeStep > 0.1f) {
        result.addError(std::string("Fixed time step out of range: %1 (0.0001-0.1)").arg(config.fixedTimeStep));
    }
    
    if (config.solverIterations < 1 || config.solverIterations > 100) {
        result.addWarning(std::string("Unusual solver iterations: %1 (1-100)").arg(config.solverIterations));
    }
    
    if (config.airDensity < 0.8f || config.airDensity > 1.5f) {
        result.addWarning(std::string("Unusual air density: %1 kg/m³").arg(config.airDensity));
    }
    
    if (config.gravity < 0.0f || config.gravity > 20.0f) {
        result.addError(std::string("Gravity out of range: %1 m/s²").arg(config.gravity));
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateSimulationStability(const SimulationState& state) {
    ValidationResult result;
    
    if (!state.isValid()) {
        result.addError("Simulation state contains NaN or Inf values");
        return result;
    }
    
    if (state.speed > Constants::MAX_REASONABLE_SPEED) {
        result.addError(std::string("Speed exceeds maximum reasonable: %1 m/s").arg(state.speed));
    }
    
    if (state.rpm > Constants::MAX_REASONABLE_RPM) {
        result.addWarning(std::string("RPM exceeds maximum reasonable: %1").arg(state.rpm));
    }
    
    float kineticEnergy = state.kineticEnergy(1000.0f);
    if (kineticEnergy > 1e8f) {
        result.addWarning("Kinetic energy unusually high - possible numerical issue");
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateTimeStep(float dt) {
    ValidationResult result;
    
    if (dt <= 0.0f) {
        result.addError("Time step must be positive");
    }
    
    if (dt > 0.1f) {
        result.addError(std::string("Time step too large: %1 (max 0.1)").arg(dt));
    }
    
    if (dt < 1e-6f) {
        result.addWarning("Very small time step - may cause performance issues");
    }
    
    return result;
}

// ============================================================================
// Tire Validation
// ============================================================================

ValidationResult PhysicsValidator::validateTireState(const TireForceData& data) {
    ValidationResult result;
    
    if (!isFinite(data.lateralForce)) {
        result.addError("Lateral force is NaN or Inf");
    }
    if (!isFinite(data.longitudinalForce)) {
        result.addError("Longitudinal force is NaN or Inf");
    }
    if (!isFinite(data.aligningMoment)) {
        result.addError("Aligning moment is NaN or Inf");
    }
    
    if (std::abs(data.lateralForce) > 50000.0f) {
        result.addWarning(std::string("Lateral force unusually high: %1 N").arg(data.lateralForce));
    }
    
    if (std::abs(data.longitudinalForce) > 50000.0f) {
        result.addWarning(std::string("Longitudinal force unusually high: %1 N").arg(data.longitudinalForce));
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateTireTemperatures(const float temps[4]) {
    ValidationResult result;
    
    for (int i = 0; i < 4; ++i) {
        if (!isFinite(temps[i])) {
            result.addError(std::string("Tire temperature %1 is NaN or Inf").arg(i));
        }
        if (temps[i] < -20.0f || temps[i] > 200.0f) {
            result.addWarning(std::string("Tire temperature %1 out of range: %2°C").arg(i).arg(temps[i]));
        }
    }
    
    // Check for large temperature differences
    float maxTemp = std::max({temps[0], temps[1], temps[2], temps[3]});
    float minTemp = std::min({temps[0], temps[1], temps[2], temps[3]});
    if (maxTemp - minTemp > 50.0f) {
        result.addWarning(std::string("Large tire temperature spread: %1°C").arg(maxTemp - minTemp));
    }
    
    return result;
}

// ============================================================================
// Driver Validation
// ============================================================================

ValidationResult PhysicsValidator::validateDriverInput(float throttle, float brake, float steering) {
    ValidationResult result;
    
    if (!isInRange(throttle, 0.0f, 1.0f)) {
        result.addError(std::string("Throttle out of range: %1").arg(throttle));
    }
    if (!isInRange(brake, 0.0f, 1.0f)) {
        result.addError(std::string("Brake out of range: %1").arg(brake));
    }
    if (!isInRange(steering, -1.0f, 1.0f)) {
        result.addError(std::string("Steering out of range: %1").arg(steering));
    }
    
    if (throttle > 0.9f && brake > 0.9f) {
        result.addWarning("Throttle and brake both near maximum - check input logic");
    }
    
    return result;
}

// ============================================================================
// Helper Methods
// ============================================================================

bool PhysicsValidator::isFinite(float value) {
    return !std::isnan(value) && !std::isinf(value);
}

bool PhysicsValidator::isInRange(float value, float min, float max) {
    return value >= min && value <= max;
}

std::string PhysicsValidator::formatFloat(float value, int precision) {
    return std::string::number(value, 'f', precision);
}

void PhysicsValidator::checkPowerToWeight(float mass, float power, ValidationResult& result) {
    float ratio = power / mass;
    if (ratio > 1.0f) {
        result.addWarning(std::string("Very high power-to-weight ratio: %1 kW/kg").arg(ratio));
        result.addSuggestion("High power-to-weight - consider aero and tire upgrades");
    }
    if (ratio < 0.1f) {
        result.addWarning(std::string("Low power-to-weight ratio: %1 kW/kg").arg(ratio));
        result.addSuggestion("Low power-to-weight - focus on cornering speed");
    }
}

void PhysicsValidator::checkTireLoad(float load, float maxLoad, ValidationResult& result) {
    if (load > maxLoad) {
        result.addWarning(std::string("Tire load exceeds maximum: %1 N (max %2)").arg(load).arg(maxLoad));
    }
}

void PhysicsValidator::checkCornerRadius(float radius, ValidationResult& result) {
    if (radius < 5.0f) {
        result.addWarning(std::string("Very tight corner radius: %1 m").arg(radius));
    }
    if (radius > 500.0f) {
        result.addWarning(std::string("Very large corner radius: %1 m").arg(radius));
    }
}

// ============================================================================
// Enhanced Validation Methods
// ============================================================================

std::optional<std::string> PhysicsValidator::validateVehicleMass(double mass) {
    if (mass <= 0.0) {
        return "Vehicle mass must be positive";
    }
    if (mass < 100.0) {
        return "Vehicle mass too low (< 100 kg)";
    }
    if (mass > 10000.0) {
        return "Vehicle mass too high (> 10000 kg)";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateEnginePower(double powerKw) {
    if (powerKw < 0.0) {
        return "Engine power cannot be negative";
    }
    if (powerKw > 2000.0) {
        return "Engine power unrealistically high (> 2000 kW)";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateWheelBase(double wheelBase) {
    if (wheelBase <= 0.0) {
        return "Wheelbase must be positive";
    }
    if (wheelBase < 1.0) {
        return "Wheelbase too short (< 1 m)";
    }
    if (wheelBase > 5.0) {
        return "Wheelbase too long (> 5 m)";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateTireSlipAngle(double slipAngleDeg) {
    if (std::abs(slipAngleDeg) > 45.0) {
        return "Slip angle exceeds 45 degrees - tire likely saturated";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateTireSlipRatio(double slipRatio) {
    if (std::abs(slipRatio) > 2.0) {
        return "Slip ratio exceeds 2.0 - wheel likely locked or spinning";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateNormalLoad(double load) {
    if (load < 0.0) {
        return "Normal load cannot be negative";
    }
    if (load > 50000.0) {
        return "Normal load unrealistically high (> 50000 N)";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateTemperature(double temp, double minTemp, double maxTemp) {
    if (temp < minTemp) {
        return "Temperature below minimum (" + std::to_string(minTemp) + " C)";
    }
    if (temp > maxTemp) {
        return "Temperature above maximum (" + std::to_string(maxTemp) + " C)";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validatePressure(double pressure, double minPressure, double maxPressure) {
    if (pressure < minPressure) {
        return "Pressure below minimum (" + std::to_string(minPressure) + " bar)";
    }
    if (pressure > maxPressure) {
        return "Pressure above maximum (" + std::to_string(maxPressure) + " bar)";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateRpm(double rpm, double maxRpm) {
    if (rpm < 0.0) {
        return "RPM cannot be negative";
    }
    if (rpm > maxRpm) {
        return "RPM exceeds maximum (" + std::to_string(maxRpm) + ")";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateSpeed(double speed) {
    if (speed < 0.0) {
        return "Speed cannot be negative";
    }
    if (speed > 400.0) {
        return "Speed exceeds 400 m/s - likely unrealistic";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateForce(double force) {
    if (std::abs(force) > 100000.0) {
        return "Force magnitude exceeds 100000 N";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateTorque(double torque) {
    if (std::abs(torque) > 10000.0) {
        return "Torque magnitude exceeds 10000 Nm";
    }
    return std::nullopt;
}

std::optional<std::string> PhysicsValidator::validateTimeStep(double dt) {
    if (dt <= 0.0) {
        return "Time step must be positive";
    }
    if (dt < 0.0001) {
        return "Time step too small (< 0.1 ms)";
    }
    if (dt > 0.1) {
        return "Time step too large (> 100 ms)";
    }
    return std::nullopt;
}

ValidationResult PhysicsValidator::validateAll(const SimulationState& state) {
    ValidationResult result;
    
    // Check position
    if (!isFinite(state.position.x()) || !isFinite(state.position.y()) || !isFinite(state.position.z())) {
        result.addError("Position contains NaN or Inf");
    }
    
    // Check velocity
    if (!isFinite(state.velocity.x()) || !isFinite(state.velocity.y()) || !isFinite(state.velocity.z())) {
        result.addError("Velocity contains NaN or Inf");
    }
    
    // Check speed
    auto speedError = validateSpeed(state.speed);
    if (speedError) {
        result.addError(std::string::fromStdString(*speedError));
    }
    
    // Check RPM
    auto rpmError = validateRpm(state.rpm);
    if (rpmError) {
        result.addError(std::string::fromStdString(*rpmError));
    }
    
    // Check tire temperatures
    for (int i = 0; i < 4; ++i) {
        auto tempError = validateTemperature(state.tyreTemp[i], -40.0, 150.0);
        if (tempError) {
            result.addWarning(std::string("Wheel %1: %2").arg(i).arg(std::string::fromStdString(*tempError)));
        }
    }
    
    return result;
}

ValidationResult PhysicsValidator::validateWheelStates(const std::array<TireWheelState, 4>& wheels) {
    ValidationResult result;
    
    for (int i = 0; i < 4; ++i) {
        const auto& wheel = wheels[i];
        
        // Check normal load
        auto loadError = validateNormalLoad(wheel.normalLoad);
        if (loadError) {
            result.addError(std::string("Wheel %1: %2").arg(i).arg(std::string::fromStdString(*loadError)));
        }
        
        // Check slip angle
        auto slipAngleError = validateTireSlipAngle(wheel.slipAngle);
        if (slipAngleError) {
            result.addWarning(std::string("Wheel %1: %2").arg(i).arg(std::string::fromStdString(*slipAngleError)));
        }
        
        // Check slip ratio
        auto slipRatioError = validateTireSlipRatio(wheel.slipRatio);
        if (slipRatioError) {
            result.addWarning(std::string("Wheel %1: %2").arg(i).arg(std::string::fromStdString(*slipRatioError)));
        }
        
        // Check temperature
        auto tempError = validateTemperature(wheel.temperature, -40.0, 150.0);
        if (tempError) {
            result.addWarning(std::string("Wheel %1: %2").arg(i).arg(std::string::fromStdString(*tempError)));
        }
        
        // Check pressure
        auto pressureError = validatePressure(wheel.pressure);
        if (pressureError) {
            result.addWarning(std::string("Wheel %1: %2").arg(i).arg(std::string::fromStdString(*pressureError)));
        }
    }
    
    return result;
}

} // namespace physics
} // namespace ks