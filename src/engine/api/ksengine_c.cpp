/**
 * C API implementation — thin wrapper around VehicleSimulator.
 * Headless: no window, no Qt, no Vulkan required for step/get_state.
 *
 * E1: load_track (data.json name/length), load_car (engine.ini).
 * E9: ks_engine_profiler_* → PhysicsProfiler (+ CSV export).
 */
#include "ksengine_c.h"
#include "../physics/VehicleSimulator.h"
#include "../physics/ReplaySystem.h"
#include "../scene/Components.h"
#include "../scene/Registry.h"
#include "../physics/PhysicsProfiler.h"
#include <memory>
#include <string>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cstdio>

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
    ks::ecs::Registry scene;
    uint64_t sceneRevision = 0;
    KsEntity vehicleBind = KS_ENTITY_NULL;
    std::string trackName;
    float trackLengthM = 0.f;
    std::string carName;
    std::string trackDir;
    std::string carDir;
};

namespace {

void bridgeVehiclePose(KsEngine* eng) {
    if (eng->vehicleBind == KS_ENTITY_NULL) return;
    if (!eng->scene.valid(eng->vehicleBind)) return;
    auto* t = eng->scene.tryGet<ks::ecs::Transform>(eng->vehicleBind);
    if (!t) return;
    const auto st = eng->vehicle.getState();
    const float pos[3] = {static_cast<float>(st.position.x),
                          static_cast<float>(st.position.y),
                          static_cast<float>(st.position.z)};
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

size_t jsonValuePos(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return std::string::npos;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return std::string::npos;
    ++pos;
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' ||
            text[pos] == '\r'))
        ++pos;
    return pos;
}

std::string jsonText(const std::string& text, const std::string& key) {
    size_t pos = jsonValuePos(text, key);
    if (pos == std::string::npos || pos >= text.size() || text[pos] != '"')
        return {};
    ++pos;
    std::string out;
    while (pos < text.size() && text[pos] != '"') {
        if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
        out.push_back(text[pos]);
        ++pos;
    }
    return out;
}

float jsonNumber(const std::string& text, const std::string& key) {
    size_t pos = jsonValuePos(text, key);
    if (pos == std::string::npos || pos >= text.size()) return 0.f;
    if (!(std::isdigit(static_cast<unsigned char>(text[pos])) ||
          text[pos] == '-' || text[pos] == '+'))
        return 0.f;
    const char* begin = text.c_str() + pos;
    char* end = nullptr;
    const float v = std::strtof(begin, &end);
    if (end == begin) return 0.f;
    return v < 0.f ? 0.f : v;
}

float iniFloat(const std::string& text, const char* key, float fallback) {
    const std::string k = std::string(key) + "=";
    size_t pos = 0;
    while (pos < text.size()) {
        size_t lineEnd = text.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string line = text.substr(pos, lineEnd - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i < line.size() && line[i] != ';' && line[i] != '#') {
            if (line.compare(i, k.size(), k) == 0)
                return static_cast<float>(std::atof(line.c_str() + i + k.size()));
        }
        pos = lineEnd + 1;
    }
    return fallback;
}

} // namespace

