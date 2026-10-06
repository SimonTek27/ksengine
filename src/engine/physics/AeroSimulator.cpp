#include "AeroSimulator.h"

namespace ks {
namespace physics {

void AeroSimulator::setConfigPreset(const std::string& preset) {
    if (preset == "gt3") {
        m_mgr.model().setConfig(AeroModel::getGT3Config());
    } else if (preset == "formula") {
        m_mgr.model().setConfig(AeroModel::getFormulaConfig());
    } else if (preset == "sedan") {
        m_mgr.model().setConfig(AeroModel::getSedanConfig());
    } else {
        m_mgr.model().setConfig(AeroModel::getRoadCarConfig());
    }
}

void AeroSimulator::loadFromCarPath(const std::string& carPath) {
    m_mgr.loadFromIni(carPath);
}

AeroSimulator::Output AeroSimulator::step(const Input& in) const {
    Output out;
    out.forces = m_mgr.calculateIntegrated(
        in.speed, in.rideHeightFront, in.rideHeightRear,
        in.position, in.forward, in.leaderPosition, in.airDensity);

    out.forceWorld = out.forces.asForceVector(in.forward);
    out.frontLoadN = out.forces.frontDownforce;
    out.rearLoadN = out.forces.rearDownforce;
    return out;
}

void AeroSimulator::applyToBodyForces(PhysVec3& forceAccum, PhysVec3& torqueAccum,
                                      const Output& out, const PhysVec3& /*cog*/,
                                      float wheelbase) {
    forceAccum += out.forceWorld;

    // Pitch moment from front/rear aero imbalance about CoG (z forward, y up)
    const float halfWb = wheelbase * 0.5f;
    const float fzFront = -out.frontLoadN; // downforce negative Y
    const float fzRear = -out.rearLoadN;
    // Moment about X (pitch): rear downforce positive nose-up if z+ forward... simplified
    torqueAccum.x += (fzRear - fzFront) * halfWb;
}

} // namespace physics
} // namespace ks
