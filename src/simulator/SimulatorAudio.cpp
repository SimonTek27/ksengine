#include "SimulatorAudio.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ks::sim {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

struct WavFile {
    std::vector<float> samples;
    int sampleRate = 0;
    int channels = 0;
    bool valid = false;
};

static WavFile loadWav(const std::string& path)
{
    WavFile result;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return result;

    char chunkId[4];
    if (fread(chunkId, 1, 4, f) != 4 || memcmp(chunkId, "RIFF", 4) != 0) { fclose(f); return result; }

    uint32_t chunkSize;
    if (fread(&chunkSize, 4, 1, f) != 1) { fclose(f); return result; }

    char format[4];
    if (fread(format, 1, 4, f) != 4 || memcmp(format, "WAVE", 4) != 0) { fclose(f); return result; }

    int16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    std::vector<uint8_t> audioData;

    while (!feof(f)) {
        char subId[4];
        if (fread(subId, 1, 4, f) != 4) break;
        uint32_t subSize;
        if (fread(&subSize, 4, 1, f) != 1) break;

        if (memcmp(subId, "fmt ", 4) == 0) {
            fread(&audioFormat, 2, 1, f);
            fread(&numChannels, 2, 1, f);
            fread(&sampleRate, 4, 1, f);
            fseek(f, subSize - 10, SEEK_CUR);
        } else if (memcmp(subId, "data", 4) == 0) {
            audioData.resize(subSize);
            fread(audioData.data(), 1, subSize, f);
        } else {
            fseek(f, subSize, SEEK_CUR);
        }
    }
    fclose(f);

    if (audioFormat != 1 || audioData.empty()) return result;

    result.sampleRate = static_cast<int>(sampleRate);
    result.channels = numChannels;

    if (bitsPerSample == 16 || (bitsPerSample == 0 && audioData.size() >= numChannels * 2)) {
        int16_t* raw = reinterpret_cast<int16_t*>(audioData.data());
        int numSamples = static_cast<int>(audioData.size() / 2);
        result.samples.resize(numSamples);
        for (int i = 0; i < numSamples; ++i)
            result.samples[i] = raw[i] / 32768.0f;
    } else if (bitsPerSample == 32 && audioFormat == 3) {
        float* raw = reinterpret_cast<float*>(audioData.data());
        int numSamples = static_cast<int>(audioData.size() / 4);
        result.samples.assign(raw, raw + numSamples);
    } else if (bitsPerSample == 32) {
        int32_t* raw = reinterpret_cast<int32_t*>(audioData.data());
        int numSamples = static_cast<int>(audioData.size() / 4);
        result.samples.resize(numSamples);
        for (int i = 0; i < numSamples; ++i)
            result.samples[i] = raw[i] / 2147483648.0f;
    }

    result.valid = !result.samples.empty();
    return result;
}

SimulatorAudio::SimulatorAudio() = default;
SimulatorAudio::~SimulatorAudio() { shutdown(); }

bool SimulatorAudio::initialize(bool withOutput)
{
    if (m_initialized) return true;

    if (withOutput) {
        m_wasapi = std::make_unique<WASAPIOutput>();
        if (!m_wasapi->initialize(0, 2, MIX_BUFFER_MS)) {
            printf("SimulatorAudio: WASAPIOutput initialize failed\n");
            m_wasapi.reset();
            return false;
        }
    }

    m_mixer = std::make_unique<AudioMixer>();
    m_mixer->setMaxFrames(MIX_BUFFER_SAMPLES);
    m_mixer->addChannel("engine_int");
    m_mixer->addChannel("engine_ext");
    m_mixer->addChannel("turbo");
    m_mixer->addChannel("wind");
    m_mixer->addChannel("skid");
    m_mixer->addChannel("gear_shift");
    m_mixer->addChannel("brakes");
    m_mixer->addChannel("transmission");
    m_mixer->addChannel("bodywork");
    m_mixer->addChannel("backfire");
    m_mixer->addChannel("limiter");
    m_mixer->addChannel("rain_ambient");
    m_mixer->addChannel("rain_car");
    m_mixer->addChannel("wiper");
    m_mixer->addChannel("csp_skid");
    m_mixer->addChannel("other_cars");
    m_mixer->addChannel("rolling");

    if (withOutput) {
        m_wasapi->setRenderCallback([this](float* output, int frames, const AudioFormat& fmt) {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            renderAudio(output, frames, fmt.channels, fmt.sampleRate);
        });

        m_wasapi->start();
    }

    m_initialized = true;
    printf("SimulatorAudio: Initialized with %s + AudioMixer (%d channels)\n",
           withOutput ? "WASAPIOutput" : "headless render",
           m_mixer->channelCount());
    return true;
}

void SimulatorAudio::renderOffline(float* output, int frames, int channels, int sampleRate)
{
    if (!m_initialized || !m_mixer || !output || frames <= 0) return;
    std::lock_guard<std::mutex> lock(m_stateMutex);
    renderAudio(output, frames, channels, sampleRate);
}

void SimulatorAudio::shutdown()
{
    if (m_wasapi) {
        m_wasapi->stop();
        m_wasapi.reset();
    }
    m_mixer.reset();
    m_bankManager.reset();
    m_loaded = false;
    m_initialized = false;
    printf("SimulatorAudio: Shutdown\n");
}

