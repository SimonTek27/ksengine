# Riparazioni in pit lane

## Flusso

```text
InGarage + stationary + requestService
    → plan(jobs from DamageSystem::calculateRepairData)
    → Running jobs (parallel ≤ maxParallel)
    → completeJob → repairPartial / repairSystem
    → SERVICE COMPLETE → leave garage
```

## Job types

| Job | Fonte durata | Effetto |
|-----|--------------|---------|
| Body | `bodyRepairTime` | `repairPartial(0.85)` |
| Suspension | `suspensionRepairTime` | `repairSystem(Suspension)` |
| Aero | `aeroRepairTime` | `repairSystem(Aero)` |
| Engine | `engineRepairTime` | `repairSystem(Engine)` |
| Transmission | fisso ~25 s | `repairSystem(Transmission)` |
| Brakes | fisso ~12 s | `repairSystem(Brakes)` |
| Tyres | `tyreChangeSec` (4.5 s) | flag `tyresChanged` |
| Fuel | litri / `fuelRateLps` | rifornimento continuo |
| FullService | max componenti + gomme | partial + susp + aero + tyres |
| Stop-go | `stopGoRepairSec` | body minimo (penalty) |

## Codice

```cpp
#include "simulator/PitLaneRepair.h"

PitLaneRepair repair;
PitRepairInput in;
in.inGarageBox = (exit.phase() == GarageExitPhase::InGarage);
in.speedMs = speed;
in.requestService = keyService;
in.wantTyres = true;
in.wantBody = true;
in.targetFuelL = 60.f;

auto out = repair.update(dt, in, damageSystem, exit.phase());
if (out.holdCar) { /* freeze in box */ }
vehicle.setFuel(out.fuelL);
if (out.tyresChanged) resetTyreWear();
// HUD: out.statusText, out.overallProgress
```

## Requisiti

- Fase garage **InGarage** (o Returning nel box)
- Velocità ≤ `minStationarySpeedMs` (0.35 m/s)
- Abort con `requestAbort`

File: `src/simulator/PitLaneRepair.h`
