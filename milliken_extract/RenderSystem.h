#pragma once
/**
 * RenderSystem — Qt-free facade.
 * Full Vulkan work is in engine/sim/NativeRenderer; this keeps module API.
 */
#include "../EngineModule.h"
#include "GfxTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace ks {
namespace engine {
namespace graphics {

enum class RenderPass { Shadow, Geometry, Post };
enum class PostEffect { Bloom, TAA, MotionBlur, MSAA, Tonemap };

struct RainState {
    bool enabled = false;
    float intensity = 0.0f;
    float wetness = 0.0f;
    int particleCount = 0;
};

class RenderSystem : public EngineModule {
public:
    static RenderSystem& instance() {
        static RenderSystem s;
        return s;
    }

    std::string moduleName() const override { return "RenderSystem"; }
    std::string moduleId() const override { return "ks.render"; }

    bool initialize() override {
        m_initialized = true;
        m_effects[PostEffect::Tonemap] = true;
        return true;
    }
    void shutdown() override { m_initialized = false; }

    void beginFrame() {}
    void runPass(RenderPass) {}
    void endFrame() {
        if (onFrameReady) onFrameReady();
    }

    void setViewMatrix(const Mat4& v) { m_view = v; }
    void setProjectionMatrix(const Mat4& p) { m_proj = p; }
    void setSun(const Vec3& dir, const Vec3& color) {
        m_sunDir = dir;
        m_sunColor = color;
    }
    void setFog(bool e, const Vec3& c, float d) {
        m_fog = e;
        m_fogColor = c;
        m_fogDensity = d;
    }
    void enableVR(bool e) { m_vr = e; }
    void enableDeferred(bool e) { m_deferred = e; }

    void enableEffect(PostEffect e, bool on) { m_effects[e] = on; }
    bool isEffectEnabled(PostEffect e) const {
        auto it = m_effects.find(e);
        return it != m_effects.end() && it->second;
    }

    void setRain(float intensity, float wetness) {
        m_rain.intensity = intensity;
        m_rain.wetness = clampf(wetness, 0.f, 1.f);
        m_rain.enabled = intensity > 0.01f || m_rain.wetness > 0.01f;
    }
    RainState rain() const { return m_rain; }
    void setWetness(float w) {
        m_rain.wetness = clampf(w, 0.f, 1.f);
        m_rain.enabled = m_rain.intensity > 0.01f || m_rain.wetness > 0.01f;
    }
    float wetSpecular() const { return m_rain.wetness * 0.8f; }

    std::vector<RenderPass> passOrder() const {
        return {RenderPass::Shadow, RenderPass::Geometry, RenderPass::Post};
    }

    // std callbacks (replaces Qt signals)
    std::function<void()> onFrameReady;
    std::function<void(RenderPass)> onPassExecuted;

private:
    Mat4 m_view, m_proj;
    Vec3 m_sunDir{0, 1, 0}, m_sunColor{1, 1, 1}, m_fogColor{0.6f, 0.7f, 0.85f};
    bool m_fog = false;
    float m_fogDensity = 0.0001f;
    bool m_vr = false, m_deferred = true;
    std::unordered_map<PostEffect, bool> m_effects;
    RainState m_rain;
};

} // namespace graphics
} // namespace engine
} // namespace ks
