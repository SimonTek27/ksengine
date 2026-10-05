# Sprint 5 — P1.9 Server browser + P1.4 Weather UI (2026-10-03)

## P1.9 Server browser

- `MenuState::ServerBrowser`
- `BrowserServerEntry` + `setServerList`
- REFRESH / join rows / MANUAL JOIN
- Multiplayer → JOIN opens browser + LAN query

## P1.4 Weather / time UI

- `MenuState::Weather` from Settings
- Time ±1h, Noon, Sunset
- Presets Dry / Damp / Wet
- `MenuFeatureBridge` → WeatherControl + discovery

## Wiring

```cpp
wireMenuToFeatures(menu, features);
```

## Next

Sprint 6: P0.6 AI racing line
