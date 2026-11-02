#!/usr/bin/env python3
"""ksim MCP server — exposes the ksim control API (:20780) as MCP tools.

The simulator already speaks a line protocol on TCP 20780 (see
docs/CONTROL_API.md). This is the bridge that lets an MCP client (an agent, an
editor, a terminal) drive a running ksim: same verbs, same replies, wrapped in
the Model Context Protocol over stdio.

Deliberately not a client library: the control API accepts ONE client at a
time, so every tool call opens the socket, does its exchange and closes it.
Nothing is held open between calls, which keeps the port free for other tools
when the agent is idle.

Environment:
    KSIM_HOST     default 127.0.0.1
    KSIM_PORT     default 20780
    KSIM_TOKEN    token for AUTH (omit when the API runs open)
    KSIM_TIMEOUT  seconds, default 2.0

The server never invents a result: if ksim is not running, or the reply is an
`ERR ...` line, the tool returns the error text as an error result so the
caller sees the real state instead of a success.
"""

import json
import os
import socket
import sys
import time

PROTOCOL_VERSION = "2024-11-05"
SERVER_INFO = {"name": "ksim-control", "version": "1.0.0"}

HOST = os.environ.get("KSIM_HOST", "127.0.0.1")
PORT = int(os.environ.get("KSIM_PORT", "20780"))
TOKEN = os.environ.get("KSIM_TOKEN", "")
TIMEOUT = float(os.environ.get("KSIM_TIMEOUT", "2.0"))

NEWLINE = b"\n"


class ControlError(Exception):
    """The control API could not be reached, or answered with ERR."""


class _LineReader:
    """Newline-delimited reads straight from the socket.

    socket.makefile() is not used on purpose: its buffered readline() misbehaves
    with a socket timeout set (on Windows it can sit on data that has already
    arrived), while a plain recv() returns that data immediately - and the
    protocol is line-based anyway.
    """

    def __init__(self, sock):
        self._sock = sock
        self._buf = b""

    def readline(self):
        while NEWLINE not in self._buf:
            chunk = self._sock.recv(4096)
            if not chunk:
                if self._buf:
                    line, self._buf = self._buf, b""
                    return line
                raise ControlError("control API closed the connection")
            self._buf += chunk
        line, _, rest = self._buf.partition(NEWLINE)
        self._buf = rest
        return line


def _readline(reader):
    raw = reader.readline()
    if not raw:
        raise ControlError("control API closed the connection")
    return raw.decode("utf-8", "replace").strip()


def control(verb, args=()):
    """Send one verb, retrying once while the simulator reaps a previous client.

    The control API serves one client at a time: it does not answer a new socket
    until the old one has gone, so the first call right after another client
    disconnects can be accepted but silent. One retry covers that; when a client
    is really still connected the retry is silent too and the caller is told why.
    """
    line = " ".join([verb] + [str(a) for a in args]).strip()
    try:
        return _control_once(line)
    except socket.timeout:
        time.sleep(0.75)
        try:
            return _control_once(line)
        except socket.timeout:
            raise ControlError(
                "control API accepted the connection but did not answer twice "
                "on %s:%d. It serves one client at a time: another client may "
                "still be connected (close it, or wait a moment and retry)."
                % (HOST, PORT))


def _control_once(line):
    """One request/response exchange on a fresh connection."""
    try:
        with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as sock:
            sock.settimeout(TIMEOUT)
            reader = _LineReader(sock)

            greeting = _readline(reader)
            if not greeting.startswith("OK "):
                raise ControlError("unexpected greeting: %s" % greeting)

            if "AUTH_REQUIRED" in greeting:
                if not TOKEN:
                    raise ControlError(
                        "control API requires AUTH but KSIM_TOKEN is not set")
                sock.sendall(("AUTH %s\n" % TOKEN).encode("utf-8"))
                reply = _readline(reader)
                if not reply.startswith("OK"):
                    raise ControlError("auth rejected: %s" % reply)

            sock.sendall((line + "\n").encode("utf-8"))
            lines = []
            while True:
                reply = _readline(reader)
                lines.append(reply)
                # EVT lines are progress, not the answer: the real reply comes
                # after them. Everything else terminates the exchange - and a
                # terminal reply is not always OK: PING answers "PONG", which
                # is why matching only "OK" here left every ping waiting for a
                # second line until the socket timed out.
                if reply.startswith("EVT "):
                    continue
                if reply.startswith("OK PB COUNT "):
                    try:
                        rows = int(reply.split()[-1])
                    except (IndexError, ValueError):
                        break
                    for _ in range(max(0, rows)):
                        lines.append(_readline(reader))
                break
            return lines
    except socket.timeout:
        raise  # control() decides whether this is worth one retry
    except OSError as exc:
        raise ControlError(
            "cannot reach ksim control API on %s:%d (%s). Is the simulator "
            "running?" % (HOST, PORT, exc))


