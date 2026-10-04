/**
 * ks_server - dedicated server, headless, built strictly on the ksengine
 * C API (roadmap 3.2). Session authority loop with a built-in pace driver:
 * no renderer, no Qt, no Vulkan.
 *
 * Usage:
 *   ks_server [--laps N] [--time S] [--countdown S] [--track-length M]
 *             [--duration S] [--status S] [--fast] [--seed N] [--replay PATH]
 *
 * Exit codes: 0 = session completed (or SIGINT), 1 = --duration exceeded,
 *             2 = bad usage.
 *
 * Session ends on the first of: --laps completed (needs --track-length),
 * --time elapsed. Defaults: time limit 300 s.
 */
#include "ksengine_c.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

// NB: ksengine's PUBLIC include path contains src/engine/sys, whose
// Signal.h would shadow the CRT's <signal.h> on a case-insensitive FS
// (same hazard documented for vendored Lua in src/engine/CMakeLists.txt).
// Use the Win32 console handler on Windows and <csignal> elsewhere.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#endif

namespace {

std::atomic<bool> g_stop{false};

#ifdef _WIN32
BOOL WINAPI onConsoleCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}
#else
void onSigInt(int) { g_stop.store(true); }
#endif

struct Opts {
    int laps = 0;
    int timeLimit = 0;
    float countdown = 5.0f;
    float trackLength = 0.0f;
    double duration = 600.0;
    double statusEvery = 5.0;
    bool fast = false;
    unsigned seed = 0;
    std::string replay;
};

void usage() {
    std::fprintf(stderr,
        "ks_server - ksengine dedicated server (C API, headless)\n"
        "  --laps N            race length in laps (needs --track-length)\n"
        "  --time S            race time limit in seconds\n"
        "  --countdown S       green-flag countdown (default 5, 0 = none)\n"
        "  --track-length M    track length in meters (enables lap counting)\n"
        "  --duration S        hard sim-time cap (default 600)\n"
        "  --status S          status print interval in sim seconds (0 = off)\n"
        "  --fast              no realtime pacing (as fast as possible)\n"
        "  --seed N            RNG seed (0 = time-based)\n"
        "  --replay PATH       record binary replay of the session\n");
}

bool parse(int argc, char** argv, Opts& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "ks_server: %s needs a value\n", what);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); std::exit(0); }
        else if (a == "--laps") { const char* v = next("--laps"); if (!v) return false; o.laps = std::atoi(v); }
        else if (a == "--time") { const char* v = next("--time"); if (!v) return false; o.timeLimit = std::atoi(v); }
        else if (a == "--countdown") { const char* v = next("--countdown"); if (!v) return false; o.countdown = std::strtof(v, nullptr); }
        else if (a == "--track-length") { const char* v = next("--track-length"); if (!v) return false; o.trackLength = std::strtof(v, nullptr); }
        else if (a == "--duration") { const char* v = next("--duration"); if (!v) return false; o.duration = std::atof(v); }
        else if (a == "--status") { const char* v = next("--status"); if (!v) return false; o.statusEvery = std::atof(v); }
        else if (a == "--fast") { o.fast = true; }
        else if (a == "--seed") { const char* v = next("--seed"); if (!v) return false; o.seed = static_cast<unsigned>(std::strtoul(v, nullptr, 10)); }
        else if (a == "--replay") { const char* v = next("--replay"); if (!v) return false; o.replay = v; }
        else {
            std::fprintf(stderr, "ks_server: unknown option '%s'\n", a.c_str());
            return false;
        }
    }
    if (o.laps > 0 && o.trackLength <= 0.0f) {
        std::fprintf(stderr, "ks_server: --laps requires --track-length\n");
        return false;
    }
    if (o.laps <= 0 && o.timeLimit <= 0) {
        o.timeLimit = 300; // sane default: time-limited session
    }
    return true;
}

const char* phaseName(KsSessionPhase p) {
    switch (p) {
        case KS_PHASE_IDLE: return "IDLE";
        case KS_PHASE_COUNTDOWN: return "COUNTDOWN";
        case KS_PHASE_GREEN: return "GREEN";
        case KS_PHASE_CHECKERED: return "CHECKERED";
    }
    return "?";
}

/** Built-in pace driver: hold a target speed on a straight (no track data). */
KsInput paceDriver(const KsVehicleState& st) {
    constexpr float target = 55.0f;
    KsInput in{};
    if (st.speed_ms < target) {
        in.throttle = 1.0f;
    } else if (st.speed_ms > target + 4.0f) {
        in.brake = 0.25f;
    } else {
        in.throttle = 0.35f;
    }
    return in;
}

} // namespace

int main(int argc, char** argv) {
    Opts opts;
    if (!parse(argc, argv, opts)) {
        usage();
        return 2;
    }
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
#else
    std::signal(SIGINT, onSigInt);
#endif

    KsEngine* eng = ks_engine_create(opts.seed);
    if (!eng) {
        std::fprintf(stderr, "ks_server: ks_engine_create failed\n");
        return 2;
    }
    ks_engine_set_timestep(eng, 0.001);
    if (!opts.replay.empty())
        ks_engine_set_replay_path(eng, opts.replay.c_str());

    KsSessionConfig cfg{};
    cfg.total_laps = opts.laps;
    cfg.time_limit_s = opts.timeLimit;
    cfg.countdown_s = opts.countdown;
    cfg.track_length_m = opts.trackLength;
    ks_engine_session_configure(eng, &cfg);
    ks_engine_session_start(eng);

    std::printf("ks_server %s: laps=%d time=%ds countdown=%.0fs track=%.0fm duration=%.0fs%s\n",
                ks_engine_version(), opts.laps, opts.timeLimit, opts.countdown,
                opts.trackLength, opts.duration, opts.fast ? " [fast]" : "");

    const double slice = 0.05; // 20 Hz server tick (C API subdivides to 1 kHz)
    double simClock = 0.0;
    double distance = 0.0;
    double nextStatus = opts.statusEvery > 0 ? opts.statusEvery : 1e18;
    int rc = 1;

    while (!g_stop) {
        KsVehicleState st{};
        ks_engine_get_state(eng, &st);
        KsInput in = paceDriver(st);
        ks_engine_set_input(eng, &in);
        ks_engine_step(eng, slice);
        simClock += slice;
        distance += double(st.speed_ms) * slice;

        const KsSessionPhase ph = ks_engine_session_phase(eng);
        if (ph == KS_PHASE_CHECKERED) {
            std::printf("SESSION COMPLETE: laps=%d time=%.1fs distance=%.0fm\n",
                        ks_engine_session_lap(eng), ks_engine_session_time(eng),
                        distance);
            rc = 0;
            break;
        }
        if (simClock >= opts.duration) {
            std::fprintf(stderr,
                "ks_server: TIMEOUT after %.1fs sim (phase=%s lap=%d)\n",
                simClock, phaseName(ph), ks_engine_session_lap(eng));
            rc = 1;
            break;
        }
        if (simClock >= nextStatus) {
            std::printf("[status] t=%.1f phase=%s lap=%d speed=%.1f rpm=%.0f\n",
                        simClock, phaseName(ph), ks_engine_session_lap(eng),
                        st.speed_ms, st.rpm);
            nextStatus += opts.statusEvery;
        }
        if (!opts.fast)
            std::this_thread::sleep_for(std::chrono::duration<double>(slice));
    }
    if (g_stop)
        std::printf("ks_server: interrupted, shutting down cleanly\n");

    ks_engine_destroy(eng); // saves replay if recording
    return rc;
}
