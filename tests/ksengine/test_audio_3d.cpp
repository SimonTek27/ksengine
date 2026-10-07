/**
 * test_audio_3d — roadmap 2.3 (P2.2) "audio 3D":
 *  - Audio3D.h math: dopplerFactor (closing speed up, opening down,
 *    clamped), distanceGain (inverse distance + pre-cutoff fade),
 *    bearingPan (left/center/right against the listener's forward axis);
 *  - the real render path, headless like test_audio_render: other-car
 *    voices attenuate with distance, vanish past maxDistance, pan hard
 *    with bearing, and shift pitch with the radial velocity (Goertzel tone
 *    probe at the exact doppler-shifted fundamental);
 *  - the rolling surface noise: silent standing still, present while
 *    rolling, louder/brighter on grass than on asphalt.
 *
 * The player's own engine is muted (setEngineVolume(0)) so the floor is
 * quiet; other cars live on the environment volume and stay audible.
 * renderAudio only fills min(frames, channelBufferFrames()) per call, so
 * the measurement renders in 2048-frame chunks.
 */
#include "KsTest.h"
#include "Audio3D.h"
#include "SimulatorAudio.h"

#include <cmath>
#include <cstdio>
#include <vector>

using ks::sim::SimulatorAudio;
namespace a3 = ks::sim::audio3d;

