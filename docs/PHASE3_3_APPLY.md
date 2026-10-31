# Apply Phase 3.3 SimulationLoop wire

Catalog, test, and cmake registration are on master. Finish the runtime wire:

```bat
git pull
bash tools/apply_phase33_wire.sh
```

Or on Windows (Git Bash / WSL):

```bat
git pull
git apply docs/patches/phase33_complete_wire.patch
```

This patches:

- `src/simulator/SimulationLoop.h` — TrackLayout include, layoutId/layoutFolder, switchLayout API, m_layouts
- `src/simulator/SimulationLoop.cpp` — discover + KS_LAYOUT + switchLayout (incl. AI line reload)

Then:

```bat
cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
cmake --build build -j
ctest --test-dir build -R track_layout_test --output-on-failure
```

Env smoke:

```bat
set KS_TRACK=content/tracks/mugello
set KS_LAYOUT=default
```
