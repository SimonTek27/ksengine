# SimulationLoop.cpp — wiring snippet

Add at end of `src/simulator/SimulationLoop.cpp`:

```cpp
#include "SimulationLoop_Features.inl"
```

In `initialize()` after vehicle create:

```cpp
startFeatureServices(false);
```

In `tick()` after `updateLapAndSurface`:

```cpp
{
    vec3 pos{};
    bool onTrack = true;
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        pos = { (float)st.position.x, (float)st.position.y, (float)st.position.z };
    }
    m_features.tick((float)m_physicsDt, &m_raceSession, 0, pos, onTrack);
    m_timeOfDay = m_features.weatherCtrl.time().hours;
}
```

In `beginRaceSession()` use:

```cpp
RaceConfig rc = toRaceConfig(m_features.sessionParams,
    m_trackData.splineLength > 0.f ? m_trackData.splineLength : 1000.f);
```

Full wired sources also in project artifacts:
- `SimulationLoop_wired.cpp`
- `SimulatorApp_wired.cpp`
- `FeatureHub.h`
