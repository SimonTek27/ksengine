#!/usr/bin/env bash
# Apply full Phase 3.3 SimulationLoop multi-layout wire
set -euo pipefail
cd "$(dirname "$0")/.."
git apply --reject --whitespace=fix docs/patches/phase33_complete_wire.patch 2>/dev/null || \
  git apply --reject --whitespace=fix docs/patches/phase33_switchLayout.patch
echo "Applied Phase 3.3 SimulationLoop wire."
echo "Build: cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON && cmake --build build -j"
echo "Test:  ctest --test-dir build -R track_layout_test --output-on-failure"
