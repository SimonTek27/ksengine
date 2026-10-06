// test_audio_render — roadmap 1.1 "audio base": renders SimulatorAudio
// headless (initialize(false), no WASAPI device) and checks that every
// telemetry-driven layer actually produces signal when its input arrives,
// that the silent floor stays silent, and that the mix is finite and
// inside [-1, 1].
//
// The engine layer always hums (idle model at rpm 0), so it is muted with
// setEngineVolume(0) to give the wind/skid/brake differential checks a
// quiet floor. All render paths are deterministic, so the comparisons
// below are stable run to run.

#include "KsTest.h"
#include "SimulatorAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using ks::sim::SimulatorAudio;

namespace {

constexpr int kFrames = 1024;
constexpr int kRate = 44100;

struct Params {
    float rpm = 0, throttle = 0, brake = 0, speed = 0, steering = 0;
    int gear = 1;
    float slipRatio = 0, slipAngle = 0, brakeTemp = 0, damage = 0;
    float wetness = 0, rain = 0;
};

struct Measurement {
    double rms = 0;
    float peak = 0;
    bool finite = true;
};

Measurement measure(SimulatorAudio& audio, const Params& p, const char* label)
{
    audio.updatePhysics(p.rpm, p.throttle, p.brake, p.speed, p.steering,
                        p.gear, false, p.slipRatio, p.slipAngle,
                        SimulatorAudio::SurfaceType::Asphalt,
                        p.damage, p.brakeTemp, 0.0f, p.speed, p.wetness,
                        p.rain, 1.0f / 60.0f);
    std::vector<float> buf(static_cast<size_t>(kFrames) * 2, 0.0f);
    audio.renderOffline(buf.data(), kFrames, 2, kRate);

    Measurement m;
    double acc = 0.0;
    for (float v : buf) {
        if (!std::isfinite(v)) m.finite = false;
        acc += static_cast<double>(v) * v;
        m.peak = std::max(m.peak, std::fabs(v));
    }
    m.rms = std::sqrt(acc / static_cast<double>(buf.size()));
    std::printf("  %-28s rms=%.5f peak=%.3f%s\n", label, m.rms, m.peak,
                m.finite ? "" : " NON-FINITE");
    return m;
}

} // namespace

int main()
{
    SimulatorAudio audio;
    KS_CHECK(audio.initialize(false)); // headless: no WASAPI device

    // Nonexistent directory -> no bank, no WAVs -> full synth fallback.
    KS_CHECK(audio.loadCarAudio("test_audio_render_no_such_car"));

    audio.setMasterVolume(1.0f);
    audio.setEnvironmentVolume(1.0f);
    audio.setEngineVolume(0.0f); // mute the always-on idle hum floor
    audio.setCameraMode(SimulatorAudio::CameraMode::Chase);

    std::printf("audio floor...\n");
    const Measurement floor = measure(audio, Params{}, "floor (all zero)");
    KS_CHECK(floor.finite);
    KS_CHECK(floor.peak <= 1.0f);
    KS_CHECK(floor.rms < 0.01);

    std::printf("audio engine...\n");
    audio.setEngineVolume(1.0f);
    Params eng;
    eng.rpm = 4000.0f;
    eng.throttle = 1.0f;
    const Measurement engine = measure(audio, eng, "rpm=4000 throttle=1");
    audio.setEngineVolume(0.0f);
    KS_CHECK(engine.finite);
    KS_CHECK(engine.peak <= 1.0f);
    KS_CHECK(engine.rms > floor.rms + 0.05);

    std::printf("audio wind...\n");
    Params wind;
    wind.speed = 200.0f;
    const Measurement wind200 = measure(audio, wind, "speed=200 (wind)");
    KS_CHECK(wind200.finite);
    KS_CHECK(wind200.peak <= 1.0f);
    KS_CHECK(wind200.rms > floor.rms + 0.01);

    std::printf("audio skid...\n");
    Params rolling;
    rolling.speed = 30.0f;
    const Measurement rollingOff = measure(audio, rolling, "speed=30 slip=0");
    Params slipRatioOn = rolling;
    slipRatioOn.slipRatio = 0.5f;
    const Measurement skidRatio =
        measure(audio, slipRatioOn, "speed=30 slipRatio=0.5");
    Params slipAngleOn = rolling;
    slipAngleOn.slipAngle = 0.4f; // rad: heavy cornering / drift
    const Measurement skidAngle =
        measure(audio, slipAngleOn, "speed=30 slipAngle=0.4");
    KS_CHECK(skidRatio.finite);
    KS_CHECK(skidRatio.peak <= 1.0f);
    KS_CHECK(skidRatio.rms > rollingOff.rms * 2.0);
    KS_CHECK(skidAngle.finite);
    KS_CHECK(skidAngle.peak <= 1.0f);
    KS_CHECK(skidAngle.rms > rollingOff.rms * 2.0);

    std::printf("audio brakes...\n");
    Params braking;
    braking.speed = 50.0f;
    braking.brake = 1.0f;
    const Measurement brakeCold = measure(audio, braking, "brake temp=0C");
    Params brakingHot = braking;
    brakingHot.brakeTemp = 300.0f;
    const Measurement brakeHot = measure(audio, brakingHot, "brake temp=300C");
    KS_CHECK(brakeHot.finite);
    KS_CHECK(brakeHot.peak <= 1.0f);
    KS_CHECK(brakeHot.rms > brakeCold.rms + 0.005);

    std::printf("audio gear shift...\n");
    // The shift transient fires on a gear change and decays over 0.5 s; it
    // is measured last so the still-active envelope cannot leak into the
    // other layers' checks.
    Params idle;
    const Measurement preShift = measure(audio, idle, "gear=1 idle");
    Params shifted = idle;
    shifted.gear = 2;
    const Measurement shiftSfx = measure(audio, shifted, "gear=2 shift tick");
    KS_CHECK(shiftSfx.finite);
    KS_CHECK(shiftSfx.peak <= 1.0f);
    KS_CHECK(shiftSfx.rms > preShift.rms + 0.005);

    return KS_TEST_RESULT("test_audio_render");
}
