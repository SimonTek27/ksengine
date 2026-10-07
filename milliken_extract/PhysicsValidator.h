#pragma once

/**
 * @file PhysicsValidator.h
 * @brief Validation utilities for physics configurations
 * @copyright KS Physics Engine
 */

#include "PhysicsCoreTypes.h"
#include "TireSimulator.h"
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <string>

namespace ks {
namespace physics {

// Forward declarations
struct BalanceSetup;
struct SimulationConfig;
struct TrackLayout;
struct TrackSector;

// ============================================================================
// Validation Result
// ============================================================================

/**
 * @brief Result of a validation operation
 */
struct ValidationResult {
    bool isValid = true;              ///< Whether validation passed
    std::vector<std::string> errors;          ///< Critical errors
    std::vector<std::string> warnings;        ///< Non-critical warnings
    std::vector<std::string> suggestions;     ///< Improvement suggestions
    
    /// Add an error
    void addError(const std::string& error) {
        isValid = false;
        errors.append(error);
    }
    
    /// Add a warning
    void addWarning(const std::string& warning) {
        warnings.append(warning);
    }
    
    /// Add a suggestion
    void addSuggestion(const std::string& suggestion) {
        suggestions.append(suggestion);
    }
    
    /// Check if there are any issues
    bool hasIssues() const {
        return !errors.isEmpty() || !warnings.isEmpty();
    }
    
    /// Generate a formatted report
    std::string formatReport() const;
};

// ============================================================================
// Physics Validator
// ============================================================================

/**
 * @brief Static validation utilities for physics parameters
 */
class PhysicsValidator {
public:
    // Vehicle validation
    static ValidationResult validateVehicleParameters(float mass, float power, float wheelbase);
    static ValidationResult validateTireParameters(float stiffness, float friction, float radius);
    static ValidationResult validateSuspensionSetup(const BalanceSetup& setup);
    static ValidationResult validateWeightDistribution(float frontPercent, float leftPercent);
    
    // Track validation
    static ValidationResult validateTrack(const TrackLayout& layout);
    static ValidationResult validateSectors(const std::vector<TrackSector>& sectors);
    static ValidationResult validateTrackLength(float length);
    static ValidationResult validateCorners(const std::vector<struct TrackCorner>& corners);
    
    // Weather validation
    static ValidationResult validateWeather(const WeatherState& weather);
    static ValidationResult validateTemperature(float temp, float minTemp = -40.0f, float maxTemp = 60.0f);
    static void validateTemperature(float temp, float minTemp, float maxTemp, ValidationResult& result);
    
    // Simulation validation
    static ValidationResult validateSimulationConfig(const struct SimulationConfig& config);
    static ValidationResult validateSimulationStability(const SimulationState& state);
    static ValidationResult validateTimeStep(float dt);
    
    // Tire validation
    static ValidationResult validateTireState(const TireForceData& data);
    static ValidationResult validateTireTemperatures(const float temps[4]);
    
    // Driver validation
    static ValidationResult validateDriverInput(float throttle, float brake, float steering);
    
    // Helper methods
    static bool isFinite(float value);
    static bool isInRange(float value, float min, float max);
    static std::string formatFloat(float value, int precision = 2);
    
    // Enhanced validation with std::optional error reporting
    static std::optional<std::string> validateVehicleMass(double mass);
    static std::optional<std::string> validateEnginePower(double powerKw);
    static std::optional<std::string> validateWheelBase(double wheelBase);
    static std::optional<std::string> validateTireSlipAngle(double slipAngleDeg);
    static std::optional<std::string> validateTireSlipRatio(double slipRatio);
    static std::optional<std::string> validateNormalLoad(double load);
    static std::optional<std::string> validateTemperature(double temp, double minTemp = -40.0, double maxTemp = 150.0);
    static std::optional<std::string> validatePressure(double pressure, double minPressure = 0.5, double maxPressure = 5.0);
    static std::optional<std::string> validateRpm(double rpm, double maxRpm = 20000.0);
    static std::optional<std::string> validateSpeed(double speed);
    static std::optional<std::string> validateForce(double force);
    static std::optional<std::string> validateTorque(double torque);
    static std::optional<std::string> validateTimeStep(double dt);
    
    // Batch validation
    static ValidationResult validateAll(const SimulationState& state);
    static ValidationResult validateWheelStates(const std::array<TireWheelState, 4>& wheels);
    
private:
    static void checkPowerToWeight(float mass, float power, ValidationResult& result);
    static void checkTireLoad(float load, float maxLoad, ValidationResult& result);
    static void checkCornerRadius(float radius, ValidationResult& result);
};

} // namespace physics
} // namespace ks