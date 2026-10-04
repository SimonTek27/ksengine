# Matrice gap prioritaria — ksim / ksengine

**Data:** 2026-10-05 (parity P0–P1 complete)

## Sprint status

| Sprint | Scope | Status |
|--------|--------|--------|
| 1–3 | Setup, FFB, surface, tyre, replay, session | **Done** |
| 4 | Limits, PB, AUTH | **Done** |
| 5 | Server browser + weather UI | **Done** |
| 6 | AI line + overtake | **Done** |
| 7 | CarStateSync UDP ≥20 Hz | **Done** |
| 8 | NetSync + pit strategy UI | **Done** |

## Optional (P2+)

| Item | Notes |
|------|-------|
| Full ksnet remote car state | Optional; UDP CarStateSync works without `HAS_KSNET` |
| Advanced multiplayer (yojimbo path) | Optional compile flag |

## Restore SimulationLoop

Stub on clone → auto-expand from `cmake/simloop_z0.b64`…`z4.b64`:

```bash
bash tools/restore_simloop.sh
# or cmake -B build -DKSIMULATOR_QT_FREE=ON
```

Vedi `docs/PARITY_STATUS.md` e `docs/RESTORE_SIMLOOP.md`.
