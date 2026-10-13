/**
 * ksengine C API — headless / GDExtension / dedicated server entry points.
 * Qt-free. ABI intended to stay stable across minor versions.
 *
 * Roadmap E1: create/step/state + load_track/load_car + session/scene.
 * Roadmap E9: ks_engine_profiler_* wraps PhysicsProfiler (report + CSV).
 */
#ifndef KSENGINE_C_H
#define KSENGINE_C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#if defined(_WIN32) && defined(KSENGINE_DLL)
#  ifdef KSENGINE_BUILD
#    define KSAPI __declspec(dllexport)
#  else
#    define KSAPI __declspec(dllimport)
#  endif
#else
#  define KSAPI
#endif

typedef struct KsEngine KsEngine;

typedef struct KsVehicleState {
    float x, y, z;
    float yaw, pitch, roll;
    float speed_ms;
    float rpm;
    int32_t gear;
    float throttle, brake, steer;
    float fuel_l;
    float tyre_temp_fl, tyre_temp_fr, tyre_temp_rl, tyre_temp_rr;
} KsVehicleState;

typedef struct KsInput {
    float throttle; /* 0..1 */
    float brake;
    float steer;    /* -1..1 */
    float clutch;
    int32_t gear_request; /* 0 = none */
} KsInput;

/** Create engine instance. seed=0 → time-based; non-zero → deterministic. */
KSAPI KsEngine* ks_engine_create(uint32_t seed);

KSAPI void ks_engine_destroy(KsEngine* eng);

/** Fixed physics step in seconds (default 0.001). */
KSAPI void ks_engine_set_timestep(KsEngine* eng, double dt);

KSAPI void ks_engine_reset(KsEngine* eng);

/** Apply driver input for subsequent steps. */
KSAPI void ks_engine_set_input(KsEngine* eng, const KsInput* in);

/** Advance simulation by dt seconds (may subdivide into fixed steps). */
KSAPI void ks_engine_step(KsEngine* eng, double dt);

/** Read primary vehicle state. */
KSAPI void ks_engine_get_state(const KsEngine* eng, KsVehicleState* out);

/** Optional: enable binary replay recording to path (empty = stop). */
KSAPI int ks_engine_set_replay_path(KsEngine* eng, const char* path_utf8);

/** Version string "major.minor.patch". */
KSAPI const char* ks_engine_version(void);

/* ==========================================================================
 * Session control (dedicated server / headless race authority).
 * ========================================================================== */

typedef struct KsSessionConfig {
    int32_t total_laps;
    int32_t time_limit_s;
    float   countdown_s;
    float   track_length_m;
} KsSessionConfig;

typedef enum KsSessionPhase {
    KS_PHASE_IDLE = 0,
    KS_PHASE_COUNTDOWN = 1,
    KS_PHASE_GREEN = 2,
    KS_PHASE_CHECKERED = 3
} KsSessionPhase;

KSAPI void ks_engine_session_configure(KsEngine* eng, const KsSessionConfig* cfg);
KSAPI void ks_engine_session_start(KsEngine* eng);
KSAPI KsSessionPhase ks_engine_session_phase(const KsEngine* eng);
KSAPI int32_t ks_engine_session_lap(const KsEngine* eng);
KSAPI double ks_engine_session_time(const KsEngine* eng);
KSAPI double ks_engine_session_remaining(const KsEngine* eng);

/* ==========================================================================
 * Scene bridge (roadmap 4.3): ksengine ECS scene <-> host.
 * ========================================================================== */

typedef uint32_t KsEntity;
#define KS_ENTITY_NULL 0xFFFFFFFFu

typedef struct KsSceneNode {
    KsEntity id;
    float position[3];
    float rotation[3];
    float scale[3];
    const char* name;
    const char* mesh;
} KsSceneNode;

KSAPI KsEntity ks_engine_scene_create(KsEngine* eng, const char* name);
KSAPI void ks_engine_scene_destroy(KsEngine* eng, KsEntity e);
KSAPI int ks_engine_scene_alive(const KsEngine* eng, KsEntity e);
KSAPI int ks_engine_scene_set_transform(KsEngine* eng, KsEntity e,
                                        const float pos[3], const float rot[3],
                                        const float scale[3]);
KSAPI int ks_engine_scene_set_mesh(KsEngine* eng, KsEntity e, const char* mesh_name);
KSAPI int ks_engine_scene_get(const KsEngine* eng, KsEntity e, KsSceneNode* out);
KSAPI int ks_engine_scene_snapshot(const KsEngine* eng, KsSceneNode* out, int max);
KSAPI uint64_t ks_engine_scene_revision(const KsEngine* eng);
KSAPI int ks_engine_scene_bind_vehicle(KsEngine* eng, KsEntity e);

/* ==========================================================================
 * Content load (roadmap E1).
 * ========================================================================== */

KSAPI int ks_engine_load_track(KsEngine* eng, const char* track_dir_utf8);
KSAPI int ks_engine_load_car(KsEngine* eng, const char* car_dir_utf8);
KSAPI const char* ks_engine_track_name(const KsEngine* eng);
KSAPI float ks_engine_track_length_m(const KsEngine* eng);
KSAPI const char* ks_engine_car_name(const KsEngine* eng);

/* ==========================================================================
 * Frame profiler (roadmap E9 / 2.14).
 * ========================================================================== */

KSAPI void ks_engine_profiler_set_enabled(int enabled);
KSAPI int  ks_engine_profiler_is_enabled(void);
KSAPI void ks_engine_profiler_begin_frame(void);
KSAPI void ks_engine_profiler_end_frame(void);
KSAPI void ks_engine_profiler_begin_section(const char* name);
KSAPI void ks_engine_profiler_end_section(const char* name);
KSAPI void ks_engine_profiler_report(const char* path_utf8_or_null);
KSAPI int  ks_engine_profiler_export_csv(const char* path_utf8);
KSAPI double ks_engine_profiler_frame_ms(void);
KSAPI int    ks_engine_profiler_fps(void);

#ifdef __cplusplus
}
#endif

#endif /* KSENGINE_C_H */
