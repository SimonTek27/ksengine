# Roadmap ksengine — motore attuale (auto)

**Ambito:** simulatore **auto** (fisica 4 ruote, sessione racing, multiplayer ksnet).  
**Fuori scope:** moto, barche, aerei (richiederebbero domini fisici separati).

**Stato al 2026-10-05:** P0 e P1 funzionali **chiusi** (fisica, sessione, AI base, MP, lobby).  
Il lavoro residuo è **qualità prodotto** (P2) e **profondità pro** (P3).

---

## Dove siamo

| Area | Stato | Note |
|------|--------|------|
| Fisica veicolo (KsTireModel, load, setup, surface) | ✅ | ~1.5 µs/auto @1 kHz; profilata |
| FFB hardware | ✅ | Sample → DeviceManager |
| Sessione / pit / garage / penalità / PB | ✅ | FeatureHub + session flow |
| AI racing line + overtake | ✅ | Base utilizzabile |
| Multiplayer ksnet (20 Hz, damage/setup, auth, secure) | ✅ | Host/join LAN |
| Lobby hosted + CLI + web | ✅ | kslobby / kslobby-cli / browser |
| Rendering KN5/LOD stabile | 🔶 | Parziale — P2.1 |
| Audio 3D | 🔶 | Stub / volumes — P2.2 |
| UI menu racing completa | 🔶 | Overlay base — P2.5 |
| Aquaplaning / rain fisico | ✅ | Curve grip + G11 + evoluzione pioggia/asciutto + rain/spray particelle (`KS_PARTICLES=1`) — P2.8 |

Riferimenti: `PARITY_STATUS.md`, `GAP_MATRIX.md`.

---

## Fase 0 — Baseline (già fatta)

**Obiettivo raggiunto:** practice offline stabile + host LAN.

- Pneumatici INI + load sensitivity + surface grip  
- Setup applicato, FFB, replay play  
- AI spline, session flow, track limits, weather UI, PB  
- CarState ≥20 Hz, damage/setup wire, auth, matchmaking, lobby  

**KPI:** 30 min practice senza NaN; 2 client in LAN.

---

## Fase 1 — Vertical slice giocabile (4–6 settimane)

Rendere un build **installabile e guidabile** end-to-end: una pista, un’auto, audio udibile, menu minimi.

| # | Deliverable | Gap | Effort | Priorità |
|---|-------------|-----|--------|----------|
| 1.1 | **Audio motore + vento base** (non ancora 3D full) | P2.2 (slice) | M | Alta |
| 1.2 | **Una track + una car** caricabili in modo stabile (mesh o placeholder solido) | P2.1 (slice) | L | Alta |
| 1.3 | **Menu minimo:** garage → track select → practice → results | P2.5 (slice) | M | Alta |
| 1.4 | **Joystick/wheel bindings** persistenti in UI | P2.7 | M | Alta |
| 1.5 | **Damage HUD** + canali telemetry body/engine/susp | P1.10 | S | Media |
| 1.6 | **Docs API control TCP** (OpenAPI / README) | P2.10 | S | Media |

**Criterio di uscita Fase 1**

- [ ] Install → avvio → practice 10 min con FFB e audio  
- [ ] Host + 1 client, 5 giri, risultati a schermo  
- [ ] Bind volante salvati al riavvio  

---

## Fase 2 — Qualità di guida e atmosfera (6–10 settimane)

Approfondire feeling e immersione **senza** cambiare architettura.

| # | Deliverable | Gap | Effort | Priorità |
|---|-------------|-----|--------|----------|
| 2.1 | **Aquaplaning + curve grip bagnato** (fisica) | P2.8 | L | Alta |
| 2.2 | **Rain visual / spray** (anche semplice) | P2.8 | M | Media |
| 2.3 | **Audio 3D** (doppler, distanza, surface noise) | P2.2 | L | Alta |
| 2.4 | **Rendering LOD** pista/auto più stabile | P2.1 | XL | Alta |
| 2.5 | **Menu racing completo** (pit, results, server browser integrato) | P2.5 | L | Media |
| 2.6 | **Team / race number / box spawn** | P2.4 | M | Bassa |
| 2.7 | **Upgrades + livrea** (subset) | P2.3 | L | Bassa |

