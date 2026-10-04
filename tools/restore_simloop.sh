#!/bin/bash
# Restore src/simulator/SimulationLoop.cpp from embedded sources.
# Prefer cmake/simloop_src_*.txt; fallback cmake/simloop_z*.b64.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src/simulator/SimulationLoop.cpp"
cd "$ROOT"

if [[ -f cmake/simloop_src_0.txt ]]; then
  cat cmake/simloop_src_0.txt cmake/simloop_src_1.txt cmake/simloop_src_2.txt \
      cmake/simloop_src_3.txt cmake/simloop_src_4.txt > "$OUT"
  echo "OK: restored $OUT from simloop_src_*.txt ($(wc -c < "$OUT") bytes)"
elif [[ -f cmake/simloop_z0.b64 ]]; then
  python3 - <<'PY'
import pathlib, zlib, base64
root = pathlib.Path(".")
raw = b""
for i in range(5):
    raw += root.joinpath(f"cmake/simloop_z{i}.b64").read_bytes()
out = root / "src/simulator/SimulationLoop.cpp"
data = zlib.decompress(base64.b64decode(raw))
out.write_bytes(data)
print(f"OK: restored {out} from simloop_z*.b64 ({len(data)} bytes)")
PY
else
  echo "error: no embedded SimulationLoop sources found" >&2
  exit 1
fi
grep -q 'SimulationLoop_FeatureTick.inl' "$OUT" && echo "FeatureTick inject present OK"
