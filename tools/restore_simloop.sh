#!/bin/bash
# Restore SimulationLoop.cpp from embedded cmake/simloop_z*.b64 (zlib+b64)
set -e
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"
B64=""
for i in 0 1 2 3 4; do
  f="cmake/simloop_z${i}.b64"
  if [ ! -f "$f" ]; then
    echo "Missing $f" >&2
    exit 1
  fi
  B64="${B64}$(tr -d ' \n\r\t' < "$f")"
done
python3 -c "
import zlib, base64, sys
b = '''${B64}'''
open('src/simulator/SimulationLoop.cpp','wb').write(zlib.decompress(base64.b64decode(b)))
print('Restored src/simulator/SimulationLoop.cpp from embedded archive')
"
grep -q 'SimulationLoop_FeatureTick.inl' src/simulator/SimulationLoop.cpp && echo "FeatureTick inject present OK"
