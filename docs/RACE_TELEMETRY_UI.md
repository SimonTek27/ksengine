# Race telemetry overlay (ksim)

In-session HUD. Independent product styling — branding **ksim** only.

## Modes
| Mode | When | Density |
|------|------|---------|
| **Compact** | Default race | Speed, gear, lap delta, position |
| **Race** | Full race board | + sector, fuel, tyre strip |
| **Engineer** | Practice / debug | Graphs + G-force + inputs |

Toggle: `T` cycles Compact → Race → Engineer → off (or hold for engineer).

## Screen layout (Race mode)

```
┌──────────────────────────────────────────────────────────────┐
│  P3/24          LAP 12/30           +0.342                   │
│  (pos)          (lap progress)      (delta vs best / ahead)  │
│                                                              │
│                         248  km/h                            │
│                         [ 4 ]                                │
│                      RPM ███████░░  8200                     │
│                                                              │
│  ┌─ inputs ──────┐                    ┌─ tyres (C°) ──────┐│
│  │ THR ████████  │                    │  92  94            ││
│  │ BRK ██░░░░░░  │                    │  88  89            ││
│  │ STR  ←─●─→    │                    │  wear ····         ││
│  └───────────────┘                    └────────────────────┘│
│                                                              │
│  FUEL 34.2 L   S1 28.1  S2 31.4  S3 —    BEST 1:22.451      │
└──────────────────────────────────────────────────────────────┘
```

### Compact (minimal)
- Top-left: position  
- Top-center: current lap time  
- Top-right: delta (green/red)  
- Bottom-center: speed + gear only  

### Engineer
- Adds sparkline history (speed / throttle / brake) bottom strip  
- Lateral / long G numeric + simple crosshair  
- Tyre temps + wear % all four  

## Visual rules
- Semi-transparent panels; no heavy frames (readable on track)
- Accent red only for warnings (low fuel, pit, limiter)
- Delta: green negative (ahead of best), red positive
- RPM bar: shifts yellow→red near redline
- Same type scale language as menu (bitmap atlas)

## Data sources
| Field | Source |
|-------|--------|
| speed, rpm, gear, inputs | VehicleSimulator |
| lap / sector / best | LapSectorTimer |
| fuel, tyre temp/wear | physics systems |
| position, field size | `RaceSessionManager::standings()` / `playerStanding()` (roadmap 2.10) |
| status (pit service / garage) | `SimulationLoop::pitServiceStatus()` / `garageStatus()` → `RaceHudSample::statusText` |
| history buffers | rolling ring (N≈200) |

## Relation to other UI
| UI | Role |
|----|------|
| Dashboard | Pedal strip cockpit-style |
| Race telemetry | Timing + race engineer data |
| Garage / Pit | Not shown while racing (or dimmed) |
| Main menu | Modal; telemetry paused/hidden |

## Non-goals
- No third-party title marks in labels
- Not a full Motec replacement (export stays separate)
