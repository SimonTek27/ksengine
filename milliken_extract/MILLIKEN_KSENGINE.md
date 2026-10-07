# Milliken theory ↔ ksengine

Riferimento: **W.F. Milliken & D.L. Milliken, *Race Car Vehicle Dynamics* (SAE R-146, 1995)**.  
Questo documento fissa come la teoria Milliken informa il motore **auto** attuale e cosa manca ancora.

Non è una riscrittura del libro: è una **mappa operativa** per fisica, setup e roadmap.

---

## 1. Ladder of abstraction (Milliken) vs ksengine

| Livello Milliken | Contenuto | ksengine oggi |
|------------------|-----------|----------------|
| Monociclo / 1 tire | Friction circle singolo | Concetto in KsTireModel (combined slip) |
| **Bicycle 2DF** | No roll, no LLT, C_F/C_R lineari, UG | **Nucleo cinematico** di `VehicleSimulator` (β, r, α_F/α_R) |
| Pair analysis | Asse L/R + FLT + load sensitivity | Parziale: 4 ruote + load sensitivity MF; **LLTD roll non completo** |
| Force–Moment / MMM | Diagrammi C_N–A_Y, constrained | Non implementato come tool offline |
| Transient 2DF/3DF | Yaw damping, lag | Yaw integrato semplice; no full modal |
| Full nonlinear + aero + compliance | Race setup reale | Target lungo termine (Fase 2–3 roadmap) |

Il sim di gara resta **time-based** (come VDS), non constrained MMM. Milliken resta la **teoria di riferimento** per equazioni, metriche e priorità di modello.

---

## 2. Concetti chiave da tenere fissi

### 2.1 Pneumatico (Ch. 2)

| Concetto | Milliken | ksengine |
|----------|----------|----------|
| Slip angle α, slip ratio κ | Forze Fx, Fy vs α, κ, Fz | `KsTireModel` + batch ×4 |
| Load sensitivity | Fy non lineare in Fz | Coeff cache Fz + INI load sens. |
| Camber / inclination | C_γ, thrust | Camber in input batch (semplificato) |
| Aligning moment Mz | Trail pneumatico | `aligningMoment` → FFB sample |
| **Friction circle / ellipse** | Limite √(Fx²+Fy²) ≤ μ·Fz (ideale) | Combined-slip path in KsTireModel |
| Tire data treatment (Ch. 14) | Normalizzazione curve | `loadFromIni` + future validation P3.3 |

### 2.2 Bicycle / stability (Ch. 5–6)

Notazione tipica:

- α_F ≈ β + (a/V)·r − δ  
- α_R ≈ β − (b/V)·r  
- Understeer gradient: UG = (W/ℓ)·(a/C_R − b/C_F) [rad] → deg/g  

In `VehicleSimulator::integrate` compaiono già forme affini (β, yaw rate, steer → slip angles anteriori/posteriori).  
**Da esporre esplicitamente** (telemetria / debug):

- β (vehicle sideslip)  
- r (yaw rate) già in `m_yawRate`  
- UG / static margin stimati da C_F, C_R efficaci e bilanciamento masse  

### 2.3 Load transfer (Ch. 7, 18)

| Tipo | Formula-idea Milliken | ksengine |
|------|------------------------|----------|
| Longitudinale | ΔW ∝ (h/ℓ)·A_x | Parziale via accel / brake + aero split |
| Laterale totale | ΔW_tot ∝ (h/t)·A_y | **Da rafforzare** |
| **LLTD** | Dipende da K_φF/K_φR, h_RC, barre | Setup ha roll proxy; **distribuzione assi incompleta** |
| FLT (fraction load transfer) | (L_L−L_R)/W_axle | Non esposto come metrica |

Milliken: aumentare FLT su un asse **riduce** la forza laterale totale dell’asse (load sensitivity) → leva di bilanciamento US/OS.

### 2.4 Moment Method & g-g (Ch. 8–9)

- **MMM:** mappe C_N vs A_Y a β, δ, thrust fissati (constrained) → ritratto stabilità/controllo.  
- **g-g diagram:** inviluppo A_x–A_y veicolo = composizione friction circle alle 4 ruote + load transfer + aero.

Per ksengine:

- Runtime: restare su integrazione temporale.  
- Offline / tool: opzionale exporter “constrained step” o post-process telemetria → g-g e stime UG (Fase 3 / tooling).

### 2.5 Aero (Ch. 3, 15)

- Forze/momenti in assi SAE; downforce cambia Fz e quindi MF.  
- `AeroModel` = drag/downforce auto (non portanza di volo). Allineato allo spirito Milliken racing aero.

### 2.6 Setup meccanico (Ch. 12, 16–23)

Priorità Milliken per “balance”:

1. LLTD (barre, molle, h_RC)  
2. Static weight / wedge  
3. Camber, toe, pressure (C_α, friction)  
4. Aero balance  
5. Compliance / steer from geometry  

