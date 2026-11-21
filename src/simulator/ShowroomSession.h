#pragma once
/**
 * ShowroomSession — the showroom's display state as a session sees it.
 *
 * A drive session started inside the hall leaves the hall on screen, and the
 * light it was built with is the room's, not the track's: the environment maps
 * were baked for the room's sun, its haze was written for its radius, its stop
 * and its [COLOR] grade are its own. Everything the room decided for the frame
 * lives here, so the same state can be applied every frame while the hall is
 * the scene and handed back when a track replaces it.
 *
 * The renderer calls sit behind ShowroomDisplaySink: the game wires the Vulkan
 * renderer into it, tests/ksengine/showroom_session_test.cpp records them and
 * asserts the six channels. Header only on purpose — the test needs nothing
 * but the include path.
 */
#include "LocalLights.h"
#include "ShowroomConfig.h"

#include <cmath>
#include <vector>

namespace ks {
namespace sim {

/** The five values a room's [COLOR] block writes. NativeRenderer keeps them
 *  apart; they are one grade here, because a room turns them on and off
 *  together. */
struct ShowroomGrade {
    float filter[3] = {1.f, 1.f, 1.f};
    float saturation = 1.f;
    float contrast = 1.f;
    float hue = 0.f;
    float sepia = 0.f;
};

inline bool sameShowroomGrade(const ShowroomGrade& a, const ShowroomGrade& b) {
    const float eps = 1e-6f;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(a.filter[i] - b.filter[i]) > eps) return false;
    return std::fabs(a.saturation - b.saturation) <= eps &&
           std::fabs(a.contrast - b.contrast) <= eps &&
           std::fabs(a.hue - b.hue) <= eps && std::fabs(a.sepia - b.sepia) <= eps;
}

/** Where the session state goes. The game's implementation draws with it, the
 *  test records it. */
struct ShowroomDisplaySink {
    virtual ~ShowroomDisplaySink() = default;
    virtual void setSun(const float direction[3], const float color[3], float level) = 0;
    virtual void setLamps(const std::vector<LocalLight>& lamps) = 0;
    virtual void setFog(const float color[3], float density, float falloff) = 0;
    virtual void setExposure(float exposure) = 0;
    virtual void setIbl(bool enabled) = 0;
    virtual void setGrade(const ShowroomGrade& grade) = 0;
};

/** What the room asked the frame to look like. `sunLevel` is already resolved:
 *  a declared level is the room's own, an undeclared one is the engine's
 *  daylight (see ShowroomSessionState::levelFor). */
struct RoomDisplayState {
    float sunDirection[3] = {0.3f, -0.8f, 0.2f}; // where the light travels
    float sunColor[3] = {1.f, 0.97f, 0.92f};
    float sunLevel = 1.f;
    std::vector<LocalLight> lamps;
    float fogColor[3] = {0.34f, 0.36f, 0.40f};
    float fogDensity = 0.0001f;
    float fogFalloff = 0.002f;
    float exposure = 1.f;
    bool iblOn = true;
    ShowroomGrade grade;
    bool hasGrade = false; // the room carried a [COLOR] block
};

struct ShowroomSessionState {
    bool hallInScene = false;
    bool roomLightValid = false;
    RoomDisplayState room;
    // What the display carried before the room took over (the track's, which
    // is what a track that replaces the hall has to get back).
    ShowroomGrade gradeBefore;
    bool gradeSaved = false;
    float exposureBefore = 1.f;
    bool iblBefore = true;

    /** The room's light answers for the scene only while the hall is what the
     *  camera is in: a track that replaced the hall must never be lit by it. */
    bool ownsTheScene() const { return hallInScene && roomLightValid; }

    /** Did the room turn anything on that leaving the hall has to undo? */
    bool touched() const { return hallInScene || roomLightValid || room.hasGrade; }

    /** A room's level: its own when it declared one, the engine's daylight when
     *  it did not. ShowroomConfig zeroes sun_intensity for a room with no
     *  [LIGHT] block, and "nothing declared" is not "no sun". */
    static float levelFor(float declared, bool declaredFlag) {
        return declaredFlag ? declared : 1.0f;
    }

    /** The haze a room of this radius gets: its numbers were written for the
     *  12 m in-memory hall, so a venue 27 m across needs them scaled. */
    static float fogScaleFor(float hallRadius) {
        const float radius = hallRadius > 12.f ? hallRadius : 12.f;
        return 12.f / radius;
    }

    /** Records the room's [COLOR] block and, the first time, the grade it found
     *  (a room that opens again does not overwrite the pre-room grade with its
     *  own). */
    void captureGrade(const ShowroomGrade& roomGrade, const ShowroomGrade& current) {
        room.grade = roomGrade;
        room.hasGrade = true;
        if (!gradeSaved) {
            gradeBefore = current;
            gradeSaved = true;
        }
    }

