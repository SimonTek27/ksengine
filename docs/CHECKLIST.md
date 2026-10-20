# Checklist operativa — ksengine / ksim

**Fonte:** `docs/ROADMAP.md`, `docs/WIREUP_GAPS.md`, `docs/GATE_PLAN_WINDOWS.md`, codice su `master`  
**Aggiornata:** 2026-10-20  
**HEAD di riferimento:** master (E9 profiler, AcRemote stub, gate 2.11/2.12/2.14)  
**Gate plan:** [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md)  
**Ambito:** runtime Qt-free (SimulatorApp / ksim).

---

## Come usare questa checklist

| Simbolo | Significato |
|---------|-------------|
| `[ ]` | Da fare |
| `[x]` | Fatto / verificato |
| 🟡 | Parziale (codice presente; manca collaudo) |
| ✅ | Consegnato e coperto da test o verifica riproducibile |
| ⬜ | Pianificato |

---

## A. Gate umani (obbligatori) — vedi GATE_PLAN_WINDOWS

### Fase 1 — rilascio

- [ ] Build/install pulito Qt-free
- [ ] Practice ~10 min audio + FFB reali
- [ ] Host + 1 client LAN → cinque giri
- [ ] Riavvio → binding e selezione persistenti

### Fase 2A / Block C

- [ ] Pista/auto AC reali (texture, LOD, materiali)
- [ ] Block C: `KS_AUTOSTART=1` + team/upgrades/AI su contenuto reale
- [ ] Mugello JSON: `KS_TRACK=content/tracks/mugello`

### 2.7 / 2.10 (codice ✅)

- [ ] Gate umano box: uscita → corsia → REQUEST SERVICE → HUD
- [ ] Verifica path menu coerenti

---

## B. Codice — consegnato

| ID | Deliverable | Stato |
|----|-------------|--------|
| [x] | 2.6 Piano pit UI | ✅ |
| [x] | 2.7 wire-up box | ✅ (gate umano aperto) |
| [x] | 2.8 Team | ✅ |
| [x] | 2.9 Upgrade | ✅ |
| [x] | 2.10 wire-up menu | ✅ |
| [x] | Track JSON + Mugello | ✅ |
| [x] | E1 C API | ✅ |
| [x] | E9 Profiler in tick | ✅ |
| [x] | AcRemote stub + publishAcRemoteTelemetry | ✅ |

---

## C. Fase 2C

- [x] **2.11** Gara 10–15 min asciutto/bagnato, 8+ auto ✅ 2026-10-14 — `docs/GATE_2_11_WET_DRY_RACE.md` / `docs/gate_2_11/`
- [x] **2.12** Soak 1 ora ✅ 2026-10-14 — `docs/GATE_2_12_STABILITY.md`
- [x] **2.14** Profilo frame ✅ 2026-10-14 — `docs/GATE_2_14_FRAME_PROFILE.md`
- [ ] **2.13** extra regressione se servono casi 🟡

---

## D. Ordine consigliato

| # | Blocco | Stato |
|---|--------|--------|
| 1 | Gate umani G3 box (2.7) | [ ] |
| 2 | G1 practice/LAN | [ ] |
| 3 | G2 contenuto AC | [ ] |
| 4 | **2.11** gara lunga | [x] |
| 5 | 2.12 + 2.14 | [x] |

---

## E. Non fare ora

- Riscrivere fisica / sessione / ksnet
- Moto / barca / aereo
- Ranked globale prima del loop offline/LAN
- Chiudere gate umani senza report macchina

---

*Aggiornare solo con evidence o report di collaudo.*
