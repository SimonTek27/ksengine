#pragma once
/**
 * Host helpers: pack vehicle damage / garage setup into ksnet messages and
 * apply on clients. Header-only for easy include from NetworkManager.
 */
#include "NetworkConfig.h"
#include "SetupGarage.h"
#include "engine/physics/DamageWireBridge.h"
#include "engine/physics/VehicleSimulator.h"

#if HAS_KSNET

namespace ks {
namespace sim {

inline void fillCarDamageMessage(net::CarDamageMessage& msg, uint32_t carId,
                                 const ks::physics::DamageSystem& dmg) {
    const auto snap = ks::physics::captureDamageWire(dmg);
    msg.carId = carId;
    msg.overall = snap.overall;
    msg.engineHealth = snap.engineHealth;
    msg.powerMul = snap.powerMul;
    msg.handlingMul = snap.handlingMul;
    msg.brakingMul = snap.brakingMul;
    msg.downforceMul = snap.downforceMul;
    msg.dragMul = snap.dragMul;
    msg.frontWing = snap.frontWing;
    msg.rearWing = snap.rearWing;
    msg.flags = snap.flags;
}

inline void applyCarDamageMessage(ks::physics::DamageSystem& dmg,
                                  const net::CarDamageMessage& msg) {
    ks::physics::DamageWireSnapshot snap;
    snap.overall = msg.overall;
    snap.engineHealth = msg.engineHealth;
    snap.powerMul = msg.powerMul;
    snap.handlingMul = msg.handlingMul;
    snap.brakingMul = msg.brakingMul;
    snap.downforceMul = msg.downforceMul;
    snap.dragMul = msg.dragMul;
    snap.frontWing = msg.frontWing;
    snap.rearWing = msg.rearWing;
    snap.flags = msg.flags;
    ks::physics::applyDamageWire(dmg, snap);
}

inline void fillCarSetupMessage(net::CarSetupMessage& msg, uint32_t carId,
                                const SetupData& s) {
    msg.carId = carId;
    msg.tirePressureFL = s.tirePressureFL;
    msg.tirePressureFR = s.tirePressureFR;
    msg.tirePressureRL = s.tirePressureRL;
    msg.tirePressureRR = s.tirePressureRR;
    msg.brakeBias = s.brakeBias;
    msg.rideHeightFront = s.rideHeightFront;
    msg.rideHeightRear = s.rideHeightRear;
    msg.springRateFront = s.springRateFront;
    msg.springRateRear = s.springRateRear;
    msg.frontWingAngle = s.frontWingAngle;
    msg.rearWingAngle = s.rearWingAngle;
    msg.diffPreload = s.diffPreload;
    msg.fuel = s.fuel;
    msg.ballast = s.ballast;
    msg.tcLevel = s.tcLevel;
    msg.absLevel = s.absLevel;
}

inline SetupData setupDataFromMessage(const net::CarSetupMessage& msg) {
    SetupData s;
    s.tirePressureFL = msg.tirePressureFL;
    s.tirePressureFR = msg.tirePressureFR;
    s.tirePressureRL = msg.tirePressureRL;
    s.tirePressureRR = msg.tirePressureRR;
    s.brakeBias = msg.brakeBias;
    s.rideHeightFront = msg.rideHeightFront;
    s.rideHeightRear = msg.rideHeightRear;
    s.springRateFront = msg.springRateFront;
    s.springRateRear = msg.springRateRear;
    s.frontWingAngle = msg.frontWingAngle;
    s.rearWingAngle = msg.rearWingAngle;
    s.diffPreload = msg.diffPreload;
    s.fuel = msg.fuel;
    s.ballast = msg.ballast;
    s.tcLevel = msg.tcLevel;
    s.absLevel = msg.absLevel;
    return s;
}

} // namespace sim
} // namespace ks

#endif
