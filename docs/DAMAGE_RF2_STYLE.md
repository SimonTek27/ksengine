# ksim damage — Kinetika mechanical model

ksim uses a **mechanical multi-system damage model** inspired by impulse-threshold
systems (`damage.ini` physical block), not a cosmetic-only panel damage model.

## Systems affected

| System | Effect on physics |
|--------|-------------------|
| **Body zones** | Structural + cosmetic; CG shift |
| **Aero** | ↓ downforce, ↑ drag (wing / floor / diffuser / rad) |
| **Suspension** | geometry, arm strength, toe/camber deviation, break |
| **Engine** | power mult, overheat, seize |
| **Transmission** | efficiency, stuck |
| **Brakes** | pad/disc/caliper fade |

## Impulse params (`Rf2DamageParams`)

| Param | Analog | Role |
|-------|--------|------|
| `engineSeizeImpulse` | `Engine=` | Seize above impulse |
| `aeroMinImpulse` | `AeroMin=` | Min impulse for aero damage |
| `aeroDiv` | `AeroDiv=` | Impulse → aero fraction |
| `suspBreakImpulse` | PartDetach-ish | Corner suspension break |
| `damageMult` | HDV damage mult | Global scale |

## API

```cpp
ks::physics::DamageSystem dmg;
ks::physics::Rf2DamageParams p; // defaults ≈ Skip Barber-ish scales

float J = ks::physics::impulseFromImpact(/*relSpeed*/25.f, /*mass*/1200.f);
ks::physics::applyRf2Impulse(dmg, p, J, contactPoint, normal);

// Each physics tick:
veh.applyDamageMultipliers(dmg); // power / drag / brakes / handling
```

## Shared memory

`AcPhysicsPage.carDamage[5]` maps overall + corners when published from SimulationLoop.

## Pit repair

`DamageSystem::repairPartial(fraction)` / `repairAll()` / `repairSystem(DamageType)`.

## Not cloned

- No third-party file format lock-in (optional future INI loader)
- No external branding in UI
- Visual mesh morph is optional downstream (Radius* reserved)
