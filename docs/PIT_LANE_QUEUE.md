# Pit lane queue

## Roles

| Role | Meaning |
|------|---------|
| **Leaving** | from the garage toward the track |
| **Entering** | from the track toward the garage |
| **Holding** | stopped for service |

## States

| Status | Meaning |
|--------|---------|
| **Waiting** | queued, cannot move |
| **ClearedToMove** | gap free, may go |
| **Moving** | moving along the pit |
| **Done** | left the queue |

## Rules

1. **Garage exit**: `requestLeave` → only the first with gap ≥ `releaseGapM` (default 12 m) is **Cleared**.
2. **Spacing**: minimum `minSpacingM` (8 m) along the pit axis.
3. **Priority**: player boost + lower garage index preferred on leave.
4. **Entry**: FIFO along the axis; blocked if someone is within min spacing.
5. **Garage exit**: `shouldBlockGarageExit` → `pathBlocked` until Cleared.

## Flow with GarageExit

```text
requestLeave(carId)
    ▼
GarageExit: Preparing / BoxClear
    ▼  pathBlocked = queue.shouldBlockGarageExit()
ClearedToMove
    ▼
RollingOut / PitLane → markMoving, limiter from suggestedMaxSpeedMs
    ▼
OnTrack → markDone
```

## Code

```cpp
PitLaneQueue queue;
queue.setAxis({ pitOriginX, pitOriginZ, pitHeading });
queue.setConfig(cfg);

// player wants to leave
queue.requestLeave(playerId, raceNum, garageIdx, true, x, z);

queue.setSimTime(t);
queue.updateCar(id, x, z, speed);
queue.update(dt);

in.pathBlocked = queue.shouldBlockGarageExit(id);
auto out = exitCtrl.update(dt, in);
integratePitQueueWithGarageExit(queue, exitCtrl, id, in, throttle);
```

File: `src/simulator/PitLaneQueue.h`
