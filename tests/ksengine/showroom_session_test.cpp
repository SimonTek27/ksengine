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
#include "simulator/ShowroomConfig.h"
#include "simulator/ShowroomSession.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using ks::sim::LocalLight;
using ks::sim::RoomDisplayState;
using ks::sim::ShowroomConfig;
using ks::sim::ShowroomDisplaySink;
using ks::sim::ShowroomGrade;
using ks::sim::ShowroomSessionState;
using ks::sim::gradeFromConfig;
using ks::sim::loadShowroom;
using ks::sim::roomDisplayFromConfig;
using ks::sim::sameShowroomGrade;

namespace fs = std::filesystem;

/** Everything the session state can ask of the display, recorded. */
struct RecordingSink : ShowroomDisplaySink {
    int sun = 0, lamps = 0, fog = 0, exposure = 0, ibl = 0, grade = 0;
    float sunDirection[3] = {0.f, 0.f, 0.f};
    float sunColor[3] = {0.f, 0.f, 0.f};
    float sunLevel = 0.f;
    std::size_t lampCount = 0;
    float firstLampIntensity = 0.f;
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
        firstLampIntensity = lights.empty() ? 0.f : lights.front().intensity;
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

    // --- the room arrives from a real showroom.json on disk ----------------
    // Two scratch rooms, read by loadShowroom exactly as the game reads them:
    // one with no light block (the engine's daylight answers for it) carrying
    // the asr [COLOR] numbers, one with a declared sun of 1.25, one lamp and
    // no post-effects at all.
    const fs::path scratch = fs::temp_directory_path() / "ks_showroom_session_test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    auto writeText = [](const fs::path& path, const char* text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    };
    fs::create_directories(scratch / "no_light", ec);
    writeText(scratch / "no_light" / "showroom.json",
              "{\"name\":\"No Light Hall\","
              "\"description\":\"a hall with no light block\"}");
    writeText(scratch / "no_light" / "ppeffects.ini",
              "[TONEMAPPING]\n"
              "EXPOSURE=0.90\n"
              "[COLOR]\n"
              "HUE=10\n"
              "SATURATION=0.7\n"
              "CONTRAST=1.1\n"
              "SEPIA=0.1\n");
    fs::create_directories(scratch / "with_light", ec);
    writeText(scratch / "with_light" / "showroom.json",
              "{\"name\":\"Lit Hall\","
              "\"description\":\"a hall with a declared sun\","
              "\"light\":{\"sun_direction\":[0.42,0.78,0.46],"
              "\"sun_color\":[1.0,0.97,0.92],\"sun_intensity\":1.25,"
              "\"ambient_color\":[0.34,0.36,0.40],\"ambient_intensity\":0.55},"
              "\"lights\":[{\"position\":[0.0,3.0,0.0],\"color\":[1.0,0.2,0.1],"
              "\"intensity\":8.0,\"range\":30.0}]}");

    // The one mapping under test: documents in, display state out. The stop
    // the room declares is its own [TONEMAPPING] number here — the preset and
    // the console outrank it in the game, but neither is a room document.
    auto sessionFromRoom = [](const ShowroomConfig& cfg, float hallRadius,
                              const ShowroomGrade& before) {
        ShowroomSessionState session;
        session.hallInScene = true;
        session.roomLightValid = true;
        session.room = roomDisplayFromConfig(cfg, hallRadius);
        session.room.exposure = cfg.exposure > 0.0f ? cfg.exposure : 1.0f;
        session.exposureBefore = 1.0f;
        session.iblBefore = true;
        if (cfg.colorDeclared) session.captureGrade(session.room.grade, before);
        return session;
    };

    std::string error;
    // --- no light block: daylight, the room's haze, stop and [COLOR] -------
    const ShowroomConfig bare = loadShowroom((scratch / "no_light").string(), &error);
    KS_CHECK(bare.valid);
    KS_CHECK(!bare.lightDeclared);
    KS_CHECK_NEAR(bare.sunIntensity, 0.0f, 1e-6f);
    KS_CHECK(bare.colorDeclared);
    KS_CHECK_NEAR(bare.exposure, 0.90f, 1e-6f);
    ShowroomSessionState bareSession = sessionFromRoom(bare, 27.0f, ppfilterGrade());
    KS_CHECK(bareSession.ownsTheScene());
    RecordingSink bareSink;
    bareSession.applyTo(bareSink);
    KS_CHECK_NEAR(bareSink.sunLevel, 1.0f, 1e-6f); // no [LIGHT] -> daylight
    KS_CHECK_NEAR(bareSink.sunDirection[0], -0.42f, 1e-6f);
    KS_CHECK_NEAR(bareSink.sunDirection[1], -0.78f, 1e-6f);
    KS_CHECK_NEAR(bareSink.fogDensity, 0.0001f * 12.0f / 27.0f, 1e-9f);
    KS_CHECK_NEAR(bareSink.exposureValue, 0.90f, 1e-6f);
    KS_CHECK(bareSink.grade == 1);
    KS_CHECK_NEAR(bareSink.gradeValue.saturation, 0.70f, 1e-6f);
    KS_CHECK_NEAR(bareSink.gradeValue.contrast, 1.10f, 1e-6f);
    KS_CHECK_NEAR(bareSink.gradeValue.hue, 10.0f, 1e-6f);
    KS_CHECK_NEAR(bareSink.gradeValue.sepia, 0.10f, 1e-6f);
    KS_CHECK_NEAR(bareSink.gradeValue.filter[0], 1.0f, 1e-6f); // no temps
    RecordingSink bareTrack;
    bareSession.releaseTo(bareTrack);
    KS_CHECK(sameShowroomGrade(bareTrack.gradeValue, ppfilterGrade()));
    KS_CHECK_NEAR(bareTrack.exposureValue, 1.0f, 1e-6f);

    // --- declared sun: its level, its lamp, and no grade anywhere ----------
    const ShowroomConfig lit = loadShowroom((scratch / "with_light").string(), &error);
    KS_CHECK(lit.valid);
    KS_CHECK(lit.lightDeclared);
    KS_CHECK_NEAR(lit.sunIntensity, 1.25f, 1e-6f);
    ShowroomSessionState litSession = sessionFromRoom(lit, 27.0f, ppfilterGrade());
    RecordingSink litSink;
    litSession.applyTo(litSink);
    KS_CHECK_NEAR(litSink.sunLevel, 1.25f, 1e-6f);
    KS_CHECK(litSink.lampCount == 1);
    KS_CHECK_NEAR(litSink.firstLampIntensity, 8.0f, 1e-6f);
    KS_CHECK(litSink.grade == 0); // no ppeffects -> no [COLOR]
    RecordingSink litTrack;
    litSession.releaseTo(litTrack);
    KS_CHECK(litTrack.grade == 0);

    fs::remove_all(scratch, ec);

    return KS_TEST_RESULT("showroom_session_test");
}
