# Roadmap ksim / ksengine — motore auto

**Aggiornata:** 2026-10-14
**Ambito:** simulatore **auto**: dinamica a quattro ruote, sessioni racing,
contenuti pista/auto e multiplayer `ksnet`.
**Fuori ambito:** moto, barche e aerei; richiedono modelli fisici e flussi di
prodotto distinti.

Questa è la roadmap di prodotto del runtime Qt-free (`ksim`). Non è una lista
di tutte le possibilità di `ksEditor`, né un piano per rendere ksengine un
motore general-purpose.

## Come leggere lo stato

| Stato | Significato |
|---|---|
| ✅ Consegnato | Codice cablato nel runtime e coperto da test mirati o da una verifica riproducibile. |
| 🟡 Parziale | Esiste il modello, il parser o l'infrastruttura; manca il collegamento end-to-end oppure il collaudo di prodotto. |
| ⬜ Pianificato | Non è ancora iniziato nel runtime. |

Una funzionalità non diventa “prodotto pronto” solo perché compila: le uscite
di fase richiedono anche il test manuale indicato.

---

## Stato consolidato

| Area | Stato | Evidenza / confine |
|---|---|---|
| Fisica, setup, FFB, superfici, AI e sessione | ✅ | P0/P1 chiusi. |
| Multiplayer LAN e lobby | ✅ | `ksnet`, sync, lobby. |
| Vertical slice menu, binding e telemetria | ✅ | Selezione auto/pista, sessioni, HUD. |
| UX racing (box/team/upgrade) | 🟡 | Codice 2.7–2.10 cablato; gate umani aperti (vedi GATE_PLAN_WINDOWS). |
| Accettazione 2C | 🟡 | **2.12** e **2.14** chiusi 2026-10-14; **2.11** gara lunga ancora aperta. |

Riferimenti: [CHECKLIST.md](CHECKLIST.md), [WIREUP_GAPS.md](WIREUP_GAPS.md),
[GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md),
[GATE_2_12_STABILITY.md](GATE_2_12_STABILITY.md),
[GATE_2_14_FRAME_PROFILE.md](GATE_2_14_FRAME_PROFILE.md).

---

## Fase 2C — Accettazione e qualità

| # | Deliverable | Stato | Criterio di accettazione |
|---|---|---|---|
| 2.11 | Gara asciutto/bagnato ripetibile | ⬜ | Gara 10–15 min, 8+ auto; grip, spray, audio e risultati coerenti. |
| 2.12 | Stabilità lunga | ✅ | Soak 1 h windowed 2026-10-14: 0 NaN recovery; evidence `docs/GATE_2_12_STABILITY.md` / `docs/gate_2_12/`. |
| 2.13 | Regressione automatica | 🟡 | CTest Qt-free e bridge test presenti; resta collaudo umano Fase 2. |
| 2.14 | Profilo di frame reale | ✅ | Run 45 s Vulkan 2026-10-14 (RTX 3070 Ti); evidence `docs/GATE_2_14_FRAME_PROFILE.md` / `docs/gate_2_14/`. |

### 2B — Gara, box e identità (sintesi)

| # | Deliverable | Stato |
|---|---|---|
| 2.6 | Piano pit UI | ✅ |
| 2.7 | Servizio box end-to-end | 🟡 codice ✅ / gate umano ⬜ |
| 2.8 | Team | ✅ codice; collaudo AC ⬜ |
| 2.9 | Upgrade | ✅ codice; collaudo AC ⬜ |
| 2.10 | Menu coerente | 🟡 codice ✅ / verifica path ⬜ |

---

## Decisioni esplicite

| Non fare ora | Motivo |
|---|---|
| Riscrivere fisica, sessione o `ksnet` | Fondazioni già funzionali. |
| Aggiungere moto/barca/aereo | Fuori ambito auto. |
| Ranked globale prima del loop offline/LAN | Serve infrastruttura e anti-cheat. |
| Dichiarare chiusi i gate umani senza report macchina | Policy accettazione. |

---

## Documenti operativi

- [CHECKLIST.md](CHECKLIST.md)
- [GATE_PLAN_WINDOWS.md](GATE_PLAN_WINDOWS.md)
- [WIREUP_GAPS.md](WIREUP_GAPS.md)
- [PARITY_STATUS.md](PARITY_STATUS.md)
- [CONTROL_API.md](CONTROL_API.md)

*Aggiornare questa roadmap alla chiusura di ogni gate e includere nel commit il
test o la misura che giustifica il cambio di stato.*
