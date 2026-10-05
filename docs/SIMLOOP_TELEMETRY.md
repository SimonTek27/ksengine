# SimulationLoop telemetry

## Channels

| Channel | Default | Enable |
|---------|---------|--------|
| Shared memory | ON | `setSharedMemoryEnabled(true)` |
| UDP | OFF | `setUdpTelemetry(true, host, port)` default `127.0.0.1:9996` |
| TCP | OFF | `setTcpTelemetry(true, port)` default `:9997` |

## Tick

After pit/repair, before render:

```
publishSharedMemory();
publishUdpTelemetry();
publishTcpTelemetry();
```

## Track garage

`loadTrackFolder` calls `loadGarageFromTrack` then `MultiCarManager::loadAiSpline`.

## Build

Ensure `SimulationLoop_FeatureMethods.cpp` is in the SimulatorApp / ksimulator source list.
