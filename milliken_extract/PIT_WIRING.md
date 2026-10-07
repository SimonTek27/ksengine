# Pit lane stack wiring

## Tick order (inside physics step)

1. `updateLapAndSurface`
2. `m_features.tick(...)`
3. **`updatePitLane(dt)`** — queue + OBB collision
4. **`updateGarageExit(dt)`** — uses `pathBlocked` from queue/collision

## Data flow

```
GarageExit phase Preparing/RollingOut
        │
        ▼
PitLaneQueue.requestLeave(player)
        │
        ▼
shouldBlockGarageExit → GarageExitInput.pathBlocked
        │
        ▼
PitLaneCollision.upsert(player body) → step → contacts
        │
        ▼
suggestedMaxSpeedMs / damageImpulse
```

## Files

| File | Role |
|------|------|
| `PitLaneQueue.h` | spacing, clearance, leave/enter queue |
| `PitLaneCollision.h` | OBB car-car + wall, soft impulse |
| `GarageExit.h` | box state machine |
| `SimulationLoop_FeatureMethods.cpp` | `updatePitLane` + `updateGarageExit` |
