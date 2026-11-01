#pragma once

#include <memory>

namespace ks {

namespace physics {

// HybridSystem lives in this namespace (physics/HybridSystem.h). Declaring it at
// global scope made the member a std::unique_ptr<::HybridSystem>, a type that
// exists nowhere, while the .cpp built the real ks::physics::HybridSystem.
class HybridSystem;

struct DrsState {
    bool enabled = false;
    bool autoActivate = false;
    bool active = false;
    bool zoneAvailable = false;
    double speedThreshold = 80;
    double zoneStart = 0;
    double zoneEnd = 0;
    double dragReduction = 0.25;
    double baseCd = 0.35;
};

struct ErsState {
    bool enabled = false;
    int mode = 0;
    double deployTorque = 0;
    double regenTorque = 0;
    float batterySoc = 100;
    float batteryTemp = 25;
};

struct ErsDrsInput {
    double dt = 0;
    double speed = 0;
    double rpm = 0;
    double throttle = 0;
    double brake = 0;
    int currentGear = 1;
    double engineTorque = 0;
};

class ErsDrsController {
public:
    ErsDrsController();

    void update(const ErsDrsInput& input);
    void reset();

    void setHybridSystem(std::unique_ptr<HybridSystem> sys);

    void setErsEnabled(bool e) { m_ers.enabled = e; }
    bool ersEnabled() const { return m_ers.enabled; }
    void setErsMode(int m) { m_ers.mode = m; }
    int ersMode() const { return m_ers.mode; }
    void activateAttackMode();
    float getErsDeployTorque() const { return m_ers.deployTorque; }
    float getErsRegenTorque() const { return m_ers.regenTorque; }
    float getErsBatterySoc() const { return m_ers.batterySoc; }
    float getErsBatteryTemp() const { return m_ers.batteryTemp; }

    void setDrsEnabled(bool e) { m_drs.enabled = e; }
    bool drsEnabled() const { return m_drs.enabled; }
    void setDrsAutoActivate(bool a) { m_drs.autoActivate = a; }
    bool drsAutoActivate() const { return m_drs.autoActivate; }
    void setDrsSpeedThreshold(double kph) { m_drs.speedThreshold = kph; }
    double drsSpeedThreshold() const { return m_drs.speedThreshold; }
    void setDrsZoneStart(double d) { m_drs.zoneStart = d; }
    double drsZoneStart() const { return m_drs.zoneStart; }
    void setDrsZoneEnd(double d) { m_drs.zoneEnd = d; }
    double drsZoneEnd() const { return m_drs.zoneEnd; }
    bool isDrsActive() const { return m_drs.active; }
    double getDrsDragReduction() const { return m_drs.dragReduction; }
    void setDrsDragReduction(double f);
    double getDrsBaseCd() const { return m_drs.baseCd; }
    void setDrsBaseCd(double cd) { m_drs.baseCd = cd; }
    bool isDrsZoneAvailable() const { return m_drs.zoneAvailable; }
    void setDrsZoneAvailable(bool a) { m_drs.zoneAvailable = a; }

    const ErsState& ersState() const { return m_ers; }
    const DrsState& drsState() const { return m_drs; }

    HybridSystem& hybridSystem();
    const HybridSystem& hybridSystem() const;

private:
    void updateDrs(double speed);
    void updateErs(const ErsDrsInput& input, double speedMs);

    ErsState m_ers;
    DrsState m_drs;
    std::unique_ptr<HybridSystem> m_hybridSystem;
};

} // namespace physics
} // namespace ks
