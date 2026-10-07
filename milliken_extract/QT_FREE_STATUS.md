# KSEngine / SimulatorApp Qt-free

**Updated:** 2026-09-28

## Done this step
1. `SimulationLoop` → `NativeRenderer*` (no `ks::VulkanRenderer` / Graphics)
2. `cmake/CMakeLists_ksimulator_QtFree.cmake` — target **ksimulator** WIN32
3. `src/simulator/NativeRenderer.h` shim → `engine/sim/NativeRenderer.h`

## Build (senza Qt)
Aggiungere in fondo al root `CMakeLists.txt`:
```cmake
option(KSIMULATOR_QT_FREE "Build Qt-free ksimulator" OFF)
if(KSIMULATOR_QT_FREE)
  include(cmake/CMakeLists_ksimulator_QtFree.cmake)
endif()
```
Poi:
```bash
cmake -DKSIMULATOR_QT_FREE=ON -DKSENGINE_QT_FREE=ON ..
cmake --build . --target ksimulator
```

## Runtime chain
SimulatorApp → NativeRenderer → SimulationLoop → InputManager → VehicleSimulator → FFB
