#pragma once

#include <QString>
#include <QMap>
#include <QVariant>
#include <QSettings>
#include <QObject>
#include <QColor>
#include <QScopedPointer>

class KsConfigLoader : public QObject
{
    Q_OBJECT

public:
    static KsConfigLoader& instance();
    
    void setSystemPath(const QString& path);
    QString systemPath() const { return m_systemPath; }
    
    bool loadAll();
    
    bool loadGraphicsConfig();
    bool loadLightingConfig();
    bool loadPhysicsConfig();
    bool loadAudioConfig();
    bool loadVRConfig();
    
    QStringList availablePPFilters() const;
    bool loadPPFilter(const QString& name);
    QVariant getPPFilterValue(const QString& section, const QString& key, const QVariant& defaultValue = QVariant());
    
    struct GraphicsSettings {
        int maxFrameLatency = 0;
        float mipLodBias = 0.0f;
        float shadowMapBias0 = 0.000002f;
        float shadowMapBias1 = 0.000015f;
        float shadowMapBias2 = 0.0003f;
        float skyboxReflectionGain = 1.5f;
        bool allowUnsupportedDX10 = false;
    };
    const GraphicsSettings& graphicsSettings() const { return m_graphics; }
    
    struct LightingSettings {
        QColor ambientColor;
        QColor horizonColor;
        QColor zenithColor;
        QColor lightColor;
        float lightIntensity = 1.0f;
        float fogDensity = 0.0f;
        QColor fogColor;
    };
    const LightingSettings& lightingSettings() const { return m_lighting; }

signals:
    void configLoaded(const QString& configName);
    void ppFilterChanged(const QString& filterName);

private:
    KsConfigLoader(QObject* parent = nullptr);
    ~KsConfigLoader();
    
    QString m_systemPath;
    QMap<QString, QSettings*> m_configs;   // owned; deleted in destructor
    GraphicsSettings m_graphics;
    LightingSettings m_lighting;
    QScopedPointer<QSettings> m_currentPPFilter;
    
    void loadGraphicsFromSettings(QSettings& settings);
    void loadLightingFromSettings(QSettings& settings);
};

#define KsConfig KsConfigLoader::instance()