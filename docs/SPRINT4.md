# Sprint 4 — P1.2 Auth · P1.3 Track limits · P1.5 PB store (2026-10-03)

## P1.2 Control API auth

```
AUTH <token>     → OK AUTH | ERR AUTH
(any other cmd)  → ERR UNAUTH if token configured
PING             → always allowed
```

- `ExternalControlApi::setAuthToken(token)` — empty = open LAN
- `FeatureHub::setControlAuthToken`

## P1.3 Track limits → penalties

| Reports | Action |
|---------|--------|
| every | warning via RSM |
| 3,6,9… | +1s time |
| 5 | Drive-Through |
| 8 | Stop-Go |
| 12 | DQ |

## P1.5 Personal Best store

- Safe directory, loadAll, leaderboard
- `user/pb/<track>__<car>.pb`
- Control: `PB` / `PB LIST`

## Commands

```
AUTH <token>
LIMITS ON|OFF|STATUS
PB [track [car]]
PB LIST [track]
```

## Next

Sprint 5: P1.9 server browser, P1.4 weather UI