namespace {

constexpr int kFrames = 8192;
constexpr int kChunk = 2048;
constexpr int kRate = 44100;

struct Band {
    double rmsL = 0, rmsR = 0;
    std::vector<float> buf; // raw stereo render, for the tone probes below
};

// One-pole high-pass at 300 Hz per channel: the RMS only sees the
// other-car tones (rpm 5000 -> 273 Hz + harmonics) and the rolling noise,
// never low-frequency rumble from anything else.
Band measureBand(SimulatorAudio& audio) {
    Band b;
    b.buf.assign(static_cast<size_t>(kFrames) * 2, 0.0f);
    for (int off = 0; off < kFrames; off += kChunk)
        audio.renderOffline(b.buf.data() + static_cast<size_t>(off) * 2,
                            kChunk, 2, kRate);

    const float a = 1.0f - std::exp(-6.2831853f * 300.0f / kRate);
    float lpL = 0.0f, lpR = 0.0f;
    double accL = 0.0, accR = 0.0;
    for (int i = 0; i < kFrames; ++i) {
        const float l = b.buf[static_cast<size_t>(i) * 2];
        const float r = b.buf[static_cast<size_t>(i) * 2 + 1];
        lpL += a * (l - lpL);
        lpR += a * (r - lpR);
        const float hL = l - lpL;
        const float hR = r - lpR;
        accL += static_cast<double>(hL) * hL;
        accR += static_cast<double>(hR) * hR;
    }
    b.rmsL = std::sqrt(accL / kFrames);
    b.rmsR = std::sqrt(accR / kFrames);
    return b;
}

// Power of a pure tone at `freq` (Goertzel) — the pitch probe for the
// doppler check: unlike zero-crossing counts this ignores the broadband
// floor, it only measures the bin at the expected shifted frequency.
double tonePower(const Band& b, float freq) {
    const float w = 6.2831853f * freq / kRate;
    const float coeff = 2.0f * std::cos(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (int i = 0; i < kFrames; ++i) {
        const float x = 0.5f * (b.buf[static_cast<size_t>(i) * 2] +
                                b.buf[static_cast<size_t>(i) * 2 + 1]);
        const float s0 = x + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double p = static_cast<double>(s1) * s1 +
                     static_cast<double>(s2) * s2 -
                     static_cast<double>(coeff) * s1 * s2;
    return p / static_cast<double>(kFrames);
}

void idleState(SimulatorAudio& audio, SimulatorAudio::SurfaceType surf,
               float speed) {
    audio.updatePhysics(0.0f, 0.0f, 0.0f, speed, 0.0f, 1, false,
                        0.0f, 0.0f, surf, 0.0f, 80.0f, 0.0f, speed,
                        0.0f, 0.0f, 1.0f / 60.0f);
}

void setVoice(SimulatorAudio& audio, float px, float pz,
              float vx = 0.0f, float vz = 0.0f, float rpm = 5000.0f) {
    a3::OtherCarVoice v;
    v.px = px;
    v.pz = pz;
    v.vx = vx;
    v.vz = vz;
    v.rpm = rpm;
    v.gain = 1.0f;
    audio.setOtherCarVoices(&v, 1);
}

} // namespace

int main() {
    // --- Audio3D.h math ---------------------------------------------------
    KS_CHECK(a3::dopplerFactor(0.0f) == 1.0f);
    KS_CHECK(a3::dopplerFactor(34.3f) > 1.09f);   // approaching: +10%
    KS_CHECK(a3::dopplerFactor(-34.3f) < 0.91f);  // opening: -10%
    KS_CHECK(std::fabs(a3::dopplerFactor(34.3f)
                       + a3::dopplerFactor(-34.3f) - 2.0f) < 1e-5f);
    KS_CHECK(a3::dopplerFactor(1.0e6f) <= a3::kDopplerMax);
    KS_CHECK(a3::dopplerFactor(-1.0e6f) >= a3::kDopplerMin);

    KS_CHECK(a3::distanceGain(1.0f) == 1.0f);
    KS_CHECK(a3::distanceGain(6.0f) == 1.0f);
    KS_CHECK(a3::distanceGain(60.0f) > 0.095f && a3::distanceGain(60.0f) < 0.105f);
    KS_CHECK(a3::distanceGain(300.0f) == 0.0f);
    KS_CHECK(a3::distanceGain(1000.0f) == 0.0f);
    KS_CHECK(a3::distanceGain(10.0f) > a3::distanceGain(100.0f));
    KS_CHECK(a3::distanceGain(100.0f) > a3::distanceGain(250.0f));
    KS_CHECK(a3::distanceGain(299.0f) < a3::distanceGain(250.0f)); // pre-cutoff fade

    // Listener looks down -Z, up +Y -> right = +X.
    KS_CHECK(std::fabs(a3::bearingPan(0, 0, -10, 0, 0, -1, 0, 1, 0)) < 1e-5f);
    KS_CHECK(a3::bearingPan(10, 0, 0, 0, 0, -1, 0, 1, 0) > 0.99f);
    KS_CHECK(a3::bearingPan(-10, 0, 0, 0, 0, -1, 0, 1, 0) < -0.99f);
    KS_CHECK(std::fabs(a3::bearingPan(0, 0, 10, 0, 0, -1, 0, 1, 0)) < 1e-5f);
    KS_CHECK(a3::bearingPan(5, 0, -5, 0, 0, -1, 0, 1, 0) > 0.65f); // 45° right

    // --- Render path ------------------------------------------------------
    SimulatorAudio audio;
    KS_CHECK(audio.initialize(false)); // headless: no WASAPI device
    KS_CHECK(audio.loadCarAudio("test_audio_3d_no_such_car"));
    audio.setMasterVolume(1.0f);
    audio.setEnvironmentVolume(1.0f);
    audio.setEngineVolume(0.0f); // mute the player's idle hum floor
    audio.setCameraMode(SimulatorAudio::CameraMode::Chase);
    audio.setListenerPosition(0.0f, 0.0f, 0.0f);
    audio.setListenerOrientation(0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
    audio.setListenerVelocity(0.0f, 0.0f, 0.0f);

    audio.setOtherCarVoices(nullptr, 0);
    idleState(audio, SimulatorAudio::SurfaceType::Asphalt, 0.0f);
    const Band floor = measureBand(audio);
    KS_CHECK(floor.rmsL < 0.005); // quiet floor (test_audio_render's bar too)

    // nearCar car dead ahead (10 m, ~273 Hz at rpm 5000).
    setVoice(audio, 0.0f, -10.0f);
    const Band nearCar = measureBand(audio);
    KS_CHECK(nearCar.rmsL > floor.rmsL + 0.01);

    // farCar car (200 m): audible but much quieter.
    setVoice(audio, 0.0f, -200.0f);
    const Band farCar = measureBand(audio);
    KS_CHECK(farCar.rmsL > floor.rmsL + 0.001);
    KS_CHECK(farCar.rmsL < nearCar.rmsL * 0.35);

    // Past maxDistance: gone.
    setVoice(audio, 0.0f, -400.0f);
    const Band gone = measureBand(audio);
    KS_CHECK(gone.rmsL < floor.rmsL + 0.005);

    // Bearing: car on the right -> right channel dominates.
    setVoice(audio, 10.0f, 0.0f);
    const Band rightCar = measureBand(audio);
    KS_CHECK(rightCar.rmsR > 0.01);
    KS_CHECK(rightCar.rmsR > rightCar.rmsL * 5.0);

    // ... and mirrored on the left.
    setVoice(audio, -10.0f, 0.0f);
    const Band leftCar = measureBand(audio);
    KS_CHECK(leftCar.rmsL > 0.01);
    KS_CHECK(leftCar.rmsL > leftCar.rmsR * 5.0);

    // Doppler: same spot, same rpm, only the radial velocity flips.
    // Source at -10 m: vz = +40 closes in, vz = -40 opens up. Probe the
    // exact doppler-shifted fundamentals with Goertzel: each render must
    // be loud at its own shifted frequency and quiet at the other one
    // (the broadband floor contributes almost nothing to a single bin).
    const float baseFreq = 30.0f + (5000.0f / 9000.0f) * 420.0f;
    const float fApp = baseFreq * a3::dopplerFactor(+40.0f);
    const float fRec = baseFreq * a3::dopplerFactor(-40.0f);
    setVoice(audio, 0.0f, -10.0f, 0.0f, +40.0f);
    const Band approaching = measureBand(audio);
    setVoice(audio, 0.0f, -10.0f, 0.0f, -40.0f);
    const Band receding = measureBand(audio);
    KS_CHECK(tonePower(approaching, fApp) > tonePower(receding, fApp) * 3.0);
    KS_CHECK(tonePower(receding, fRec) > tonePower(approaching, fRec) * 3.0);

    // --- Rolling surface noise -------------------------------------------
    audio.setOtherCarVoices(nullptr, 0);
    idleState(audio, SimulatorAudio::SurfaceType::Grass, 0.0f);
    const Band still = measureBand(audio); // standing still: rolling is off
    KS_CHECK(still.rmsL < 0.005);

    // Grass is the loud surface — presence check that does not depend on
    // how subtle the asphalt hum is mixed (test_audio_render pins that).
    idleState(audio, SimulatorAudio::SurfaceType::Grass, 30.0f);
    const Band grass = measureBand(audio);
    KS_CHECK(grass.rmsL > still.rmsL + 0.005);

    idleState(audio, SimulatorAudio::SurfaceType::Asphalt, 30.0f);
    const Band asph = measureBand(audio);
    KS_CHECK(asph.rmsL > still.rmsL);        // asphalt still rolls...
    KS_CHECK(grass.rmsL > asph.rmsL * 1.3);  // ...but grass is louder + brighter

    std::printf("audio_3d: nearCar=%.4f farCar=%.4f rightR=%.4f leftL=%.4f "
                "tone(app@fApp/rec@fApp)=%.1f/%.1f asph=%.4f grass=%.4f\n",
                nearCar.rmsL, farCar.rmsL, rightCar.rmsR, leftCar.rmsL,
                tonePower(approaching, fApp), tonePower(receding, fApp),
                asph.rmsL, grass.rmsL);

    return KS_TEST_RESULT("test_audio_3d");
}
