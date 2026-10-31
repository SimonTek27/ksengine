# Phase 3 — Pro depth

**Started:** 2026-10-20  
**Product:** Kinetika (ksim / ksengine)  
**Prerequisite:** Phase 2 code closed; human gates G1–G3 still open on Windows (do not block Phase 3 tooling).

---

## Deliverables

| # | Item | Priority | Status |
|---|------|----------|--------|
| **3.1** | Tyre validation vs real telemetry | High | Tooling ready (`kstirenorm`, Milliken G1–G18 ✅); needs **data campaign** (real logs) |
| **3.2** | ksnet secure-connect, reconnect, dedicated server | Medium | **✅ Delivered** — secure transport, client auto-reconnect, headless host key/token flags; evidence `test_multiplayer` + `ks_server_smoke` |
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

### Done on master (2026-10-31)

- [x] `ksnet.h`: `PRIVATE_KEY_BYTES`, `Client::SecureConnect`, `Server::SetPrivateKey` / `ClearPrivateKey` / `isSecure`
- [x] **Body on master** (2026-10-31): `src/engine/network/ksnet/ksnet.cpp`
      carries the implementation — XOR keystream after the 13-byte header,
      8-byte CONNECT key proof, `kRejectKeyMismatch` = 2 — promoted from
      `milliken_extract/ksnet.cpp` (44 KB superset of the old 36 KB file).
      The staging tree is gone.
- [x] Repo cleanup of VS noise / backups / LUTParser.i / milliken zip
- [x] `tools/cleanup_superfluous.ps1` + `.sh` — promote body + delete `milliken_extract/`
- [x] **NetworkLowLevel** — `setPrivateKey` / `clearPrivateKey` / `hasPrivateKey`;
      `connect()` calls `SecureConnect` when a key is set, `start()` calls
      `SetPrivateKey` before `Start()`; `isSecure()` on both endpoints.
- [x] **NetworkManager** — key pass-through to host and client, plus
      `KS_NET_KEY=<hex64>` at construction (invalid value stays insecure).
- [x] **Test** — `test_multiplayer` covers secure handshake, guest join with
      key proof, malformed-key rejection, mismatched-key reject and client
      auto-reconnect after a server restart.
- [x] **Reconnect** — `NetworkManager::setAutoReconnect(enabled, attempts, delay)`;
      unexpected drops schedule CONNECT retries (0 attempts = unlimited).
      `disconnectFromServer()` / auth failures cancel the retry loop.
- [x] **Dedicated host** — `SimulatorServer` accepts `--secure-key HEX64` /
      `--auth-token TOKEN` (and `secure-key` / `auth-token` in
      `server/kssimserver.ini`), validates both before binding and reports the
      resulting mode. `tools/ks_server` already provides the C-API headless
      harness covered by `ks_server_smoke`.

### Remaining (optional)

1. **Lobby advert** — expose secure mode in the server list.

### One local command (historical — already applied)

```bat
powershell -ExecutionPolicy Bypass -File tools\cleanup_superfluous.ps1
```

That script promoted `milliken_extract/ksnet.cpp` → `src/engine/network/ksnet/ksnet.cpp`,
removed the staging tree and committed. It is kept for reproducible rebuilds of the
cleanup but is a no-op on current master.

### Next steps

1. ~~Wire NetworkLowLevel~~ — done 2026-10-31 (`SecureConnect` / `SetPrivateKey`).
2. ~~Wire NetworkManager~~ — done 2026-10-31 (`setPrivateKey` / `clearPrivateKey` / `hasPrivateKey`).
3. ~~Optional env~~ — done: `KS_NET_KEY=<hex64>` for host/client smoke.
4. ~~Reconnect~~ — done 2026-10-31 (`setAutoReconnect`, retry loop in `update`).
5. ~~Dedicated server~~ — done 2026-10-31: `SimulatorServer --secure-key/--auth-token`
   (or `secure-key`/`auth-token` in `server/kssimserver.ini`).

### Smoke test

```bat
set KS_NET_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
kssimserver.exe --game-port 40000 --secure-key %KS_NET_KEY%
ksim.exe ... join 127.0.0.1
```

Mismatch key → CONNECT_REJECT reason 2; no key → legacy InsecureConnect.
Automated coverage: `test_multiplayer` (secure handshake, reject, auto-reconnect)
and `ks_server_smoke`.

---

## Suggested order

1. Start **3.1 data campaign** when real tyre logs exist.
2. **3.4** driver swap after human box gate G3.
3. Defer 3.5–3.8 until Windows soak is routine.

---

## Explicit non-goals (unchanged)

- Rewrite physics / session / ksnet reliable channel
- Bike / boat / aircraft
- Global ranked before offline/LAN loop is solid

*Update this file when a Phase 3 item closes; link evidence path in the same commit.*
