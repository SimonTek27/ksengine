#include "AudioEffects.h"
#include <QDebug>
#include <functional>

namespace ks {
namespace audio {

// ============================================================================
// Effect registry — maps type name to parameter list + factory function
// ============================================================================

using ProcessFn = QVector<float>(*)(QObject*, const QVector<float>&, int);

struct EffectRegistration {
    QStringList params;
    std::function<QObject*()> create;
    ProcessFn process = nullptr;
};

template<typename T>
static QVector<float> processDsp(QObject* obj, const QVector<float>& input, int sampleRate) {
    return static_cast<T*>(obj)->process(input, sampleRate);
}

static QMap<QString, EffectRegistration>& registry()
{
    static QMap<QString, EffectRegistration> reg;
    static bool init = false;
    if (!init) {
        init = true;

        reg["Chorus"] = {
            {"rate", "depth", "mix"},
            []() -> QObject* { return new ChorusEffect(); },
            processDsp<ChorusEffect>
        };
        reg["Flanger"] = {
            {"rate", "depth", "feedback", "mix", "delay"},
            []() -> QObject* { return new FlangerEffect(); },
            processDsp<FlangerEffect>
        };
        reg["NoiseGate"] = {
            {"threshold", "ratio", "attack", "release"},
            []() -> QObject* { return new NoiseGate(); },
            processDsp<NoiseGate>
        };
        reg["NoiseReduction"] = {
            {"noiseFloor"},
            []() -> QObject* { return new NoiseReduction(); },
            processDsp<NoiseReduction>
        };
        reg["PitchShifter"] = {
            {"semitones"},
            []() -> QObject* { return new PitchShifter(); },
            processDsp<PitchShifter>
        };
        reg["SFXReverb"] = {
            {"roomSize", "damping", "wet", "dry"},
            []() -> QObject* { return new SFXReverb(); },
            processDsp<SFXReverb>
        };

        reg["ConvolutionReverb"] = {
            {"mix", "preDelay", "gain"},
            []() -> QObject* { return new ConvolutionReverb(); },
            processDsp<ConvolutionReverb>
        };
        reg["MultibandCompressor"] = {
            {"bandCount", "crossover0", "crossover1", "crossover2"},
            []() -> QObject* { return new MultibandCompressor(); },
            processDsp<MultibandCompressor>
        };
        reg["TapeEmulator"] = {
            {"drive", "mix", "wowRate", "wowDepth", "hissLevel", "bias"},
            []() -> QObject* { return new TapeEmulator(); },
            processDsp<TapeEmulator>
        };
        reg["GuitarAmpSimulator"] = {
            {"gain", "bass", "mid", "treble", "volume", "drive", "presence"},
            []() -> QObject* { return new GuitarAmpSimulator(); },
            processDsp<GuitarAmpSimulator>
        };
        reg["TransientDesigner"] = {
            {"attack", "sustain", "sensitivity"},
            []() -> QObject* { return new TransientDesigner(); },
            processDsp<TransientDesigner>
        };
        reg["StereoEnhancer"] = {
            {"width", "midGain", "sideGain", "swapChannels"},
            []() -> QObject* { return new StereoEnhancer(); },
            processDsp<StereoEnhancer>
        };

        reg["SpectralGate"] = {
            {"threshold", "floor", "attack", "release", "lowCut", "highCut"},
            []() -> QObject* { return new SpectralGate(); },
            processDsp<SpectralGate>
        };
        reg["FormantFilter"] = {
            {"shift", "formant1Freq", "formant1Gain", "formant1BW",
             "formant2Freq", "formant2Gain", "formant2BW",
             "formant3Freq", "formant3Gain", "formant3BW",
             "formant4Freq", "formant4Gain", "formant4BW",
             "formant5Freq", "formant5Gain", "formant5BW"},
            []() -> QObject* { return new FormantFilter(); },
            processDsp<FormantFilter>
        };
        reg["RingMod"] = {
            {"frequency", "mix"},
            []() -> QObject* { return new RingMod(); },
            processDsp<RingMod>
        };
        reg["AutoWah"] = {
            {"filterType", "freqMin", "freqMax", "resonance", "sensitivity",
             "attack", "release", "depth", "mix", "envelopeFollower", "lfoRate"},
            []() -> QObject* { return new AutoWah(); },
            processDsp<AutoWah>
        };
        reg["BitCrusher"] = {
            {"bitDepth", "sampleRateReduction", "mix", "noiseShaping"},
            []() -> QObject* { return new BitCrusher(); },
            processDsp<BitCrusher>
        };
        reg["Ducker"] = {
            {"threshold", "depth", "attack", "release", "sidechainGain", "mix"},
            []() -> QObject* { return new Ducker(); },
            processDsp<Ducker>
        };
        reg["DeEsser"] = {
            {"threshold", "frequency", "bandwidth", "reduction",
             "attack", "release", "mix", "listenMode"},
            []() -> QObject* { return new DeEsser(); },
            processDsp<DeEsser>
        };
        reg["SaturationDistortion"] = {
            {"drive", "bias", "mix", "type", "outputGain"},
            []() -> QObject* { return new SaturationDistortion(); },
            processDsp<SaturationDistortion>
        };
        reg["TremoloModulation"] = {
            {"rate", "depth", "shape", "waveform", "mix", "stereoPhase"},
            []() -> QObject* { return new TremoloModulation(); },
            processDsp<TremoloModulation>
        };
        reg["MultiBandSplitter"] = {
            {"bandCount", "crossover0", "crossover1", "crossover2",
             "band0Gain", "band1Gain", "band2Gain", "band3Gain",
             "band0Mute", "band1Mute", "band2Mute", "band3Mute",
             "band0Solo", "band1Solo", "band2Solo", "band3Solo"},
            []() -> QObject* { return new MultiBandSplitter(); },
            processDsp<MultiBandSplitter>
        };
    }
    return reg;
}

// ============================================================================
// EffectInstance — one slot in the processing chain
// ============================================================================

struct EffectInstance {
    QString type;
    bool bypassed = false;
    QMap<QString, double> params;
    QSharedPointer<QObject> dsp;