bool SimulatorAudio::loadCarAudio(const std::string& carDirectory)
{
    if (!m_initialized) return false;
    printf("SimulatorAudio: Loading from %s\n", carDirectory.c_str());

    m_bankManager = std::make_unique<AudioBankManager>();

    std::string carId;
    size_t lastSlash = carDirectory.find_last_of("/\\");
    if (lastSlash != std::string::npos)
        carId = carDirectory.substr(lastSlash + 1);
    else
        carId = carDirectory;

    bool bankLoaded = m_bankManager->loadBank(carDirectory, carId);
    if (!bankLoaded) {
        printf("SimulatorAudio: No bank found, trying WAV files\n");
    }

    std::string soundsIniPath = carDirectory + "/data/sounds.ini";
    SoundsIniParser iniParser;
    if (iniParser.parse(soundsIniPath)) {
        m_soundsIni = iniParser.data();
        printf("SimulatorAudio: sounds.ini loaded — skids.mixVolume=%.2f, wind.volumeGain=%.2f, turbo.volume=%.2f\n",
               m_soundsIni.skids.mixVolume, m_soundsIni.wind.volumeGain, m_soundsIni.turbo.volume);
    }

    auto loadSfx = [&](const std::string& subdir, SoundCategory cat) -> bool {
        std::string path = carDirectory + "/sfx/" + subdir + ".wav";
        auto wav = loadWav(path);
        if (!wav.valid) { path = carDirectory + "/" + subdir + ".wav"; wav = loadWav(path); }
        if (wav.valid) {
            m_sfxSamples[static_cast<int>(cat)] = { std::move(wav.samples), wav.sampleRate, wav.channels,
                cat != SoundCategory::EngineInterior && cat != SoundCategory::EngineExterior };
            return true;
        }
        return false;
    };

    int loadedCount = 0;

    for (int i = 0; i < ENGINE_LAYERS_PER_SET; ++i) {
        std::string path = carDirectory + "/sfx/engine_int_" + std::to_string(i) + ".wav";
        auto wav = loadWav(path);
        if (wav.valid) {
            m_engineIntLayers[i] = { std::move(wav.samples), wav.sampleRate, wav.channels,
                                     i * 1125.0f, (i + 1) * 1125.0f, 1.0f, true };
            loadedCount++;
        }
    }

    for (int i = 0; i < ENGINE_LAYERS_PER_SET; ++i) {
        std::string path = carDirectory + "/sfx/engine_ext_" + std::to_string(i) + ".wav";
        auto wav = loadWav(path);
        if (wav.valid) {
            m_engineExtLayers[i] = { std::move(wav.samples), wav.sampleRate, wav.channels,
                                     i * 1125.0f, (i + 1) * 1125.0f, 1.0f, true };
            loadedCount++;
        }
    }

    if (loadedCount == 0) {
        printf("SimulatorAudio: No WAV samples found, generating synthetic\n");
        for (int i = 0; i < ENGINE_LAYERS_PER_SET; ++i) {
            generateSynthEngineLayer(m_engineIntLayers[i], i * 1125.0f, (i + 1) * 1125.0f, 44100);
            generateSynthEngineLayer(m_engineExtLayers[i], i * 1125.0f, (i + 1) * 1125.0f, 44100);
        }
        loadedCount = ENGINE_LAYERS_PER_SET * 2;
    }

    loadSfx("turbo", SoundCategory::Turbo);
    loadSfx("wastegate", SoundCategory::Wastegate);
    loadSfx("blowoff", SoundCategory::Blowoff);
    loadSfx("wind", SoundCategory::Wind);
    loadSfx("transmission", SoundCategory::Transmission);
    loadSfx("transmission_ext", SoundCategory::TransmissionExt);
    loadSfx("skid", SoundCategory::SkidAsphalt);
    loadSfx("skid_grass", SoundCategory::SkidGrass);
    loadSfx("skid_gravel", SoundCategory::SkidGravel);
    loadSfx("skid_kerb", SoundCategory::SkidKerb);
    loadSfx("skid_wet", SoundCategory::SkidWet);
    loadSfx("gear", SoundCategory::GearShift);
    loadSfx("gear_int", SoundCategory::GearClonk);
    loadSfx("brakes", SoundCategory::Brakes);
    loadSfx("bodywork", SoundCategory::Bodywork);
    loadSfx("backfire", SoundCategory::Backfire);
    loadSfx("limiter", SoundCategory::Limiter);

    for (int i = 0; i < static_cast<int>(SoundCategory::Count); ++i) {
        if (m_sfxSamples[i].samples.empty())
            generateSynthSfx(m_sfxSamples[i], static_cast<SoundCategory>(i), 44100);
    }

    m_totalSamples = loadedCount;
    m_loaded = true;
    m_prevGear = -1;
    printf("SimulatorAudio: %d engine layers + %d categories (bank: %s)\n",
           loadedCount, static_cast<int>(SoundCategory::Count),
           bankLoaded ? "loaded" : "none");
    return true;
}

bool SimulatorAudio::loadBank(const std::string& bankPath)
{
    if (!m_bankManager) m_bankManager = std::make_unique<AudioBankManager>();

    size_t lastSlash = bankPath.find_last_of("/\\");
    size_t lastDot = bankPath.find_last_of('.');
    std::string dir = (lastSlash != std::string::npos) ? bankPath.substr(0, lastSlash) : ".";
    std::string file = (lastSlash != std::string::npos) ? bankPath.substr(lastSlash + 1) : bankPath;
    std::string carId = (lastDot != std::string::npos) ? file.substr(0, lastDot - lastSlash - 1) : file;

    size_t sfxPos = dir.find("/sfx");
    if (sfxPos == std::string::npos) sfxPos = dir.find("\\sfx");
    std::string carDir = (sfxPos != std::string::npos) ? dir.substr(0, sfxPos) : dir;

    return m_bankManager->loadBank(carDir, carId);
}

void SimulatorAudio::update(float rpm, float throttle, float brake, float speed, int gear,
                             CameraMode mode, float dt)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_prevState = m_state;
    m_state.rpm = rpm;
    m_state.throttle = throttle;
    m_state.brake = brake;
    m_state.speed = speed;
    m_state.gear = gear;
    m_cameraMode = static_cast<int>(mode);
}

void SimulatorAudio::updatePhysics(float rpm, float throttle, float brake, float speed,
                                    float steering, int gear, bool isShifting,
                                    float slipRatio, float slipAngle,
                                    SurfaceType surface, float suspensionDamage,
                                    float brakeTemp, float boostPressure,
                                    float windSpeed, float wetness, float rainIntensity,
                                    float dt)
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_prevState = m_state;
    m_state.rpm = rpm;
    m_state.throttle = throttle;
    m_state.brake = brake;
    m_state.speed = speed;
    m_state.steering = steering;
    m_state.gear = gear;
    m_state.isShifting = isShifting;
    m_state.slipRatio = slipRatio;
    m_state.slipAngle = slipAngle;
    m_state.surface = static_cast<int>(surface);
    m_state.suspensionDamage = suspensionDamage;
    m_state.brakeTemp = brakeTemp;
    m_state.boostPressure = boostPressure;
    m_state.windSpeed = windSpeed;
    m_state.wetness = wetness;
    m_state.rainIntensity = rainIntensity;
}

