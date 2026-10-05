# Sprint 6 — P0.6 AI racing line + overtake (2026-10-03)

## AIController

- Load `ai/fast_lane.ai` via AiFileReader
- Lookahead steering + rate limit
- Speed from spline or curvature
- Skill / aggression / speedFactor
- Traffic: detect ahead → lateral offset overtake → match speed → return to line

## MultiCarManager

- Traffic snapshot each tick
- `ai->update(..., traffic, selfId)` with real heading

## Tuning

| Param | Default |
|-------|--------|
| lookahead | 30 m + 0.4*speed |
| speedFactor | 1.0 |
| aggression | 0.5 |
| skill | 0.75 |

## Next

P1.1 multiplayer CarState sync
