# SimulationLoop.cpp tick inject

`SimulationLoop.h` already exposes FeatureHub + CarStateSync.
Method bodies live in `src/simulator/SimulationLoop_NetSync.cpp`.

Inside `void SimulationLoop::tick()`, after `m_multiCar->update(...)`:

```cpp
m_simTimeSec += m_physicsDt;
updateNetworkSync(static_cast<float>(m_physicsDt));
{
    vec3 pos{};
    bool onTrack = true;
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        pos = { (float)st.position.x, (float)st.position.y, (float)st.position.z };
    }
    m_features.tick(static_cast<float>(m_physicsDt), &m_raceSession, 0, pos, onTrack);
}
```

Add `SimulationLoop_NetSync.cpp` to the simulator CMake target if not already listed.
