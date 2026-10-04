#include "AudioEffectsQmlBridge.h"
#include "sdk/kseditor/engine/Audio/AudioEffects.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace ks {
namespace audio {

AudioEffectsQmlBridge* AudioEffectsQmlBridge::s_instance = nullptr;

AudioEffectsQmlBridge::AudioEffectsQmlBridge(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

AudioEffectsQmlBridge::~AudioEffectsQmlBridge()
{
    s_instance = nullptr;
}

AudioEffectsQmlBridge* AudioEffectsQmlBridge::instance()
{
    if (!s_instance)
        s_instance = new AudioEffectsQmlBridge();
    return s_instance;
}

int AudioEffectsQmlBridge::effectCount() const
{
    return ks::audio::effectCount();
}

bool AudioEffectsQmlBridge::masterBypassed() const
{
    return ks::audio::masterBypassed();
}

int AudioEffectsQmlBridge::eqBandCount() const
{
    return ks::audio::eqBandCount();
}

QStringList AudioEffectsQmlBridge::availableEffectTypes() const
{
    return ks::audio::availableEffectTypes();
}

QStringList AudioEffectsQmlBridge::effectParameters(const QString& type) const
{
    return ks::audio::effectParameters(type);
}

int AudioEffectsQmlBridge::addEffect(const QString& type)
{
    int idx = ks::audio::addEffect(type);
    if (idx >= 0)
        emit effectChainChanged();
    return idx;
}

void AudioEffectsQmlBridge::removeEffect(int index)
{
    ks::audio::removeEffect(index);
    emit effectChainChanged();
}

void AudioEffectsQmlBridge::clearEffects()
{
    ks::audio::clearEffects();
    emit effectChainChanged();
}

QString AudioEffectsQmlBridge::effectTypeAt(int index) const
{
    return ks::audio::effectTypeAt(index);
}

bool AudioEffectsQmlBridge::isEffectBypassed(int index) const
{
    return ks::audio::isEffectBypassed(index);
}

void AudioEffectsQmlBridge::bypassEffect(int index, bool bypassed)
{
    ks::audio::bypassEffect(index, bypassed);
    emit effectChainChanged();
}

void AudioEffectsQmlBridge::moveEffect(int from, int to)
{
    ks::audio::moveEffect(from, to);
    emit effectChainChanged();
}

void AudioEffectsQmlBridge::resetEffect(int index)
{
    ks::audio::resetEffect(index);
    emit effectParamChanged(index, QString(), 0.0);
}

void AudioEffectsQmlBridge::setEffectParam(int index, const QString& param, double value)
{
    ks::audio::setEffectParam(index, param, value);
    emit effectParamChanged(index, param, value);
}

double AudioEffectsQmlBridge::getEffectParam(int index, const QString& param) const
{
    return ks::audio::getEffectParam(index, param);
}

QVariantMap AudioEffectsQmlBridge::getEffectParams(int index) const
{
    QMap<QString, double> params = ks::audio::allEffectParams(index);
    QVariantMap result;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        result[it.key()] = it.value();
    return result;
}

QVariantList AudioEffectsQmlBridge::getEffectChain() const
{
    QVariantList chain;
    int count = ks::audio::effectCount();
    for (int i = 0; i < count; ++i) {
        QVariantMap entry;
        entry["index"] = i;
        entry["type"] = ks::audio::effectTypeAt(i);
        entry["bypassed"] = ks::audio::isEffectBypassed(i);
        entry["params"] = QVariant::fromValue(getEffectParams(i));
        chain.append(entry);
    }
    return chain;
}

void AudioEffectsQmlBridge::setMasterBypass(bool bypass)
{
    ks::audio::setMasterBypass(bypass);
    emit masterBypassChanged();
}

void AudioEffectsQmlBridge::setEqBand(int band, float gain)
{
    ks::audio::setEqBand(band, gain);
    emit masterSettingsChanged();
}

float AudioEffectsQmlBridge::getEqBand(int band) const
{
    return ks::audio::getEqBand(band);
}

void AudioEffectsQmlBridge::setCompressor(float threshold, float ratio, float attack, float release)
{
    ks::audio::setCompressor(threshold, ratio, attack, release);
    emit masterSettingsChanged();
}

void AudioEffectsQmlBridge::setReverb(float roomSize, float damping, float wet, float dry)
{
    ks::audio::setReverb(roomSize, damping, wet, dry);
    emit masterSettingsChanged();
}

void AudioEffectsQmlBridge::setDelay(float time, float feedback, float mix)
{
    ks::audio::setDelay(time, feedback, mix);
    emit masterSettingsChanged();
}

void AudioEffectsQmlBridge::setLimiter(float threshold, float release)
{
    ks::audio::setLimiter(threshold, release);
    emit masterSettingsChanged();
}

void AudioEffectsQmlBridge::resetMasterChain()
{
    ks::audio::resetMasterChain();
    emit masterSettingsChanged();
}

QVariantMap AudioEffectsQmlBridge::savePreset(const QString& name) const
{
    QJsonObject preset = ks::audio::saveMasterPreset(name);
    return QJsonDocument(preset).object().toVariantMap();
}

void AudioEffectsQmlBridge::loadPreset(const QVariantMap& preset)
{
    QJsonObject obj = QJsonObject::fromVariantMap(preset);
    ks::audio::loadMasterPreset(obj);
    emit masterSettingsChanged();
}

void AudioEffectsQmlBridge::applyEqPreset(const QString& name)
{
    struct Preset { float bands[10]; };
    static const QMap<QString, Preset> presets = {
        { "Flat",       {0,0,0,0,0,0,0,0,0,0} },
        { "Voice",      {-2,-1,0,2,3,4,3,2,1,0} },
        { "Bass Boost", {5,4,3,1,0,-1,-2,-3,-4,-5} },
        { "Treble",     {-4,-3,-2,-1,0,1,2,3,4,5} },
        { "Rock",       {4,3,2,1,0,0,1,2,3,4} },
        { "Pop",        {2,3,2,1,0,-1,0,1,2,3} },
        { "Custom",     {} }
    };

    auto it = presets.find(name);
    if (it == presets.end()) return;

    const Preset& p = it.value();
    for (int i = 0; i < 10; ++i)
        setEqBand(i, p.bands[i]);

    emit masterSettingsChanged();
}

QVariantList AudioEffectsQmlBridge::processAudio(const QVariantList& samples, int sampleRate)
{
    QVector<float> input(samples.size());
    for (int i = 0; i < samples.size(); ++i)
        input[i] = static_cast<float>(samples[i].toDouble());

    QVector<float> output = ks::audio::processEffects(input, sampleRate);

    QVariantList result;
    result.reserve(output.size());
    for (float s : output)
        result.append(static_cast<double>(s));
    return result;
}

} // namespace audio
} // namespace ks
