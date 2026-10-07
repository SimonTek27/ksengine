# Sprint 6 — P0.6 AI racing line + overtake (2026-10-03)

## AIController

- Load `ai/fast_lane.ai` (binary AC or text fallback via AiFileReader)
- Lookahead steering with rate limit
- Speed from spline field or curvature-derived
- Skill / aggression / speed factor
- **Traffic / overtake**
  - Detect cars ahead in corridor
  - Lateral offset (left/right) up to ~3.5 m
  - Match speed if too close
  - Return to line when clear

## MultiCarManager

- `update(dt)` builds `AiTrafficCar` snapshot
- Non-player cars call `ai->update(..., traffic, selfId)`
- Uses real `state.heading` (was 0)

## Tuning

| Param | Default | Effect |
|-------|---------|--------|
| lookahead | 30 m + 0.4*speed | turn-in |
| speedFactor | 1.0 | overall pace |
| aggression | 0.5 | brake later / overtake offset |
| skill | 0.75 | smoother steer + look |

## Files

| Path | Role |
|------|------|
| `AIController.h/.cpp` | line + overtake |
| `MultiCarManager.cpp` | traffic feed |
| `docs/SPRINT6.md` | this |

## Next

P1.1 multiplayer CarState sync · P2 backlog
