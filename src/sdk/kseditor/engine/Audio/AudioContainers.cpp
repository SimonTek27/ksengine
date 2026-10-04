#include "AudioContainers.h"
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace ks { namespace audio {

// ============================================================================
// RTPCBinding
// ============================================================================

float RTPCBinding::evaluate(float parameterValue) const
{
    if (keyframes.isEmpty()) return 0.0f;
    if (keyframes.size() == 1) return keyframes.first().value;

    // Normalize parameter to 0..1
    float range = parameterMax - parameterMin;
    float normalized = (range > 0.0f) ? (parameterValue - parameterMin) / range : 0.0f;
    normalized = qBound(0.0f, normalized, 1.0f);

    // Find surrounding keyframes
    for (int i = 0; i < keyframes.size() - 1; ++i) {
        if (normalized >= keyframes[i].normalizedPosition &&
            normalized <= keyframes[i + 1].normalizedPosition) {
            float dt = keyframes[i + 1].normalizedPosition - keyframes[i].normalizedPosition;
            if (dt <= 0.0f) return keyframes[i].value;

            float t = (normalized - keyframes[i].normalizedPosition) / dt;

            // Apply curve type
            const QString& curve = keyframes[i].curveType;
            if (curve == "scurve") {
                // Smooth S-curve
                t = t * t * (3.0f - 2.0f * t);
            } else if (curve == "exponential") {
                t = t * t;
            } else if (curve == "logarithmic") {
                t = 1.0f - (1.0f - t) * (1.0f - t);
            }
            // "linear" uses t as-is

            return keyframes[i].value + t * (keyframes[i + 1].value - keyframes[i].value);
        }
    }

    if (normalized < keyframes.first().normalizedPosition) return keyframes.first().value;
    return keyframes.last().value;
}

void RTPCBinding::addKeyframe(const RTPCKeyframe& kf)
{
    keyframes.append(kf);
    sortKeyframes();
}

void RTPCBinding::removeKeyframe(int index)
{
    if (index >= 0 && index < keyframes.size()) {
        keyframes.removeAt(index);
    }
}

void RTPCBinding::sortKeyframes()
{
    std::sort(keyframes.begin(), keyframes.end(),
              [](const RTPCKeyframe& a, const RTPCKeyframe& b) {
                  return a.normalizedPosition < b.normalizedPosition;
              });
}

// ============================================================================
// SwitchContainer
// ============================================================================

QJsonObject SwitchEntry::toJson() const
{
    QJsonObject obj;
    obj["switchValue"] = switchValue;
    obj["audioFilePath"] = audioFilePath;
    obj["volume"] = static_cast<double>(volume);
    obj["pitch"] = static_cast<double>(pitch);
    return obj;
}

void SwitchEntry::fromJson(const QJsonObject& obj)
{
    switchValue = obj["switchValue"].toString();
    audioFilePath = obj["audioFilePath"].toString();
    volume = static_cast<float>(obj["volume"].toDouble(1.0));
    pitch = static_cast<float>(obj["pitch"].toDouble(1.0));
}

const SwitchEntry* SwitchContainer::findEntry(const QString& switchValue) const
{
    for (const auto& entry : entries) {
        if (entry.switchValue == switchValue) return &entry;
    }
    return nullptr;
}

const SwitchEntry* SwitchContainer::getDefaultEntry() const
{
    return findEntry(defaultEntry);
}

void SwitchContainer::addEntry(const SwitchEntry& entry)
{
    // Remove existing entry with same switch value
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries[i].switchValue == entry.switchValue) {
            entries.removeAt(i);
            break;
        }
    }
    entries.append(entry);
}

void SwitchContainer::removeEntry(const QString& switchValue)
{
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries[i].switchValue == switchValue) {
            entries.removeAt(i);
            return;
        }
    }
}

// ============================================================================
// RandomizerContainer
// ============================================================================

QJsonObject RandomizerEntry::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["audioFilePath"] = audioFilePath;
    obj["weight"] = static_cast<double>(weight);
    obj["volume"] = static_cast<double>(volume);
    obj["pitch"] = static_cast<double>(pitch);
    return obj;
}

void RandomizerEntry::fromJson(const QJsonObject& obj)
{
    id = obj["id"].toString();
    audioFilePath = obj["audioFilePath"].toString();
    weight = static_cast<float>(obj["weight"].toDouble(1.0));
    volume = static_cast<float>(obj["volume"].toDouble(1.0));
    pitch = static_cast<float>(obj["pitch"].toDouble(1.0));
}

QString RandomizerContainer::getNextEntry()
{
    if (entries.isEmpty()) return QString();

    if (shuffleOnPlay) {
        // Weighted random selection
        float totalWeight = 0.0f;
        for (const auto& entry : entries) {
            if (!avoidRepeat || !entry.used) {
                totalWeight += entry.weight;
            }
        }

        if (totalWeight <= 0.0f) {
            // All entries used, reset
            reset();
            totalWeight = 0.0f;
            for (const auto& entry : entries) {
                totalWeight += entry.weight;
            }
        }

        std::uniform_real_distribution<float> dist(0.0f, totalWeight);
        float randomValue = dist(m_rng);

        float cumulative = 0.0f;
        for (int i = 0; i < entries.size(); ++i) {
            if (avoidRepeat && entries[i].used) continue;
            cumulative += entries[i].weight;
            if (randomValue <= cumulative) {
                if (avoidRepeat) {
                    entries[i].used = true;
                    // Check if all entries are used
                    bool allUsed = true;
                    for (const auto& e : entries) {
                        if (!e.used) { allUsed = false; break; }
                    }
                    if (allUsed) reset();
                }
                m_lastIndex = i;
                return entries[i].audioFilePath;
            }
        }
    }

    // Simple sequential with wrap
    int nextIndex = (m_lastIndex + 1) % entries.size();
    m_lastIndex = nextIndex;
    return entries[nextIndex].audioFilePath;
}

