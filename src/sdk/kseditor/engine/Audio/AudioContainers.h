#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QUuid>
#include <QVariant>
#include <functional>
#include <random>

namespace ks { namespace audio {

// ============================================================================
// RTPC (Real-Time Parameter Control) — parameter-driven automation curves
// ============================================================================

struct RTPCKeyframe {
    float normalizedPosition = 0.0f;  // 0..1 along the parameter range
    float value = 0.0f;               // output value at this keyframe
    QString curveType = "linear";     // "linear", "scurve", "exponential", "logarithmic"

    RTPCKeyframe() = default;
    RTPCKeyframe(float pos, float val, const QString& curve = "linear")
        : normalizedPosition(pos), value(val), curveType(curve) {}
};

struct RTPCBinding {
    QString parameterName;              // game parameter that drives this binding
    float parameterMin = 0.0f;
    float parameterMax = 1.0f;
    QVector<RTPCKeyframe> keyframes;

    float evaluate(float parameterValue) const;
    void addKeyframe(const RTPCKeyframe& kf);
    void removeKeyframe(int index);
    void sortKeyframes();
};

// ============================================================================
// Switch Container — play different audio based on a switch parameter value
// ============================================================================

struct SwitchEntry {
    QString switchValue;       // e.g. "surface_asphalt", "surface_grass"
    QString audioFilePath;     // audio to play for this switch value
    float volume = 1.0f;
    float pitch = 1.0f;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
};

struct SwitchContainer {
    QString id;
    QString name;
    QString switchParameter;   // name of the parameter that selects the switch
    QVector<SwitchEntry> entries;
    QString defaultEntry;      // fallback switch value

    const SwitchEntry* findEntry(const QString& switchValue) const;
    const SwitchEntry* getDefaultEntry() const;
    void addEntry(const SwitchEntry& entry);
    void removeEntry(const QString& switchValue);
};

// ============================================================================
// Randomizer Container — play randomly selected audio with weighted random
// ============================================================================

struct RandomizerEntry {
    QString id;
    QString audioFilePath;
    float weight = 1.0f;        // probability weight (higher = more likely)
    float volume = 1.0f;
    float pitch = 1.0f;
    bool used = false;          // for "avoid repeat" mode

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
};

struct RandomizerContainer {
    QString id;
    QString name;
    QVector<RandomizerEntry> entries;
    bool avoidRepeat = false;   // don't play the same entry twice in a row
    bool shuffleOnPlay = false; // re-randomize order on each play

    QString getNextEntry();
    void reset();
    void addEntry(const RandomizerEntry& entry);
    void removeEntry(const QString& entryId);

private:
    int m_lastIndex = -1;
    std::mt19937 m_rng{std::random_device{}()};
};

// ============================================================================
// RTPC System — manages all RTPC bindings for a project
// ============================================================================

class RTPCSystem : public QObject {
    Q_OBJECT
public:
    explicit RTPCSystem(QObject* parent = nullptr);

    // Parameter management
    void registerParameter(const QString& name, float minVal, float maxVal, float defaultVal);
    void setParameterValue(const QString& name, float value);
    float getParameterValue(const QString& name) const;
    QStringList registeredParameters() const;

    // RTPC bindings
    void addBinding(const QString& targetId, const RTPCBinding& binding);
    void removeBinding(const QString& targetId);
    RTPCBinding* getBinding(const QString& targetId);
    float evaluateBinding(const QString& targetId) const;

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void parameterChanged(const QString& name, float value);
    void bindingChanged(const QString& targetId);

private:
    struct Parameter {
        QString name;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        float currentValue = 0.0f;
    };

    QMap<QString, Parameter> m_parameters;
    QMap<QString, RTPCBinding> m_bindings;
};

}} // namespace ks::audio