    EffectInstance() = default;
};

// ============================================================================
// Global effect chain
// ============================================================================

static QVector<EffectInstance> s_chain;
static AudioEffects* s_masterChain = nullptr; // EQ + compressor + delay + reverb + limiter

AudioEffects* masterChain()
{
    if (!s_masterChain)
        s_masterChain = new AudioEffects();
    return s_masterChain;
}

// ============================================================================
// Public API Implementation
// ============================================================================

QStringList availableEffectTypes()
{
    return registry().keys();
}

QStringList effectParameters(const QString& type)
{
    auto it = registry().find(type);
    if (it != registry().end())
        return it->params;
    return {};
}

int addEffect(const QString& type)
{
    auto it = registry().find(type);
    if (it == registry().end()) {
        qWarning() << "AudioEffects: unknown effect type" << type;
        return -1;
    }

    EffectInstance inst;
    inst.type = type;
    inst.bypassed = false;
    inst.dsp.reset(it->create());

    // Populate default parameter values
    const auto& reg = registry();
    auto rit = reg.find(type);
    for (const auto& p : rit->params) {
        if (inst.dsp) {
            QVariant val = inst.dsp->property(p.toUtf8().constData());
            if (val.isValid())
                inst.params[p] = val.toDouble();
            else
                inst.params[p] = 0.0;
        } else {
            inst.params[p] = 0.0;
        }
    }

    s_chain.append(std::move(inst));
    return s_chain.size() - 1;
}

void removeEffect(int index)
{
    if (index >= 0 && index < s_chain.size())
        s_chain.removeAt(index);
}

void clearEffects()
{
    s_chain.clear();
}

int effectCount()
{
    return s_chain.size();
}

QString effectTypeAt(int index)
{
    if (index >= 0 && index < s_chain.size())
        return s_chain[index].type;
    return {};
}

bool isEffectBypassed(int index)
{
    if (index >= 0 && index < s_chain.size())
        return s_chain[index].bypassed;
    return false;
}

void bypassEffect(int index, bool bypassed)
{
    if (index >= 0 && index < s_chain.size())
        s_chain[index].bypassed = bypassed;
}

void setEffectParam(int index, const QString& param, double value)
{
    if (index < 0 || index >= s_chain.size())
        return;
    auto& inst = s_chain[index];
    inst.params[param] = value;

    if (inst.dsp) {
        inst.dsp->setProperty(param.toUtf8().constData(), static_cast<float>(value));
    }
}

double getEffectParam(int index, const QString& param)
{
    if (index >= 0 && index < s_chain.size()) {
        auto it = s_chain[index].params.find(param);
        if (it != s_chain[index].params.end())
            return it.value();
    }
    return 0.0;
}

QMap<QString, double> allEffectParams(int index)
{
    if (index >= 0 && index < s_chain.size())
        return s_chain[index].params;
    return {};
}

void resetEffect(int index)
{
    if (index < 0 || index >= s_chain.size())
        return;
    auto& inst = s_chain[index];
    inst.params.clear();

    if (inst.dsp) {
        QMetaObject::invokeMethod(inst.dsp.data(), "reset", Qt::DirectConnection);

        const auto& reg = registry();
        auto rit = reg.find(inst.type);
        if (rit != reg.end()) {
            for (const auto& p : rit->params) {
                QVariant val = inst.dsp->property(p.toUtf8().constData());
                inst.params[p] = val.isValid() ? val.toDouble() : 0.0;
            }
        }
    }
}

void moveEffect(int from, int to)
{
    if (from < 0 || from >= s_chain.size() || to < 0 || to >= s_chain.size() || from == to)
        return;
    EffectInstance inst = std::move(s_chain[from]);
    s_chain.removeAt(from);
    if (to > from) --to;
    s_chain.insert(to, std::move(inst));
}

// ============================================================================
// Audio processing — run the chain + master chain on samples
// ============================================================================

QVector<float> processEffects(const QVector<float>& input, int sampleRate)
{
    QVector<float> output = input;

    for (auto& inst : s_chain) {
        if (inst.bypassed || !inst.dsp)
            continue;

        auto it = registry().find(inst.type);
        if (it != registry().end() && it->process) {
            output = it->process(inst.dsp.data(), output, sampleRate);
        }
    }

    if (s_masterChain && !s_masterChain->isBypassed())
        output = s_masterChain->process(output);

    return output;
}

// ============================================================================
// Master chain (AudioEffects) proxy
// ============================================================================

void setMasterBypass(bool bypass)
{
    masterChain()->setBypass(bypass);
}

bool masterBypassed()
{
    return masterChain()->isBypassed();
}

void setEqBand(int band, float gain)
{
    masterChain()->setEqBandGain(band, gain);
}

float getEqBand(int band)
{
    return masterChain()->getEqBandGain(band);
}

int eqBandCount()
{
    return masterChain()->getEqBandCount();
}

void setCompressor(float threshold, float ratio, float attack, float release)
{
    masterChain()->setCompressorThreshold(threshold);
    masterChain()->setCompressorRatio(ratio);
    masterChain()->setCompressorAttack(attack);
    masterChain()->setCompressorRelease(release);
}

void setReverb(float roomSize, float damping, float wet, float dry)
{
    masterChain()->setReverbRoomSize(roomSize);
    masterChain()->setReverbDamping(damping);
    masterChain()->setReverbWetLevel(wet);
    masterChain()->setReverbDryLevel(dry);
}

void setDelay(float time, float feedback, float mix)
{
    masterChain()->setDelayTime(time);
    masterChain()->setDelayFeedback(feedback);
    masterChain()->setDelayMix(mix);
}

void setLimiter(float threshold, float release)
{
    masterChain()->setLimiterThreshold(threshold);
    masterChain()->setLimiterRelease(release);
}

void resetMasterChain()
{
    masterChain()->reset();
}

QJsonObject saveMasterPreset(const QString& name)
{
    return masterChain()->savePreset(name);
}

void loadMasterPreset(const QJsonObject& preset)
{
    masterChain()->loadPreset(preset);
}

}} // ks::audio