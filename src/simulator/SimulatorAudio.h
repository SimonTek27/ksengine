#pragma once

#include "WASAPIOutput.h"
#include "AudioMixer.h"
#include "AudioBankManager.h"
#include "SoundsIniParser.h"
#include "Audio/AudioTypes.h"
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <unordered_map>

namespace ks::sim {

class SimulatorAudio {
public:
    enum class SoundCategory {
        EngineInterior,
        EngineExterior,
        Turbo,
        Wastegate,
        Blowoff,
        Wind,
        Transmission,
        TransmissionExt,
        SkidAsphalt,
        SkidGrass,
        SkidGravel,
        SkidKerb,
        SkidWet,
        GearShift,
        GearClonk,
        Brakes,
        Bodywork,
        Backfire,
        Limiter,
        Starter,
        Ignition,
        RainAmbient,
        RainCar,
        RainThunder,
        Wiper,
        CspSurfacesSkid,
        CspSurfacesIce,
        Count
    };

    enum class CameraMode { Cockpit, Chase, Free, TV, Replay };
    enum class SurfaceType { Asphalt, Grass, Gravel, Kerb, Wet, Ice, Count };

    SimulatorAudio();
    ~SimulatorAudio();

    /**
     * withOutput=true (default) opens the WASAPI render device and starts the
     * callback thread; a failed device open returns false (headless runs stay
     * silent). withOutput=false initializes headless: the mixer and the synth
     * fully work through renderOffline() without touching any device — used
     * by tests and machines without an output device.
     */
    bool initialize(bool withOutput = true);
    void shutdown();

    /**
     * Render one block on the caller's thread through the same path the
     * WASAPI callback uses (also the headless entry point).
     */
    void renderOffline(float* output, int frames, int channels = 2,
                       int sampleRate = 44100);

    bool loadCarAudio(const std::string& carDirectory);
    bool loadBank(const std::string& bankPath);

    /**
     * Apply race/upgrade sound pack on top of baseline car audio.
     * Paths absolute or already resolved against car folder.
     */
    bool applySoundPack(
        const std::string& bankPath,
        const std::string& soundsIniPath,
        const std::string& engineIniPath,
        float engineGain,
        float exteriorGain,
        float turboGain,
        const std::unordered_map<std::string, std::string>& sampleOverrides,
        const std::string& carDirectory);

    void update(float rpm, float throttle, float brake, float speed, int gear,
                CameraMode mode, float dt);

    void updatePhysics(float rpm, float throttle, float brake, float speed,
                       float steering, int gear, bool isShifting,
                       float slipRatio, float slipAngle,
                       SurfaceType surface, float suspensionDamage,
                       float brakeTemp, float boostPressure,
                       float windSpeed, float wetness, float rainIntensity,
                       float dt);

    void setListenerPosition(float x, float y, float z);
    void setListenerOrientation(float forwardX, float forwardY, float forwardZ,
                                float upX, float upY, float upZ);
    void setSourcePosition(SoundCategory cat, float x, float y, float z);

    void setMasterVolume(float v) { m_masterVolume.store(v); }
    void setEngineVolume(float v) { m_engineVolume.store(v); }
    void setEnvironmentVolume(float v) { m_environmentVolume.store(v); }
    void setInteriorMix(float v) { m_interiorMix.store(v); }

    float masterVolume() const { return m_masterVolume.load(); }
    float engineVolume() const { return m_engineVolume.load(); }
    bool isLoaded() const { return m_loaded.load(); }
    int soundCount() const { return m_totalSamples.load(); }
    const BankAudioData* bankData() const { return m_bankManager ? m_bankManager->data() : nullptr; }
    const SoundsIniData& soundsIni() const { return m_soundsIni; }

    void setCameraMode(CameraMode mode) { m_cameraMode.store(static_cast<int>(mode)); }

    struct EngineLayer {
        std::vector<float> samples;
        int sampleRate = 44100;
        int channels = 2;
        float rpmMin = 0;
        float rpmMax = 9000;
        float volume = 1.0f;
        bool loop = true;
    };

    struct SfxSample {
        std::vector<float> samples;
        int sampleRate = 44100;
        int channels = 2;
        bool loop = false;
        float volume = 1.0f;
    };

    struct AudioState {
        float rpm = 0, throttle = 0, brake = 0, speed = 0, steering = 0;
        int gear = 0;
        bool isShifting = false;
        float slipRatio = 0, slipAngle = 0;
        int surface = 0;
        float suspensionDamage = 0, brakeTemp = 0, boostPressure = 0;
        float windSpeed = 0, wetness = 0, rainIntensity = 0;
    };

