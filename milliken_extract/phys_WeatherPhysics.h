#pragma once

#include "PhysicsEngine.h"
#include <QObject>
#include <QVector>
#include <QMap>
#include <QColor>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace ks {

namespace physics {

struct WeatherEffects { float aquaplaningRisk = 0, trackGripReduction = 0; float windForceX = 0, windForceY = 0, windForceZ = 0; };

struct WeatherKeyframe {
    float time = 0.0f;
    QString type;
    float cloudCoverage = 0.0f;
    float precipitation = 0.0f;
    float windSpeed = 0.0f;
    float windDirection = 0.0f;
    float temperature = 20.0f;
    float humidity = 0.5f;
    float pressure = 1013.25f;
    float visibility = 10.0f;
    QString transitionType = "linear";
    QMap<QString, double> defaultValuesForType(const QString& type);
    bool isValid() const;
};

struct WeatherSequence {
    QString name;
    QString description;
    float startTime = 0.0f;
    float duration = 24.0f;
    bool loop = false;
    QList<WeatherKeyframe> keyframes;
};

struct WeatherConfig {
    QString name;
    QString trackName;
    float baseTime = 12.0f;
    float timeMultiplier = 1.0f;
    bool dynamicWeather = false;
    float weatherChangeInterval = 2.0f;
    QString solConfigPath;
    QString weatherLuaPath;
    QList<WeatherSequence> sequences;
};

// ============================================================================
// WeatherConfigParser - parses weather configuration files
// ============================================================================

class WeatherConfigParser : public QObject {
    Q_OBJECT
public:
    struct WeatherPreset {
        QString name;
        QString description;
        float ambientTemperature = 25.0f;
        float roadTemperature = 30.0f;
        float cloudIntensity = 0.0f;
        float rainIntensity = 0.0f;
        float windSpeed = 0.0f;
        float windDirection = 0.0f;
        float humidity = 0.5f;
        float fogIntensity = 0.0f;
        float fogDensity = 0.0f;
        float fogHeightFalloff = 0.0f;
        QColor fogColor;
        float sunIntensity = 1.0f;
        float ambientIntensity = 0.3f;
        float exposure = 1.0f;
        float saturation = 1.0f;
        float contrast = 1.0f;
        float temperature = 0.0f;
        float timeOfDay = 12.0f;
        float timeMultiplier = 1.0f;
        QString weatherController;
        bool useRealWeather = false;
        bool useLiveConditions = false;
    };

    struct WeatherZone {
        QString name;
        float startKm = 0.0f;
        float endKm = 0.0f;
        float position = 0.0f;
        float radius = 1000.0f;
        WeatherPreset preset;
    };

    explicit WeatherConfigParser(QObject* parent = nullptr);

    // Static parsing operations
    static WeatherPreset parsePureConfig(const QString& configPath);
    static WeatherPreset parseSolConfig(const QString& configPath);
    static WeatherPreset parseCspConfig(const QString& configPath);
    static WeatherPreset parseLuaScript(const QString& luaPath);
    
    // Static saving operations
    static bool savePureConfig(const WeatherPreset& preset, const QString& configPath);
    static bool saveSolConfig(const WeatherPreset& preset, const QString& configPath);
    static bool saveCspConfig(const WeatherPreset& preset, const QString& configPath);
    
    // Static weather zones
    static QVector<WeatherZone> parseWeatherZones(const QString& trackPath);
    static bool saveWeatherZones(const QVector<WeatherZone>& zones, const QString& trackPath);
    
    // Static preset management
    static QVector<WeatherPreset> loadPresets(const QString& directory);
    static bool savePreset(const WeatherPreset& preset, const QString& directory);
    static WeatherPreset getPreset(const QString& name);
    
    // Default presets
    static WeatherPreset getDefaultClear();
    static WeatherPreset getDefaultCloudy();
    static WeatherPreset getDefaultRain();
    static WeatherPreset getDefaultStorm();
    static WeatherPreset getDefaultNight();
    
    // Static validation
    static bool validatePreset(const WeatherPreset& preset, QString* error = nullptr);
    