`ApplySetup` + INI devono continuare a mappare questi manopoli su grandezze che il modello **sente** (Fz per ruota, camber, μ, split aero).

---

## 3. Cosa il codice fa già in chiave Milliken

```
VehicleSimulator::integrate
  slip angles ~ bicycle kinematics (β, r, δ, a, b)
  Fz per ruota (static + aero + pressure scale)
  KsTireModel combined slip → Fx, Fy, Mz
  Σ Fy → A_y ; yaw moment → ṙ
  damage scale su handling/power
  TrackSurface μ locale
```

Punti di forza:

- Combined slip / ellipse-oriented tire path  
- Load sensitivity nel MF  
- Separazione moduli (tire / aero / susp / damage) come in Part II Milliken  

---

## 4. Gap prioritari (teoria → implementazione)

Ordinati per impatto sul **feeling di bilanciamento** race (non per completezza accademica):

| # | Gap | Rif. Milliken | Azione ksengine |
|---|-----|---------------|-----------------|
| G1 | **LLTD esplicito** (roll stiffness F/R, RC height) | Ch. 7, 16, 18 | **Done** — Kφ da molle+ARB, h_RC, `lltdFrontFraction()` |
| G2 | **Longitudinal load transfer** coerente con A_x e h | Ch. 18 | **Done** — `m * Ax * (h/ℓ)` su assi; Ax da step precedente |
| G3 | **Slip ratio** reale da ω ruota / V (non solo proxy) | Ch. 2 | **Done** — `m_wheelOmega[]`, κ=(ωR−Vx)/|Vx|, RWD torque, TC/ABS soft |
| G4 | Metriche **UG / SM / β** in telemetry | Ch. 5 | **Done** — `sideslipBeta`, `understeerGradientDegG`, `staticMargin` in state + API |
| G5 | Friction-circle telemetry | Ch. 2, 8 | **Done** — tireFx/Fy/Fz + frictionCircleUsage[4] |
| G5 **Done** | **Friction-circle usage** telemetria (Fx,Fy per ruota) | Ch. 2, 9 | Export per g-g offline |
| G6 | Transient yaw (damping-in-yaw, lag) | Ch. 6 | **Done** — Iz, N_r, Mz+align, Fy relaxation length |
| G7 | Compliance steer / roll steer | Ch. 23, 17 | **Done** — φ quasi-static, rollSteer F/R, lat/align compliance |
| G8 | Tool MMM / constrained | Ch. 8 | **Done** — `MomentMethodTool` + `ksmmm` CLI, CSV C_N–A_Y |
| G9 | **g-g diagram** envelope offline | Ch. 9 | **Done** — friction-circle + load transfer, `ksmmm --gg` |
| G10 | **Tire data normalization** / validation | Ch. 14 | **Done** — `TireNormalizeTool` + `kstirenorm` CLI |
| G11 | **Aquaplaning / wet grip** runtime | Ch. 2 | **Done** — Vcrit(p,water), μ collapse, Fz lift, telemetry |
| G12 | **Aero balance + tire thermal** | Ch. 3, 15, 2 | **Done** — wing→DF split, per-tire T, batch psi/temp |
| G13 | **Differential + FLT telemetry** | Ch. 5, 18 | **Done** — LSD in RWD path, FLT F/R, diff slip/lock |
| G14 | **Camber da sospensione + tire wear** | Ch. 17, 2 | **Done** — SuspensionModel→α camber, wear scale Fx/Fy |
| G15 | **Brake thermal fade** | systems | **Done** — BrakeThermalModel in path, disc temp/fade telemetry |
| G16 | **Ride-height aero + engine coast** | Ch. 3, 15 | **Done** — RH→ground effect, coast torque, DF/drag telem |
| G17 | **Tire pressure thermal lag** | Ch. 2 | **Done** — p rises with tyreTemp |
| G18 | **Pair analysis + pitch** | Ch. 5, 18 | **Done** — `ksmmm --pair`; quasi-static pitchAngle |
| — | **Theory gap set G1–G18** | — | **CLOSED** |

---

## 5. Equazioni minime da rispettare nel modello

**Kinematica slip (bicycle, steady / quasi-steady):**

\[
\alpha_F = \beta + \frac{a\, r}{V} - \delta, \qquad
\alpha_R = \beta - \frac{b\, r}{V}
\]

**Yaw (piano):**

\[
I_z \dot{r} = N = a\, F_{yF} - b\, F_{yR} + N_{\mathrm{align}} + \cdots
\]

**Load transfer laterale (forma usata in setup):**

\[
\Delta W_F + \Delta W_R = \frac{W\, h}{t_{\mathrm{avg}}} A_y
\]

con distribuzione F/R governata da roll rates e altezze roll center (LLTD).

**Friction limit (ideale):**

\[
\sqrt{F_x^2 + F_y^2} \le \mu_{\mathrm{eff}}(F_z, T, \mathrm{surface})\, F_z
\]

