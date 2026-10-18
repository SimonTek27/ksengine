# ksengine — Design

**Product:** Kinetika  
**Updated:** 2026-10-18  

Design rules, ownership, non-goals, and quality policy for the simulation core
and the Qt-free race runtime (`ksim`). Architecture layout lives in
[ksengine_architecture.md](ksengine_architecture.md); release sequencing in
[ksengine.roadmap.md](ksengine.roadmap.md).

---

## 1. Product identity

| Name | Role |
|------|------|
| **Kinetika** | Product brand for the engine, runtime, and modding suite |
| **ksengine** | C++17 simulation library (Qt-free) |
| **ksim / SimulatorApp** | Standalone race client |
| **ksEditor** | Optional Qt6 authoring UI |
| **ksnet** | Official multiplayer transport name |

Brand-specific commercial titles are **not** part of the public product name.
Format compatibility lives in adapters and (where needed) editor plugins only.

---

## 2. Design rules

1. **Qt only in the editor**  
   Never include Qt from `src/engine/` or `src/simulator/`. Use `KsQtFreeGuard`
   and CI checks (`tools/check_no_qt.ps1`).

2. **Engine is generic**  
   No hard-coded dependency on a single commercial sim title in core code.
   Physics, session, and networking APIs stay title-neutral.

3. **Adapters are bridges**  
   Formats in → neutral data structures out. Adapters must not own vehicle
   integrate, UI, or session state machines.

4. **FeatureHub owns parity services**  
   Session mode, LAN discovery (`:20779`), control API (`:20780`), track limits,
   weather, personal bests, setup helpers, and replay helpers go through
   FeatureHub — not scattered globals.

5. **Pit / garage is explicit state**  
   Garage exit, pit queue, collision, and repair are a dedicated stack, not
   buried inside tire or chassis code.

6. **Single source of truth for session state**  
   Overlays and renderers read owners listed in the architecture doc; they do
   not keep parallel copies of standings, flags, or pit status.

7. **Telemetry is multi-channel**  
   Shared memory + UDP + TCP must not block the physics step. Publish after
   integrate.

8. **ksnet is the preferred multiplayer path**  
   CarStateSync UDP remains available without `HAS_KSNET`; production sessions
   should prefer ksnet (auth token, damage/setup/collision messages).

9. **Compile ≠ product-ready**  
   A feature is “delivered” only when wired in the runtime **and** covered by
   targeted tests or a reproducible human gate (see roadmap).

10. **Branding in docs and UI strings**  
    Prefer **Kinetika** in user-visible text and documentation outside plugins.
    Plugin folders under `plugins/simulators/kunos/` may keep legacy names.

---

## 3. Ownership map

| Concern | Owner | Must not |
|---------|--------|----------|
| Fixed physics step | `SimulationLoop` | Run concurrent integrators |
| Vehicle dynamics state | `VehicleSimulator` + physics modules | Duplicate in UI |
| Session phase / mode | `SimulationLoop` + `FeatureHub` | Hard-code in overlays |
| Standings / penalties | `RaceSessionManager` | Local HUD counters |
| Pit plan & repair jobs | Pit strategy / `PitLaneRepair` | Second pit UI with its own timers |
| Discovery / control | FeatureHub services | Ad-hoc sockets in modules |
| Content format parse | adapters + FileFormat | Core physics headers |
| Editor-only workflows | ksEditor modules / plugins | Pull Qt into engine |

---

## 4. Quality and acceptance policy

| Level | Meaning |
|-------|---------|
| **Wired** | Code is linked into the tick path or menu path. |
| **Tested** | CTest or standalone test covers the behaviour. |
| **Gated** | Human acceptance checklist completed with evidence (log, screenshot, soak report). |

Release language:

- ✅ **Consegnato** — wired + tested (or gated where specified).
- 🟡 **Parziale** — model/parser present; E2E or human gate still open.
- ⬜ **Pianificato** — not started in the runtime.

Do **not** mark human gates closed without a machine-readable report under
`docs/GATE_*` or `docs/gate_*`.

---

## 5. Non-goals (explicit)

| Non-goal | Reason |
|----------|--------|
| Motorbike / boat / aircraft dynamics | Separate product physics and content flows |
| Ranked global ladder before offline/LAN loop is solid | Needs infra + anti-cheat |
| Rewriting physics or ksnet foundations | Foundations already functional |
| Declaring gates closed from compile-only evidence | Acceptance policy |
| Coupling core to a commercial title’s private APIs | Portability and licensing |

---

## 6. Security and hardening posture

- Path guards (no `..`, length limits) on content and control paths.
- Control API authentication where exposed (`AUTH` / tokens).
- ksnet application-level auth: host `setAuthToken`, client `setJoinToken`
  (empty = open LAN).
- Transport secure-connect private-key path is **optional** future hardening.
- Sanitize CLI and external command inputs (SimulatorServer, control API).

See also `docs/SECURITY_HARDENING.md`.

---

## 7. Content and presentation principles

- Prefer **baked native meshes** (NMS2) at runtime over parsing heavy formats
  on the hot path.
- Materials and textures reach the frame through an explicit cache
  (`materials.txt`, DDS, descriptor sets).
- Reference content under `content/` must be runnable without a full commercial
  install for CI and smoke tests.
- Visual features that are optional (particles, IBL, brake glow) stay opt-in or
  fail-open so headless and low-end paths remain stable.

---

## 8. Multiplayer design notes

- Official name: **ksnet** (macros `KSNET_*`; deprecated `YOJIMBO_*` aliases only).
- Car-state target rate: ~20 Hz.
- Damage / setup / collision as first-class messages where enabled.
- Matchmaking: LAN discovery + optional HTTP lobby registry.
- Do not block the physics thread on network IO; queue and apply on tick.

---

## 9. Documentation layout (reorganised)

| File | Role |
|------|------|
| **ksengine_architecture.md** | Layers, tree, data flow, FeatureHub, pit stack |
| **ksengine_design.md** | This file — rules, ownership, non-goals |
| **ksengine.roadmap.md** | Phases, gates, priorities |
| PARITY_STATUS.md | Detailed wired checklist |
| GAP_MATRIX.md | Historical gap inventory |
| GATE_PLAN_WINDOWS.md + GATE_2_* | Human acceptance evidence |
| CONTROL_API.md / TCP_TELEMETRY.md / … | Feature-specific references |
| SPRINT*.md, SIMLOOP_*, WIREUP_* | Historical / operational notes |

When in doubt, update the three `ksengine_*` files first, then point feature
docs at them.

---

## 10. Change policy

- Architecture or design rule changes land in the same PR as the code they
  describe.
- Roadmap state changes require a one-line evidence pointer (test name or
  `docs/GATE_*` path).
- Do not expand scope into non-goals without an explicit product decision.

---

*Design companion to architecture and roadmap. Brand: Kinetika.*
