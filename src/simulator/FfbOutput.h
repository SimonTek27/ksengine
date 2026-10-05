#pragma once
/**
 * FfbOutput — Sprint 1 / P0.4
 * VehicleFFBSample → FFBBridge torque → Logitech/Fanatec/Moza (optional HW).
 */
#include "engine/devices/FFBBridge.h"
#include "engine/physics/VehicleSimulator.h"
#include "engine/physics/PacejkaTireModel.h"
#include <memory>
#include <cstdio>
#include <algorithm>
#include <cmath>

#if defined(_WIN32) || defined(HAS_FFB_HW)
#  include "engine/devices/LogitechFFB.h"
#  include "engine/devices/FanatecFFB.h"
#  include "engine/devices/MozaFFB.h"
#  define KS_HAS_FFB_HW 1
#else
#  define KS_HAS_FFB_HW 0
#endif

namespace ks {
namespace sim {

class FfbOutput {
public:
    bool initialize() {
#if KS_HAS_FFB_HW
        m_logi = std::make_unique<device::LogitechFFB>();
        if (m_logi->initialize() && m_logi->isSupported()) {
            m_active = Active::Logitech;
            std::fprintf(stderr, "FfbOutput: Logitech acquired (%s)\n", m_logi->modelName().c_str());
            return true;
        }
        m_logi.reset();
        m_fanatec = std::make_unique<device::FanatecFFB>();
        if (m_fanatec->initialize() && m_fanatec->isSupported()) {
            m_active = Active::Fanatec;
            std::fprintf(stderr, "FfbOutput: Fanatec acquired\n");
            return true;
        }
        m_fanatec.reset();
        m_moza = std::make_unique<device::MozaFFB>();
        if (m_moza->initialize()) {
            m_active = Active::Moza;
            std::fprintf(stderr, "FfbOutput: Moza acquired\n");
            return true;
        }
        m_moza.reset();
#endif
        m_active = Active::None;
        std::fprintf(stderr, "FfbOutput: no hardware — soft FFB only\n");
        return true;
    }

    void shutdown() {
#if KS_HAS_FFB_HW
        if (m_logi) { m_logi->shutdown(); m_logi.reset(); }
        if (m_fanatec) { m_fanatec->shutdown(); m_fanatec.reset(); }
        if (m_moza) { m_moza->shutdown(); m_moza.reset(); }
#endif
        m_active = Active::None;
    }

    void update(const physics::VehicleSimulator& veh, float gain = 1.0f) {
        const auto& s = veh.ffbSample();
        device::FFBInputs in;
        in.slipAngleFL = s.slipAngleFL;
        in.slipAngleFR = s.slipAngleFR;
        in.loadFL = s.loadFL;
        in.loadFR = s.loadFR;
        in.camberFL = s.camberFL;
        in.camberFR = s.camberFR;
        in.speedMs = s.speedMs;
        in.steerAngle = s.steerAngle;

        float torqueNm = s.aligningMomentNm * 0.05f;
        const physics::PacejkaTireModel* tire =
            &const_cast<physics::VehicleSimulator&>(veh).tires();
        float bridgeNm = device::FFBBridge::computeSteeringTorque(in, tire, tire);
        torqueNm = 0.35f * torqueNm + 0.65f * bridgeNm;
        torqueNm *= std::clamp(gain, 0.f, 2.f);
        if (!std::isfinite(torqueNm)) torqueNm = 0.f;
        torqueNm = std::clamp(torqueNm, -25.f, 25.f);
        m_lastTorqueNm = torqueNm;

#if KS_HAS_FFB_HW
        switch (m_active) {
        case Active::Logitech: if (m_logi) m_logi->updateFFB(torqueNm); break;
        case Active::Fanatec:  if (m_fanatec) m_fanatec->updateFFB(torqueNm); break;
        case Active::Moza:     if (m_moza) m_moza->updateFFB(torqueNm); break;
        default: break;
        }
#endif
    }

    float lastTorqueNm() const { return m_lastTorqueNm; }
    bool hasHardware() const { return m_active != Active::None; }

private:
    enum class Active { None, Logitech, Fanatec, Moza };
    Active m_active = Active::None;
    float m_lastTorqueNm = 0.f;
#if KS_HAS_FFB_HW
    std::unique_ptr<device::LogitechFFB> m_logi;
    std::unique_ptr<device::FanatecFFB> m_fanatec;
    std::unique_ptr<device::MozaFFB> m_moza;
#endif
};

} // namespace sim
} // namespace ks