void SimulatorAudio::setListenerPosition(float x, float y, float z) { m_listenerX = x; m_listenerY = y; m_listenerZ = z; }
void SimulatorAudio::setListenerOrientation(float fx, float fy, float fz, float ux, float uy, float uz)
{
    m_listenerForwardX = fx; m_listenerForwardY = fy; m_listenerForwardZ = fz;
    m_listenerUpX = ux; m_listenerUpY = uy; m_listenerUpZ = uz;
}

void SimulatorAudio::setListenerVelocity(float vx, float vy, float vz)
{
    m_listenerVX = vx; m_listenerVY = vy; m_listenerVZ = vz;
}

void SimulatorAudio::setOtherCarVoices(const ks::sim::audio3d::OtherCarVoice* voices, int count)
{
    if (!voices || count <= 0) { m_otherCarCount = 0; return; }
    m_otherCarCount = std::min(count, kMaxOtherCars);
    for (int i = 0; i < m_otherCarCount; ++i) m_otherCars[i] = voices[i];
}
void SimulatorAudio::setSourcePosition(SoundCategory, float, float, float) {}

void SimulatorAudio::generateSynthEngineLayer(EngineLayer& layer, float rpmMin, float rpmMax, int sampleRate)
{
    layer.sampleRate = sampleRate;
    layer.channels = 2;
    layer.rpmMin = rpmMin;
    layer.rpmMax = rpmMax;
    layer.loop = true;

    float centerRpm = (rpmMin + rpmMax) * 0.5f;
    float rpmNorm = centerRpm / 9000.0f;
    float baseFreq = 30.0f + rpmNorm * 350.0f;
    int totalSamples = sampleRate * 2 * 2;
    layer.samples.resize(totalSamples);

    float dt = 1.0f / sampleRate;
    float p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0;

    for (int i = 0; i < totalSamples; i += 2) {
        p1 += baseFreq * dt;
        p2 += baseFreq * 2.003f * dt;
        p3 += baseFreq * 3.011f * dt;
        p4 += baseFreq * 4.987f * dt;
        p5 += baseFreq * 0.501f * dt;

        float sig = 0;
        sig += 0.35f * std::sin(p1 * 6.2831853f);
        sig += 0.20f * std::sin(p2 * 6.2831853f);
        sig += 0.10f * std::sin(p3 * 6.2831853f);
        sig += 0.06f * std::sin(p4 * 6.2831853f);
        sig += 0.15f * std::sin(p5 * 6.2831853f);

        float crankPulse = std::fmod(p1, 1.0f);
        sig += crankPulse * crankPulse * 0.08f * rpmNorm;
        sig *= 0.4f + 0.6f * rpmNorm;
        sig = std::max(-0.95f, std::min(0.95f, sig));

        layer.samples[i] = sig;
        layer.samples[i + 1] = sig * 0.98f;

        if (p1 > 1000.0f) p1 -= 1000.0f;
        if (p2 > 1000.0f) p2 -= 1000.0f;
        if (p3 > 1000.0f) p3 -= 1000.0f;
        if (p4 > 1000.0f) p4 -= 1000.0f;
        if (p5 > 1000.0f) p5 -= 1000.0f;
    }
}

