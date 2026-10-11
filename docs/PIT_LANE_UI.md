# Pit lane screen design (ksim)

> **Stato integrazione (2026-10-11): nessuna implementazione.** Il file che
> esisteva (`src/simulator/ui/PitLaneOverlay.h`) è stato **rimosso** come
> overlay duplicato: aveva tempi e semantica propri (`fuelLitres * 0.12f`,
> riparazioni istantanee) che sarebbero andati in deriva dal servizio reale.
> Questo documento resta come **design**. Il servizio in box vive oggi in
> `PitStrategyBridge` / `PitLaneRepair` / `GameMenuOverlay` (roadmap 2.6/2.7)
> e lo stato mostrato al giocatore è la riga di stato del race HUD
> (`RaceHudSample::statusText`). Non reintrodurre un secondo overlay.

Independent cinematic racing UI — product branding: **ksim** only.

## Purpose
In-session / in-box flow: request service, choose work, confirm, watch progress,
then release. Complements **Garage** (setup offline) with **live pit actions**.

## Layout

```
┌─ red rail ──────────────────────────────────────────────────────────┐
│  KSIM · PIT LANE                         LAP 12  ·  P3              │
│  status: ON APPROACH | IN BOX | SERVICING | READY                   │
│                                                                     │
│  ┌─ services (left) ─────────┐  ┌─ summary (right) ───────────────┐│
│  │ ☐ TYRE CHANGE             │  │  ESTIMATED TIME                 ││
│  │   · Soft / Medium / Hard  │  │  12.4 s                         ││
│  │ ☐ REFUEL            40 L  │  │  [========----] progress        ││
│  │ ☐ FRONT WING REPAIR       │  │                                 ││
│  │ ☐ REAR WING REPAIR        │  │  NEXT: release when READY       ││
│  │ ☐ DAMAGE CHECK            │  │                                 ││
│  └───────────────────────────┘  └─────────────────────────────────┘│
│                                                                     │
│  Footer: ↑↓ select  SPACE toggle  ←→ adjust  ENTER confirm  ESC    │
└─────────────────────────────────────────────────────────────────────┘
```

## States
| State | Meaning |
|-------|---------|
| **Approach** | Car on pit entry / lane; board available |
| **InBox** | Stopped on marks; crew can start |
| **Servicing** | Work in progress; progress bar |
| **Ready** | Work done; wait for clear / accelerate |
| **Closed** | Overlay hidden |

## Services
| Service | Adjustable | Notes |
|---------|------------|-------|
| Tyre change | Compound Soft/Med/Hard (+Wet later) | Adds fixed + per-wheel time |
| Refuel | Litres target | Time ∝ Δ fuel |
| Front wing | on/off | Flat time if damaged |
| Rear wing | on/off | Flat time |
| Damage check | on/off | Short inspection |

## Timing model (simple, tunable)
```
base_box = 2.0 s
tyres    = selected ? 2.8 + 0.4*wheels : 0
refuel   = litres * 0.12 s
wing_f   = selected ? 3.5 : 0
wing_r   = selected ? 3.0 : 0
inspect  = selected ? 1.5 : 0
total    = base_box + max(tyres, refuel+wings+inspect)  // parallel crew groups
```
(Parallel groups: tyre crew vs fuel/body — adjustable later.)

## Input
| Key | Action |
|-----|--------|
| ↑ ↓ | Select service row |
| Space / Enter | Toggle service on/off |
| ← → | Adjust compound or fuel litres |
| Enter (on CONFIRM row) | Commit and start servicing if InBox |
| Esc | Close board (cancel if not Servicing) |

## Visual language
Same as main menu / garage: dark scrim, red accent, large titles, selection plate,
progress bar on summary pane. No third-party marks.

## Relation to systems
| System | Hook |
|--------|------|
| Session / race | Lap, position, pit limiter |
| Vehicle | Fuel, tyre compound, damage flags |
| Network | Pit request sync (optional) |
| Garage | Setup remains separate; pit is live actions |
