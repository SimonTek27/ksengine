#pragma once
/**
 * PostProcessing stub — Qt-free.
 * Real post FX live in Vulkan/NativeRenderer; this keeps API for sim callers.
 */
#include <cstdint>

namespace ks {
namespace sim {

class PostProcessing {
public:
    PostProcessing() = default;
    ~PostProcessing() = default;

    bool initialize() { m_initialized = true; return true; }
    void shutdown() { m_initialized = false; }

    void apply(int /*width*/, int /*height*/) {}

    void setBloomEnabled(bool e) { m_bloomEnabled = e; }
    void setBloomIntensity(float i) { m_bloomIntensity = i; }
    void setVignetteIntensity(float i) { m_vignetteIntensity = i; }
    void setFogEnabled(bool e) { m_fogEnabled = e; }
    void setFogDensity(float d) { m_fogDensity = d; }

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized = false;
    bool m_bloomEnabled = false;
    float m_bloomIntensity = 0.5f;
    float m_vignetteIntensity = 0.3f;
    bool m_fogEnabled = false;
    float m_fogDensity = 0.01f;
};

} // namespace sim
} // namespace ks
