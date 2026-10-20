# Phase 3 — Pro depth

**Started:** 2026-10-20  
**Product:** Kinetika (ksim / ksengine)  
**Prerequisite:** Phase 2 code closed; human gates G1–G3 still open on Windows (do not block Phase 3 tooling).

---

## Deliverables

| # | Item | Priority | Status |
|---|------|----------|--------|
| **3.1** | Tyre validation vs real telemetry | High | Tooling ready (`kstirenorm`, Milliken G1–G18 ✅); needs **data campaign** (real logs) |
| **3.2** | ksnet secure-connect, reconnect, dedicated server | Medium | **In progress** — API on master; body = one local script |
| **3.3** | Multi-layout without full reload | Medium | Not started |
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

## 3.2 — ksnet SecureConnect (current focus)

### Design

| Piece | Behaviour |
|-------|-----------|
| Key | Exactly **32 bytes** (`ksnet::PRIVATE_KEY_BYTES`) |
| Client | `SecureConnect(key, 32, addr)` — CONNECT carries 8-byte key-proof |
| Server | `SetPrivateKey(key, 32)` before `Start()` |
| Reject | reason **2** = key mismatch (`kRejectKeyMismatch`) |
| Payload | After 13-byte header: XOR keystream (nonce + key) |
| Default | No key → `InsecureConnect` (LAN open, unchanged) |

Application auth (`setAuthToken` / `setJoinToken`) remains independent and already shipped.

### Done on master (2026-10-20)

- [x] `ksnet.h`: `PRIVATE_KEY_BYTES`, `Client::SecureConnect`, `Server::SetPrivateKey` / `ClearPrivateKey` / `isSecure`
- [x] Repo cleanup of VS noise / backups / LUTParser.i / milliken zip
- [x] `tools/cleanup_superfluous.ps1` + `.sh` — promote body + delete `milliken_extract/`

### One local command (finishes 3.2 body + extract purge)

```bat
powershell -ExecutionPolicy Bypass -File tools\cleanup_superfluous.ps1
```

or:

```bash
bash tools/cleanup_superfluous.sh
```

That copies `milliken_extract/ksnet.cpp` → `src/engine/network/ksnet/ksnet.cpp`, removes the staging tree, commits, and pushes.

### After the body is on master

1. **Wire NetworkLowLevel** — if private key set, call `SecureConnect` / `SetPrivateKey` instead of `InsecureConnect`.
2. **Wire NetworkManager** — `setPrivateKey` / `clearPrivateKey` / `hasPrivateKey` pass-through.
3. **Optional env** — `KS_NET_KEY=<hex64>` for host/client smoke.
4. **Reconnect** — client auto-retry CONNECT (follow-up).
5. **Dedicated server** — harden `tools/ks_server` (follow-up).

### Smoke test (after body + wire)

```bat
set KS_NET_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
ksim.exe ... host
ksim.exe ... join 127.0.0.1
```

Mismatch key → CONNECT_REJECT reason 2; no key → legacy InsecureConnect.

---

## Suggested order

1. Run **cleanup_superfluous** (body + drop extract).
2. Wire NetworkManager private key.
3. Start **3.1 data campaign** when real tyre logs exist.
4. **3.4** driver swap after human box gate G3.
5. Defer 3.5–3.8 until Windows soak is routine.

---

## Explicit non-goals (unchanged)

- Rewrite physics / session / ksnet reliable channel
- Bike / boat / aircraft
- Global ranked before offline/LAN loop is solid

*Update this file when a Phase 3 item closes; link evidence path in the same commit.*