void SimulatorAudio::generateSynthSfx(SfxSample& sfx, SoundCategory cat, int sampleRate)
{
    sfx.sampleRate = sampleRate;
    sfx.channels = 2;
    sfx.loop = (cat == SoundCategory::Wind || cat == SoundCategory::Transmission || cat == SoundCategory::Bodywork);

    int duration = (cat == SoundCategory::GearShift || cat == SoundCategory::GearClonk) ? sampleRate :
                   (cat == SoundCategory::Backfire || cat == SoundCategory::Limiter) ? sampleRate * 2 :
                   (cat == SoundCategory::Starter) ? sampleRate * 3 : sampleRate * 4;

    int totalSamples = duration * 2;
    sfx.samples.resize(totalSamples);
    float dt = 1.0f / sampleRate;

    for (int i = 0; i < totalSamples; i += 2) {
        float t = (float)i / (float)sampleRate;
        float sig = 0;

        switch (cat) {
        case SoundCategory::Turbo: {
            float phase = std::fmod(t * 8000.0f, 1.0f);
            sig = std::sin(phase * 6.2831853f) * 0.15f * std::min(1.0f, t * 4.0f);
            break;
        }
        case SoundCategory::Wind: {
            float phase = std::fmod(t * 3000.0f + std::sin(t * 7.0f) * 500.0f, 1.0f);
            sig = (std::fmod(phase, 1.0f) * 2.0f - 1.0f) * 0.2f * (0.5f + 0.5f * std::sin(t * 0.8f * 6.2831853f));
            break;
        }
        case SoundCategory::GearShift: {
            float env = std::max(0.0f, 1.0f - t * 3.0f);
            sig = ((std::fmod(t * 200.0f, 1.0f) * 2.0f - 1.0f) * 0.3f + std::sin(t * 150.0f * 6.2831853f) * 0.2f) * env;
            break;
        }
        case SoundCategory::GearClonk: {
            float env = std::max(0.0f, 1.0f - t * 5.0f);
            float impact = std::exp(-t * 30.0f);
            sig = std::sin(t * 80.0f * 6.2831853f) * impact * 0.3f * env;
            break;
        }
        case SoundCategory::Brakes: {
            float env = std::min(1.0f, t * 10.0f) * std::max(0.0f, 1.0f - (t - 2.0f) * 2.0f);
            sig = (std::sin(t * 4000.0f * 6.2831853f) * 0.08f + std::sin(t * 6000.0f * 6.2831853f) * 0.04f) * env;
            break;
        }
        case SoundCategory::Backfire: {
            float boom = std::exp(-t * 8.0f);
            sig = ((std::fmod(t * 100.0f, 1.0f) * 2.0f - 1.0f) * 0.4f + std::sin(t * 60.0f * 6.2831853f) * 0.3f) * boom;
            break;
        }
        case SoundCategory::Limiter: {
            float env = 0.5f + 0.5f * std::sin(t * 12.0f * 6.2831853f);
            sig = (std::sin(t * 200.0f * 6.2831853f) * 0.15f + (std::fmod(t * 1000.0f, 1.0f) * 2.0f - 1.0f) * 0.05f) * env;
            break;
        }
        case SoundCategory::Transmission: {
            float phase = std::fmod(t * 2000.0f, 1.0f);
            sig = (std::sin(phase * 6.2831853f) * 0.08f + std::sin(phase * 4000.0f * 6.2831853f) * 0.03f) *
                  (0.5f + 0.5f * std::sin(t * 0.3f * 6.2831853f));
            break;
        }
        case SoundCategory::Bodywork: {
            sig = (std::fmod(t * 800.0f, 1.0f) * 2.0f - 1.0f) * 0.02f * (0.5f + 0.5f * std::sin(t * 0.5f * 6.2831853f));
            break;
        }
        // Skid categories (roadmap 1.1): without them the skid layer stays
        // completely silent whenever the car has no sound bank — the most
        // important driving feedback after the engine. Frequencies are
        // generated at half the target because the loop writes interleaved
        // stereo with t = i / sampleRate (content plays back 2x).
        case SoundCategory::SkidAsphalt: {
            // Screech: bright broadband noise with a slow warble.
            float n1 = std::fmod(t * 675.0f + std::sin(t * 11.0f) * 17.0f, 1.0f) * 2.0f - 1.0f;
            float n2 = std::fmod(t * 890.0f + std::sin(t * 7.0f) * 23.0f, 1.0f) * 2.0f - 1.0f;
            sig = (n1 * 0.6f + n2 * 0.4f) * 0.18f * (0.6f + 0.4f * std::sin(t * 4.5f * 6.2831853f));
            break;
        }
        case SoundCategory::SkidWet: {
            // Hiss: flat high-frequency wash over water.
            float n1 = std::fmod(t * 1000.0f + std::sin(t * 13.0f) * 31.0f, 1.0f) * 2.0f - 1.0f;
            float n2 = std::fmod(t * 1375.0f, 1.0f) * 2.0f - 1.0f;
            sig = (n1 + n2) * 0.5f * 0.12f * (0.7f + 0.3f * std::sin(t * 2.5f * 6.2831853f));
            break;
        }
        case SoundCategory::SkidGrass: {
            // Soft swish: mid-band noise, gentle amplitude.
            float n1 = std::fmod(t * 325.0f + std::sin(t * 6.0f) * 13.0f, 1.0f) * 2.0f - 1.0f;
            sig = n1 * 0.12f * (0.5f + 0.5f * std::sin(t * 1.5f * 6.2831853f));
            break;
        }
        case SoundCategory::SkidGravel: {
            // Crunch: mid noise plus granular impacts.
            float n1 = std::fmod(t * 425.0f + std::sin(t * 9.0f) * 19.0f, 1.0f) * 2.0f - 1.0f;
            float grain = std::fmod(t * 18.5f, 1.0f);
            float impact = grain < 0.15f ? (1.0f - grain * 6.0f) : 0.0f;
            sig = n1 * 0.10f + impact * 0.12f * std::sin(t * 110.0f * 6.2831853f);
            break;
        }
        case SoundCategory::SkidKerb: {
            // Kerb ripple: strong periodic bumps.
            float bump = std::fmod(t * 12.0f, 1.0f);
            sig = (bump * 2.0f - 1.0f) * 0.2f * (0.5f + 0.5f * std::sin(t * 1.0f * 6.2831853f));
            break;
        }
        default:
            break;
        }

        sig = std::max(-1.0f, std::min(1.0f, sig));
        sfx.samples[i] = sig;
        sfx.samples[i + 1] = sig;
    }
}

float SimulatorAudio::sampleAt(const std::vector<float>& samples, float position, int channel, int totalChannels) const
{
    if (samples.empty()) return 0;
    int totalFrames = static_cast<int>(samples.size()) / totalChannels;
    if (totalFrames <= 0) return 0;
    float framePos = std::fmod(position, (float)totalFrames);
    if (framePos < 0) framePos += (float)totalFrames;
    int frame = static_cast<int>(framePos);
    float frac = framePos - frame;
    int nextFrame = (frame + 1) % totalFrames;
    return samples[frame * totalChannels + channel] * (1.0f - frac) + samples[nextFrame * totalChannels + channel] * frac;
}

