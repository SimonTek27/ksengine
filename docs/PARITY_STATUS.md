# Parity status — 2026-10-04

## Complete on master

| Item | Status |
|------|--------|
| README | OK |
| FeatureHub stack | OK |
| CarStateSync + NetSync | OK |
| SimulationLoop.h API | OK |
| FeatureTick.inl | OK |
| AI + MultiCar traffic | OK |
| cmake/restore_simloop.cmake | **auto-restores .cpp on configure** |
| tools/restore_simloop.sh | OK |

## SimulationLoop.cpp

Stub on master is intentional (API size limit). **CMake configure restores the full file + FeatureTick automatically.**

Or: `bash tools/restore_simloop.sh`