implementato via combined-slip MF, non con un cerchio rigido post-hoc.

---

## 6. Impatto su roadmap

Allineamento con `ROADMAP.md` (motore auto):

| Roadmap | Milliken |
|---------|----------|
| Fase 1 (slice giocabile) | Non blocca; tenere notazione/telemetry β, r |
| Fase 2 — aquaplaning / wet | μ_eff e load sens. come Ch. 2 + surface |
| Fase 2 — feeling | **G1–G3** (load transfer + κ reale) prima di cosmetics |
| Fase 3 — validazione tyre | Ch. 14 normalization vs dati banco |
| Tooling opzionale | g-g da log; MMM non obbligatorio in runtime |

Regola di progetto:

> Ogni manopola di setup esposta in UI deve cambiare una grandezza che compare in Milliken (Fz, α, κ, μ, LLTD, aero balance) e che il modello propaga a Fy/Fx/N.

---

## 7. Naming e telemetria consigliati

| Simbolo Milliken / SAE | Campo / canale ksengine |
|------------------------|-------------------------|
| β | `vehicleSideslip` |
| r | `m_yawRate` / `yawRate` |
| α_FL…α_RR | già in path tire; esporre in HUD debug |
| A_x, A_y | da `SimulationState.acceleration` |
| UG | calcolato offline o a bassa rate |
| FLT / LLTD % | nuovi, post G1 |
| M_z tire | `aligningMoment` (FFB) |

Assi: preferire convenzioni **SAE vehicle/tire** dove possibile (Ch. 4) per coerenza con letteratura e dati pneumatico.

---

## 8. Sintesi operativa

1. **Teoria di riferimento** per dinamica auto ksengine = Milliken RCVD.  
2. Il runtime resta **simulazione temporale** con MF + 4 ruote; non si sostituisce con MMM.  
3. Priorità modellistica guidata da Milliken: **load transfer (lat/long) → slip ratio fisico → metriche UG/β → compliance**.  
4. KsTireModel resta il cuore “force generation”; VehicleSimulator deve diventare il cuore “force → motion” più fedele a Ch. 5–7–18.  
5. Documenti collegati: `MEMORY_MAP.md` (ownership), `ROADMAP.md` (fasi), `PARITY_STATUS.md` (stato), questo file (teoria).

---

*Allineamento teorico registrato 2026-10-05. Aggiornare la tabella Gap quando G1–G3 entrano nel codice.*

## Changelog implementazione

- **2026-10-05:** G1 LLTD + G2 longitudinal load transfer in `VehicleSimulator::integrate`. Setup: `arbFront/Rear`, `rollCenter*M`, `cgHeightM`, `frontWeightFrac`. API: `lltdFrontFraction()`, `lastAx()`, `lastAy()`.
- **2026-10-05:** G3 physical slip ratio — wheel spin integration, κ to KsTireModel, long force from tire Fx; MF Dx/Dy floor ~μFz; peak-κ clamp.
- **2026-10-05:** G4 — β, UG (deg/g), static margin, lltdFront published on SimulationState + getters.
- **2026-10-06:** G5 friction-circle telemetry — tireFx/Fy/Fz, usage |F|/(μFz), α/κ per wheel on SimulationState.
- **2026-10-06:** G6 transient yaw — physical N_r damping, Iz from a/b, aligning Mz, tire Fy relaxation lag; state.yawAccel/yawDampingNr.
- **2026-10-06:** G7 compliance/roll steer — body roll from Kφ, rollSteerFront/Rear, lat+align compliance on α; state.rollAngle.
- **2026-10-06:** G8 offline MMM — MomentMethodTool (β/δ sweeps, C_N–A_Y), ksmmm CLI, optional --steady r≈Ay/V.
- **2026-10-06:** G9 g-g envelope — MomentMethodTool::sweepGgEnvelope, ksmmm --gg, CSV ax/ay/g_mag.
- **2026-10-06:** G10 tire normalize — TireNormalizeTool metrics/μ scale/curves, kstirenorm CLI.
- **2026-10-06:** G11 aquaplaning — speed/pressure/water film factor, μ scale + front-biased Fz lift; state.aquaplaneFactor/effectiveMu/waterDepthMm.
- **2026-10-06:** G12 aero balance from wings + tire thermal energy balance; per-wheel temp/pressure into KsTireModel batch.
- **2026-10-06:** G13 DifferentialModel in drive path; FLT front/rear; diffSlip/lockTorque telemetry; preload from setup.
- **2026-10-06:** G14 suspension camber into MF; tire wear accumulation + force scale; state.tireCamber.
- **2026-10-06:** G15 brake thermal — disc/pad heat, fade 400–700°C, effectiveTorque per wheel; state.brakeDiscTemp/fade.
- **2026-10-06:** G16 ride-height into AeroState, engine braking on closed throttle; state aero DF/drag/RH.
- **2026-10-06:** G17–G18 pressure thermal, pair CLI, pitch; Milliken priority gaps closed.
