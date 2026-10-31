#!/usr/bin/env bash
# Apply Phase 3.3 complete SimulationLoop multi-layout wire.
# Requires TrackLayout.{h,cpp} + mugello layouts.json already on tree (master has them).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PATCH="$ROOT/docs/patches/phase33_complete_wire.patch"
if [[ ! -f "$PATCH" ]]; then
  echo "missing $PATCH" >&2
  exit 1
fi
if git apply --check "$PATCH"; then
  git apply "$PATCH"
  echo "phase33 complete wire applied"
else
  echo "patch does not apply cleanly; try: git apply --3way $PATCH" >&2
  exit 1
fi
