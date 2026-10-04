/**
 * C API implementation — thin wrapper around VehicleSimulator.
 * Headless: no window, no Qt, no Vulkan required for step/get_state.
 */
#include "ksengine_c.h"
#include "../physics/VehicleSimulator.h"
#include "../physics/ReplaySystem.h"
#include "../scene/Components.h"
#include "../scene/Registry.h"
#include <memory>
#include <string>
#include <cmath>
#include <cstring>

struct KsEngine {
    ks::physics::VehicleSimulator vehicle;
    ks::physics::ReplaySystem replay;
    KsInput input{};
    double fixedDt = 0.001;
    double accum = 0.0;
    double time = 0.0;
    uint32_t seed = 0;
    std::string replayPath;
    bool recording = false;
    KsSessionConfig sessionCfg{};
    KsSessionPhase phase = KS_PHASE_IDLE;
    double sessionTime = 0.0;
    double countdownLeft = 0.0;
    double lapDistance = 0.0;
    int32_t lap = 0;
    // Scene bridge (roadmap 4.3)
    ks::ecs::Registry scene;
    uint64_t sceneRevision = 0;
    KsEntity vehicleBind = KS_ENTITY_NULL;
};

namespace {

// Write the vehicle pose into the bound scene entity after each step.
// Bumps the scene revision only when the pose actually changed, so an idle
// engine does not churn the host's dirty counter.
void bridgeVehiclePose(KsEngine* eng) {
    if (eng->vehicleBind == KS_ENTITY_NULL) return;
    if (!eng->scene.valid(eng->vehicleBind)) return;
    auto* t = eng->scene.tryGet<ks::ecs::Transform>(eng->vehicleBind);
    if (!t) return;
    const auto st = eng->vehicle.getState();
    const float pos[3] = {static_cast<float>(st.position.x),
                          static_cast<float>(st.position.y),
                          static_cast<float>(st.position.z)};
    // Transform euler convention (see Components.h worldMatrix):
    // rotation.x = roll, rotation.y = pitch, rotation.z = yaw.
    const float rot[3] = {static_cast<float>(st.rotation.z),
                          static_cast<float>(st.rotation.x),
                          st.heading};
    const float eps = 1e-6f;
    const bool changed =
        std::fabs(t->position.x - pos[0]) > eps ||
        std::fabs(t->position.y - pos[1]) > eps ||
        std::fabs(t->position.z - pos[2]) > eps ||
        std::fabs(t->rotation.x - rot[0]) > eps ||
        std::fabs(t->rotation.y - rot[1]) > eps ||
        std::fabs(t->rotation.z - rot[2]) > eps;
    if (!changed) return;
    t->position = {pos[0], pos[1], pos[2]};
    t->rotation = {rot[0], rot[1], rot[2]};
    ++eng->sceneRevision;
}

void fillNode(ks::ecs::Registry& reg, ks::ecs::Entity e,
              const ks::ecs::Name& n, const ks::ecs::Transform& t,
              KsSceneNode* out) {
    out->id = e;
    out->position[0] = t.position.x;
    out->position[1] = t.position.y;
    out->position[2] = t.position.z;
    out->rotation[0] = t.rotation.x;
    out->rotation[1] = t.rotation.y;
    out->rotation[2] = t.rotation.z;
    out->scale[0] = t.scale.x;
    out->scale[1] = t.scale.y;
    out->scale[2] = t.scale.z;
    out->name = n.value.c_str();
    out->mesh = reg.has<ks::ecs::MeshInstance>(e)
                    ? reg.get<ks::ecs::MeshInstance>(e).meshName.c_str()
                    : "";
}

} // namespace

