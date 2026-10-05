# Sprint 7 — P1.1 CarState multiplayer sync ≥20 Hz (2026-10-03)

## Design

UDP protocol **without** requiring `HAS_KSNET`:

```
CarStatePacket { magic, seq, count, simTime, cars[] }
CarStatePacked { id, pos, heading, speed, controls, gear, flags }
```

| Role | Port | Behaviour |
|------|------|-----------|
| Host | 40001 | Broadcast all cars 20 Hz |
| Client | → host:40001 | Send local, receive all |

## Integration

```cpp
#include "CarStateSync.h"
#include "CarStateSyncBridge.h"
netsync::CarStateSync sync;
sync.startHost(40001);
// tick: poll → publishFromMultiCar → applyToMultiCar
```

`HAS_KSNET` path remains preferred when available.

## Files

- `src/simulator/CarStateSync.h`
- `src/simulator/CarStateSyncBridge.h`
- `docs/SPRINT7.md`
