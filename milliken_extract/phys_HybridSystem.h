#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QPair>

/**
 * @brief ERS/Hybrid System for Assetto Corsa Physics
 *
 * Simulates hybrid powertrain systems including:
 * - MGU-K (Motor Generator Unit - Kinetic): regen braking + torque assist
 * - MGU-H (Motor Generator Unit - Heat): exhaust energy harvesting + turbo spool
 * - Battery (Energy Store): SOC, temperature, charge/discharge limits
 * - Deployment strategies: None, Low, Medium, High, Attack, Overtake, Qualify
 *
 * Supports multiple modern racing hybrid architectures:
 * - F1 2014-2020: 120kW MGU-K, unlimited MGU-H, 4MJ/lap
 * - F1 2026: 350kW MGU-K, no MGU-H, 8.5MJ/lap
 * - LMP1: 200kW front axle hybrid
 * - LMDh: 50kW rear axle hybrid
 * - Road hybrid: Various mild/full hybrid configurations
 */

class HybridSystem {
public:
    enum class ErsMode {
        None = 0,
        Low,
        Medium,
        High,
        Attack,
        Overtake,
        Qualifying,
        Auto
    };

    enum class HybridArchitecture {
        F1_2014,        // 120kW MGU-K + MGU-H, 4MJ/lap
        F1_2026,        // 350kW MGU-K only, 8.5MJ/lap, active aero
        LMP1,           // 200kW front hybrid, 7MJ/lap
        LMDh,           // 50kW rear hybrid, std battery
        Road_Mild,      // 15kW mild hybrid (48V)
        Road_Full,      // 100kW full hybrid (400V)
        Road_PlugIn,    // 150kW plug-in hybrid
        Electric        // Full EV
    };

    struct MgukConfig {
        float maxPowerKw = 120.0f;         // Maximum MGU-K power (kW)
        float maxRegenKw = 120.0f;         // Maximum regen power (kW)
        float maxTorqueNm = 200.0f;        // Maximum assist torque (Nm)
        float regenEfficiency = 0.85f;     // Regen efficiency (0-1)
        float deployEfficiency = 0.92f;    // Deployment efficiency (0-1)
        float inertia = 0.05f;             // Rotational inertia (kg*m^2)
        bool connectedToCrank = true;      // Directly connected to engine crank
    };

    struct MguhConfig {
        float maxPowerKw = 120.0f;         // Maximum MGU-H power (kW)
        float maxHarvestKw = 120.0f;       // Maximum harvest power (kW)
        float harvestEfficiency = 0.80f;   // Exhaust energy harvest efficiency
        float spoolPowerKw = 5.0f;         // Power for anti-lag turbo spooling
        bool connectedToTurbo = true;      // Connected to turbo shaft
        bool canTransferToMguk = true;     // F1-style MGU-H -> MGU-K transfer
    };

    struct BatteryConfig {
        float capacityMj = 4.0f;            // Total energy capacity (MJ)
        float maxChargeKw = 120.0f;         // Max charge rate (kW)
        float maxDischargeKw = 120.0f;      // Max discharge rate (kW)
        float perLapEnergyMj = 4.0f;        // Max energy deploy per lap (MJ)
        float internalResistanceOhm = 0.05f; // Internal resistance (Ohm)
        float nominalVoltage = 800.0f;       // Nominal voltage (V)
        float thermalMass = 5.0f;           // Thermal mass (kJ/K)
        float coolingRate = 0.1f;           // Cooling rate per second
        float optimalTemp = 35.0f;          // Optimal operating temperature (C)
        float maxTemp = 60.0f;              // Maximum safe temperature (C)
        float minSoc = 5.0f;                // Minimum state of charge (%)
        float maxSoc = 95.0f;               // Maximum state of charge (%)
    };

    struct ErsConfig {
        HybridArchitecture architecture = HybridArchitecture::F1_2014;
        ErsMode deploymentMode = ErsMode::Auto;
        bool enabled = false;

        // Sub-component configs
        MgukConfig mguk;
        MguhConfig mguh;
        BatteryConfig battery;

        // Deployment strategy parameters
        float deployThresholdSpeed = 80.0f;   // km/h minimum for deployment
        float attackDuration = 30.0f;         // Attack mode duration (seconds)
        float attackCooldown = 60.0f;         // Attack mode cooldown (seconds)
        float autoDeployRate = 0.5f;          // Auto mode deploy rate (0-1)
    };

