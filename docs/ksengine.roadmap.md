# ksengine — Roadmap

**Product:** Kinetika (ksim / ksengine)  
**Updated:** 2026-10-20  
**Scope:** four-wheel racing runtime (Qt-free). Not a full ksEditor feature list.

**Out of scope:** motorbikes, boats, aircraft.

---

## How to read status

| Status | Meaning |
|--------|---------|
| ✅ Delivered | Wired in the runtime and covered by targeted tests or a reproducible gate. |
| 🟡 Partial | Model, parser, or infra exists; E2E link or human gate still open. |
| ⬜ Planned | Not started in the runtime. |

Compile success alone does not close a phase.

---

## Consolidated status

| Area | Status | Notes |
|------|--------|--------|
| Physics, setup, FFB, surfaces, AI, session | ✅ | P0/P1 closed; Milliken tooling documented and wired. |
| Multiplayer LAN + lobby | ✅ | ksnet ~20 Hz, damage/setup, app auth, discovery; secure-connect optional. |
| Menu vertical slice, bindings, telemetry | ✅ | Car/track select, practice/quick race/time attack, results, HUD, Control API. |
| Rain, aquaplaning, 3D audio | ✅ | Wet physics, opt-in particles, Doppler/distance, surface rolling. |
| Mesh rendering (bake, LOD, materials, IBL) | ✅ | KN5→NMS2, DDS, materials.txt, reference content, AI brief S1–S6. |
| Racing UX (box / team / upgrades / menu coherence) | 🟡 | Code for 2.7–2.10 wired; human gates still open. |
| Acceptance 2C | 🟡 | **2.11** long race, **2.12** soak and **2.14** frame profile closed 2026-10-14; human gates G1–G3 remain. |

Evidence: [PARITY_STATUS.md](PARITY_STATUS.md), [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md),
[GATE_2_11_WET_DRY_RACE.md](GATE_2_11_WET_DRY_RACE.md),
[GATE_2_12_STABILITY.md](GATE_2_12_STABILITY.md), [GATE_2_14_FRAME_PROFILE.md](GATE_2_14_FRAME_PROFILE.md).

---

## Phase 0 — Racing foundations

**Status: ✅ closed.**

- Tyres from INI, load sensitivity, surface grip, setup, FFB.
- Session flow, AI line + overtake, personal bests, track limits.
- Control API base, path guards, NaN recovery.

---

## Phase 1 — Playable vertical slice

**Status: ✅ closed (code + tests).**

| Item | Status |
|------|--------|
| Audio base + vehicle hooks | ✅ |
| Track/car stable load (bake + placeholders) | ✅ |
| Minimum menu (car/track, results) | ✅ |
| Wheel bindings + rebind overlay | ✅ |
| Damage HUD + channels | ✅ |
| Practice / quick race / time attack gated | ✅ |
| CarState sync ≥20 Hz, ksnet host path | ✅ |

Human smoke on a clean Windows build remains good practice before demos.

---

## Phase 2 — Race identity and presentation

### 2A — Wet, audio 3D, rendering

| # | Deliverable | Status |
|---|-------------|--------|
| 2.1 | Wet physics + aquaplaning | ✅ |
| 2.2 | Rain / spray particles (opt-in) | ✅ |
| 2.3 | Audio 3D (Doppler, distance, rolling) | ✅ |
| 2.4–2.5 | LOD, materials, reference content, GPU verify | ✅ |
| S1–S6 | Mips/anisotropy, PBR maps, IBL, clear-coat, surfaces, brake glow | ✅ |

### 2B — Box, team, upgrades, menu

| # | Deliverable | Code | Human gate |
|---|-------------|------|------------|
| 2.6 | Pit strategy UI | ✅ | — |
| 2.7 | Box service end-to-end | ✅ | ⬜ |
| 2.8 | Team + grid | ✅ | ⬜ real content |
| 2.9 | Upgrades + sound pack | ✅ | ⬜ real content |
| 2.10 | Coherent menu / single state source | ✅ | ⬜ menu path check |

