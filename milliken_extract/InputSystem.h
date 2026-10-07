#pragma once
#include "../EngineModule.h"
#include <string>

namespace ks::engine::devices {

struct InputState {
    float throttle = 0, brake = 0, steering = 0, clutch = 0, handbrake = 0;
    bool drs = false;
    int gearShift = 0;
};

class InputSystem : public EngineModule {
public:
    static InputSystem& instance() { static InputSystem s; return s; }
    std::string moduleName() const override { return "InputSystem"; }
    std::string moduleId() const override { return "ks.input"; }
    bool initialize() override { m_initialized = true; return true; }
    void shutdown() override { m_initialized = false; }

    void setState(const InputState& s) { m_state = s; }
    InputState state() const { return m_state; }

    void setFFB(float center, float stiffness, float damping) {
        m_ffbCenter = center; m_ffbStiff = stiffness; m_ffbDamp = damping;
    }
    void applyFFB(float /*velocity*/, float /*coeff*/) {}
    void rumble(float /*intensity*/, int /*ms*/) {}

private:
    InputState m_state;
    float m_ffbCenter = 0, m_ffbStiff = 0, m_ffbDamp = 0;
};

} // namespace ks::engine::devices
