/**
 * TEMPORARY recovery stub — full SimulationLoop.cpp was accidentally replaced.
 *
 * Restore the last good version (before PLACEHOLDER):
 *
 *   git fetch origin
 *   git checkout c78b0a9a0f7a58ac439525b32ba54b239e664f72 -- src/simulator/SimulationLoop.cpp
 *
 * Then wire FeatureHub/CarStateSync in the physics loop, after multiCar->update:
 *
 *   #include "SimulationLoop_FeatureTick.inl"
 *
 * Related (already on master):
 *   - SimulationLoop.h (FeatureHub + CarStateSync API)
 *   - SimulationLoop_NetSync.cpp (method bodies)
 *   - SimulationLoop_FeatureTick.inl (tick body)
 *   - cmake/CMakeLists_ksimulator_QtFree.cmake (lists NetSync.cpp)
 *
 * Local agent backup: artifacts/SimulationLoop.cpp (with FeatureTick include).
 */

#error "Restore SimulationLoop.cpp: git checkout c78b0a9a0f7a58ac439525b32ba54b239e664f72 -- src/simulator/SimulationLoop.cpp"