extern "C" {

KSAPI const char* ks_engine_version(void) {
    return "0.1.0";
}

KSAPI KsEngine* ks_engine_create(uint32_t seed) {
    auto* e = new KsEngine();
    e->seed = seed;
    e->vehicle.reset();
    e->vehicle.startSimulation();
    return e;
}

KSAPI void ks_engine_destroy(KsEngine* eng) {
    if (!eng) return;
    if (eng->recording && !eng->replayPath.empty())
        eng->replay.save(eng->replayPath);
    delete eng;
}

KSAPI void ks_engine_set_timestep(KsEngine* eng, double dt) {
    if (!eng) return;
    if (dt > 1e-5 && dt < 0.05)
        eng->fixedDt = dt;
}

KSAPI void ks_engine_reset(KsEngine* eng) {
    if (!eng) return;
    eng->vehicle.reset();
    eng->vehicle.startSimulation();
    eng->accum = 0;
    eng->time = 0;
    eng->replay.clear();
}

KSAPI void ks_engine_set_input(KsEngine* eng, const KsInput* in) {
    if (!eng || !in) return;
    eng->input = *in;
    eng->vehicle.setThrottle(in->throttle);
    eng->vehicle.setBrake(in->brake);
    eng->vehicle.setSteering(in->steer);
}

KSAPI void ks_engine_step(KsEngine* eng, double dt) {
    if (!eng || dt <= 0.0) return;
    eng->accum += dt;
    int guard = 0;
    while (eng->accum >= eng->fixedDt && guard++ < 64) {
        eng->vehicle.updatePhysics(eng->fixedDt);
        eng->time += eng->fixedDt;
        eng->accum -= eng->fixedDt;

        if (eng->phase == KS_PHASE_COUNTDOWN) {
            eng->countdownLeft -= eng->fixedDt;
            if (eng->countdownLeft <= 0.0) {
                eng->countdownLeft = 0.0;
                eng->sessionTime = 0.0;
                eng->phase = KS_PHASE_GREEN;
            }
        } else if (eng->phase == KS_PHASE_GREEN) {
            eng->sessionTime += eng->fixedDt;
            const float len = eng->sessionCfg.track_length_m;
            if (len > 0.0f) {
                eng->lapDistance += eng->vehicle.getState().speed * eng->fixedDt;
                const int32_t completed = static_cast<int32_t>(eng->lapDistance / len);
                if (completed > eng->lap) eng->lap = completed;
            }
            const bool lapsDone = eng->sessionCfg.total_laps > 0 &&
                                  eng->lap >= eng->sessionCfg.total_laps;
            const bool timeDone = eng->sessionCfg.time_limit_s > 0 &&
                                  eng->sessionTime >= eng->sessionCfg.time_limit_s;
            if (lapsDone || timeDone) eng->phase = KS_PHASE_CHECKERED;
        }

        if (eng->recording) {
            ks::physics::ReplayFrame f;
            f.time = eng->time;
            const auto st = eng->vehicle.getState();
            f.x = static_cast<float>(st.position.x);
            f.y = static_cast<float>(st.position.y);
            f.z = static_cast<float>(st.position.z);
            f.speed = static_cast<float>(st.speed);
            f.throttle = eng->input.throttle;
            f.brake = eng->input.brake;
            f.steer = eng->input.steer;
            f.gear = eng->vehicle.currentGear();
            eng->replay.recordFrame(f);
        }
    }
    // Scene bridge (roadmap 4.3): mirror the vehicle pose into the bound
    // entity so the host sees movement without polling ks_engine_get_state.
    bridgeVehiclePose(eng);
}

KSAPI void ks_engine_get_state(const KsEngine* eng, KsVehicleState* out) {
    if (!eng || !out) return;
    std::memset(out, 0, sizeof(*out));
    const auto st = eng->vehicle.getState();
    out->x = static_cast<float>(st.position.x);
    out->y = static_cast<float>(st.position.y);
    out->z = static_cast<float>(st.position.z);
    out->yaw = st.heading;
    out->pitch = static_cast<float>(st.rotation.x);
    out->roll = static_cast<float>(st.rotation.z);
    out->speed_ms = static_cast<float>(st.speed);
    out->rpm = static_cast<float>(eng->vehicle.rpm());
    out->gear = eng->vehicle.currentGear();
    out->throttle = eng->input.throttle;
    out->brake = eng->input.brake;
    out->steer = eng->input.steer;
    out->fuel_l = static_cast<float>(st.fuel);
    out->tyre_temp_fl = static_cast<float>(st.tyreTemp[0]);
    out->tyre_temp_fr = static_cast<float>(st.tyreTemp[1]);
    out->tyre_temp_rl = static_cast<float>(st.tyreTemp[2]);
    out->tyre_temp_rr = static_cast<float>(st.tyreTemp[3]);
}

KSAPI int ks_engine_set_replay_path(KsEngine* eng, const char* path_utf8) {
    if (!eng) return 0;
    if (!path_utf8 || !path_utf8[0]) {
        if (eng->recording && !eng->replayPath.empty())
            eng->replay.save(eng->replayPath);
        eng->recording = false;
        eng->replayPath.clear();
        eng->replay.setRecording(false);
        return 1;
    }
    eng->replayPath = path_utf8;
    eng->replay.clear();
    eng->replay.setRecording(true);
    eng->recording = true;
    return 1;
}

KSAPI void ks_engine_session_configure(KsEngine* eng, const KsSessionConfig* cfg) {
    if (!eng || !cfg) return;
    eng->sessionCfg = *cfg;
    eng->phase = KS_PHASE_IDLE;
    eng->sessionTime = 0;
    eng->countdownLeft = 0;
    eng->lapDistance = 0;
    eng->lap = 0;
}

KSAPI void ks_engine_session_start(KsEngine* eng) {
    if (!eng) return;
    eng->sessionTime = 0;
    eng->lapDistance = 0;
    eng->lap = 0;
    if (eng->sessionCfg.countdown_s > 0.0f) {
        eng->countdownLeft = eng->sessionCfg.countdown_s;
        eng->phase = KS_PHASE_COUNTDOWN;
    } else {
        eng->countdownLeft = 0;
        eng->phase = KS_PHASE_GREEN;
    }
}

KSAPI KsSessionPhase ks_engine_session_phase(const KsEngine* eng) {
    return eng ? eng->phase : KS_PHASE_IDLE;
}

KSAPI int32_t ks_engine_session_lap(const KsEngine* eng) {
    return eng ? eng->lap : 0;
}

KSAPI double ks_engine_session_time(const KsEngine* eng) {
    return eng ? eng->sessionTime : 0.0;
}

KSAPI double ks_engine_session_remaining(const KsEngine* eng) {
    if (!eng) return -1.0;
    if (eng->phase == KS_PHASE_COUNTDOWN) return eng->countdownLeft;
    if (eng->phase == KS_PHASE_GREEN && eng->sessionCfg.time_limit_s > 0)
        return eng->sessionCfg.time_limit_s - eng->sessionTime;
    return -1.0;
}

/* ==========================================================================
 * Scene bridge (roadmap 4.3)
 * ========================================================================== */

KSAPI KsEntity ks_engine_scene_create(KsEngine* eng, const char* name) {
    if (!eng) return KS_ENTITY_NULL;
    const ks::ecs::Entity e = eng->scene.create();
    if (e == ks::ecs::kNullEntity) return KS_ENTITY_NULL;
    eng->scene.emplace<ks::ecs::Name>(
        e, ks::ecs::Name{std::string(name ? name : "")});
    eng->scene.emplace<ks::ecs::Transform>(e);
    ++eng->sceneRevision;
    return e;
}

KSAPI void ks_engine_scene_destroy(KsEngine* eng, KsEntity e) {
    if (!eng) return;
    if (!eng->scene.valid(e)) return;
    eng->scene.destroy(e);
    if (eng->vehicleBind == e) eng->vehicleBind = KS_ENTITY_NULL;
    ++eng->sceneRevision;
}

KSAPI int ks_engine_scene_alive(const KsEngine* eng, KsEntity e) {
    return eng && eng->scene.valid(e) ? 1 : 0;
}

KSAPI int ks_engine_scene_set_transform(KsEngine* eng, KsEntity e,
                                        const float pos[3], const float rot[3],
                                        const float scale[3]) {
    if (!eng || !eng->scene.valid(e)) return 0;
    auto* t = eng->scene.tryGet<ks::ecs::Transform>(e);
    if (!t) return 0;
    if (pos) t->position = {pos[0], pos[1], pos[2]};
    if (rot) t->rotation = {rot[0], rot[1], rot[2]};
    if (scale) t->scale = {scale[0], scale[1], scale[2]};
    ++eng->sceneRevision;
    return 1;
}

KSAPI int ks_engine_scene_set_mesh(KsEngine* eng, KsEntity e, const char* mesh_name) {
    if (!eng || !eng->scene.valid(e)) return 0;
    if (!mesh_name || !mesh_name[0]) {
        eng->scene.remove<ks::ecs::MeshInstance>(e);
    } else {
        eng->scene.emplace<ks::ecs::MeshInstance>(
            e, ks::ecs::MeshInstance{std::string(mesh_name)});
    }
    ++eng->sceneRevision;
    return 1;
}

KSAPI int ks_engine_scene_get(const KsEngine* eng, KsEntity e, KsSceneNode* out) {
    if (!eng || !out || !eng->scene.valid(e)) return 0;
    // Logically const: the registry is only read (pool getters are
    // non-const on the engine's own member, no aliasing escape).
    auto& reg = const_cast<KsEngine*>(eng)->scene;
    auto* n = reg.tryGet<ks::ecs::Name>(e);
    auto* t = reg.tryGet<ks::ecs::Transform>(e);
    if (!n || !t) return 0;
    fillNode(reg, e, *n, *t, out);
    return 1;
}

KSAPI int ks_engine_scene_snapshot(const KsEngine* eng, KsSceneNode* out, int max) {
    if (!eng) return 0;
    auto& reg = const_cast<KsEngine*>(eng)->scene;
    // Pass 1: count (every scene entity carries Name + Transform).
    int total = 0;
    reg.each<ks::ecs::Name, ks::ecs::Transform>(
        [&](ks::ecs::Entity, const ks::ecs::Name&, const ks::ecs::Transform&) {
            ++total;
        });
    if (!out || max <= 0 || total > max) return total;
    // Pass 2: fill only when the whole scene fits the buffer.
    int i = 0;
    reg.each<ks::ecs::Name, ks::ecs::Transform>(
        [&](ks::ecs::Entity e, const ks::ecs::Name& n, const ks::ecs::Transform& t) {
            fillNode(reg, e, n, t, &out[i++]);
        });
    return total;
}

KSAPI uint64_t ks_engine_scene_revision(const KsEngine* eng) {
    return eng ? eng->sceneRevision : 0;
}

KSAPI int ks_engine_scene_bind_vehicle(KsEngine* eng, KsEntity e) {
    if (!eng) return 0;
    if (e == KS_ENTITY_NULL) {
        eng->vehicleBind = KS_ENTITY_NULL;
        ++eng->sceneRevision;
        return 1;
    }
    if (!eng->scene.valid(e)) return 0;
    eng->vehicleBind = e;
    ++eng->sceneRevision;
    return 1;
}

} // extern "C"
