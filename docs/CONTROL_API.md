# External control API — TCP `:20780`

Reference for the InSim-style bidirectional control channel served by
`src/simulator/ExternalControlApi.h` and dispatched by
`FeatureHub::handleControl` (`src/simulator/FeatureHub.h`).

Together with the shared-memory / telemetry sections below this covers
**GAP P2.10** (external API documentation).

## Endpoint

| | |
|--|--|
| Transport | TCP, **one client at a time** (next accept served after disconnect) |
| Bind | `0.0.0.0:20780` (`ExternalControlApi::kDefaultPort`) |
| Started | `FeatureHub::startServices()` (SimulatorApp / SimulatorServer) |
| Encoding | UTF-8 text, one message per line (`\n` or `\r\n` terminated) |
| Parsing | verb is case-insensitive (uppercased at parse), arguments are whitespace-split — no quoting |

Commands are polled from the simulation tick
(`FeatureHub::tick` → `control.poll()`), replies and events are written
back synchronously on the same connection.

## Handshake

On accept the server sends exactly one greeting line:

```
OK ksim control API v1                # no auth configured
OK ksim control API v1 AUTH_REQUIRED  # token configured
```

## Authentication

Auth is optional — LAN trust model (same documented stance as `ksnet.h`):

- A token is configured with `FeatureHub::setControlAuthToken(token)`;
  a non-empty token switches the API to `AUTH_REQUIRED` mode.
- `AUTH <token>` → `OK AUTH` / `ERR AUTH`.
- While a token is required and the client has not authenticated, only
  `PING` and `AUTH` are accepted; every other verb answers
  `ERR UNAUTH - send AUTH <token>`.
- The greeting advertises the mode up front. Empty token = open LAN API.
- Status: the mechanism is complete, but the token is **not yet exposed
  via config/CLI** (GAP **P1.2**: default deployments run open).

## Commands (client → server)

| Verb | Arguments | Reply |
|------|-----------|-------|
| `PING` | — | `PONG` |
| `AUTH` | `<token>` | `OK AUTH` / `ERR AUTH` |
| `SESSION` | `[MODE] [LAPS]` | `OK SESSION <MODE>` |
| `WEATHER` | `<preset>` | `OK WEATHER` |
| `TIME` | `<hours>` | `OK TIME` |
| `LIMITS` | `[ON\|OFF]` | `OK LIMITS ON\|OFF` |
| `PB` | `[LIST [track]]` | `OK PB`, or `OK PB COUNT <n>` + n rows |
| `REPLAY` | `LOAD <path>` | `OK REPLAY` / `ERR REPLAY` |
| `REPLAY` | `PLAY` | `OK PLAY` |
| `REPLAY` | `STOP` | `OK STOP` |
| `RESULT` | — | n × `EVT RESULT ...`, then `OK RESULT` |
| anything else | | `ERR unknown <VERB>` |

### SESSION `[MODE] [LAPS]`

The mode is matched as a **case-insensitive substring** of the first
argument (`modeFromMenuEntry`):

| Match | Mode |
|-------|------|
| `QUAL*` | `QUALIFYING` |
| `PRACT*` | `PRACTICE` |
| `TIME*` | `TIME_ATTACK` |
| `HOT*` | `HOTLAP` |
| `REPLAY` | `REPLAY` |
| default | `RACE` |

An optional second argument sets total laps (`0`/omitted = mode default,
e.g. timed practice/qualy). The session starts immediately
(`onBeginSession` fires) and the reply echoes the resolved mode:
`OK SESSION PRACTICE`.

Note: arguments are whitespace-split, laps are read from the *second*
argument — use a single token for modes with underscores:
`SESSION TIME_ATTACK 10`, not `SESSION TIME ATTACK 10`.

### WEATHER `<preset>`

`presetByName`: first argument lowercased and matched as substring —
`wet` → wet preset, `damp` → damp preset, anything else → dry.
Always replies `OK WEATHER`.

### TIME `<hours>`

Time of day as a float in hours (`0..24`, wrapped mod 24).

### LIMITS `[ON|OFF]`

Toggles track-limits enforcement. With no argument it only queries; the
current state is always echoed: `OK LIMITS ON` / `OK LIMITS OFF`.

### PB `[LIST [track]]`

`PB LIST [track]` replies `OK PB COUNT <n>` followed by `<n>` rows
`PB <carId> <bestLapSeconds>` (top 10, default track `default`).
Bare `PB` replies `OK PB`.

### REPLAY

