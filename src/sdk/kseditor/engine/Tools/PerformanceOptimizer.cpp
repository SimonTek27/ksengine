#include "PerformanceOptimizer.h"
#include <QDebug>
#include <QElapsedTimer>

namespace ks {

PerformanceOptimizer* PerformanceOptimizer::s_instance = nullptr;

PerformanceOptimizer* PerformanceOptimizer::instance()
{
    if (!s_instance) s_instance = new PerformanceOptimizer();
    return s_instance;
}

PerformanceOptimizer::PerformanceOptimizer(QObject* parent) : QObject(parent) {}
PerformanceOptimizer::~PerformanceOptimizer() { s_instance = nullptr; }

void PerformanceOptimizer::registerProfile(const OptimizationProfile& p)
{
    m_profiles.insert(p.id, p);
}

void PerformanceOptimizer::unregisterProfile(const QString& id) { m_profiles.remove(id); }

void PerformanceOptimizer::setCurrentProfile(const QString& id)
{
    if (!m_profiles.contains(id)) return;
    m_currentProfileId = id;
    applyProfile(m_profiles[id]);
    emit profileChanged(id);
}

void PerformanceOptimizer::applyProfile(const OptimizationProfile& p)
{
    qDebug() << "[PerformanceOptimizer] Applying profile:" << p.name;
    m_textureOpt.maxTextureSize = qMin(m_textureOpt.maxTextureSize, p.maxTextureSize);
    m_meshOpt.maxVertices = qMin(m_meshOpt.maxVertices, p.maxVertices);
    m_meshOpt.mergeMeshes = p.mergeMeshes;
    m_meshOpt.generateLODs = p.generateLODs;
    m_meshOpt.lodReduction = p.lodReduction;
    m_meshOpt.optimizeUVs = p.optimizeUVs;
    m_textureOpt.atlasTextures = p.atlasTextures;
    m_textureOpt.compressTextures = p.compressTextures;
    m_textureOpt.textureQuality = p.textureQuality;
    emit optimizationApplied(p.id);
}

void PerformanceOptimizer::applyOptimization(const QString& category)
{
    qDebug() << "[PerformanceOptimizer] Applying optimization category:" << category;
    if (category == "textures") {
        m_textureOpt.maxTextureSize = qMax(512, m_textureOpt.maxTextureSize / 2);
        m_textureOpt.compressTextures = true;
        m_textureOpt.textureQuality = 75;
    } else if (category == "meshes") {
        m_meshOpt.maxVertices = qMax(10000, m_meshOpt.maxVertices / 2);
        m_meshOpt.mergeMeshes = true;
        m_meshOpt.generateLODs = true;
    } else if (category == "shadows") {
        m_shadowOpt.shadowResolution = 1024;
        m_shadowOpt.shadowCascades = 2;
    } else if (category == "lighting") {
        m_lightingOpt.maxLights = 4;
        m_lightingOpt.disableShadows = true;
    }
    emit optimizationApplied(category);
}

void PerformanceOptimizer::setMeshOptimization(const MeshOptimization& opt)
{
    m_meshOpt = opt;
}

void PerformanceOptimizer::setTextureOptimization(const TextureOptimization& opt)
{
    m_textureOpt = opt;
}

QJsonObject PerformanceOptimizer::benchmarkScene(const QVariantMap& sceneData) const
{
    QElapsedTimer t;
    t.start();

    QJsonObject result;
    result["meshCount"]    = sceneData.value("meshCount", 0).toInt();
    result["vertexCount"]  = sceneData.value("vertexCount", 0).toInt();
    result["textureCount"] = sceneData.value("textureCount", 0).toInt();
    result["benchmarkMs"]  = static_cast<double>(t.elapsed());

    // Rough FPS estimate: < 10k verts → 60fps, etc.
    int verts = sceneData.value("vertexCount", 0).toInt();
    double fps = verts < 10000 ? 60.0 : verts < 100000 ? 30.0 : 15.0;
    result["estimatedFps"] = fps;

    return result;
}

QVector<QString> PerformanceOptimizer::getWarnings(const QVariantMap& sceneData) const
{
    QVector<QString> warnings;
    if (sceneData.value("vertexCount", 0).toInt() > 500000)
        warnings << "High vertex count (>500k) may cause performance issues";
    if (sceneData.value("textureCount", 0).toInt() > 64)
        warnings << "High texture count (>64) may cause VRAM pressure";
    if (sceneData.value("drawCalls", 0).toInt() > 200)
        warnings << "High draw call count (>200); consider merging meshes";
    return warnings;
}

} // namespace ks