    struct PlaybackState {
        float position = 0;
        bool playing = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        float fadeVolume = 1.0f;
    };

    enum ChannelId {
        ChEngineInt = 0, ChEngineExt, ChTurbo, ChWind, ChSkid, ChGearShift,
        ChBrakes, ChTransmission, ChBodywork, ChBackfire, ChLimiter,
        ChRainAmbient, ChRainCar, ChWiper, ChCspSkid, ChCount
    };

private:
    static constexpr int ENGINE_LAYERS_PER_SET = 8;
    static constexpr int MIX_BUFFER_SAMPLES = 2048;

    void renderAudio(float* output, int frames, int channels, int sampleRate);
    void renderEngine(float* output, int frames, int channels, int sampleRate);
    void renderWind(float* output, int frames, int channels, int sampleRate, float dt);
    void renderTurbo(float* output, int frames, int channels, int sampleRate, float dt);
    void renderSkid(float* output, int frames, int channels, int sampleRate, float dt);
    void renderGearShift(float* output, int frames, int channels, int sampleRate);
    void renderBrakes(float* output, int frames, int channels, int sampleRate);
    void renderTransmission(float* output, int frames, int channels, int sampleRate);
    void renderBodywork(float* output, int frames, int channels, int sampleRate);
    void renderBackfire(float* output, int frames, int channels, int sampleRate);
    void renderLimiter(float* output, int frames, int channels, int sampleRate);
    void renderAcoustics(float* output, int frames, int channels, int sampleRate);
    void renderSpatialization(float* output, int frames, int channels, int sampleRate);
    void renderModulation(float* output, int frames, int channels, int sampleRate);
    void renderWeather(float* output, int frames, int channels, int sampleRate);
    void renderCspSurfaces(float* output, int frames, int channels, int sampleRate);

    void generateSynthEngineLayer(EngineLayer& layer, float rpmMin, float rpmMax, int sampleRate);
    void generateSynthSfx(SfxSample& sfx, SoundCategory cat, int sampleRate);

    float sampleAt(const std::vector<float>& samples, float position, int channel, int totalChannels) const;

    std::array<EngineLayer, ENGINE_LAYERS_PER_SET> m_engineIntLayers;
    std::array<EngineLayer, ENGINE_LAYERS_PER_SET> m_engineExtLayers;
    std::array<SfxSample, static_cast<int>(SoundCategory::Count)> m_sfxSamples;
    std::array<PlaybackState, static_cast<int>(SoundCategory::Count)> m_playback;

    AudioState m_state;
    AudioState m_prevState;

    std::atomic<float> m_masterVolume{0.8f};
    std::atomic<float> m_engineVolume{1.0f};
    std::atomic<float> m_environmentVolume{0.6f};
    std::atomic<float> m_interiorMix{0.85f};
    std::atomic<int> m_cameraMode{0};

    float m_listenerX = 0, m_listenerY = 0, m_listenerZ = 0;
    float m_listenerForwardX = 0, m_listenerForwardY = 0, m_listenerForwardZ = -1;
    float m_listenerUpX = 0, m_listenerUpY = 1, m_listenerUpZ = 0;

    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_initialized{false};
    std::atomic<int> m_totalSamples{0};
    std::atomic<float> m_time{0};

    float m_prevRpm = 0;
    int m_prevGear = -1;
    float m_gearShiftTimer = 0;
    float m_backfireTimer = 0;
    float m_limiterTimer = 0;
    float m_windNoisePhase = 0;
    float m_turboPhase = 0;
    float m_skidPhase = 0;
    float m_transmissionPhase = 0;
    float m_thunderTimer = 0;
    float m_rainPhase = 0;

    std::unique_ptr<WASAPIOutput> m_wasapi;
    std::unique_ptr<AudioMixer> m_mixer;
    std::unique_ptr<AudioBankManager> m_bankManager;
    SoundsIniData m_soundsIni;
    struct ExtConfigData {
        std::unordered_map<std::string, float> volumeOverrides;
        std::unordered_map<std::string, float> pitchOverrides;
        std::unordered_map<std::string, std::pair<std::string, float>> parameterOverrides;
    } m_extConfig;
    int64_t m_sampleCounter = 0;

    mutable std::mutex m_stateMutex;

    static constexpr int MIX_BUFFER_MS = 20;
};

} // namespace ks::sim
