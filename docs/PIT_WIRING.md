# Pit lane stack (complete)

## Tick order

```
m_features.tick
updatePitLane(dt)      // queue + OBB
updateGarageExit(dt)   // pathBlocked includes service busy
updatePitRepair(dt)    // jobs while InGarage + stationary
```

## Session start

```
beginSession
  → setupDefaultGarageLayout(N)   // linear boxes + configurePitAxis
  → bindBox(0, pose) for player
  → forceEnterGarage if Practice/Qualify
```

## Service request

```cpp
loop.requestPitService(true);  // or Stop-Go penalty auto-sets flag
// when InGarage and speed < 0.35 m/s → plan jobs from DamageSystem
```

## Modules

| Module | Role |
|--------|------|
| GarageSpawn | box layout, start-in-garage policy |
| GarageExit | state machine InGarage…OnTrack |
| PitLaneQueue | spacing / clearance |
| PitLaneCollision | OBB soft contact |
| PitLaneRepair | parallel body/susp/aero/tyre/fuel jobs |
