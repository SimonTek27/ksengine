# Vehicle mechanical damage

## Two layers

1. **Impact** — `DamageSystem::processCollision` / `applyMechanicalImpact` (body zones + engine/susp/aero)
2. **Continuous wear** — `applyMechanicalWear` every frame from telemetry

## Continuous wear

| System | Trigger | Effect |
|---------|---------|---------|
| **Engine** | over-rev, overheating, low oil | health↓, powerLoss, seize |
| **Gearbox** | clutch slipping, power-shift | clutchDamage, gearDamage, stuck |
| **Suspension** | bottom-out, kerbs | geometry, arm, toe/camber, broken |
| **Brakes** | pad wear, temp | fade, discDamage |

## Impact

```cpp
applyMechanicalImpact(dmg, energy, localX, localY, localZ, nx, ny, nz);
// or from the pit:
applyPitContactDamage(dmg, contact.impulse, mass, localX, localZ);
```

## Simulation loop

```cpp
// collisions / walls / pit
if (hit) applyMechanicalImpact(...);

// every frame
MechTelemetry t;
t.rpm = …; t.maxRpm = …; t.coolantTempC = …;
t.brakeTempC[i] = …; t.suspensionTravel[i] = …; t.curbLoad[i] = …;
applyMechanicalWear(damageSystem, dt, t);

auto m = sampleMechanicalEffects(damageSystem);
vehicle.setPowerScale(m.power);
vehicle.setAeroScale(m.downforce, m.drag);
vehicle.setBrakeScale(m.braking);
if (m.engineDead) cutIgnition();
```

## Files

- `src/engine/physics/DamageSystem.h/.cpp` — zones + components + repair
- `src/engine/physics/MechanicalDamage.h` — wear + impact bridge
- `docs/DAMAGE_RF2_STYLE.md` — previous overview