void SimulatorAudio::renderAudio(float* output, int frames, int channels, int sampleRate)
{
    float masterVol = m_masterVolume.load();
    float engineVol = m_engineVolume.load();
    float envVol = m_environmentVolume.load();
    float interiorMix = m_interiorMix.load();
    int camMode = m_cameraMode.load();

    int bufFrames = std::min(frames, m_mixer->channelBufferFrames());

    float* chBuf = m_mixer->channelBuffer(ChEngineInt);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderEngine(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChEngineInt, engineVol * masterVol);
    m_mixer->setPan(ChEngineInt, 0.0f);

    chBuf = m_mixer->channelBuffer(ChEngineExt);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderEngine(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChEngineExt, engineVol * masterVol);
    m_mixer->setPan(ChEngineExt, 0.0f);

    chBuf = m_mixer->channelBuffer(ChTurbo);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderTurbo(chBuf, bufFrames, 2, sampleRate, (float)frames / sampleRate);
    }
    m_mixer->setVolume(ChTurbo, engineVol * masterVol);

    chBuf = m_mixer->channelBuffer(ChWind);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderWind(chBuf, bufFrames, 2, sampleRate, (float)frames / sampleRate);
    }
    m_mixer->setVolume(ChWind, envVol * masterVol);

    chBuf = m_mixer->channelBuffer(ChSkid);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderSkid(chBuf, bufFrames, 2, sampleRate, (float)frames / sampleRate);
    }
    m_mixer->setVolume(ChSkid, envVol * masterVol);

    chBuf = m_mixer->channelBuffer(ChGearShift);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderGearShift(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChGearShift, masterVol);

    chBuf = m_mixer->channelBuffer(ChBrakes);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderBrakes(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChBrakes, envVol * masterVol);

    chBuf = m_mixer->channelBuffer(ChTransmission);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderTransmission(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChTransmission, envVol * masterVol * 0.3f);

    chBuf = m_mixer->channelBuffer(ChBodywork);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderBodywork(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChBodywork, envVol * masterVol * 0.15f);

    chBuf = m_mixer->channelBuffer(ChBackfire);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderBackfire(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChBackfire, masterVol);

    chBuf = m_mixer->channelBuffer(ChLimiter);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderLimiter(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChLimiter, masterVol);

    chBuf = m_mixer->channelBuffer(ChRainAmbient);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderWeather(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChRainAmbient, envVol * masterVol);

    chBuf = m_mixer->channelBuffer(ChRainCar);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderWeather(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChRainCar, envVol * masterVol * 0.7f);

    chBuf = m_mixer->channelBuffer(ChWiper);
    if (chBuf)
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
    m_mixer->setBypassed(ChWiper, m_state.rainIntensity < 0.1f);

    chBuf = m_mixer->channelBuffer(ChCspSkid);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderCspSurfaces(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChCspSkid, envVol * masterVol);

    // Roadmap 2.3: other cars on track (environment category — audible
    // regardless of the player's own engine volume) and the continuous
    // rolling noise under the tires.
    chBuf = m_mixer->channelBuffer(ChOtherCars);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderOtherCars(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChOtherCars, envVol * masterVol);
    m_mixer->setPan(ChOtherCars, 0.0f); // pan is baked in per voice

    chBuf = m_mixer->channelBuffer(ChRolling);
    if (chBuf) {
        std::fill(chBuf, chBuf + bufFrames * 2, 0.0f);
        renderRolling(chBuf, bufFrames, 2, sampleRate);
    }
    m_mixer->setVolume(ChRolling, envVol * masterVol);

    m_mixer->mix(output, bufFrames, channels, sampleRate);

    float extVol = (camMode == 0) ? (1.0f - interiorMix) * 0.4f : 1.0f;
    for (int i = 0; i < bufFrames * channels; ++i)
        output[i] *= extVol;

    if (bufFrames < frames) {
        for (int i = bufFrames * channels; i < frames * channels; ++i)
            output[i] = 0;
    }
}

void SimulatorAudio::renderEngine(float* output, int frames, int channels, int sampleRate)
{
    float rpm = m_state.rpm;
    float throttle = m_state.throttle;
    float rpmNorm = rpm / 9000.0f;
    float t = m_time.load();
    float dt = (float)frames / (float)sampleRate;
    m_time.store(t + dt);

    float freq = 40.0f + rpmNorm * 450.0f + throttle * 30.0f;
    float rpmVol = 0.3f + 0.7f * rpmNorm;
    float throttleMod = 1.0f + throttle * 0.3f;

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float sig = 0;
        sig += 0.40f * std::sin(std::fmod(time * freq, 1.0f) * 6.2831853f);
        sig += 0.22f * std::sin(std::fmod(time * freq * 2.007f, 1.0f) * 6.2831853f);
        sig += 0.10f * std::sin(std::fmod(time * freq * 3.013f, 1.0f) * 6.2831853f);
        sig += 0.12f * std::sin(std::fmod(time * freq * 0.501f, 1.0f) * 6.2831853f);

        float crankPulse = std::fmod(time * freq * 0.5f, 1.0f);
        sig += crankPulse * crankPulse * 0.08f * rpmNorm;
        sig *= throttleMod * rpmVol;
        sig = std::max(-0.95f, std::min(0.95f, sig));

        for (int ch = 0; ch < channels; ++ch) {
            float chOff = (ch == 1) ? 0.003f : 0.0f;
            float s = 0;
            s += 0.40f * std::sin(std::fmod(time * freq + chOff * freq, 1.0f) * 6.2831853f);
            s += 0.22f * std::sin(std::fmod(time * freq * 2.007f + chOff * freq * 2, 1.0f) * 6.2831853f);
            s += 0.10f * std::sin(std::fmod(time * freq * 3.013f, 1.0f) * 6.2831853f);
            s += 0.12f * std::sin(std::fmod(time * freq * 0.501f, 1.0f) * 6.2831853f);
            s *= throttleMod * rpmVol;
            s = std::max(-0.95f, std::min(0.95f, s));
            output[i * channels + ch] = s;
        }
    }
}

void SimulatorAudio::renderWind(float* output, int frames, int channels, int sampleRate, float)
{
    float speedNorm = std::min(1.0f, m_state.speed / 250.0f);
    if (speedNorm < 0.01f) return;

    float t = m_time.load();
    float vol = speedNorm * 0.25f;

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float w1 = (std::fmod(time * 1500.0f + std::sin(time * 3.0f) * 400.0f, 1.0f) * 2.0f - 1.0f) * 0.3f;
        float w2 = (std::fmod(time * 3000.0f + std::sin(time * 5.0f) * 600.0f, 1.0f) * 2.0f - 1.0f) * 0.15f;
        float wind = (w1 + w2) * (0.5f + 0.5f * std::sin(time * 0.7f * 6.2831853f)) *
                     (0.5f + 0.5f * std::sin(time * 1.3f * 6.2831853f)) * vol;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = std::max(-0.5f, std::min(0.5f, wind));
    }
}