**Criterio di uscita Fase 2**

- [ ] Gara bagnata percepibile (grip + audio/visual)  
- [ ] 8–16 auto in griglia senza crollo frame (CPU fisica già ok; GPU dipende da 2.4)  
- [ ] Flusso menu senza “debug overlay only”  

---

## Fase 3 — Profondità “pro” (backlog, on-demand)

Solo dopo Fase 1–2 stabili.

| # | Deliverable | Gap | Effort | Note |
|---|-------------|-----|--------|------|
| 3.1 | Validazione pneumatici vs telemetria reale | P3.3 | XL | Campagna test + tuning coeff |
| 3.2 | Multi-layout track senza reload completo | P1.6 | M | Se contenuti multi-layout |
| 3.3 | Driver swap endurance | P3.4 | L | Dopo pit strategy solida |
| 3.4 | VR OpenXR | P3.5 | L | Header già opzionali |
| 3.5 | Ranked / skill rating | P3.1 | XL | Server dedicato, non core engine |
| 3.6 | Triple monitor | P2.6 | M | Nice-to-have |
| 3.7 | Laser-scan track pipeline | P3.2 | XL | Tooling offline |

---

## Ordine di lavoro consigliato (prossimi 3 mesi)

```text
Settimane 1–2   Audio base + damage HUD + docs API
Settimane 2–4   Menu minimo + bindings volante
Settimane 3–6   Track/car load stabile (vertical slice grafica)
Settimane 6–8   Aquaplaning / wet grip
Settimane 8–12  Audio 3D + push rendering LOD
```

Parallelizzabile: **audio** e **menu/bindings** non dipendono dal render LOD completo.

---

## Cosa non fare (con il motore attuale)

| Tentazione | Perché evitarla ora |
|------------|---------------------|
| Moto / barche / aerei | Altro dominio fisico; deraglia il focus auto |
| Rewrite fisica da zero | KsTireModel + VehicleSimulator già sotto budget e unificati |
| Ranked online globale | Serve infra + anti-cheat; dopo prodotto offline/LAN solido |
| Editor completo Qt | Solo se serve pipeline contenuti; P2.9 opzionale |

---

## Metriche di successo

| Milestone | KPI |
|-----------|-----|
| Fine Fase 1 | Sessione practice “da giocatore”, non da developer |
| Fine Fase 2 | Gara 10–15 min bagnato/asciutto, 8+ auto, audio coerente |
| Stabilità | 0 NaN su run 1 h; MP 2–4 client senza desync grossolano |
| Performance | Fisica ≤5% frame @1 kHz anche a 16 auto (già verificato in profilo) |

---

## Dipendenze tecniche note

- **Fisica / MP / lobby:** mature — mantenere, non rifare  
- **Render:** `NativeRenderer` parziale → vincolo principale alla “sensazione prodotto”  
- **Audio:** backend da scegliere/stabilizzare (P2.2)  
- **Contenuti:** almeno 1 track + 1 car “ufficiali” per la vertical slice  

---

## Documenti collegati

- `PARITY_STATUS.md` — cosa è wired  
- `GAP_MATRIX.md` — gap P0–P3 dettagliati  
- `PHYSICS_PROFILE_REPORT.txt` / `VEHICLE_SIM_PROFILE_REPORT.txt` — budget CPU  
- `ARCHITECTURE.md` — struttura moduli  

---

*Roadmap allineata al motore **auto-only** esistente. Aggiornare a ogni chiusura di deliverable Fase 1–2.*

## Teoria veicolo

Fisica e bilanciamento allineati a Milliken RCVD: vedi **MILLIKEN_KSENGINE.md**. Priorità modello: LLTD / load transfer (G1–G2), slip ratio fisico (G3), metriche UG/β.
