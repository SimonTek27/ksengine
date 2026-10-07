# Sprint 4 — P1.2 Auth · P1.3 Track limits · P1.5 PB store (2026-10-03)

## P1.2 Control API auth

```
AUTH <token>     → OK AUTH | ERR AUTH
(any other cmd)  → ERR UNAUTH if token configured and not authenticated
PING             → always allowed
```

- `ExternalControlApi::setAuthToken(token)` — empty = open LAN
- On connect: `OK ksim control API v1 AUTH_REQUIRED` when token set
- `FeatureHub::setControlAuthToken`

## P1.3 Track limits → penalties

Escalation (`TrackLimitsConfig`):

| Reports | Action |
|---------|--------|
| every | warning via RSM |
| 3,6,9… | +1s time penalty |
| 5 | Drive-Through |
| 8 | Stop-Go |
| 12 | DQ |

- Lateral distance vs centerline + `onTrackHint`
- Grace + cooldown to avoid spam

## P1.5 Personal Best store

- `setDirectory` with path safety (`..` rejected)
- `loadAll` / `submitLap` / `leaderboard(track, N)`
- Files: `user/pb/<track>__<car>.pb`
- Control: `PB track car` · `PB LIST track`

## Control commands added

```
LIMITS ON|OFF|STATUS
PB [track [car]]
PB LIST [track]
AUTH <token>
```

## Files

| Path | Change |
|------|--------|
| `TrackLimitsMonitor.h` | escalation |
| `PersonalBestStore.h` | leaderboard + safe dir |
| `ExternalControlApi.h` | AUTH |
| `FeatureHub.h` | wiring + commands |

## Next

Sprint 5: P1.9 server browser UI, P1.4 weather UI