void SimulatorAudio::renderTurbo(float* output, int frames, int channels, int sampleRate, float)
{
    float boost = m_state.boostPressure;
    float throttle = m_state.throttle;
    float rpmNorm = m_state.rpm / 9000.0f;

    float turboIntensity = (throttle > 0.2f && rpmNorm > 0.3f) ?
        (throttle - 0.2f) * rpmNorm * std::min(1.0f, boost / 1.5f) : 0;
    if (turboIntensity < 0.01f) return;

    float t = m_time.load();
    float vol = turboIntensity * 0.18f;
    float turboFreq = 4000.0f + turboIntensity * 6000.0f;

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float phase = std::fmod(time * turboFreq, 1.0f);
        float sig = (std::sin(phase * 6.2831853f) * 0.4f + std::sin(phase * 2.0f * 6.2831853f) * 0.15f) *
                    (0.7f + 0.3f * std::sin(time * 3.0f * 6.2831853f)) * vol;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
}

void SimulatorAudio::renderSkid(float* output, int frames, int channels, int sampleRate, float)
{
    float slip = std::max(std::abs(m_state.slipRatio), std::abs(m_state.slipAngle));
    if (slip < 0.05f || m_state.speed < 2.0f) return;

    float slipNorm = std::min(1.0f, slip / 0.5f);
    float wetness = m_state.wetness;
    float vol = slipNorm * 0.2f * std::min(1.0f, m_state.speed / 30.0f);
    vol *= (1.0f + wetness * 0.3f);

    SoundCategory cat;
    if (wetness > 0.3f) {
        cat = SoundCategory::SkidWet;
    } else {
        switch (m_state.surface) {
        case 1:  cat = SoundCategory::SkidGrass; break;
        case 2:  cat = SoundCategory::SkidGravel; break;
        case 3:  cat = SoundCategory::SkidKerb; break;
        case 4:  cat = SoundCategory::SkidWet; break;
        default: cat = SoundCategory::SkidAsphalt; break;
        }
    }

    const auto& sfx = m_sfxSamples[static_cast<int>(cat)];
    if (sfx.samples.empty()) return;

    float pitch = 0.8f + slipNorm * 0.4f - wetness * 0.1f;
    for (int i = 0; i < frames; ++i) {
        m_skidPhase += (float)sfx.sampleRate * pitch / (float)sampleRate;
        int pos = static_cast<int>(m_skidPhase) % (static_cast<int>(sfx.samples.size()) / sfx.channels);
        for (int ch = 0; ch < channels; ++ch) {
            int idx = (pos * sfx.channels + ch) % static_cast<int>(sfx.samples.size());
            output[i * channels + ch] = sfx.samples[idx] * vol;
        }
    }
}

void SimulatorAudio::renderGearShift(float* output, int frames, int channels, int sampleRate)
{
    int gear = m_state.gear;
    if (m_prevGear < 0) { m_prevGear = gear; return; }
    if (gear != m_prevGear && !m_state.isShifting) {
        m_gearShiftTimer = 0.5f;
        m_backfireTimer = 0.3f;
    }
    m_prevGear = gear;

    if (m_gearShiftTimer <= 0) return;

    const auto& sfx = m_sfxSamples[static_cast<int>(SoundCategory::GearShift)];
    if (sfx.samples.empty()) return;

    float env = std::min(1.0f, m_gearShiftTimer * 4.0f);
    // One-shot: play from the start of the sample on every shift. Indexing
    // by the global clock (m_time) landed on an arbitrary — often silent —
    // part of the loop, so most shift ticks were inaudible.
    const float start = 0.5f - m_gearShiftTimer;
    const int totalFrames = static_cast<int>(sfx.samples.size()) / sfx.channels;
    if (totalFrames <= 0) return;

    for (int i = 0; i < frames; ++i) {
        const float sampleTime = start + (float)i / (float)sampleRate;
        int pos = static_cast<int>(sampleTime * (float)sfx.sampleRate) % totalFrames;
        for (int ch = 0; ch < channels; ++ch) {
            int idx = (pos * sfx.channels + ch) % static_cast<int>(sfx.samples.size());
            output[i * channels + ch] = sfx.samples[idx] * env;
        }
    }
    m_gearShiftTimer -= (float)frames / (float)sampleRate;
}

void SimulatorAudio::renderBrakes(float* output, int frames, int channels, int sampleRate)
{
    float brake = m_state.brake;
    float speed = m_state.speed;
    if (brake < 0.05f || speed < 5.0f) return;

    float intensity = brake * std::min(1.0f, speed / 50.0f) * std::min(1.0f, m_state.brakeTemp / 200.0f);
    if (intensity < 0.01f) return;

    float t = m_time.load();
    float vol = intensity * 0.08f;

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float sig = (std::sin(time * 5000.0f * 6.2831853f) * 0.3f + std::sin(time * 7000.0f * 6.2831853f) * 0.15f +
                     (std::fmod(time * 2000.0f, 1.0f) * 2.0f - 1.0f) * 0.05f) *
                    (0.7f + 0.3f * std::sin(time * 0.5f * 6.2831853f)) * vol;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
}

void SimulatorAudio::renderTransmission(float* output, int frames, int channels, int sampleRate)
{
    float rpmNorm = m_state.rpm / 9000.0f;
    float intensity = 0.1f + 0.4f * rpmNorm + 0.2f * std::min(1.0f, m_state.speed / 100.0f);
    float freq = 800.0f + rpmNorm * 2000.0f;
    float t = m_time.load();

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float phase = std::fmod(time * freq, 1.0f);
        float sig = (std::sin(phase * 6.2831853f) * 0.06f + std::sin(phase * 2.0f * 6.2831853f) * 0.02f) *
                    (0.5f + 0.5f * std::sin(time * 0.2f * 6.2831853f)) * intensity;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
}

void SimulatorAudio::renderBodywork(float* output, int frames, int channels, int sampleRate)
{
    float intensity = 0.02f + 0.1f * std::min(1.0f, m_state.speed / 100.0f) + 0.2f * m_state.suspensionDamage;
    if (intensity < 0.01f) return;

    float t = m_time.load();
    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float sig = (std::fmod(time * 600.0f, 1.0f) * 2.0f - 1.0f) * 0.02f *
                    (0.5f + 0.5f * std::sin(time * 0.3f * 6.2831853f)) * intensity;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
}

