# ksim MCP bridge

Exposes the simulator's control API (`docs/CONTROL_API.md`, TCP `:20780`) as
[MCP](https://modelcontextprotocol.io) tools, so an agent can drive a running
ksim — start a session, change weather and time, toggle track limits, read
standings and set personal bests — and see the real replies.

The bridge adds nothing to the engine: it is a stdio process that speaks the
existing line protocol. No control command was invented for it, and no reply is
reinterpreted.

## Run

```bash
# with ksim running (SimulatorApp starts the control API on 20780)
python tools/mcp/ksim_mcp.py
```

Environment:

| Variable | Default | Meaning |
|----------|---------|---------|
| `KSIM_HOST` | `127.0.0.1` | control API host |
| `KSIM_PORT` | `20780` | control API port |
| `KSIM_TOKEN` | *(empty)* | `AUTH` token, when the API requires one |
| `KSIM_TIMEOUT` | `2.0` | socket timeout in seconds |

## Replies

An exchange ends on the first line that is not an `EVT ...` event: `OK ...`,
`ERR ...` or `PONG`. `OK PB COUNT <n>` is followed by `n` rows. Nothing else is
reinterpreted.

Silence is diagnosable from ksim's own side: start the simulator with
`KS_CONTROL_DEBUG=1` and it traces every accept, read, parsed command and reply
on stderr. That trace is how the missing `PONG` was found - a terminal reply
that does not start with `OK`.

## MCP client config

```json
{
  "mcpServers": {
    "ksim": {
      "command": "python",
      "args": ["E:/Users/Simon/source/repos/ksengine/tools/mcp/ksim_mcp.py"],
      "env": { "KSIM_PORT": "20780" }
    }
  }
}
```

## Tools

| Tool | Verbs | Notes |
|------|-------|-------|
| `ksim_ping` | `PING` | reachability check |
| `ksim_start_session` | `SESSION <mode> [laps]` | mode is a case-insensitive substring; `TIME_ATTACK` must be one token |
| `ksim_set_weather` | `WEATHER <preset>` | `wet`, `damp`, anything else = dry |
| `ksim_set_time` | `TIME <hours>` | float hours, wrapped mod 24 |
| `ksim_track_limits` | `LIMITS [ON\|OFF]` | omit `enabled` to query |
| `ksim_results` | `RESULT` | returns the `EVT RESULT` lines |
| `ksim_personal_bests` | `PB LIST <track>` | top 10 rows |
| `ksim_replay` | `REPLAY LOAD <path>`, `REPLAY PLAY`, `REPLAY STOP` | |

## Limits

- The control API serves **one client at a time**: a call made while another
  client is still connected is accepted but stays unanswered. Each tool call
  opens the socket, completes its exchange and closes it, and one retry covers
  the moment when the simulator is still reaping the previous one; a client
  that really is connected makes the retry fail too, and the error says so.
- No `mid-session` guarantees: the verbs are what `FeatureHub::handleControl`
  implements. Telemetry (`:20777` / `:20778`) and shared memory are separate
  channels and not wrapped here.
- `ERR ...` replies are returned as `isError` results with the original text.
  A missing simulator surfaces as a connection error, never as a success.