    struct ErsState {
        float mgukPowerKw = 0.0f;           // Current MGU-K power (+deploy, -regen)
        float mguhPowerKw = 0.0f;           // Current MGU-H power (+deploy, -harvest)
        float batterySoc = 50.0f;           // State of charge (%)
        float batteryTemp = 30.0f;           // Battery temperature (C)
        float batteryPowerKw = 0.0f;         // Current battery power
        float energyDeployedMj = 0.0f;       // Energy deployed this lap (MJ)
        float energyHarvestedMj = 0.0f;      // Energy harvested this lap (MJ)
        float totalDeployedMj = 0.0f;        // Total energy deployed (MJ)
        float totalHarvestedMj = 0.0f;       // Total energy harvested (MJ)
        float deployTorqueNm = 0.0f;         // Current ERS assist torque (Nm)
        float regenTorqueNm = 0.0f;          // Current regen braking torque (Nm)
        float lapsRemaining = 0.0f;          // Laps of energy remaining
        bool harvesting = false;
        bool deploying = false;
        bool overTemperature = false;
        bool energyDepleted = false;
        bool attackModeActive = false;
        float attackModeTimer = 0.0f;
        float attackModeCooldown = 0.0f;
        bool coasting = false;
    };

    // Core simulation
    void update(float dt, float engineRpm, float throttle, float brake,
                float speed, float gearRatio, float engineTorque,
                float turboBoost, float turboLag);

    void resetLap();
    void reset();

    // Configuration
    void setConfig(const ErsConfig& config);
    ErsConfig getConfig() const { return m_config; }
    ErsState getState() const { return m_state; }

    // Deployment control
    void setDeploymentMode(ErsMode mode);
    void activateAttackMode();
    void deactivateAttackMode();
    void setAutoDeployRate(float rate);

    // MGU-K operations
    float calculateRegenTorque(float brakePedal, float speed) const;
    float calculateDeployTorque(float engineRpm, float throttle, float speed) const;
    float getAvailableMgukPower() const;

    // MGU-H operations
    float calculateExhaustEnergy(float engineRpm, float throttle, float turboBoost) const;
    float calculateMguhHarvestPower(float exhaustEnergy) const;
    float calculateTurboSpoolPower() const;
    bool canTransferToMguk() const;

    // Battery operations
    float getSoc() const { return m_state.batterySoc; }
    float getBatteryTemp() const { return m_state.batteryTemp; }
    float getBatteryPower() const { return m_state.batteryPowerKw; }
    float getDeployableEnergy() const;
    float getRemainingLapEnergy() const;
    bool isFullyCharged() const { return m_state.batterySoc >= m_config.battery.maxSoc; }
    bool isDepleted() const { return m_state.energyDepleted; }
    bool isOverheating() const { return m_state.overTemperature; }

    // Torque contribution
    float getDeployTorque() const { return m_state.deployTorqueNm; }
    float getRegenTorque() const { return m_state.regenTorqueNm; }
    float getNetTorque() const { return m_state.deployTorqueNm - m_state.regenTorqueNm; }
    float getNetPowerKw() const { return m_state.mgukPowerKw + m_state.mguhPowerKw; }

    // Energy management
    void chargeBattery(float powerKw, float dt);
    void dischargeBattery(float powerKw, float dt);
    void updateBatteryTemperature(float dt, float powerKw);

    // State queries
    bool isEnabled() const { return m_config.enabled; }
    bool isDeploying() const { return m_state.deploying; }
    bool isHarvesting() const { return m_state.harvesting; }
    bool isAttackModeAvailable() const;
    bool isAttackModeActive() const { return m_state.attackModeActive; }

    // Architecture presets
    static ErsConfig getF1_2014();
    static ErsConfig getF1_2026();
    static ErsConfig getLMP1();
    static ErsConfig getLMDh();
    static ErsConfig getRoadMild();
    static ErsConfig getRoadFull();
    static ErsConfig getRoadPlugIn();
    static ErsConfig getElectric();
    static ErsConfig getDisabled();

    // Validation
    static bool validateConfig(const ErsConfig& config, QString* error = nullptr);

    // Utility
    static QString getArchitectureName(HybridArchitecture arch);
    static QString getModeName(ErsMode mode);
    static float kwToHp(float kw) { return kw * 1.341f; }
    static float hpToKw(float hp) { return hp / 1.341f; }
    static float mjToKwh(float mj) { return mj / 3.6f; }

private:
    void updateDeployment(float dt, float engineRpm, float throttle,
                          float brake, float speed, float gearRatio);
    void updateMguk(float dt, float brake, float speed);
    void updateMguh(float dt, float engineRpm, float throttle, float turboBoost);
    void updateEnergyBalance(float dt);
    float calculateDeployRate(ErsMode mode, float speed, float throttle) const;

    ErsConfig m_config;
    ErsState m_state;
    float m_deployRate = 0.0f;
    float m_prevEngineRpm = 0.0f;
};
