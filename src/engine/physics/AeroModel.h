#pragma once

/**
 * @file AeroModel.h
 * @brief Aerodynamic model (wings, body, ground effect) — Qt-free
 */

#include "PhysicsCoreTypes.h"
#include "AeroDraft.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ks {
namespace physics {

class AeroModel {
public:
    struct Wing {
        std::string name;
        float chord = 1.0f;
        float span = 1.5f;
        float angle = 0.0f;
        float position[3] = {0, 0, 0}; // x,y,z relative to CoG (z+ = forward)

        std::vector<std::pair<float, float>> aoaClLut;
        std::vector<std::pair<float, float>> aoaCdLut;
        std::vector<std::pair<float, float>> heightClLut;
        std::vector<std::pair<float, float>> heightCdLut;

        float clGain = 1.0f;
        float cdGain = 1.0f;
        float cl = 0.0f;
        float cd = 0.0f;

        float area() const { return chord * span; }
    };

    struct AeroConfig {
        std::vector<Wing> wings;
        float frontalArea = 2.0f;
        float dragCoefficient = 0.35f;
        float liftCoefficient = -0.1f; // negative = downforce
        float groundEffectFactor = 1.0f;
        float rideHeightSensitivity = 1.0f;
    };

    struct AeroState {
        float speed = 0.0f;
        float rideHeightFront = 0.05f;
        float rideHeightRear = 0.07f;
        float yawAngle = 0.0f;
        float rollAngle = 0.0f;
        float pitchAngle = 0.0f;
        float airDensity = Constants::DEFAULT_AIR_DENSITY;
        // Optional draft (from nearby car)
        float draftDragScale = 1.0f;      // 1 - dragReduction
        float draftDownforceScale = 1.0f; // 1 - downforceLoss
    };

    struct AeroForces {
        float downforce = 0.0f;
        float drag = 0.0f;
        float lateralForce = 0.0f;
        float frontDownforce = 0.0f;
        float rearDownforce = 0.0f;
        float aeroBalance = 0.5f;
        float ldRatio = 0.0f;

        /** World-ish force vector: drag opposes velocity (+X forward), downforce -Y. */
        PhysVec3 asForceVector(const PhysVec3& forwardDir) const {
            PhysVec3 f = forwardDir.normalized();
            // drag opposite to forward, downforce downward
            return f * (-drag) + PhysVec3{0.0f, -downforce, 0.0f} +
                   PhysVec3{-f.z, 0.0f, f.x} * lateralForce; // crude lateral
        }
    };

    AeroForces calculate(const AeroState& state) const;
    float calculateWingForce(const Wing& wing, const AeroState& state) const;

    float interpolateCl(const Wing& wing, float aoa) const;
    float interpolateCd(const Wing& wing, float aoa) const;
    float interpolateHeightCl(const Wing& wing, float height) const;
    float interpolateHeightCd(const Wing& wing, float height) const;

    void setConfig(const AeroConfig& config) { m_config = config; }
    AeroConfig getConfig() const { return m_config; }
    void loadFromIniFile(const std::string& iniPath) { m_config = loadFromIni(iniPath); }

    void addWing(const Wing& wing);
    void removeWing(int index);
    void clearWings();
    int wingCount() const { return static_cast<int>(m_config.wings.size()); }
    Wing& wing(int index) { return m_config.wings[index]; }
    const Wing& wing(int index) const { return m_config.wings[index]; }

    static AeroConfig getSedanConfig();
    static AeroConfig getGT3Config();
    static AeroConfig getFormulaConfig();
    static AeroConfig getRoadCarConfig();

    static AeroConfig loadFromIni(const std::string& iniPath);
    static bool saveToIni(const AeroConfig& config, const std::string& iniPath);
    static bool validateConfig(const AeroConfig& config, std::string* error = nullptr);

    static float calculateDynamicPressure(float speed, float airDensity);
    static float calculateReynoldsNumber(float speed, float chord, float viscosity = 1.5e-5f);

private:
    AeroConfig m_config;
    float interpolateLut(const std::vector<std::pair<float, float>>& lut, float x) const;
};

/**
 * High-level aero + optional drafting for vehicle integration.
 */
class AeroModelManager {
public:
    AeroModelManager();

    AeroModel& model() { return m_model; }
    const AeroModel& model() const { return m_model; }

    void loadFromIni(const std::string& carPath);
    void saveToIni(const std::string& carPath) const;

    AeroModel::AeroForces calculateForces(float speed, float rideHeightFront, float rideHeightRear,
                                          float airDensity = Constants::DEFAULT_AIR_DENSITY) const;

    /**
     * Full step: base aero + draft from leader car, returns forces ready for RigidBody.
     */
    AeroModel::AeroForces calculateIntegrated(
        float speed,
        float rideHeightFront,
        float rideHeightRear,
        const PhysVec3& egoPos,
        const PhysVec3& egoForward,
        const PhysVec3* leaderPos = nullptr,
        float airDensity = Constants::DEFAULT_AIR_DENSITY) const;

    float calculateTopSpeed(float enginePower, float weight) const;
    float calculateCorneringForce(float speed, float cornerRadius) const;
    float calculateBrakeDistance(float speed, float friction) const;

    std::map<std::string, std::pair<float, float>> compareAero(const AeroModelManager& other) const;

    void setWeight(float kg) { m_weight = kg; }
    void setWheelbase(float m) { m_wheelbase = m; }

private:
    AeroModel m_model;
    float m_weight = 1300.0f;
    float m_wheelbase = 2.6f;
    float m_trackWidth = 1.5f;
};

} // namespace physics
} // namespace ks
