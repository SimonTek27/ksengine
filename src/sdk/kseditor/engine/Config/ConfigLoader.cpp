#include "ConfigLoader.h"
#include <QDir>
#include <QFileInfo>

KsConfigLoader& KsConfigLoader::instance()
{
    static KsConfigLoader loader;
    return loader;
}

KsConfigLoader::KsConfigLoader(QObject* parent)
    : QObject(parent)
{
}

KsConfigLoader::~KsConfigLoader()
{
    qDeleteAll(m_configs);
}

void KsConfigLoader::setSystemPath(const QString& path)
{
    m_systemPath = path;
    if (!m_systemPath.endsWith('/') && !m_systemPath.endsWith('\\')) {
        m_systemPath += '/';
    }
}

bool KsConfigLoader::loadAll()
{
    if (m_systemPath.isEmpty()) {
        qWarning() << "KsConfigLoader: System path not set";
        return false;
    }

    bool success = true;
    success &= loadGraphicsConfig();
    success &= loadLightingConfig();
    success &= loadPhysicsConfig();
    success &= loadAudioConfig();
    success &= loadVRConfig();

    return success;
}

bool KsConfigLoader::loadGraphicsConfig()
{
    QString path = m_systemPath + "cfg/graphics.ini";
    if (!QFileInfo::exists(path)) {
        qWarning() << "KsConfigLoader: graphics.ini not found at" << path;
        return false;
    }

    delete m_configs.value("graphics", nullptr);
    QSettings* settings = new QSettings(path, QSettings::IniFormat);
    m_configs.insert("graphics", settings);

    loadGraphicsFromSettings(*settings);
    emit configLoaded("graphics");
    return true;
}

void KsConfigLoader::loadGraphicsFromSettings(QSettings& settings)
{
    settings.beginGroup("DX11");
    m_graphics.maxFrameLatency = settings.value("MAXIMUM_FRAME_LATENCY", 0).toInt();
    m_graphics.mipLodBias = settings.value("MIP_LOD_BIAS", 0.0f).toFloat();
    m_graphics.shadowMapBias0 = settings.value("SHADOW_MAP_BIAS_0", 0.000002f).toFloat();
    m_graphics.shadowMapBias1 = settings.value("SHADOW_MAP_BIAS_1", 0.000015f).toFloat();
    m_graphics.shadowMapBias2 = settings.value("SHADOW_MAP_BIAS_2", 0.0003f).toFloat();
    m_graphics.allowUnsupportedDX10 = settings.value("ALLOW_UNSUPPORTED_DX10", false).toBool();
    m_graphics.skyboxReflectionGain = settings.value("SKYBOX_REFLECTION_GAIN", 1.5f).toFloat();
    settings.endGroup();
}

bool KsConfigLoader::loadLightingConfig()
{
    QString path = m_systemPath + "cfg/lighting.ini";
    if (!QFileInfo::exists(path)) {
        qWarning() << "KsConfigLoader: lighting.ini not found at" << path;
        return false;
    }

    delete m_configs.value("lighting", nullptr);
    QSettings* settings = new QSettings(path, QSettings::IniFormat);
    m_configs.insert("lighting", settings);

    loadLightingFromSettings(*settings);
    emit configLoaded("lighting");
    return true;
}

void KsConfigLoader::loadLightingFromSettings(QSettings& settings)
{
    settings.beginGroup("LIGHTING");

    QStringList ambientRGB = settings.value("AMBIENT", "0.5,0.5,0.5").toString().split(',');
    if (ambientRGB.size() >= 3) {
        m_lighting.ambientColor.setRedF(ambientRGB[0].toDouble());
        m_lighting.ambientColor.setGreenF(ambientRGB[1].toDouble());
        m_lighting.ambientColor.setBlueF(ambientRGB[2].toDouble());
    }

    QStringList horizonRGB = settings.value("HORIZON", "0.7,0.7,0.7").toString().split(',');
    if (horizonRGB.size() >= 3) {
        m_lighting.horizonColor.setRedF(horizonRGB[0].toDouble());
        m_lighting.horizonColor.setGreenF(horizonRGB[1].toDouble());
        m_lighting.horizonColor.setBlueF(horizonRGB[2].toDouble());
    }

    QStringList zenithRGB = settings.value("ZENITH", "0.2,0.2,0.5").toString().split(',');
    if (zenithRGB.size() >= 3) {
        m_lighting.zenithColor.setRedF(zenithRGB[0].toDouble());
        m_lighting.zenithColor.setGreenF(zenithRGB[1].toDouble());
        m_lighting.zenithColor.setBlueF(zenithRGB[2].toDouble());
    }

    settings.endGroup();
}

bool KsConfigLoader::loadPhysicsConfig()
{
    QString path = m_systemPath + "cfg/physics.ini";
    if (!QFileInfo::exists(path)) {
        qWarning() << "KsConfigLoader: physics.ini not found at" << path;
        return false;
    }

    delete m_configs.value("physics", nullptr);
    QSettings* settings = new QSettings(path, QSettings::IniFormat);
    m_configs.insert("physics", settings);
    emit configLoaded("physics");
    return true;
}

bool KsConfigLoader::loadAudioConfig()
{
    QString path = m_systemPath + "cfg/audio.ini";
    if (!QFileInfo::exists(path)) {
        qWarning() << "KsConfigLoader: audio.ini not found at" << path;
        return false;
    }

    delete m_configs.value("audio", nullptr);
    QSettings* settings = new QSettings(path, QSettings::IniFormat);
    m_configs.insert("audio", settings);
    emit configLoaded("audio");
    return true;
}

bool KsConfigLoader::loadVRConfig()
{
    QString path = m_systemPath + "cfg/vr.ini";
    if (!QFileInfo::exists(path)) {
        qWarning() << "KsConfigLoader: vr.ini not found at" << path;
        return false;
    }

    delete m_configs.value("vr", nullptr);
    QSettings* settings = new QSettings(path, QSettings::IniFormat);
    m_configs.insert("vr", settings);
    emit configLoaded("vr");
    return true;
}

QStringList KsConfigLoader::availablePPFilters() const
{
    QStringList filters;
    QDir ppDir(m_systemPath + "cfg/ppfilters");

    if (ppDir.exists()) {
        QStringList files = ppDir.entryList({"*.ini"}, QDir::Files);
        for (const QString& file : files) {
            filters.append(file.chopped(4));
        }
    }

    return filters;
}

bool KsConfigLoader::loadPPFilter(const QString& name)
{
    QString path = m_systemPath + "cfg/ppfilters/" + name + ".ini";
    if (!QFileInfo::exists(path)) {
        qWarning() << "KsConfigLoader: PPFilter not found:" << name;
        return false;
    }

    m_currentPPFilter.reset(new QSettings(path, QSettings::IniFormat));
    emit ppFilterChanged(name);
    return true;
}

QVariant KsConfigLoader::getPPFilterValue(const QString& section, const QString& key, const QVariant& defaultValue)
{
    if (!m_currentPPFilter) {
        return defaultValue;
    }

    m_currentPPFilter->beginGroup(section);
    QVariant value = m_currentPPFilter->value(key, defaultValue);
    m_currentPPFilter->endGroup();

    return value;
}
