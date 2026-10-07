#pragma once
#include "../EngineModule.h"
#include <QObject>
#include <QVector3D>
#include <QMatrix4x4>
#include <QVector>
#include <QMap>

namespace ks::engine::graphics {

enum class RenderPass { Shadow, Geometry, Post };
enum class PostEffect { Bloom, TAA, MotionBlur, MSAA, Tonemap };

struct RainState {
    bool enabled = false;
    float intensity = 0.0f;
    float wetness = 0.0f;
    int particleCount = 0;
};

class RenderSystem : public QObject, public EngineModule {
    Q_OBJECT
public:
    static RenderSystem& instance(){ static RenderSystem s; return s; }
    QString moduleName() const override { return "RenderSystem"; }
    QString moduleId() const override { return "ks.render"; }
    bool initialize() override;
    void shutdown() override;

    void beginFrame();
    void runPass(RenderPass p);
    void endFrame();
    void setViewMatrix(const QMatrix4x4& v){ m_view = v; }
    void setProjectionMatrix(const QMatrix4x4& p){ m_proj = p; }
    void setSun(const QVector3D& dir, const QVector3D& color){ m_sunDir = dir; m_sunColor = color; }
    void setFog(bool e, const QVector3D& c, float d){ m_fog = e; m_fogColor = c; m_fogDensity = d; }
    void enableVR(bool e){ m_vr = e; }
    void enableDeferred(bool e){ m_deferred = e; }
    void enableEffect(PostEffect e, bool on);
    bool isEffectEnabled(PostEffect e) const { return m_effects.contains(e) && m_effects[e]; }

    void setRain(float intensity, float wetness);
    RainState rain() const { return m_rain; }
    void setWetness(float w) { m_rain.wetness = qBound(0.0f, w, 1.0f); m_rain.enabled = m_rain.intensity > 0.01f || m_rain.wetness > 0.01f; }
    float wetSpecular() const { return m_rain.wetness * 0.8f; }

    QVector<RenderPass> passOrder() const { return {RenderPass::Shadow, RenderPass::Geometry, RenderPass::Post}; }

signals:
    void frameReady();
    void passExecuted(RenderPass p);

private:
    QMatrix4x4 m_view, m_proj;
    QVector3D m_sunDir{0,1,0}, m_sunColor{1,1,1}, m_fogColor{0.6f,0.7f,0.85f};
    bool m_fog = false; float m_fogDensity = 0.0001f;
    bool m_vr = false, m_deferred = true;
    QMap<PostEffect,bool> m_effects;
    RainState m_rain;
};

} // namespace ks::engine::graphics
