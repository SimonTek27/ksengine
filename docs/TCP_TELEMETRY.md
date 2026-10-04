# TCP communication in ksim — exploration

## Repo status (today)

| Channel | Exists | Use |
|--------|--------|-----|
| **Shared memory** | Yes | Local overlays (AC-compatible layout) |
| **UDP out** | Yes (`UdpTelemetryBridge`, :20777) | Best-effort LAN telemetry |
| **UDP in** | Yes (`UdpTelemetryListener`, :20747) | External streams |
| **yojimbo / netcode** | Partial (`NetworkManager`) | Multiplayer game state |
| **TCP telemetry** | **No** (up to this doc) | — |
| **MQTT** | Stub (`MqttClient`) | Not production |

The code **did not** yet contain a dedicated TCP publisher for telemetry.
Multiplayer relies on **application-level reliable UDP** (yojimbo), not raw TCP.

---

## Channel comparison (sim racing)

| | Shared memory | UDP | TCP |
|--|---------------|-----|-----|
| **Latency** | Minimal (same PC) | Low | Higher (handshake, buffering) |
| **Reliability** | N/A local | No (losses ok) | Yes (ordering + retransmission) |
| **Remote** | No | Yes | Yes |
| **Multi-reader** | Excellent | Broadcast/multicast | 1 connection = 1 client (or server fan-out) |
| **Typical rate** | 100–1000 Hz | 20–60 Hz | 10–60 Hz |
| **Use cases** | Local dash, FFB apps | Phone dash, light logging | Remote logger, analysis tools, WAN relay |

Industry practice: **SM** and **UDP** dominate live telemetry; **TCP** shows up for
reliable logging, remote coaching, or an "SM → network" bridge.

---

## When to use TCP in ksim

1. **Remote logger** on another PC that cannot tolerate gaps in samples
2. **Analysis tool** doing request/response (e.g. "give me the last lap")
3. **Relay** reading SM and forwarding to cloud / coach
4. **Not** for FFB or a 1 kHz HUD (too much jitter)

## When not to use it

- FFB / motion (prefer local SM or UDP)
- Multiplayer gameplay (yojimbo already targets lag compensation)
- Broadcast to N apps on the same host (SM wins)

---

## Proposed design: `TcpTelemetryBridge`

```
SimulationLoop::tick
  → publishSharedMemory()   // local
  → publishUdpTelemetry()   // best-effort
  → publishTcpTelemetry()   // only if a client is connected
```

### Protocol (v1)

**TCP** stream, little-endian, length-prefixed messages:

```
[uint32 le length][payload]

payload = same layout as UdpTelemPacket (magic KSIM, version 1)
   or
payload = UTF-8 JSON (if jsonMode)
```

- Server: listens on `0.0.0.0:20778` (default)
- Accepts **one client** (v1); on disconnect → back to listen
- Rate limit: send at most every N ms (default 16 ms ≈ 60 Hz) even if the tick is 1 kHz

### ksim ports (summary)

| Port | Protocol | Direction |
|-------|------------|-----------|
| 20747 | UDP | In (external listener) |
| 20777 | UDP | Telemetry out |
| 20778 | TCP | Reliable telemetry out |

---

## Multiplayer vs telemetry

| | Game net (yojimbo) | Telemetry TCP |
|--|--------------------|---------------|
| Payload | Input, car state, session | Read-only telemetry |
| QoS | Lag compensation, snapshot | Monotonic sample stream |
| Security | Netcode auth | Bind localhost or trusted LAN |

Do not mix the two: the race client must not depend on the telemetry channel.

---

## TCP roadmap

| Step | Description |
|------|-------------|
| **P0** | TCP server + length-prefix + `UdpTelemPacket` (this commit) |
| P1 | Multi-client fan-out |
| P2 | Client commands (`PING`, `SET_RATE`, `GET_STATIC`) |
| P3 | Optional TLS / token auth for WAN |
