# SimulationLoop tick parity

## Header (done on master)

`FeatureHub m_features`, `netsync::CarStateSync m_carSync`, `m_simTimeSec`, public API.

## NetSync methods (done)

`SimulationLoop_NetSync.cpp` — compile into simulator target.

## Physics step (one-line include)

After `m_multiCar->update(m_physicsDt);` add:

```cpp
#include "SimulationLoop_FeatureTick.inl"
```

File: `src/simulator/SimulationLoop_FeatureTick.inl` (on master).

Or expand the `.inl` contents inline.
