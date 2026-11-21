/**
 * ShowroomSession — the display state a session keeps while the hall is what the
 * camera is in (src/simulator/ShowroomSession.h).
 *
 * This pins, as a test, the thing a capture found by eye twice: the room's sun,
 * its lamps, its haze, its stop, its IBL choice and its [COLOR] grade answer for
 * the frame only while the hall is the scene, and a track that replaces the hall
 * gets the display back — the grade the room found, not a neutral guess.
 * Header only plus a recording sink: no Vulkan, no content, no window.
 */
#include "KsTest.h"
#include "simulator/ShowroomSession.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using ks::sim::LocalLight;
using ks::sim::ShowroomDisplaySink;
using ks::sim::ShowroomGrade;
using ks::sim::ShowroomSessionState;
using ks::sim::sameShowroomGrade;

/** Everything the session state can ask of the display, recorded. */
struct RecordingSink : ShowroomDisplaySink {
    int sun = 0, lamps = 0, fog = 0, exposure = 0, ibl = 0, grade = 0;
    float sunDirection[3] = {0.f, 0.f, 0.f};
    float sunColor[3] = {0.f, 0.f, 0.f};
    float sunLevel = 0.f;
    std::size_t lampCount = 0;
    float fogColor[3] = {0.f, 0.f, 0.f};
    float fogDensity = 0.f;
    float fogFalloff = 0.f;
    float exposureValue = 0.f;
    bool iblEnabled = false;
    ShowroomGrade gradeValue;

    void setSun(const float direction[3], const float color[3], float level) override {
        ++sun;
        for (int i = 0; i < 3; ++i) {
            sunDirection[i] = direction[i];
            sunColor[i] = color[i];
        }
        sunLevel = level;
    }
    void setLamps(const std::vector<LocalLight>& lights) override {
        ++lamps;
        lampCount = lights.size();
    }
    void setFog(const float color[3], float density, float falloff) override {
        ++fog;
        for (int i = 0; i < 3; ++i) fogColor[i] = color[i];
        fogDensity = density;
        fogFalloff = falloff;
    }
    void setExposure(float value) override {
        ++exposure;
        exposureValue = value;
    }
    void setIbl(bool enabled) override {
        ++ibl;
        iblEnabled = enabled;
    }
    void setGrade(const ShowroomGrade& value) override {
        ++grade;
        gradeValue = value;
    }
};

LocalLight lamp(float x, float y, float z, float intensity) {
    LocalLight light;
    light.position[0] = x;
    light.position[1] = y;
    light.position[2] = z;
    light.color[0] = 1.f;
    light.color[1] = 0.2f;
    light.color[2] = 0.1f;
    light.intensity = intensity;
    light.range = 30.f;
    return light;
}

/** What the display carried before any room opened: the ppfilter's grade. */
ShowroomGrade ppfilterGrade() {
    ShowroomGrade grade;
    grade.filter[0] = 0.98f;
    grade.filter[1] = 1.0f;
    grade.filter[2] = 1.02f;
    return grade;
}

/** The [COLOR] block of the asr room: hue 10, sat 0.70, contrast 1.10, sepia 0.10. */
ShowroomGrade asrGrade() {
    ShowroomGrade grade;
    grade.saturation = 0.70f;
    grade.contrast = 1.10f;
    grade.hue = 10.f;
    grade.sepia = 0.10f;
    return grade;
}

/** A session in a room of the asr shape: no [LIGHT], one lamp, its haze, its
 *  stop, the IBL off, a [COLOR] block. */
ShowroomSessionState asrSession() {
    ShowroomSessionState session;
    session.hallInScene = true;
    session.roomLightValid = true;
    const float fogScale = ShowroomSessionState::fogScaleFor(27.0f);
    session.room.sunDirection[0] = -0.42f;
    session.room.sunDirection[1] = -0.78f;
    session.room.sunDirection[2] = -0.46f;
    session.room.sunColor[0] = 1.f;
    session.room.sunColor[1] = 0.97f;
    session.room.sunColor[2] = 0.92f;
    // No [LIGHT] block: the loader's sun_intensity is 0 and the level stays the
    // engine's daylight.
    session.room.sunLevel = ShowroomSessionState::levelFor(0.0f, false);
    session.room.lamps.push_back(lamp(0.f, 3.f, 0.f, 8.f));
    session.room.fogColor[0] = 0.34f;
    session.room.fogColor[1] = 0.36f;
    session.room.fogColor[2] = 0.40f;
    session.room.fogDensity = 0.0001f * fogScale;
    session.room.fogFalloff = 0.002f * fogScale;
    session.room.exposure = 0.90f;
    session.room.iblOn = false;
    session.exposureBefore = 1.f; // the track's stop
    session.iblBefore = true;     // the track's sky
    session.captureGrade(asrGrade(), ppfilterGrade());
    return session;
}

} // namespace

