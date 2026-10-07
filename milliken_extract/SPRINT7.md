# Sprint 7 — P1.1 CarState multiplayer sync ≥20 Hz (2026-10-03)

## Design

UDP protocol **without** requiring `HAS_KSNET` / ksnet:

```
CarStatePacket { magic, seq, count, simTime, cars[] }
CarStatePacked { id, pos, heading, speed, controls, gear, flags }
```

| Role | Port default | Behaviour |
|------|--------------|-----------|
| Host | 40001 | Broadcast all cars 20 Hz + learn client addrs |
| Client | ephemeral → host:40001 | Send local car, receive all |

## Interpolation

`sample()` lerps position/heading between last two packets (light extrapolate ≤1.25).

## Integration

```cpp
#include "CarStateSync.h"
#include "CarStateSyncBridge.h"

netsync::CarStateSync sync;
sync.startHost(40001); // or startClient("192.168.1.10", 40001)
sync.setSendHz(20.f);

// each tick:
sync.poll();
carStateSyncPublishFromMultiCar(sync, *multiCar, simTime, playerId);
carStateSyncApplyToMultiCar(sync, *multiCar, playerId);
```

When `HAS_KSNET=1`, existing `NetworkManager` / ksnet path remains the preferred transport; this UDP layer is the always-available fallback.

## Files

| Path | Role |
|------|------|
| `CarStateSync.h` | protocol + host/client |
| `CarStateSyncBridge.h` | MultiCar publish/apply |
| `docs/SPRINT7.md` | this |

## Next

P1.8 pit strategy UI · P1.6 multi-layout · P2 render
