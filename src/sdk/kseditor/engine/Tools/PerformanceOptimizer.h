#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QVariantMap>

namespace ks {

class PerformanceOptimizer : public QObject
{
    Q_OBJECT

public:
    static PerformanceOptimizer* instance();

    struct OptimizationProfile {
        QString id;
        QString name;
        int maxTextureSize = 4096;
        int maxVertices = 65536;
        bool mergeMeshes = true;
        bool generateLODs = false;
        float lodReduction = 0.5f;
        bool optimizeUVs = true;
        bool atlasTextures = false;
        bool compressTextures = true;
        int textureQuality = 90;
        QJsonObject settings;
    };

    void registerProfile(const OptimizationProfile& profile);
    void unregisterProfile(const QString& profileId);

    void setCurrentProfile(const QString& profileId);
    QString getCurrentProfileId() const { return m_currentProfileId; }

    void applyProfile(const OptimizationProfile& p);

    void applyOptimization(const QString& category);

    struct MeshOptimization {
        int maxVertices = 65536;
        bool removeDoubles = true;
        bool mergeVertices = true;
        bool mergeMeshes = true;
        bool optimizeNormals = true;
        bool optimizeUVs = true;
        bool generateLODs = false;
        int lods = 3;
        float lodReduction = 0.5f;
    };
    void setMeshOptimization(const MeshOptimization& opt);
    MeshOptimization getMeshOptimization() const { return m_meshOpt; }

    struct TextureOptimization {
        bool generateMipmaps = true;
        bool compressTextures = true;
        bool resizeLarge = true;
        int maxTextureSize = 4096;
        int targetQuality = 90;
        bool atlasTextures = false;
        int textureQuality = 90;
    };
    void setTextureOptimization(const TextureOptimization& opt);
    TextureOptimization getTextureOptimization() const { return m_textureOpt; }

    struct ShadowOptimization {
        int shadowResolution = 2048;
        int shadowCascades = 4;
    };

    struct LightingOptimization {
        int maxLights = 8;
        bool disableShadows = false;
    };

    QJsonObject benchmarkScene(const QVariantMap& sceneData) const;
    QVector<QString> getWarnings(const QVariantMap& sceneData) const;

signals:
    void profileChanged(const QString& profileId);
    void optimizationApplied(const QString& category);
    void optimizationStarted(const QString& category);
    void optimizationProgress(float progress);
    void optimizationCompleted(const QString& category);

private:
    PerformanceOptimizer(QObject* parent = nullptr);
    ~PerformanceOptimizer();
    Q_DISABLE_COPY(PerformanceOptimizer)

    static PerformanceOptimizer* s_instance;

    QString m_currentProfileId;
    QMap<QString, OptimizationProfile> m_profiles;
    MeshOptimization m_meshOpt;
    TextureOptimization m_textureOpt;
    ShadowOptimization m_shadowOpt;
    LightingOptimization m_lightingOpt;
};

} // namespace ks
