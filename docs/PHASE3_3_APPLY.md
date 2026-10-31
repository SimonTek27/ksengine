# Apply Phase 3.3 SimulationLoop wire

Catalog + test are already on master. Finish the runtime wire locally:

```bat
git pull

rem 1) Header — add include + fields + API (if not present):
rem    #include "TrackLayout.h"
rem    SimTrackData: layoutId, layoutFolder
rem    bool switchLayout(const std::string& layoutId);
rem    const TrackLayoutCatalog& layoutCatalog() const;
rem    TrackLayoutCatalog m_layouts;

rem 2) Body
git apply docs/patches/phase33_switchLayout.patch

rem 3) Register unit test (tests/ksengine/CMakeLists_p2.cmake) if missing:
rem    track_layout_test links TrackLayout.cpp only

cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
cmake --build build -j
ctest --test-dir build -R track_layout_test --output-on-failure
```

Env smoke after wire:

```bat
set KS_TRACK=content/tracks/mugello
set KS_LAYOUT=default
```
