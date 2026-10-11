# Garage exit — detailed logic

> **Stato integrazione (2026-10-11): CABLATO** nel loop compilato.
> `SimulationLoop_Pit.cpp::updateGarageExit()` (chiamata da `tick()` prima
> della fisica) guida `GarageExitController` sullo stesso box del servizio in
> box: partenza in garage = tenuta finché il giocatore non chiede di uscire
> (`P` o riga **LEAVE GARAGE** del menu GARAGE), rientro = entrare lentamente
> nel proprio box. La coda box multi-auto (`PitLaneQueue.h`) **non** è ancora
> cablata: `pathBlocked` resta false, quindi BoxClear non aspetta nessuno.
> Il limiter di corsia usa `pitLaneOpen = true` sempre: la finestra box in
> gara va presa da `RaceSessionManager`.

## State machine

```text
InGarage
   │ requestLeave
   ▼
Preparing ──► (engine off + autoStart) ──► EngineStart
   │
   ▼
BoxClear ◄── path busy (wait / timeout → Blocked)
   │ path free
   ▼
RollingOut  (pit limiter ON, hold released)
   │ distance ≥ rollOutDistanceM
   ▼
PitLane     (limiter ON)
   │ along pit ≥ pitLaneLengthM
   ▼
TrackEntry  (limiter fade)
   │
   ▼
OnTrack

Returning ──► (near box + slow) ──► InGarage
Blocked   ──► recovery when condition clears + still requesting
```

## Exit conditions

| Check | Effect |
|-------|---------|
| Session Practice/Qualify | exit always allowed |
| Session Race | only if `allowExitInRace` and pit open |
| `pitLaneOpen` | otherwise Blocked / PitClosed |
| Engine running | required if `requireEngineOn` |
| Exit cone free | otherwise wait in BoxClear |
| `requestCancel` | returns to InGarage |

## Output to simulation

| Flag | Use |
|------|-----|
| `holdControls` | ignore throttle in garage |
| `snapToBox` | keeps the pose on the box |
| `pitLimiterActive` | applies `applyPitLimiter` |
| `engineShouldRun` | starts the engine |
| `allowDrive` | integrates physics |
| `statusText` | HUD |

## Integration example

```cpp
GarageExitController exit;
exit.bindBox(slot.garageIndex, boxPose, pitHeading);

GarageExitInput in;
in.requestLeave = playerPressedLeaveGarage;
in.engineRunning = …;
in.speedMs = …;
in.posX/Z = …;
in.pathBlocked = isExitPathBlocked(box, heading, 15.f, 2.5f, ox, oz, n, x, z);
in.session = SessionType::Practice;
in.pitLaneOpen = session.state().pitLaneOpen;

auto out = exit.update(dt, in);
if (out.holdControls) { throttle = brake = 0; }
if (out.snapToBox) { vehicle.setPose(out.boxPose); }
if (out.pitLimiterActive)
    throttle = GarageExitController::applyPitLimiter(speed, throttle, out.pitLimiterMaxMs);
```

## Recommended config

```cpp
GarageExitConfig cfg;
cfg.pitLimiterKmh = 60.f;
cfg.rollOutDistanceM = 12.f;
cfg.pitLaneLengthM = 80.f;
cfg.prepareTimeoutSec = 10.f;
cfg.holdCarWhileInGarage = true;
cfg.autoStartEngineOnRequest = true;
```

File: `src/simulator/GarageExit.h`