void RandomizerContainer::reset()
{
    for (auto& entry : entries) {
        entry.used = false;
    }
    m_lastIndex = -1;
}

void RandomizerContainer::addEntry(const RandomizerEntry& entry)
{
    entries.append(entry);
}

void RandomizerContainer::removeEntry(const QString& entryId)
{
    for (int i = entries.size() - 1; i >= 0; --i) {
        if (entries[i].id == entryId) {
            entries.removeAt(i);
            return;
        }
    }
}

// ============================================================================
// RTPCSystem
// ============================================================================

RTPCSystem::RTPCSystem(QObject* parent)
    : QObject(parent) {}

void RTPCSystem::registerParameter(const QString& name, float minVal, float maxVal, float defaultVal)
{
    Parameter param;
    param.name = name;
    param.minValue = minVal;
    param.maxValue = maxVal;
    param.defaultValue = defaultVal;
    param.currentValue = defaultVal;
    m_parameters[name] = param;
}

void RTPCSystem::setParameterValue(const QString& name, float value)
{
    if (!m_parameters.contains(name)) return;

    Parameter& param = m_parameters[name];
    param.currentValue = qBound(param.minValue, value, param.maxValue);
    emit parameterChanged(name, param.currentValue);
}

float RTPCSystem::getParameterValue(const QString& name) const
{
    if (!m_parameters.contains(name)) return 0.0f;
    return m_parameters[name].currentValue;
}

QStringList RTPCSystem::registeredParameters() const
{
    return m_parameters.keys();
}

void RTPCSystem::addBinding(const QString& targetId, const RTPCBinding& binding)
{
    m_bindings[targetId] = binding;
    emit bindingChanged(targetId);
}

void RTPCSystem::removeBinding(const QString& targetId)
{
    m_bindings.remove(targetId);
}

RTPCBinding* RTPCSystem::getBinding(const QString& targetId)
{
    return m_bindings.contains(targetId) ? &m_bindings[targetId] : nullptr;
}

float RTPCSystem::evaluateBinding(const QString& targetId) const
{
    if (!m_bindings.contains(targetId)) return 0.0f;

    const RTPCBinding& binding = m_bindings[targetId];
    if (!m_parameters.contains(binding.parameterName)) return 0.0f;

    float paramValue = m_parameters[binding.parameterName].currentValue;
    return binding.evaluate(paramValue);
}

QJsonObject RTPCSystem::toJson() const
{
    QJsonObject root;

    // Parameters
    QJsonArray paramsArr;
    for (auto it = m_parameters.constBegin(); it != m_parameters.constEnd(); ++it) {
        QJsonObject po;
        po["name"] = it.value().name;
        po["min"] = static_cast<double>(it.value().minValue);
        po["max"] = static_cast<double>(it.value().maxValue);
        po["default"] = static_cast<double>(it.value().defaultValue);
        paramsArr.append(po);
    }
    root["parameters"] = paramsArr;

    // Bindings
    QJsonArray bindingsArr;
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        QJsonObject bo;
        bo["targetId"] = it.key();
        bo["parameter"] = it.value().parameterName;
        bo["paramMin"] = static_cast<double>(it.value().parameterMin);
        bo["paramMax"] = static_cast<double>(it.value().parameterMax);

        QJsonArray kfsArr;
        for (const auto& kf : it.value().keyframes) {
            QJsonObject kfo;
            kfo["pos"] = static_cast<double>(kf.normalizedPosition);
            kfo["value"] = static_cast<double>(kf.value);
            kfo["curve"] = kf.curveType;
            kfsArr.append(kfo);
        }
        bo["keyframes"] = kfsArr;
        bindingsArr.append(bo);
    }
    root["bindings"] = bindingsArr;

    return root;
}

void RTPCSystem::fromJson(const QJsonObject& obj)
{
    m_parameters.clear();
    m_bindings.clear();

    // Load parameters
    QJsonArray paramsArr = obj["parameters"].toArray();
    for (const auto& p : paramsArr) {
        QJsonObject po = p.toObject();
        registerParameter(po["name"].toString(),
                         static_cast<float>(po["min"].toDouble()),
                         static_cast<float>(po["max"].toDouble()),
                         static_cast<float>(po["default"].toDouble()));
    }

    // Load bindings
    QJsonArray bindingsArr = obj["bindings"].toArray();
    for (const auto& b : bindingsArr) {
        QJsonObject bo = b.toObject();
        RTPCBinding binding;
        binding.parameterName = bo["parameter"].toString();
        binding.parameterMin = static_cast<float>(bo["paramMin"].toDouble());
        binding.parameterMax = static_cast<float>(bo["paramMax"].toDouble());

        QJsonArray kfsArr = bo["keyframes"].toArray();
        for (const auto& kf : kfsArr) {
            QJsonObject kfo = kf.toObject();
            RTPCKeyframe keyframe;
            keyframe.normalizedPosition = static_cast<float>(kfo["pos"].toDouble());
            keyframe.value = static_cast<float>(kfo["value"].toDouble());
            keyframe.curveType = kfo["curve"].toString("linear");
            binding.keyframes.append(keyframe);
        }

        m_bindings[bo["targetId"].toString()] = binding;
    }
}

}} // namespace ks::audio
