#!/bin/bash
# Restore SimulationLoop.cpp from git history and apply FeatureTick
set -e
cd "$(git rev-parse --show-toplevel)"
git fetch origin
git checkout c78b0a9a0f7a58ac439525b32ba54b239e664f72 -- src/simulator/SimulationLoop.cpp
if ! grep -q 'SimulationLoop_FeatureTick.inl' src/simulator/SimulationLoop.cpp; then
  python3 -c '
from pathlib import Path
p = Path("src/simulator/SimulationLoop.cpp")
c = p.read_text()
old = """        if (m_multiCar && !m_raceSession.isCountingDown())
            m_multiCar->update(m_physicsDt);
        if (m_sessionPhase == PHASE_GREEN_FLAG) updateLapAndSurface(m_physicsDt);"""
new = """        if (m_multiCar && !m_raceSession.isCountingDown())
            m_multiCar->update(m_physicsDt);
#include \"SimulationLoop_FeatureTick.inl\"
        if (m_sessionPhase == PHASE_GREEN_FLAG) updateLapAndSurface(m_physicsDt);"""
if old not in c:
    raise SystemExit("pattern not found")
p.write_text(c.replace(old, new, 1))
print("FeatureTick inject applied")
'
fi
echo "OK - SimulationLoop.cpp restored. Review and commit."