extern "C" {

KSAPI const char* ks_engine_version(void) { return "0.90"; }

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
    if (dt > 1e-5 && dt < 0.05) eng->fixedDt = dt;
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

KSAPI KsEntity ks_engine_scene_create(KsEngine* eng, const char* name) {
    if (!eng) return KS_ENTITY_NULL;
    const ks::ecs::Entity e = eng->scene.create();
    if (e == ks::ecs::kNullEntity) return KS_ENTITY_NULL;
    eng->scene.emplace<ks::ecs::Name>(e, ks::ecs::Name{std::string(name ? name : "")});
    eng->scene.emplace<ks::ecs::Transform>(e);
    ++eng->sceneRevision;
    return e;
}

KSAPI void ks_engine_scene_destroy(KsEngine* eng, KsEntity e) {
    if (!eng || !eng->scene.valid(e)) return;
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
    if (!mesh_name || !mesh_name[0]) eng->scene.remove<ks::ecs::MeshInstance>(e);
    else eng->scene.emplace<ks::ecs::MeshInstance>(e, ks::ecs::MeshInstance{std::string(mesh_name)});
    ++eng->sceneRevision;
    return 1;
}

KSAPI int ks_engine_scene_get(const KsEngine* eng, KsEntity e, KsSceneNode* out) {
    if (!eng || !out || !eng->scene.valid(e)) return 0;
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
    int total = 0;
    reg.each<ks::ecs::Name, ks::ecs::Transform>(
        [&](ks::ecs::Entity, const ks::ecs::Name&, const ks::ecs::Transform&) { ++total; });
    if (!out || max <= 0 || total > max) return total;
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

KSAPI int ks_engine_load_track(KsEngine* eng, const char* track_dir_utf8) {
    if (!eng || !track_dir_utf8 || !track_dir_utf8[0]) return 0;
    namespace fs = std::filesystem;
    const fs::path root(track_dir_utf8);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return 0;
    eng->trackDir = track_dir_utf8;
    eng->trackName = root.filename().string();
    eng->trackLengthM = 0.f;
    const fs::path candidates[] = {root / "data" / "data.json", root / "data.json"};
    for (const fs::path& c : candidates) {
        if (!fs::is_regular_file(c, ec)) continue;
        std::ifstream in(c);
        if (!in) continue;
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        if (text.find('{') == std::string::npos) continue;
        const std::string name = jsonText(text, "name");
        float len = jsonNumber(text, "length");
        if (len <= 1.f) len = jsonNumber(text, "length_m");
        if (!name.empty()) eng->trackName = name;
        if (len > 1.f) eng->trackLengthM = len;
        break;
    }
    if (eng->trackLengthM > 1.f)
        eng->sessionCfg.track_length_m = eng->trackLengthM;
    return 1;
}

KSAPI int ks_engine_load_car(KsEngine* eng, const char* car_dir_utf8) {
    if (!eng || !car_dir_utf8 || !car_dir_utf8[0]) return 0;
    namespace fs = std::filesystem;
    const fs::path root(car_dir_utf8);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return 0;
    eng->carDir = car_dir_utf8;
    eng->carName = root.filename().string();
    const fs::path iniCandidates[] = {root / "data" / "engine.ini", root / "engine.ini"};
    for (const fs::path& c : iniCandidates) {
        if (!fs::is_regular_file(c, ec)) continue;
        std::ifstream in(c);
        if (!in) continue;
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        const float power = iniFloat(text, "POWER", -1.f);
        const float mass = iniFloat(text, "MASS", -1.f);
        const float maxRpm = iniFloat(text, "MAX_RPM", -1.f);
        const float fuelCap = iniFloat(text, "FUEL_CAPACITY", -1.f);
        if (mass > 50.f) eng->vehicle.setMass(mass);
        if (power > 10.f) eng->vehicle.setEnginePower(power);
        if (maxRpm > 1000.f) eng->vehicle.setMaxRpm(maxRpm);
        if (fuelCap > 1.f) eng->vehicle.setFuelTankCapacity(fuelCap);
        break;
    }
    return 1;
}

KSAPI const char* ks_engine_track_name(const KsEngine* eng) {
    return eng ? eng->trackName.c_str() : "";
}
KSAPI float ks_engine_track_length_m(const KsEngine* eng) {
    return eng ? eng->trackLengthM : 0.f;
}
KSAPI const char* ks_engine_car_name(const KsEngine* eng) {
    return eng ? eng->carName.c_str() : "";
}

KSAPI void ks_engine_profiler_set_enabled(int enabled) {
    ks::physics::PhysicsProfiler::instance().setEnabled(enabled != 0);
}
KSAPI int ks_engine_profiler_is_enabled(void) {
    return ks::physics::PhysicsProfiler::instance().isEnabled() ? 1 : 0;
}
KSAPI void ks_engine_profiler_begin_frame(void) {
    ks::physics::PhysicsProfiler::instance().beginFrame();
}
KSAPI void ks_engine_profiler_end_frame(void) {
    ks::physics::PhysicsProfiler::instance().endFrame();
}
KSAPI void ks_engine_profiler_begin_section(const char* name) {
    if (name) ks::physics::PhysicsProfiler::instance().beginSection(name);
}
KSAPI void ks_engine_profiler_end_section(const char* name) {
    if (name) ks::physics::PhysicsProfiler::instance().endSection(name);
}
KSAPI void ks_engine_profiler_report(const char* path_utf8_or_null) {
    auto& p = ks::physics::PhysicsProfiler::instance();
    if (path_utf8_or_null && path_utf8_or_null[0]) {
        FILE* f = std::fopen(path_utf8_or_null, "w");
        if (f) { p.report(f); std::fclose(f); return; }
    }
    p.report(stderr);
}
KSAPI int ks_engine_profiler_export_csv(const char* path_utf8) {
    if (!path_utf8 || !path_utf8[0]) return 0;
    FILE* f = std::fopen(path_utf8, "w");
    if (!f) return 0;
    auto& prof = ks::physics::PhysicsProfiler::instance();
    std::fprintf(f, "name,total_ms,avg_us,hits\n");
    for (const auto& s : prof.sectionsSorted()) {
        std::fprintf(f, "\"%s\",%.6f,%.3f,%d\n",
                     s.name.c_str(), s.totalMs, s.avgUs, s.hits);
    }
    std::fprintf(f, "\"__frame__\",%.6f,0,1\n", prof.frameTimeMs());
    if (prof.gpuFrameTimeMs() > 0.0)
        std::fprintf(f, "\"__gpu_frame__\",%.6f,0,1\n", prof.gpuFrameTimeMs());
    std::fclose(f);
    return 1;
}
KSAPI double ks_engine_profiler_frame_ms(void) {
    return ks::physics::PhysicsProfiler::instance().frameTimeMs();
}
KSAPI int ks_engine_profiler_fps(void) {
    return ks::physics::PhysicsProfiler::instance().fps();
}
KSAPI double ks_engine_profiler_gpu_ms(void) {
    return ks::physics::PhysicsProfiler::instance().gpuFrameTimeMs();
}
KSAPI double ks_engine_profiler_gpu_avg_ms(void) {
    return ks::physics::PhysicsProfiler::instance().gpuAvgFrameTimeMs();
}
KSAPI int ks_engine_profiler_gpu_fps(void) {
    return ks::physics::PhysicsProfiler::instance().gpuFps();
}

} /* extern "C" */