- `REPLAY LOAD <path>` — load a replay file (`ERR REPLAY` on failure).
- `REPLAY PLAY` / `REPLAY STOP` — control playback.
- An unknown `REPLAY` subcommand is silently ignored (no reply);
  `REPLAY` with no arguments falls through to `ERR unknown REPLAY`.

### RESULT

Requests the current standings: one `EVT RESULT` line per standing is
emitted **first**, then `OK RESULT` closes the exchange.

## Events (server → client)

Unsolicited lines interleaved with command replies on the same stream:

| Event | Emitted when |
|-------|--------------|
| `EVT PB track=<track> lap=<seconds>` | the lap improved the personal best (lap or sector) for that track+car (`PersonalBestStore::submitLap` returned true) |
| `EVT LAP time=<seconds>` | every completed lap |
| `EVT RESULT pos=<n> car=<car> driver=<name> lap=<n> best=<seconds> [finished]` | after `RESULT`, one per standing |

Floats use `std::to_string` formatting (6 decimals) except `RESULT`
`best=` (3 decimals).

## Error handling

- `OK ...` = success, `ERR ...` = failure — always a complete line.
- Codes: `ERR AUTH`, `ERR UNAUTH - send AUTH <token>`, `ERR REPLAY`,
  `ERR unknown <VERB>` (unknown verb, or a verb missing its required
  first argument: `WEATHER`, `TIME`, `REPLAY`),
  `ERR unknown handler` (no `onCommand` handler wired — cannot happen in
  a normal sim, the FeatureHub always wires one).

## Example session

```
$ nc 127.0.0.1 20780
OK ksim control API v1
PING
PONG
SESSION PRACTICE 10
OK SESSION PRACTICE
WEATHER WET
OK WEATHER
TIME 17.5
OK TIME
LIMITS OFF
OK LIMITS OFF
EVT LAP time=92.413000
RESULT
EVT RESULT pos=1 car=ks_base driver=you lap=10 best=91.876000 finished
OK RESULT
```

## Security

- Binds all interfaces with **no encryption** — LAN/localhost trust
  model, matching the scope note in `src/engine/network/ksnet.h`.
- Recommended: localhost or a trusted LAN only; wire a token via
  `setControlAuthToken` before exposing the port any further.

---

## Related channels — ksim port map

| Port | Transport | Purpose | Doc |
|------|-----------|---------|-----|
| 20747 | UDP in | external telemetry streams (`UdpTelemetryListener`, AC-compatible) | [TCP_TELEMETRY.md](TCP_TELEMETRY.md) |
| 20777 | UDP out | best-effort LAN telemetry (`UdpTelemetryBridge`, KSIM v2) | [TCP_TELEMETRY.md](TCP_TELEMETRY.md) |
| 20778 | TCP out | reliable telemetry (`TcpTelemetryBridge`, length-prefixed KSIM v2) | [TCP_TELEMETRY.md](TCP_TELEMETRY.md) |
| 20779 | UDP | LAN server discovery (`ServerDiscovery`) | — |
| 20780 | TCP | **control API (this document)** | — |
| 8080 | HTTP | optional lobby registry (`kslobby`, `--port`) | `LobbyServerApp.h` |

## Shared memory layout (AC-compatible)

Published every physics tick by `AcSharedMemoryPublisher`
(`src/adapters/assetto_corsa/`):

| Page | Windows | Linux |
|------|---------|-------|
| Physics | `Local\acpmf_physics` | `/acpmf_physics` |
| Graphics | `Local\acpmf_graphics` | `/acpmf_graphics` |
| Static | `Local\acpmf_static` | `/acpmf_static` |

- Layout is **AC-compatible** (fixed, `#pragma pack(4)`) so third-party
  AC tools read it unchanged; the publisher keeps a soft in-process
  mirror as fallback when mapping fails.
- Damage channels: the physics page carries `carDamage[5]` =
  front, rear, left, right, overall (0..1). Derivation from
  `DamageSystem` and the full mapping are documented in
  [DAMAGE_TELEMETRY.md](DAMAGE_TELEMETRY.md).

## Telemetry packet v2 (UDP `:20777` / TCP `:20778`)

Binary little-endian `UdpTelemPacket` (magic `KSIM`, `version = 2`;
length-prefixed on TCP) carries the damage channels:
`damageWarning`, `engineSeized`, `damageOverall`, `engineHealth`,
`powerMult`, `dragMult`, `downforceMult`, `carDamage[5]`,
`suspIntegrity[4]`. TCP JSON mode carries the subset
`dmg` / `engH` / `pwr` / `warn` / `seized`. Framing details:
[TCP_TELEMETRY.md](TCP_TELEMETRY.md).