int main() {
    // --- the level rule, in one place for the menu and the session ---------
    KS_CHECK_NEAR(ShowroomSessionState::levelFor(1.25f, true), 1.25f, 1e-6f);
    KS_CHECK_NEAR(ShowroomSessionState::levelFor(0.0f, true), 0.0f, 1e-6f);
    KS_CHECK_NEAR(ShowroomSessionState::levelFor(0.0f, false), 1.0f, 1e-6f);
    // --- the haze scale: the room's numbers were written for the 12 m hall --
    KS_CHECK_NEAR(ShowroomSessionState::fogScaleFor(27.0f), 12.0f / 27.0f, 1e-6f);
    KS_CHECK_NEAR(ShowroomSessionState::fogScaleFor(6.0f), 1.0f, 1e-6f);

    // --- while the hall is the scene, all six channels are the room's ------
    ShowroomSessionState session = asrSession();
    const float fogScale = ShowroomSessionState::fogScaleFor(27.0f);
    KS_CHECK(session.ownsTheScene());
    KS_CHECK(session.touched());

    RecordingSink hall;
    session.applyTo(hall);
    KS_CHECK(hall.sun == 1);
    KS_CHECK_NEAR(hall.sunLevel, 1.0f, 1e-6f); // no [LIGHT] -> daylight
    KS_CHECK_NEAR(hall.sunDirection[1], -0.78f, 1e-6f);
    KS_CHECK_NEAR(hall.sunColor[1], 0.97f, 1e-6f);
    KS_CHECK(hall.lamps == 1 && hall.lampCount == 1);
    KS_CHECK(hall.fog == 1);
    KS_CHECK_NEAR(hall.fogDensity, 0.0001f * fogScale, 1e-9f);
    KS_CHECK_NEAR(hall.fogFalloff, 0.002f * fogScale, 1e-9f);
    KS_CHECK(hall.exposure == 1);
    KS_CHECK_NEAR(hall.exposureValue, 0.90f, 1e-6f);
    KS_CHECK(hall.ibl == 1 && !hall.iblEnabled);
    KS_CHECK(hall.grade == 1);
    KS_CHECK(sameShowroomGrade(hall.gradeValue, asrGrade()));

    // The session runs this every frame: applying twice is the same frame, not
    // a state that drifts (a setter that accumulated would show here).
    RecordingSink again;
    session.applyTo(again);
    KS_CHECK(again.sun == 1 && again.lamps == 1 && again.fog == 1);
    KS_CHECK(again.exposure == 1 && again.ibl == 1 && again.grade == 1);
    KS_CHECK(sameShowroomGrade(again.gradeValue, asrGrade()));

    // --- a track replaces the hall: the display goes back, the room stops --
    RecordingSink track;
    session.releaseTo(track);
    KS_CHECK(track.grade == 1);
    KS_CHECK(sameShowroomGrade(track.gradeValue, ppfilterGrade()));
    KS_CHECK(track.exposure == 1);
    KS_CHECK_NEAR(track.exposureValue, 1.0f, 1e-6f);
    KS_CHECK(track.ibl == 1 && track.iblEnabled);
    KS_CHECK(!session.ownsTheScene());
    KS_CHECK(!session.touched());

    // Nothing of the room can reach the track's frame any more — the sun, the
    // lamps and the haze are the weather branch's from here on.
    RecordingSink frameAfter;
    session.applyTo(frameAfter);
    KS_CHECK(frameAfter.sun == 0 && frameAfter.lamps == 0 && frameAfter.fog == 0);
    KS_CHECK(frameAfter.exposure == 0 && frameAfter.ibl == 0 && frameAfter.grade == 0);

    // --- a room with no [COLOR] applies no grade and restores none ---------
    ShowroomSessionState plain;
    plain.hallInScene = true;
    plain.roomLightValid = true;
    plain.room.sunLevel = ShowroomSessionState::levelFor(0.0f, false);
    RecordingSink plainSink;
    plain.applyTo(plainSink);
    KS_CHECK_NEAR(plainSink.sunLevel, 1.0f, 1e-6f);
    KS_CHECK(plainSink.grade == 0);
    RecordingSink plainTrack;
    plain.releaseTo(plainTrack);
    KS_CHECK(plainTrack.grade == 0);
    KS_CHECK(plainTrack.exposure == 1 && plainTrack.ibl == 1);

    // --- a second room must not overwrite the pre-room grade ---------------
    ShowroomSessionState twoRooms;
    twoRooms.hallInScene = true;
    twoRooms.captureGrade(asrGrade(), ppfilterGrade());
    ShowroomGrade other = asrGrade();
    other.saturation = 1.2f;
    twoRooms.captureGrade(other, asrGrade()); // what the display carries now
    RecordingSink restored;
    twoRooms.releaseTo(restored);
    KS_CHECK(sameShowroomGrade(restored.gradeValue, ppfilterGrade()));

    // --- the level the room declared is the one that reaches the frame -----
    ShowroomSessionState declared;
    declared.hallInScene = true;
    declared.roomLightValid = true;
    declared.room.sunLevel = ShowroomSessionState::levelFor(1.25f, true);
    RecordingSink declaredSink;
    declared.applyTo(declaredSink);
    KS_CHECK_NEAR(declaredSink.sunLevel, 1.25f, 1e-6f);

    return KS_TEST_RESULT("showroom_session_test");
}
