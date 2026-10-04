/**
 * ksengine C API — headless / GDExtension / dedicated server entry points.
 * Qt-free. ABI intended to stay stable across minor versions.
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
 * State advances inside ks_engine_step() on simulated time.
 * ========================================================================== */

typedef struct KsSessionConfig {
    int32_t total_laps;     /* 0 = no lap limit            */
    int32_t time_limit_s;   /* 0 = no time limit           */
    float   countdown_s;    /* 0 = start immediately green */
    float   track_length_m; /* 0 = no lap counting         */
} KsSessionConfig;

typedef enum KsSessionPhase {
    KS_PHASE_IDLE = 0,
    KS_PHASE_COUNTDOWN = 1,
    KS_PHASE_GREEN = 2,
    KS_PHASE_CHECKERED = 3
} KsSessionPhase;

/** Store session config; resets phase to IDLE. */
KSAPI void ks_engine_session_configure(KsEngine* eng, const KsSessionConfig* cfg);

/** Begin the session (countdown if configured, else green). */
KSAPI void ks_engine_session_start(KsEngine* eng);

KSAPI KsSessionPhase ks_engine_session_phase(const KsEngine* eng);

/** Completed laps (distance-based; needs track_length_m > 0). */
KSAPI int32_t ks_engine_session_lap(const KsEngine* eng);

/** Session time in seconds (excludes countdown). */
KSAPI double ks_engine_session_time(const KsEngine* eng);

/** Countdown/time-limit seconds left; -1 when unlimited. */
KSAPI double ks_engine_session_remaining(const KsEngine* eng);

/* ==========================================================================
 * Scene bridge (roadmap 4.3): ksengine ECS scene <-> host (Godot).
 * The host (GDExtension layer) creates/populates entities and re-mirrors
 * them into its own node tree. ks_engine_scene_revision() is a monotonic
 * dirty counter: it is bumped by every scene mutation and by every step
 * that moved the bound vehicle entity, so the host can skip re-mirroring
 * when nothing changed.
 * ========================================================================== */

typedef uint32_t KsEntity;
#define KS_ENTITY_NULL 0xFFFFFFFFu

typedef struct KsSceneNode {
    KsEntity id;
    float position[3];
    float rotation[3]; /* euler radians: x = roll, y = pitch, z = yaw */
    float scale[3];
    const char* name;  /* valid until the next ks_engine_scene_* call */
    const char* mesh;  /* "" when the entity has no mesh; same lifetime */
} KsSceneNode;

/** Create a scene entity with identity transform; name may be NULL. */
KSAPI KsEntity ks_engine_scene_create(KsEngine* eng, const char* name);

/** Destroy an entity (its handle becomes permanently stale). */
KSAPI void ks_engine_scene_destroy(KsEngine* eng, KsEntity e);

/** 1 when the handle still refers to a live entity. */
KSAPI int ks_engine_scene_alive(const KsEngine* eng, KsEntity e);

/** Set position/rotation/scale; rot/scale arrays may be NULL (kept as-is). */
KSAPI int ks_engine_scene_set_transform(KsEngine* eng, KsEntity e,
                                        const float pos[3], const float rot[3],
                                        const float scale[3]);

/** Attach (or with NULL/"" detach) a mesh reference to the entity. */
KSAPI int ks_engine_scene_set_mesh(KsEngine* eng, KsEntity e, const char* mesh_name);

/** Read one entity back. Returns 0 for a stale/null handle. */
KSAPI int ks_engine_scene_get(const KsEngine* eng, KsEntity e, KsSceneNode* out);

/**
 * Enumerate every alive entity into out[0..max-1].
 * Returns the total alive count; when out is NULL or max is below that
 * count, nothing is written (query the size first, then reallocate).
 */
KSAPI int ks_engine_scene_snapshot(const KsEngine* eng, KsSceneNode* out, int max);

/** Monotonic dirty counter for cheap change detection. */
KSAPI uint64_t ks_engine_scene_revision(const KsEngine* eng);

/**
 * Bind the entity whose transform is written from the vehicle state at the
 * end of every ks_engine_step (KS_ENTITY_NULL unbinds). Returns 0 when the
 * handle is stale.
 */
KSAPI int ks_engine_scene_bind_vehicle(KsEngine* eng, KsEntity e);

#ifdef __cplusplus
}
#endif

#endif /* KSENGINE_C_H */
