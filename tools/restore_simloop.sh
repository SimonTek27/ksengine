#!/bin/bash
# Restore src/simulator/SimulationLoop.cpp from cmake/simloop_z0..z4.b64
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src/simulator/SimulationLoop.cpp"
cd "$ROOT"
python3 - <<'PY'
import pathlib, zlib, base64
root = pathlib.Path(".")
raw = b""
for i in range(5):
    p = root / f"cmake/simloop_z{i}.b64"
    if not p.exists():
        raise SystemExit(f"missing {p}")
    raw += p.read_bytes()
out = root / "src/simulator/SimulationLoop.cpp"
data = zlib.decompress(base64.b64decode(raw))
out.write_bytes(data)
print(f"OK: restored {out} ({len(data)} bytes)")
if b"SimulationLoop_FeatureTick.inl" not in data:
    raise SystemExit("FeatureTick missing")
print("FeatureTick inject present OK")
PY
