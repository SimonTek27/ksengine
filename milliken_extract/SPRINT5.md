# Sprint 5 — P1.9 Server browser + P1.4 Weather UI (2026-10-03)

## P1.9 Server browser

- `MenuState::ServerBrowser`
- `BrowserServerEntry` + `setServerList`
- Menu: REFRESH, server rows (join), MANUAL JOIN, BACK
- Multiplayer → JOIN SESSION opens browser + LAN query
- `onJoinServerRequested(entry)` / `onRefreshServerListRequested`

## P1.4 Weather / time UI

- `MenuState::Weather` (from Settings → WEATHER)
- Time ±1h, Noon, Sunset
- Presets Dry / Damp / Wet
- Callbacks → `WeatherControl` via `MenuFeatureBridge`

## Wiring

```cpp
wireMenuToFeatures(menu, features);
// each frame after discovery.tick:
//   rebuild server list if browser open
```

## Files

| Path | Role |
|------|------|
| `GameMenuOverlay.h/.cpp` | UI states |
| `MenuFeatureBridge.h` | FeatureHub glue |
| `docs/SPRINT5.md` | this |

## Next

Sprint 6: P0.6 AI racing line
