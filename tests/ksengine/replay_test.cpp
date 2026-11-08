/**
 * replay_test — the replay recorder's playback clock (GAP_MATRIX P0.5).
 *
 * ReplayRecorder had load/save and playback accessors, but updatePlayback(),
 * seekTo(), seekProgress() and isFinished() were declared and never defined, so
 * nothing could drive a playhead and the class could not even link when used.
 * This pins the behaviour the loop relies on: interpolation by time, speed
 * scaling, clamping seeks, the finish callback firing exactly once, and a
 * truncated file being refused instead of half-loaded into the frame vector.
 */
#include "ReplayRecorder.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

static int g_fails = 0;

static void check(bool ok, const char* what, int line) {
    if (ok) return;
    ++g_fails;
    std::printf("FAIL %s:%d  %s\n", __FILE__, line, what);
}
#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_NEAR(a, b, eps) check(std::fabs((a) - (b)) <= (eps), #a " ~= " #b, __LINE__)

namespace {

void putF(std::FILE* f, float v) { std::fwrite(&v, sizeof(v), 1, f); }
void putI(std::FILE* f, int v) { std::fwrite(&v, sizeof(v), 1, f); }

// Same 52-byte layout ReplayRecorder::saveReplay writes.
void putFrame(std::FILE* f, float t, float x, float speed, int gear) {
    putF(f, t);
    putF(f, x); putF(f, 0.f); putF(f, 0.f);    // position
    putF(f, 0.f); putF(f, 0.f); putF(f, 0.f);  // rotation
    putF(f, speed); putF(f, 1200.f); putI(f, gear);
    putF(f, 0.6f); putF(f, 0.f); putF(f, 0.2f); // throttle / brake / steering
}

/** Writes a replay whose header may lie about the frame count on purpose. */
bool writeReplay(const std::string& path, int declaredCount, int realFrames) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::uint32_t magic = 0x52504C59u;
    const std::uint32_t version = 1u;
    const std::uint32_t count = static_cast<std::uint32_t>(declaredCount);
    std::fwrite(&magic, sizeof(magic), 1, f);
    std::fwrite(&version, sizeof(version), 1, f);
    std::fwrite(&count, sizeof(count), 1, f);
    for (int i = 0; i < realFrames; ++i) {
        const float t = static_cast<float>(i);
        const float x = (i == 0) ? 0.f : (i == 1 ? 10.f : 30.f);
        putFrame(f, t, x, 20.f + static_cast<float>(i), i + 2);
    }
    std::fclose(f);
    return true;
}

void drop(const std::string& path) { std::remove(path.c_str()); }

} // namespace

int main() {
    const std::string path = "replay_test.ksrep";
    const std::string copyPath = "replay_test_copy.ksrep";
    const std::string brokenPath = "replay_test_broken.ksrep";
    const std::string emptyPath = "replay_test_empty.ksrep";

    CHECK(writeReplay(path, 3, 3));
    ks::sim::ReplayRecorder rec;
    CHECK(rec.loadReplay(path));
    CHECK(rec.frameCount() == 3);
    CHECK(rec.hasFrames());
    CHECK_NEAR(rec.duration(), 2.0f, 1e-5f);
    CHECK(!rec.isPlaying());
    CHECK(!rec.isFinished());

    // Play from the start: the frame under the playhead is interpolated.
    rec.startPlayback();
    CHECK(rec.isPlaying());
    rec.updatePlayback(0.5f);
    CHECK_NEAR(rec.playbackTime(), 0.5f, 1e-4f);
    CHECK_NEAR(rec.getPlaybackFrame().position.x, 5.0f, 1e-3f); // 0 -> 10
    CHECK(rec.getPlaybackFrame().gear == 3);                    // nearest keyframe

    // Playback speed scales the wall clock it is fed.
    rec.setPlaybackSpeed(2.0f);
    rec.updatePlayback(0.25f); // +0.5 s
    CHECK_NEAR(rec.playbackTime(), 1.0f, 1e-4f);
    CHECK_NEAR(rec.getPlaybackFrame().position.x, 10.0f, 1e-3f);

    // Seeking clamps to the recording, and a paused player still answers.
    rec.seekTo(100.0f);
    CHECK_NEAR(rec.playbackTime(), 2.0f, 1e-4f);
    CHECK(rec.isFinished());
    CHECK_NEAR(rec.getPlaybackFrame().position.x, 30.0f, 1e-3f);
    rec.seekProgress(0.25f);
    CHECK_NEAR(rec.playbackTime(), 0.5f, 1e-4f);
    CHECK(!rec.isFinished());

    // Running off the end stops the player and reports it exactly once.
    int finished = 0;
    rec.onPlaybackFinished = [&finished]() { ++finished; };
    rec.setPlaybackSpeed(1.0f);
    rec.startPlayback();
    rec.updatePlayback(5.0f);
    CHECK(!rec.isPlaying());
    CHECK(finished == 1);
    rec.updatePlayback(5.0f); // no-op while stopped
    CHECK(finished == 1);

    // Speed 0 freezes the playhead without stopping playback.
    rec.startPlayback();
    rec.setPlaybackSpeed(0.0f);
    rec.updatePlayback(1.0f);
    CHECK(rec.isPlaying());
    CHECK_NEAR(rec.playbackTime(), 0.0f, 1e-6f);
    rec.setPlaybackSpeed(1.0f);
    rec.stopPlayback();
    CHECK(!rec.isPlaying());

    // Round trip: what was loaded is what a save writes.
    CHECK(rec.saveReplay(copyPath));
    ks::sim::ReplayRecorder copyRec;
    CHECK(copyRec.loadReplay(copyPath));
    CHECK(copyRec.frameCount() == rec.frameCount());
    CHECK_NEAR(copyRec.duration(), rec.duration(), 1e-5f);

    // Saving creates the folder it needs: on a clean install nothing has made
    // replay/ yet, and the control API must still be able to save there.
    const std::string nested = "replay_test_missing/sub/run.ksrep";
    CHECK(rec.saveReplay(nested));
    ks::sim::ReplayRecorder nestedRec;
    CHECK(nestedRec.loadReplay(nested));
    CHECK(nestedRec.frameCount() == rec.frameCount());
    std::filesystem::remove_all("replay_test_missing");

    // A truncated file is refused, and the frames already loaded survive.
    CHECK(writeReplay(brokenPath, 5, 2)); // header claims 5, file holds 2
    CHECK(!copyRec.loadReplay(brokenPath));
    CHECK(copyRec.frameCount() == 3);

    // An empty recording is valid but cannot play.
    CHECK(writeReplay(emptyPath, 0, 0));
    ks::sim::ReplayRecorder emptyRec;
    CHECK(emptyRec.loadReplay(emptyPath));
    CHECK(emptyRec.frameCount() == 0);
    CHECK(emptyRec.isFinished());
    emptyRec.startPlayback();
    CHECK(!emptyRec.isPlaying());

    drop(path);
    drop(copyPath);
    drop(brokenPath);
    drop(emptyPath);

    if (g_fails == 0) {
        std::printf("replay_test: OK\n");
        return 0;
    }
    std::printf("replay_test: %d failure(s)\n", g_fails);
    return 1;
}
