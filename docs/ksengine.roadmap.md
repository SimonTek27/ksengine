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
| Multiplayer LAN + lobby | ✅ | ksnet ~20 Hz, damage/setup, app auth, discovery; secure-connect **in progress** (Phase 3.2). |
| Menu vertical slice, bindings, telemetry | ✅ | Car/track select, practice/quick race/time attack, results, HUD, Control API. |
| Rain, aquaplaning, 3D audio | ✅ | Wet physics, opt-in particles, Doppler/distance, surface rolling. |
| Mesh rendering (bake, LOD, materials, IBL) | ✅ | KN5→NMS2, DDS, materials.txt, reference content, AI brief S1–S6. |
| Racing UX (box / team / upgrades / menu coherence) | 🟡 | Code for 2.7–2.10 wired; human gates still open. |
| Acceptance 2C | 🟡 | **2.11** long race, **2.12** soak and **2.14** frame profile closed 2026-10-14; human gates G1–G3 remain. |
| **Phase 3** | 🟡 | Started 2026-10-20 — see [PHASE3_PLAN.md](PHASE3_PLAN.md). |

Evidence: [PARITY_STATUS.md](PARITY_STATUS.md), [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md),
[GATE_2_11_WET_DRY_RACE.md](GATE_2_11_WET_DRY_RACE.md),
[GATE_2_12_STABILITY.md](GATE_2_12_STABILITY.md), [GATE_2_14_FRAME_PROFILE.md](GATE_2_14_FRAME_PROFILE.md).

---

## Phase 0 — Racing foundations

**Status: ✅ closed.**

---

## Phase 1 — Playable vertical slice

**Status: ✅ closed (code + tests).**

---

## Phase 2 — Race identity and presentation

### 2A — Wet, audio 3D, rendering — ✅
### 2B — Box, team, upgrades, menu — code ✅ / human gates ⬜
### 2C — Acceptance — 2.11/2.12/2.14 ✅; human G1–G3 open

---

## Phase 3 — Pro depth

| # | Deliverable | Priority | Status |
|---|-------------|----------|--------|
| 3.1 | Tyre validation vs real telemetry | High | Tooling ready; data campaign open |
| 3.2 | ksnet secure-connect, reconnect, dedicated server | Medium | **🟡 API on master**; body = copy `milliken_extract/ksnet.cpp` |
| 3.3 | Multi-layout without full reload | Medium | ⬜ |
| 3.4 | Driver swap (endurance) | Medium | ⬜ |
| 3.5 | VR OpenXR | Low | ⬜ |
| 3.6 | Triple monitor | Low | ⬜ |
| 3.7 | Ranked / skill rating | Low | ⬜ |
| 3.8 | Laser-scan track pipeline | Low | ⬜ |

Details and commands: [PHASE3_PLAN.md](PHASE3_PLAN.md).

---

## Windows human gate plan (summary)

Priority for operators (see [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md)):

1. **G3** — Box service (2.7) on real session path  
2. **G1 / G2** — Build cleanliness + menu paths (2.10)  

Already closed with evidence: **2.11** long race, **2.12** soak, **2.14** frame profile.

---

## Operational docs

| Doc | Use |
|-----|-----|
| [ksengine_architecture.md](ksengine_architecture.md) | System structure |
| [ksengine_design.md](ksengine_design.md) | Rules and non-goals |
| [PHASE3_PLAN.md](PHASE3_PLAN.md) | Phase 3 execution |
| [CHECKLIST.md](CHECKLIST.md) | Day-to-day checklist |
| [CONTROL_API.md](CONTROL_API.md) | External control verbs |
| [PARITY_STATUS.md](PARITY_STATUS.md) | Fine-grained done list |

---

*Update this roadmap when a gate closes; include the test name or evidence path in the same commit.*
