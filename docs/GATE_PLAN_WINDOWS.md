# Gate plan — Windows (accettazione umana)

**Aggiornata:** 2026-10-20  
**HEAD di riferimento:** master (post E9 profiler fix + AcRemote stub + gate 2.11/2.12/2.14)  
**Ambito:** runtime Qt-free (`ksim` / SimulatorApp). Non chiude fasi solo perché compila.

---

## Stato codice vs gate

| Area | Codice | Gate umano |
|------|--------|------------|
| 2.7 Servizio box | ✅ cablato | ⬜ |
| 2.8 Team | ✅ | ⬜ collaudo AC reale |
| 2.9 Upgrade | ✅ | ⬜ collaudo AC reale |
| 2.10 Menu coerente | ✅ | ⬜ path menu |
| Track JSON Mugello | ✅ | ⬜ `KS_TRACK=…/mugello` |
| E1 C API | ✅ | `ctest -R c_api_test` |
| E9 Profiler | ✅ in `tick` | `KS_PROFILE=1` |
| **2.11** Gara 10–15 min | — | ✅ 2026-10-14 (`docs/GATE_2_11_WET_DRY_RACE.md`) |
| **2.12** Soak 1 h | — | ✅ 2026-10-14 (`docs/GATE_2_12_STABILITY.md`) |
| **2.14** Profilo frame | — | ✅ 2026-10-14 (`docs/GATE_2_14_FRAME_PROFILE.md`) |

---

## Prerequisiti build

```bat
git pull
cmake -B build -DKSENGINE_QT_FREE=ON -DKSIMULATOR_QT_FREE=ON
cmake --build build -j
ctest --test-dir build -R "c_api_test|pit_strategy|menu_session" --output-on-failure
```

Se compaiono errori `AcRemoteTelemetryServer` incomplete type:

```bat
git apply docs/patches/AcRemoteTelemetryServer_include.patch
```

(Include già presente su master recente; verificare `#include "adapters/assetto_corsa/AcRemoteTelemetryServer.h"` in `SimulationLoop.cpp`.)

---

## G1 — Fase 1 rilascio

| # | Check | Come | Pass? |
|---|--------|------|-------|
| G1.1 | Build/install pulito | cmake Qt-free senza errori | [ ] |
| G1.2 | Practice ~10 min | audio reale + FFB reale | [ ] |
| G1.3 | LAN 2 client | 5 giri, risultati, no desync grossolano | [ ] |
| G1.4 | Persistenza | riavvio → binding e selezione contenuti ok | [ ] |

## G2 — Contenuto reale (2A / Block C)

| # | Check | Come | Pass? |
|---|--------|------|-------|
| G2.1 | Texture/LOD/materiali | pista+auto AC da cache, windowed | [ ] |
| G2.2 | Nessun fallback bianco inatteso | ispezione visiva | [ ] |
| G2.3 | Block C | `KS_AUTOSTART=1 KS_SESSION=race KS_TEAM=… KS_UPGRADES=… KS_AI_CARS=n` | [ ] |
| G2.4 | Mugello JSON | `KS_TRACK=content/tracks/mugello` → nome/length | [ ] |

## G3 — Box e menu (2.7 / 2.10)

| # | Check | Come | Pass? |
|---|--------|------|-------|
| G3.1 | Piano pit | fuel/lavori → CONFIRM PLAN | [ ] |
| G3.2 | Uscita box | LEAVE GARAGE o `P` → corsia | [ ] |
| G3.3 | Servizio | in box fermo → REQUEST SERVICE → job + fuel HUD | [ ] |
| G3.4 | Gomme | tyres job → usura azzerata | [ ] |
| G3.5 | Serbatoio | fill non supera `fuelTankCapacityL` | [ ] |
| G3.6 | Standings HUD | posizione da `RaceSessionManager`, no overlay pit duplicato | [ ] |

## G4 — Gara lunga (2.11) — chiuso

| # | Check | Criterio | Pass? |
|---|--------|----------|-------|
| G4.1 | Gara 10–15 min | 8+ auto, asciutto | [x] 2026-10-14 |
| G4.2 | Bagnato | grip/spray/audio coerenti | [x] (temp + wetness misurati; audio non verificabile su macchina senza device) |
| G4.3 | Risultati | classifica e tempi plausibili | [x] exit pulito, 0 NaN |

Evidence: `docs/GATE_2_11_WET_DRY_RACE.md` + `docs/gate_2_11/`.

**2.12 e 2.14 sono già chiusi con evidence** — non ripetere salvo regressione.

---

## Variabili utili

| Var | Uso |
|-----|-----|
| `KS_TRACK` | cartella pista (es. `content/tracks/mugello`) |
| `KS_TEAM` / `KS_UPGRADES` / `KS_AI_CARS` | Block C |
| `KS_PIT_QUEUE=0` | disattiva coda box multi-auto |
| `KS_AUTOSTART=1` | salta menu |
| `KS_PROFILE=1` | profiler frame (E9) |
| `KS_HEADLESS=1` | smoke senza finestra |
| `KS_WEATHER` | `dry` / `wet` (gate 2.11) |

---

## Come chiudere un gate

1. Eseguire i check sulla macchina Windows target.  
2. Annotare build (commit SHA), hardware, contenuto usato.  
3. Aggiornare `docs/CHECKLIST.md` e, se misura, cartella `docs/gate_*`.  
4. **Non** segnare `[x]` senza report riproducibile.

---

## Ordine consigliato oggi

1. G1.1 rebuild pulito (verifica AcRemote + profiler)  
2. G3 box (2.7) — massima priorità prodotto  
3. G2 contenuto AC reale  
4. Solo dopo → Fase 3 roadmap  

*Documento operativo; aggiornare alla chiusura di ogni riga G\*.*
