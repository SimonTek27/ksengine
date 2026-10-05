# Sprint 1 — P0.3 Setup + P0.4 FFB (2026-10-03)

## P0.3 Setup → VehicleSimulator

- `VehicleSimulator::SetupParams` + `applySetup()`
- Effects in `integrate()`:
  - tyre pressure → grip scale on loads
  - spring rates → front/rear load split
  - wing angles → Cl/Cd aero
  - ballast + fuel → mass
  - TC / ABS soft limiters on drive/brake force
  - fuel burn with throttle
- `applySetupToVehicle()` in `ApplySetup.h`
- Called from: `initialize`, `loadCar`, `beginSession`

## P0.4 FFB pipeline

```
VehicleFFBSample → FFBBridge::computeSteeringTorque → FfbOutput → Logitech/Fanatec/Moza
```

- `src/simulator/FfbOutput.h`
- `SimulationLoop::updateForceFeedback()` after each physics step
- Soft mode if no HW (`KS_HAS_FFB_HW` / Win32)

## Files

| Path | Change |
|------|--------|
| `engine/physics/VehicleSimulator.h/.cpp` | SetupParams + integrate |
| `simulator/ApplySetup.h` | applySetupToVehicle |
| `simulator/FfbOutput.h` | new |
| `simulator/SimulationLoop.*` | wire apply + FFB |
| `simulator/SimulationLoop_FeatureMethods.cpp` | methods |

## Next sprint

P0.2 surface grip + P0.1 tyre INI load sensitivity.