    // Static utility
    static QString getWeatherName(float cloudIntensity, float rainIntensity);
    static QString getTimeOfDayName(float hour);
    static QColor getSkyColor(float timeOfDay, float cloudIntensity);
    
    // Instance methods (QObject version)
    bool parseWeatherConfig(const QString& filePath, WeatherConfig& config, QString* error = nullptr);
    bool writeWeatherConfig(const QString& filePath, const WeatherConfig& config, QString* error = nullptr);
    bool validateConfig(const WeatherConfig& config, QStringList* errors = nullptr);

signals:
    void parsingFinished(bool success, const QString& message);

private:
    bool parseWeatherLua(const QString& filePath, WeatherConfig& config, QString* error = nullptr);
    bool parseSOLConfig(const QString& filePath, WeatherConfig& config, QString* error = nullptr);
    bool writeWeatherLua(const QString& filePath, const WeatherConfig& config, QString* error = nullptr);
    bool writeSOLConfig(const QString& filePath, const WeatherConfig& config, QString* error = nullptr);
    bool parseIniSection(QTextStream& in, const QString& sectionName, QMap<QString, QString>& out);
    bool parseLuaTable(const QString& luaCode, const QString& tableName, QJsonObject& out);
    QString interpolateLuaString(const QString& str, const QJsonObject& variables);
    QString generateLuaConfig(const WeatherConfig& config);
    QString generateIniConfig(const WeatherConfig& config);
    QString generateSOLConfig(const WeatherConfig& config);
    
    static QMap<QString, WeatherPreset> m_presets;
};

class WeatherSimulator : public QObject {
    Q_OBJECT
public:
    explicit WeatherSimulator(QObject* parent = nullptr);
    ~WeatherSimulator() override;

    void setWeatherState(const WeatherState& s);
    WeatherState& weatherState() { return m_weather; }
    const WeatherState& weatherState() const { return m_weather; }
    void setTrackWetness(double w);
    void setRainIntensity(double mmh);
    void setAirDensity(double d);
    void update(double dt);
    float aquaplaningRisk() const { return m_effects.aquaplaningRisk; }
    float trackGripReduction() const { return m_effects.trackGripReduction; }
    WeatherEffects currentEffects() const { return m_effects; }

    void startSimulation();
    void stopSimulation();
    void reset();
    bool isRunning() const { return m_running; }

    void setTimeMultiplier(float multiplier) { m_timeMultiplier = multiplier; }
    float timeMultiplier() const { return m_timeMultiplier; }

    // Preset management
    bool loadPresetsFromDirectory(const QString& directory);
    bool selectPreset(const QString& presetName);
    bool selectPreset(int index);
    int currentPresetIndex() const { return m_currentPresetIndex; }
    QString currentPresetName() const { return m_currentPresetName; }
    QVector<QString> availablePresets() const;

signals:
    void weatherChanged(const WeatherState& state);
    void simulationStarted();
    void simulationStopped();
    void simulationReset();
    void presetSelected(const QString& presetName);

private:
    void updateEffects();
    WeatherState presetToWeatherState(const struct WeatherConfigParser::WeatherPreset& preset) const;
    WeatherState m_weather;
    WeatherEffects m_effects;
    bool m_running = false;
    float m_timeMultiplier = 1.0f;
    int m_currentPresetIndex = -1;
    QString m_currentPresetName;
    QVector<struct WeatherConfigParser::WeatherPreset> m_presets;
};

// ============================================================================
// WeatherFxParser - parses WeatherFX Lua scripts
// ============================================================================

class WeatherFxParser {
public:
    struct WeatherFxConfig {
        QString scriptPath;
        QString scriptName;
        QMap<QString, bool> boolSettings;
        QMap<QString, float> floatSettings;
        QMap<QString, int> intSettings;
        QMap<QString, QString> stringSettings;
    };

    static WeatherFxConfig parseScript(const QString& luaPath);
    static bool saveScript(const WeatherFxConfig& config, const QString& luaPath);
    static QVector<QString> getAvailableScripts(const QString& weatherDir);
};

} // namespace physics
} // namespace ks
