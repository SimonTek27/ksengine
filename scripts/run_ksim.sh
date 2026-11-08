#!/usr/bin/env bash
# Run the staged simulator for a bounded time with a captured log, and make sure
# it is gone afterwards.
#
#   KS_TRACK=... KS_MIRROR=1 scripts/run_ksim.sh [seconds] [log]
#     seconds   how long to let it run (default 15)
#     log       where stderr/stdout go (default out/build/ksim_run.log)
#
# Why this exists: with `cd dir && ./ksim.exe &` the shell's $! is the PID of
# the subshell, not of the simulator, so killing it can leave ksim.exe alive and
# holding the binary — the next build then dies with LNK1104 ("cannot open
# ksim.exe"). Here the launch goes through exec, so $! is the simulator itself,
# and the trap kills it on normal exit, Ctrl-C and TERM.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
bin="$root/out/build/qtfree-sim/bin/Release"
seconds=${1:-15}
log=${2:-$root/out/build/ksim_run.log}
status=0

if [ ! -x "$bin/ksim.exe" ]; then
    printf 'run_ksim: no ksim.exe in %s — build the qtfree-sim target first\n' "$bin" >&2
    exit 2
fi

# A second instance would lock ksim.exe against the next build. Report it and
# stop: killing a session somebody else started is not this script's business.
running=$(tasklist //FI "IMAGENAME eq ksim.exe" //FO CSV //NH 2>/dev/null |
    awk -F'","' '{print $2}' | tr -d '"' | tr '\n' ' ')
if [ -n "${running// /}" ]; then
    printf 'run_ksim: ksim.exe is already running (PID %s); close it first\n' "$running" >&2
    exit 3
fi

mkdir -p "$(dirname "$log")"
: >"$log"

( cd "$bin" && exec ./ksim.exe ) >"$log" 2>&1 &
pid=$!

stop() {
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.5
        done
        kill -9 "$pid" 2>/dev/null
    fi
    wait "$pid" 2>/dev/null
}
trap stop EXIT INT TERM

elapsed=0
while [ "$elapsed" -lt "$seconds" ] && kill -0 "$pid" 2>/dev/null; do
    sleep 1
    elapsed=$((elapsed + 1))
done

if kill -0 "$pid" 2>/dev/null; then
    printf 'run_ksim: ran %ss, stopping it\n' "$elapsed"
else
    wait "$pid" 2>/dev/null
    status=$?
    printf 'run_ksim: the simulator exited on its own after %ss (status %s)\n' \
        "$elapsed" "$status"
fi

stop
trap - EXIT INT TERM

printf 'run_ksim: %s lines in %s\n' "$(wc -l <"$log" | tr -d ' ')" "$log"
if grep -qiE 'VUID|validation layer' "$log"; then
    printf 'run_ksim: Vulkan validation messages in the log\n'
    grep -iE 'VUID|validation layer' "$log" | head -5
fi
exit "$status"