TOOLS = [
    {
        "name": "ksim_ping",
        "description": "Check that a ksim instance is running and its control "
                       "API answers. Returns PONG.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "ksim_start_session",
        "description": "Start a session immediately. mode is matched as a "
                       "case-insensitive substring: QUALIFYING, PRACTICE, "
                       "TIME_ATTACK (send it as one token), HOTLAP, REPLAY, "
                       "or anything else for RACE. laps=0 keeps the mode "
                       "default.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "mode": {"type": "string",
                         "description": "session mode, e.g. PRACTICE"},
                "laps": {"type": "integer", "minimum": 0,
                         "description": "total laps, 0 = mode default"},
            },
            "required": ["mode"],
        },
    },
    {
        "name": "ksim_set_weather",
        "description": "Set the weather preset: 'wet', 'damp' or anything "
                       "else for dry (matched as a substring).",
        "inputSchema": {
            "type": "object",
            "properties": {"preset": {"type": "string"}},
            "required": ["preset"],
        },
    },
    {
        "name": "ksim_set_time",
        "description": "Set time of day in hours (0..24, wrapped).",
        "inputSchema": {
            "type": "object",
            "properties": {"hours": {"type": "number"}},
            "required": ["hours"],
        },
    },
    {
        "name": "ksim_track_limits",
        "description": "Toggle track-limits enforcement, or query it when "
                       "enabled is omitted. Returns OK LIMITS ON|OFF.",
        "inputSchema": {
            "type": "object",
            "properties": {"enabled": {"type": "boolean"}},
        },
    },
    {
        "name": "ksim_results",
        "description": "Current standings: one EVT RESULT line per car "
                       "(position, car, driver, lap, best lap, finished).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "ksim_personal_bests",
        "description": "Top-10 personal best laps for a track.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "track": {"type": "string", "description": "default 'default'"},
            },
        },
    },
    {
        "name": "ksim_replay",
        "description": "Replay control: action 'load' with path, 'play', or "
                       "'stop'.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "action": {"type": "string",
                           "enum": ["load", "play", "stop"]},
                "path": {"type": "string",
                         "description": "replay file, required for load"},
            },
            "required": ["action"],
        },
    },
]


def call_tool(name, args):
    """Map a tool call onto control-API verbs. Raises ControlError on ERR."""
    args = args or {}

    if name == "ksim_ping":
        lines = control("PING")
    elif name == "ksim_start_session":
        mode = str(args.get("mode", "")).strip()
        if not mode:
            raise ControlError("mode is required")
        laps = int(args.get("laps", 0) or 0)
        lines = control("SESSION", [mode, laps] if laps > 0 else [mode])
    elif name == "ksim_set_weather":
        lines = control("WEATHER", [str(args.get("preset", ""))])
    elif name == "ksim_set_time":
        lines = control("TIME", [repr(float(args.get("hours", 12.0)))])
    elif name == "ksim_track_limits":
        enabled = args.get("enabled")
        if enabled is None:
            lines = control("LIMITS")
        else:
            lines = control("LIMITS", ["ON" if enabled else "OFF"])
    elif name == "ksim_results":
        lines = control("RESULT")
    elif name == "ksim_personal_bests":
        track = str(args.get("track") or "default")
        lines = control("PB", ["LIST", track])
    elif name == "ksim_replay":
        action = str(args.get("action", "")).lower()
        if action == "load":
            path = str(args.get("path", ""))
            if not path:
                raise ControlError("path is required for load")
            lines = control("REPLAY", ["LOAD", path])
        elif action in ("play", "stop"):
            lines = control("REPLAY", [action.upper()])
        else:
            raise ControlError("action must be load, play or stop")
    else:
        raise ControlError("unknown tool: %s" % name)

    for line in lines:
        if line.startswith("ERR"):
            raise ControlError(line)
    return lines


def tool_result(text, is_error=False):
    return {
        "content": [{"type": "text", "text": text}],
        "isError": is_error,
    }


def handle(request):
    """Return the JSON-RPC response for one request, or None for a notification."""
    method = request.get("method")
    request_id = request.get("id")
    params = request.get("params") or {}

    if request_id is None:
        return None  # notification: notifications/initialized, cancelled, ...

    if method == "initialize":
        result = {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": SERVER_INFO,
        }
    elif method == "tools/list":
        result = {"tools": TOOLS}
    elif method == "tools/call":
        name = params.get("name", "")
        arguments = params.get("arguments") or {}
        try:
            lines = call_tool(name, arguments)
            result = tool_result("\n".join(lines))
        except ControlError as exc:
            result = tool_result(str(exc), is_error=True)
    elif method == "ping":
        result = {}
    else:
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "error": {"code": -32601, "message": "method not found: %s" % method},
        }

    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def main():
    out = sys.stdout
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            request = json.loads(raw)
        except ValueError:
            out.write(json.dumps({
                "jsonrpc": "2.0", "id": None,
                "error": {"code": -32700, "message": "parse error"},
            }) + "\n")
            out.flush()
            continue
        response = handle(request)
        if response is not None:
            out.write(json.dumps(response) + "\n")
            out.flush()


if __name__ == "__main__":
    main()