void SimulatorAudio::renderBackfire(float* output, int frames, int channels, int sampleRate)
{
    if (m_backfireTimer <= 0) return;

    float t = m_time.load();
    float env = m_backfireTimer / 0.3f;

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float boom = std::exp(-time * 10.0f) * env;
        float sig = ((std::fmod(time * 80.0f, 1.0f) * 2.0f - 1.0f) * 0.3f + std::sin(time * 50.0f * 6.2831853f) * 0.2f) * boom;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
    m_backfireTimer -= (float)frames / (float)sampleRate;
}

void SimulatorAudio::renderLimiter(float* output, int frames, int channels, int sampleRate)
{
    if (m_state.rpm > 8500.0f && m_state.throttle > 0.8f)
        m_limiterTimer = 0.3f;
    if (m_limiterTimer <= 0) return;

    float t = m_time.load();
    float env = m_limiterTimer / 0.3f;

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float sig = (std::sin(time * 180.0f * 6.2831853f) * 0.12f + (std::fmod(time * 800.0f, 1.0f) * 2.0f - 1.0f) * 0.04f) * env;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
    m_limiterTimer -= (float)frames / (float)sampleRate;
}

void SimulatorAudio::renderWeather(float* output, int frames, int channels, int sampleRate)
{
    float rainIntensity = m_state.rainIntensity;
    float speed = m_state.speed;
    if (rainIntensity < 0.01f) return;

    float t = m_time.load();

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;

        float rainAmb = rainIntensity * 0.3f;
        rainAmb *= 0.7f + 0.3f * std::sin(time * 0.3f * 6.2831853f);
        float noise1 = std::fmod(time * 1200.0f + std::sin(time * 5.0f) * 200.0f, 1.0f) * 2.0f - 1.0f;
        float noise2 = std::fmod(time * 2400.0f + std::sin(time * 8.0f) * 300.0f, 1.0f) * 2.0f - 1.0f;
        float rain = (noise1 * 0.15f + noise2 * 0.08f) * rainAmb;

        float rainCar = rainIntensity * std::min(1.0f, speed / 200.0f) * 0.2f;
        float carNoise = std::fmod(time * 3000.0f, 1.0f) * 2.0f - 1.0f;
        float carImpact = std::fmod(time * 800.0f + std::sin(time * 2.0f) * 100.0f, 1.0f);
        carImpact = carImpact * carImpact;
        float carRain = carNoise * rainCar * (0.5f + 0.5f * carImpact);

        float sig = rain + carRain;
        sig = std::max(-0.5f, std::min(0.5f, sig));

        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
    m_rainPhase += (float)frames / (float)sampleRate;
}

void SimulatorAudio::renderCspSurfaces(float* output, int frames, int channels, int sampleRate)
{
    if (m_state.surface != 5) return;

    float speed = m_state.speed;
    if (speed < 1.0f) return;

    float t = m_time.load();
    float vol = 0.1f * std::min(1.0f, speed / 50.0f);

    for (int i = 0; i < frames; ++i) {
        float time = t + (float)i / (float)sampleRate;
        float crunch = std::fmod(time * 1500.0f + std::sin(time * 3.0f) * 200.0f, 1.0f);
        crunch = crunch * crunch * crunch;
        float sig = (std::fmod(time * 2000.0f, 1.0f) * 2.0f - 1.0f) * 0.05f * crunch * vol;
        for (int ch = 0; ch < channels; ++ch)
            output[i * channels + ch] = sig;
    }
}

void SimulatorAudio::renderAcoustics(float* output, int frames, int channels, int sampleRate)
{
    // Apply KSCarAcoustics cabin modeling to interior audio
    // and exterior acoustic filtering
    float interiorMix = m_interiorMix.load();
    float cabinVolume = 0.8f; // default RaceCar cabin volume
    float cabinAbsorption = 0.6f;
    float doorSeal = 0.2f;
    float windowArea = 0.5f;

    // Simple cabin simulation: lowpass filter for interior
    // Cutoff frequency based on car type and speed
    float cutoff = 2000.0f + m_state.speed * 10.0f; // higher speed = higher cutoff
    float resonance = 0.5f + cabinAbsorption * 0.5f;

    for (int i = 0; i < frames; ++i) {
        float time = m_time.load() + (float)i / (float)sampleRate;

        // Apply simple first-order lowpass to simulate cabin resonance
        // This is a placeholder - full KSCarAcoustics would be called here
        float sampleL = output[i * channels];
        float sampleR = (channels > 1) ? output[i * channels + 1] : sampleL;

        // Simulate lowpass: average with previous (simplified)
        static float prevL = 0, prevR = 0;
        float alpha = cutoff / (cutoff + sampleRate);
        prevL = alpha * prevL + (1.0f - alpha) * sampleL;
        prevR = alpha * prevR + (1.0f - alpha) * sampleR;

        output[i * channels] = prevL;
        if (channels > 1)
            output[i * channels + 1] = prevR;
    }
    m_time.store(m_time.load() + (float)frames / (float)sampleRate);
}

void SimulatorAudio::renderSpatialization(float* output, int frames, int channels, int sampleRate)
{
    // Apply SpatialSurroundSystem 3D positioning for track events
    // Simple implementation: panning based on car direction and position
    float speed = m_state.speed;
    float direction = m_state.steering; // -1 to 1

    for (int i = 0; i < frames; ++i) {
        float time = m_time.load() + (float)i / (float)sampleRate;
        float pan = direction * 0.5f; // -0.5 to 0.5 pan

        // Equal-power panning
        float angle = (pan + 1.0f) * kPi * 0.25f;
        float gainL = std::cos(angle);
        float gainR = std::sin(angle);

        // Limit gain to valid range
        gainL = std::clamp(gainL, -1.0f, 1.0f);
        gainR = std::clamp(gainR, -1.0f, 1.0f);

        output[i * channels]     *= gainL;
        if (channels > 1)
            output[i * channels + 1] *= gainR;
    }
    m_time.store(m_time.load() + (float)frames / (float)sampleRate);
}

