#pragma once

/**
 * @file WeatherConfig.h
 * @brief Weather configuration model for MVC architecture
 * @copyright KS Physics Engine
 */

#include "PhysicsCoreTypes.h"
#include <vector>
#include <string>
namespace ks {
namespace physics {
namespace config {

struct WeatherKeyframe {
    float time = 0.0f;
    WeatherState state;
    std::string interpolation = "linear";
};

struct WeatherSequence {
    std::string name;
    float duration = 300.0f;
    bool loop = false;
    std::vector<WeatherKeyframe> keyframes;
};

class WeatherConfig {
    public:
    explicit WeatherConfig();
    ~WeatherConfig() override = default;
    
    void addSequence(const WeatherSequence& sequence);
    void removeSequence(int index);
    void updateSequence(int index, const WeatherSequence& sequence);
    std::vector<WeatherSequence> sequences() const { return m_sequences; }
    
    void addKeyframe(int sequenceIndex, const WeatherKeyframe& keyframe);
    void removeKeyframe(int sequenceIndex, int keyframeIndex);
    void updateKeyframe(int sequenceIndex, int keyframeIndex, const WeatherKeyframe& keyframe);
    
    int sequenceCount() const { return m_sequences.size(); }
    int keyframeCount(int sequenceIndex) const;
    WeatherState interpolateWeather(int sequenceIndex, float time) const;
    
    bool validate() const;
    std::string validationError() const;
    
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
    
    bool loadFromFile(const std::string& filePath);
    bool saveToFile(const std::string& filePath) const;
    
    void reset();
    void loadDefaults();
    
signals:
    void sequenceAdded(int index);
    void sequenceRemoved(int index);
    void sequenceUpdated(int index);
    void keyframeAdded(int sequenceIndex, int keyframeIndex);
    void keyframeRemoved(int sequenceIndex, int keyframeIndex);
    void keyframeUpdated(int sequenceIndex, int keyframeIndex);
    void configChanged();
    
private:
    WeatherState interpolateLinear(const WeatherState& a, const WeatherState& b, float t) const;
    WeatherState interpolateSmooth(const WeatherState& a, const WeatherState& b, float t) const;
    float clamp(float value, float min, float max) const;
    
    std::vector<WeatherSequence> m_sequences;
    mutable std::string m_validationError;
};

} // namespace config
} // namespace physics
} // namespace ks