### 2C — Acceptance and quality

| # | Deliverable | Status | Acceptance |
|---|-------------|--------|------------|
| 2.11 | Repeatable dry/wet race | ✅ | 10–15 min, 8+ cars; grip, spray, audio, results coherent — evidence 2026-10-14 `docs/GATE_2_11_*` / `docs/gate_2_11/` |
| 2.12 | Long stability | ✅ | 1 h soak 2026-10-14; 0 NaN recovery — `docs/GATE_2_12_*` |
| 2.13 | Automated regression | 🟡 | CTest Qt-free green; human Fase 2 still required |
| 2.14 | Real frame profile | ✅ | 45 s Vulkan 2026-10-14 (RTX 3070 Ti) — `docs/GATE_2_14_*` |

### Suggested execution order (historical)

| Block | Depends on | Observable result |
|-------|------------|-------------------|
| A. Textures + materials | Baker/cache | Cars/tracks not monochrome |
| B. Reference content | A | Windowed scene with LOD + screenshot |
| C. Team + upgrades | B | Player choice changes spawn/look/audio |
| D. Box service + UX | C | Full garage → pit → results loop |
| E. Soak / performance | A–D | Release gates from measurements |

---

## Phase 3 — Pro depth (after Phase 2)

| # | Deliverable | Priority | Note |
|---|-------------|----------|------|
| 3.1 | Tyre validation vs real telemetry | High | Data campaign; Milliken as reference |
| 3.2 | ksnet secure-connect, reconnect, dedicated server | Medium | After LAN soak |
| 3.3 | Multi-layout without full reload | Medium | If content needs it |
| 3.4 | Driver swap (endurance) | Medium | After pit strategy + long session |
| 3.5 | VR OpenXR | Low | Needs stable camera/render loop |
| 3.6 | Triple monitor | Low | Multi-view not in runtime yet |
| 3.7 | Ranked / skill rating | Low | Server infra + anti-cheat |
| 3.8 | Laser-scan track pipeline | Low | Offline tooling |

---

## Release metrics

| Milestone | Gate |
|-----------|------|
| Phase 1 | Playable practice; FFB/audio; persistent binds; LAN 2-client on clean build |
| Phase 2 | 10–15 min dry and wet race, 8+ cars, textured track/car with LOD |
| Stability | Zero NaN in 1 h soak; no crash or severe desync in LAN test |
| Performance | Physics within budget; GPU changes only after measurement |

---

## Explicit decisions (do not do now)

| Item | Reason |
|------|--------|
| Rewrite physics, session, or ksnet core | Foundations already work |
| Add bike/boat/aircraft | Out of car scope |
| Global ranked before offline/LAN loop | Infra + anti-cheat |
| Close human gates without machine evidence | Acceptance policy |

---

## Windows human gate plan (summary)

Priority for operators (see [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md)):

1. **G3** — Box service (2.7) on real session path  
2. **G1 / G2** — Build cleanliness + menu paths (2.10)  

Already closed with evidence: **2.11** long race, **2.12** soak, **2.14** frame profile.

Prerequisites:

```bat
cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
cmake --build build -j
ctest --test-dir build -R "c_api_test|pit_strategy|menu_session" --output-on-failure
```

---

## Operational docs

| Doc | Use |
|-----|-----|
| [ksengine_architecture.md](ksengine_architecture.md) | System structure |
| [ksengine_design.md](ksengine_design.md) | Rules and non-goals |
| [CHECKLIST.md](CHECKLIST.md) | Day-to-day checklist |
| [WIREUP_GAPS.md](WIREUP_GAPS.md) | Remaining wire-up notes |
| [CONTROL_API.md](CONTROL_API.md) | External control verbs |
| [PARITY_STATUS.md](PARITY_STATUS.md) | Fine-grained done list |

---

*Update this roadmap when a gate closes; include the test name or evidence path in the same commit.*