void SimulatorAudio::renderModulation(float* output, int frames, int channels, int sampleRate)
{
    // Apply ext_config.ini overrides for volume/pitch/parameter modification
    float masterVol = m_masterVolume.load();
    float extVol = 1.0f;

    // Simple override check - in production, would parse ext_config.ini
    // For now, just apply a basic exterior volume boost if outside
    int camMode = m_cameraMode.load();
    if (camMode == 1 || camMode == 3) { // Chase or Free mode
        extVol = 1.1f; // slight exterior boost
    }

    for (int i = 0; i < frames * channels; ++i) {
        output[i] *= extVol * masterVol;
        output[i] = std::max(-1.0f, std::min(1.0f, output[i]));
    }
}

void SimulatorAudio::renderOtherCars(float* output, int frames, int channels, int sampleRate)
{
    // Roadmap 2.3 (P2.2): synthesized engine tone per hearable car —
    // distance gain, bearing pan and doppler pitch (Audio3D.h), baked into
    // the stereo channel buffer. The channel's own volume/pan stay neutral
    // (set in renderAudio); the level here is deliberately modest: these
    // are the cars you *hear*, not the one you drive.
    if (m_otherCarCount <= 0 || sampleRate <= 0) return;

    for (int v = 0; v < m_otherCarCount; ++v) {
        const auto& car = m_otherCars[v];
        const float dx = car.px - m_listenerX;
        const float dy = car.py - m_listenerY;
        const float dz = car.pz - m_listenerZ;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float atten = ks::sim::audio3d::distanceGain(dist);
        if (atten <= 0.0015f || car.gain <= 0.0f) continue;
        const float inv = 1.0f / dist;
        const float sx = dx * inv, sy = dy * inv, sz = dz * inv;

        // Radial speed along listener→car: positive = receding, so the
        // closing speed the doppler term wants is its negation.
        const float rvx = car.vx - m_listenerVX;
        const float rvy = car.vy - m_listenerVY;
        const float rvz = car.vz - m_listenerVZ;
        const float vr = rvx * sx + rvy * sy + rvz * sz;
        const float dop = ks::sim::audio3d::dopplerFactor(-vr);

        const float pan = ks::sim::audio3d::bearingPan(
            dx, dy, dz,
            m_listenerForwardX, m_listenerForwardY, m_listenerForwardZ,
            m_listenerUpX, m_listenerUpY, m_listenerUpZ);
        const float angle = (pan + 1.0f) * kPi * 0.25f;
        const float gL = std::cos(angle);
        const float gR = std::sin(angle);

        // Same harmonic shape as renderEngine, rpm-driven and doppler-scaled.
        const float rpmNorm = std::clamp(car.rpm / 9000.0f, 0.0f, 1.0f);
        const float freq = (30.0f + rpmNorm * 420.0f) * dop;
        const float level = atten * car.gain * 0.55f;
        const float step = freq * 6.2831853f / static_cast<float>(sampleRate);

        float ph = m_otherCarPhase[v];
        for (int i = 0; i < frames; ++i) {
            float s = 0.45f * std::sin(ph)
                    + 0.20f * std::sin(ph * 2.007f)
                    + 0.08f * std::sin(ph * 3.013f);
            s *= level;
            output[i * channels] += s * gL;
            if (channels > 1) output[i * channels + 1] += s * gR;
            ph += step;
        }
        m_otherCarPhase[v] = std::fmod(ph, 6.2831853f);
    }
}

void SimulatorAudio::renderRolling(float* output, int frames, int channels, int sampleRate)
{
    // Roadmap 2.3 (P2.2): continuous contact noise. renderSkid() only
    // fires under slip; this is what the tires make just *rolling*,
    // tinted per surface (asphalt hum, grass/gravel rumble, wet hiss) and
    // scaled with speed. Deterministic LCG noise + one-pole lowpass,
    // decorrelated L/R.
    const float speed = std::fabs(m_state.speed);
    if (speed < 0.5f || sampleRate <= 0) return;

    // Asphalt is the quiet surface: this hum sits below the slip-skid
    // event (test_audio_render pins skid > 2x the no-slip baseline, so a
    // louder asphalt layer would bury the event it is meant to flag).
    float gain = 0.015f;
    float cutoff = 700.0f;
    switch (m_state.surface) {
    case 1:  gain = 0.17f; cutoff = 1800.0f; break; // grass
    case 2:  gain = 0.19f; cutoff = 2400.0f; break; // gravel
    case 3:  gain = 0.12f; cutoff = 1400.0f; break; // kerb
    case 4:  gain = 0.11f; cutoff = 2200.0f; break; // wet
    case 5:  gain = 0.05f; cutoff = 500.0f;  break; // ice
    default: break;                                 // asphalt
    }
    // Speed build-up: nothing at a crawl, full tone from ~30 m/s on.
    gain *= std::min(1.0f, speed / 30.0f);
    // A wet asphalt film adds hiss on top of the hum.
    if (m_state.surface == 0 && m_state.wetness > 0.1f) {
        gain *= 1.0f + m_state.wetness * 0.5f;
        cutoff += m_state.wetness * 900.0f;
    }

    const float a = 1.0f - std::exp(-6.2831853f * cutoff /
                                    static_cast<float>(sampleRate));
    float lpL = m_rollingLpL;
    float lpR = m_rollingLpR;
    std::uint32_t rng = m_rollingRng;
    for (int i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float nL = static_cast<float>((rng >> 8) & 0x00FFFFFFu) / 8388608.0f - 1.0f;
        rng = rng * 1664525u + 1013904223u;
        const float nR = static_cast<float>((rng >> 8) & 0x00FFFFFFu) / 8388608.0f - 1.0f;
        lpL += a * (nL - lpL);
        lpR += a * (nR - lpR);
        output[i * channels] += lpL * gain;
        if (channels > 1) output[i * channels + 1] += lpR * gain;
    }
    m_rollingLpL = lpL;
    m_rollingLpR = lpR;
    m_rollingRng = rng;
}

} // namespace ks::sim
