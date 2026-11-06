# Phase 3 — Pro depth
**Started:** 2026-10-20
**Status re-checked:** 2026-10-31 (master `a617f7c1`)
**Product:** Kinetika (ksim / ksengine)
**Prerequisite:** Phase 2 code closed; human gates G1–G3 still open on Windows (do not block Phase 3 tooling).

---

## Deliverables

| # | Item | Priority | Status |
|---|------|----------|--------|
| **3.1** | Tyre validation vs real telemetry | High | Tooling ready (`kstirenorm`, Milliken G1–G18 ✅); needs **data campaign** (real logs) |
| **3.2** | ksnet secure-connect, reconnect, dedicated server | Medium | **✅ Delivered** — secure transport, client auto-reconnect, headless host key/token flags; evidence `test_multiplayer` + `ks_server_smoke` |
| **3.3** | Multi-layout without full reload | Medium | **✅ Delivered** — catalog, `switchLayout`/`KS_LAYOUT`, LAYOUT picker, layout announced in the welcome, and a guest told (chat + HUD) when the announced layout is missing from its copy; `track_layout_test` + `menu_layout_picker_test` + `test_multiplayer`. Remaining is content (a second real layout) |
| **3.4** | Driver swap (endurance) | Medium | Not started (depends on pit + long session) |
| **3.5** | VR OpenXR | Low | Not started |
| **3.6** | Triple monitor | Low | Not started |
| **3.7** | Ranked / skill rating | Low | Not started |
| **3.8** | Laser-scan track pipeline | Low | Not started |

---

## 3.1 — Tyre validation (data campaign)

**Already in tree**
- `KsTireModel` + load sensitivity, friction-circle telemetry
- `TireNormalizeTool` + CLI `kstirenorm` (Milliken Ch.14)
- `MomentMethodTool` + `ksmmm` / `--gg` / `--pair`
- Milliken gap table G1–G18 **CLOSED** (`docs/MILLIKEN_KSENGINE.md`)

**Still needed (human / data)**
1. Capture real telemetry (bank or car) at known Fz / α / κ / T / p.
2. Run `kstirenorm --ini <tyres.ini> --curves out.csv --report out.txt`.
3. Compare peak μ, α_peak, loadSensRatio vs measured; adjust INI or normalize with `--normalize --target-mu`.
4. Store campaign notes under `docs/gate_3_1/` when available.

No further code is required to *start* the campaign.

---

## 3.2 — ksnet SecureConnect

**✅ Delivered 2026-10-31** — body, NetworkLowLevel/Manager wire, reconnect, dedicated host flags, `test_multiplayer` + `ks_server_smoke`.

Optional remaining: lobby advert for secure mode.

---

## 3.3 — Multi-layout without full reload

**Goal:** change race layout (AI line, length, pit/start metadata, optional local kn5)
without tearing down cars / renderer / session stack.

### Model

| Piece | Behaviour |
|-------|-----------|
| Catalog | `TrackLayoutCatalog` in `src/simulator/TrackLayout.{h,cpp}` |
| Manifest | Optional `layouts.json` at track root (authoritative) |
| Scan | Else: subdirs with `data/`, `ai/`, or `*.kn5` |
| Default | Always expose `id=default` → track root when usable |
| Env | `KS_LAYOUT=<id>` applied in `loadTrackFolder` |
| Switch | `SimulationLoop::switchLayout(id)` rebinds length/name/kn5, resets lap distance |

### Done on master (2026-10-31)

- [x] `TrackLayoutCatalog::discover` / `selectById` / manifest + scan
- [x] Mugello `content/tracks/mugello/layouts.json` (default GP)
- [x] Unit test `track_layout_test`
- [x] `cmake/CMakeLists_ksimulator_QtFree.cmake` compiles `TrackLayout.cpp`
- [x] `SimulationLoop` wire (`switchLayout`, `KS_LAYOUT`) — merged 2026-11-06; the patches under `docs/patches/` are superseded
- [x] `resolveLayoutBinding` (TrackLayout): a layout's own `data.json`, `.kn5` and `ai/` folder, covered by `track_layout_test`
- [x] LAYOUT row on the SINGLE PLAYER page (`layoutChoices`/`activeLayoutId`/`onLayoutChosen`), covered by `menu_layout_picker_test`

### Remaining

1. ~~Merge SimulationLoop header/cpp wire (layout fields + `switchLayout`)~~ — done 2026-11-06.
2. ~~Reload AI racing line on switch when layout ships its own `ai/*.ai`~~ — done: the binding names the folder, `switchLayout` reloads the line.
3. ~~Menu UI to pick layout before session start~~ — done 2026-11-06: LAYOUT row, hidden below two layouts.
4. ~~Multiplayer: broadcast active layout id on join~~ — done 2026-11-06: `ServerWelcomeMessage.layoutId` (protocol 3), the client rebinds on welcome.
5. Real second layout content pack (e.g. Mugello national) when available.

### Smoke

```bat
set KS_TRACK=content/tracks/mugello
set KS_LAYOUT=default
ksim.exe
ctest -R track_layout_test --output-on-failure
```

---

## Suggested order

1. Finish **3.3** remaining (SimulationLoop wire, AI line on switch, menu picker).
2. Start **3.1 data campaign** when real tyre logs exist.
3. **3.4** driver swap after human box gate G3.
4. Defer 3.5–3.8 until Windows soak is routine.

---

## Explicit non-goals (unchanged)

- Rewrite physics / session / ksnet reliable channel
- Bike / boat / aircraft
- Global ranked before offline/LAN loop is solid

*Update this file when a Phase 3 item closes; link evidence path in the same commit.*
