# SimulationLoop runtime hooks (must be in .cpp)

These hooks are applied in `SimulationLoop.cpp` on master when the full file is synced.
Local artifacts: `SimulationLoop.cpp` (wired).

## 1. End of `initialize()`

```cpp
startFeatureServices(false);
return true;
```

## 2. Inside physics while-loop (after lap/surface)

```cpp
if (m_sessionPhase == PHASE_GREEN_FLAG) updateLapAndSurface(m_physicsDt);
{
    vec3 pos{};
    bool onTrack = (m_sessionPhase == PHASE_GREEN_FLAG);
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        pos = { (float)st.position.x, (float)st.position.y, (float)st.position.z };
    }
#endif
    m_features.tick((float)m_physicsDt, &m_raceSession, 0, pos, onTrack);
    updateGarageExit((float)m_physicsDt);
    m_timeOfDay = m_features.weatherCtrl.time().hours;
}
```

## 3. On lap complete (`updateLapAndSurface`)

```cpp
float sectors[3] = {0.f, 0.f, 0.f};
m_features.onLapCompleted(m_trackData.name, m_carName, "Player",
                          m_lapTimer.lastTimeMs() * 0.001f, sectors);
```

## Already on master

- `SimulationLoop.h` — FeatureHub + GarageExit API
- `SimulationLoop_FeatureMethods.cpp` — beginSession (garage start), services, updateGarageExit
- `SimulatorApp.cpp` — menu session / replay / host wiring
