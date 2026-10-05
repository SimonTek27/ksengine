# Sprint 2 — P0.2 Surface grip + P0.1 Tyre INI load sensitivity (2026-10-03)

## P0.1 Pacejka load sensitivity + INI

- `TireCoefficients`: `nominalLoadN`, `loadSensExp`, `loadSensMin/Max`, `optPressurePsi`, `optTempC`
- `calculateLoadSensitivity(Fz)` → `(Fz/Fz0)^exp` clamped
- Applied to peak factors `D_x` / `D_y` in `calculateForces`
- `loadFromIni(path)` parses A1–A13, B1–B8, FZ0, LOAD_EXP, DY0/DX0, OPT_PRESSURE/TEMP
- `VehicleSimulator::loadTyresFromIni` calls `m_tires.loadFromIni`
- Combined slip overload with `frictionMu`, pressure, temp

## P0.2 Track surface grip

- `TrackSurface::setMaterialGrip` / off-track / kerb
- `AcSurfacesLoader` registers SURFACE materials + GRASS/KERB
- `integrate` samples grip → tyre μ; rubber deposit on slip

## Example tyre.ini

```ini
[TYRES]
A2=1100
B2=1200
NOMINAL_LOAD=4000
LOAD_SENSITIVITY=0.9
PRESSURE_OPT=26
TEMP_OPT=80
```

## Next

Sprint 3: P0.5 replay play + P1.7 session results