    /** Every channel the room owns, for a frame in which it is the scene. */
    void applyTo(ShowroomDisplaySink& sink) const {
        if (!ownsTheScene()) return;
        sink.setSun(room.sunDirection, room.sunColor, room.sunLevel);
        sink.setLamps(room.lamps);
        sink.setFog(room.fogColor, room.fogDensity, room.fogFalloff);
        sink.setExposure(room.exposure);
        sink.setIbl(room.iblOn);
        if (room.hasGrade) sink.setGrade(room.grade);
    }

    /** A track replaced the hall: hand back what the room found, then forget it.
     *  The sun, the lamps and the haze need no call of their own — the weather
     *  branch writes the track's on the next frame. */
    void releaseTo(ShowroomDisplaySink& sink) {
        if (room.hasGrade && gradeSaved) sink.setGrade(gradeBefore);
        if (touched()) {
            sink.setExposure(exposureBefore);
            sink.setIbl(iblBefore);
        }
        reset();
    }

    void reset() {
        hallInScene = false;
        roomLightValid = false;
        room = RoomDisplayState{};
        gradeBefore = ShowroomGrade{};
        gradeSaved = false;
        exposureBefore = 1.f;
        iblBefore = true;
    }
};

/** The grade a room's own [COLOR] block describes: hue, saturation, contrast
 *  and sepia go straight to the display pass, while brightness and the
 *  white-balance pair are solved on the CPU into the three-float filter
 *  (ShowroomColor.h). Whether the block answers for the frame is the caller's
 *  captureGrade, not this — the console's own grade outranks the room's. */
inline ShowroomGrade gradeFromConfig(const ShowroomConfig& cfg) {
    ShowroomGrade grade;
    showroomColorFilter(cfg.colorBrightness, cfg.colorTemp, cfg.whiteBalance, grade.filter);
    grade.saturation = cfg.colorSaturation;
    grade.contrast = cfg.colorContrast;
    grade.hue = cfg.colorHueDeg;
    grade.sepia = cfg.colorSepia;
    return grade;
}

/** The display state a room's own documents describe: its sun (the file points
 *  at it, the renderer wants the way the light travels), its lamps, its haze
 *  for this hall radius and its [COLOR] values. The one mapping the menu's
 *  applyShowroomLight feeds the renderer, so a session inside the hall and the
 *  menu that opened it cannot disagree about what the room said.
 *
 *  hasGrade stays unset on purpose: turning the block on is captureGrade's job
 *  (it also saves what the display carried). The stop and the IBL choice are
 *  resolved by the caller too: the stop has a priority chain (console, room,
 *  preset, fallback) and the IBL depends on whether the hall is a photo. */
inline RoomDisplayState roomDisplayFromConfig(const ShowroomConfig& cfg, float hallRadius) {
    RoomDisplayState room;
    room.sunDirection[0] = -cfg.sunDirection[0];
    room.sunDirection[1] = -cfg.sunDirection[1];
    room.sunDirection[2] = -cfg.sunDirection[2];
    room.sunColor[0] = cfg.sunColor[0];
    room.sunColor[1] = cfg.sunColor[1];
    room.sunColor[2] = cfg.sunColor[2];
    room.sunLevel = ShowroomSessionState::levelFor(cfg.sunIntensity, cfg.lightDeclared);
    room.lamps.reserve(cfg.localLights.size());
    for (const ShowroomLocalLight& lamp : cfg.localLights) {
        LocalLight built;
        for (int axis = 0; axis < 3; ++axis) {
            built.position[axis] = lamp.position[axis];
            built.color[axis] = lamp.color[axis];
            built.direction[axis] = lamp.direction[axis];
        }
        built.range = lamp.range > 0.0f ? lamp.range : 12.0f;
        built.intensity = lamp.intensity;
        if (lamp.spot) {
            built.spot = true;
            built.cosOuter = std::cos(lamp.outerAngleDeg * 3.14159265358979f / 180.0f);
            built.cosInner = std::cos(lamp.innerAngleDeg * 3.14159265358979f / 180.0f);
        }
        room.lamps.push_back(built);
    }
    const float fogScale = ShowroomSessionState::fogScaleFor(hallRadius);
    room.fogColor[0] = cfg.ambientColor[0];
    room.fogColor[1] = cfg.ambientColor[1];
    room.fogColor[2] = cfg.ambientColor[2];
    room.fogDensity = 0.0001f * fogScale;
    room.fogFalloff = 0.002f * fogScale;
    room.grade = gradeFromConfig(cfg);
    return room;
}

} // namespace sim
} // namespace ks
