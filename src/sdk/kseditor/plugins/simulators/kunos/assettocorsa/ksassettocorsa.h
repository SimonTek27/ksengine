#ifndef KS_ASSETTOCORSA_H
#define KS_ASSETTOCORSA_H

#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QVector>
#include <QSet>
#include <QPair>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDataStream>
#include <QIODevice>
#include <QVariant>
#include <QSettings>
#include <QDateTime>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QUrl>
#include <QRect>
#include <QSize>
#include <QPoint>
#include <QPainter>
#include <QLinearGradient>
#include <QFont>
#include "sdk/kseditor/engine/Video/VideoEncoder.h"
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QMap>
#include <QList>
#include <QVector>
#include <cmath>
#include <algorithm>
#include <limits>

#include "sdk/SDKBackend.h"

#include "assettocorsa_core.h"

namespace ks {

using ks::plugins::kunos::ks::KsWaypoint2D;
using ks::plugins::kunos::ks::KsTrackSector;
using ks::plugins::kunos::ks::KsTrackGeometry;
using ks::plugins::kunos::ks::KsCameraSpline;
using ks::plugins::kunos::ks::KsTrackSectorConfig;
using ks::plugins::kunos::ks::KsTrackDatabase;
using ks::plugins::kunos::ks::KsTrackManager;

using ks::plugins::kunos::ks::KsWheelState;
using ks::plugins::kunos::ks::KsChassisState;
using ks::plugins::kunos::ks::KsEngineState;
using ks::plugins::kunos::ks::KsAeroState;
using ks::plugins::kunos::ks::KsPhysicsEngine;
using ks::plugins::kunos::ks::KsPhysicsSimulator;
using ks::plugins::kunos::ks::calculateIdealRacingLine;
using ks::plugins::kunos::ks::calculateBrakePoint;
using ks::plugins::kunos::ks::estimateLapTime;

using ks::plugins::kunos::ks::KsServerInfo;
using ks::plugins::kunos::ks::KsClientInfo;
using ks::plugins::kunos::ks::KsLapEntry;
using ks::plugins::kunos::ks::KsServerBrowser;
using ks::plugins::kunos::ks::KsNetworkClient;
using ks::plugins::kunos::ks::KsServerConfig;
using ks::plugins::kunos::ks::KsServerManager;
using ks::plugins::kunos::ks::KsSteamLobby;
using ks::plugins::kunos::ks::KsUserAuth;

struct KsAIDriverProfile {
    QString name;
    float aggression;
    float defensive;
    float qualifying;
    float consistency;
    float mistakeRate;
    float pitSkill;
    float tireManagement;
    float fuelManagement;

    float brakeBalance;
    float cornerEntry;
    float cornerExit;
    float straightLine;

    KsAIDriverProfile() : aggression(0.5f), defensive(0.5f), qualifying(0.5f),
        consistency(0.7f), mistakeRate(0.1f), pitSkill(0.7f),
        tireManagement(0.6f), fuelManagement(0.6f),
        brakeBalance(0.5f), cornerEntry(0.5f), cornerExit(0.5f), straightLine(0.5f) {}
};

struct KsWaypointAI {
    float position[3];
    float tangent[2];
    float curvature;
    float width;
    float preferredRadius;
    float turnIn;
    float apex;
    float brakePoint;
    float throttlePoint;
    float gear;
    float targetSpeed;

    int sector;
    bool isCorner;
    bool isStraight;
    bool isBrakingZone;
    bool isApexZone;

    KsWaypointAI() : curvature(0), width(10), preferredRadius(0),
        turnIn(0), apex(0), brakePoint(0), throttlePoint(0), gear(4),
        targetSpeed(0), sector(0), isCorner(false), isStraight(false),
        isBrakingZone(false), isApexZone(false) {
        position[0] = position[1] = position[2] = 0;
        tangent[0] = 1; tangent[1] = 0;
    }
};

struct KsAICarState {
    float position[3];
    float velocity[3];
    float heading;
    float speed;
    float slipAngle;
    float throttle;
    float brake;
    float steering;
    int gear;
    float rpm;
    float targetSpeed;

    int currentWaypoint;
    float distanceToNext;
    float distanceToApex;

    float throttleHistory[10];
    float brakeHistory[10];
    float steerHistory[10];

    float lapTime;
    float lastLapTime;
    float bestLapTime;
    int lapsCompleted;

    KsAICarState() : heading(0), speed(0), slipAngle(0), throttle(0),
        brake(0), steering(0), gear(0), rpm(1000), targetSpeed(0),
        currentWaypoint(0), distanceToNext(0), distanceToApex(0),
        lapTime(0), lastLapTime(0), bestLapTime(0), lapsCompleted(0) {
        for (int i = 0; i < 10; i++) {
            throttleHistory[i] = brakeHistory[i] = steerHistory[i] = 0;
        }
    }
};

class KsAIModel {
public:
    QString name;
    KsAIDriverProfile profile;

    float perceptionRange;
    float reactionTime;
    float lookAheadTime;

    bool useRubberBanding;
    bool useBlockPassing;
    bool useTireManagement;
    bool useFuelManagement;

    KsAIModel() : perceptionRange(100), reactionTime(0.1f), lookAheadTime(1.0f),
        useRubberBanding(true), useBlockPassing(true),
        useTireManagement(true), useFuelManagement(true) {}

    float calculateThrottle(const KsAICarState& state, const KsWaypointAI& target, float dt) {
        float speedError = target.targetSpeed - state.speed;
        float throttle = qBound(0.0f, speedError * 0.01f + 0.5f, 1.0f);
        throttle *= (1.0f - profile.aggression * 0.3f);
        if (state.brake > 0.1f) throttle = 0;
        return throttle;
    }

    float calculateBrake(const KsAICarState& state, const KsWaypointAI& target, float dt) {
        float distToBrake = state.distanceToNext - target.brakePoint;
        float speedError = target.targetSpeed - state.speed;
        float brake = 0;
        if (distToBrake > 0 && speedError < -5) {
            brake = qBound(0.0f, -speedError * 0.02f, 1.0f);
        }
        if (state.speed > target.targetSpeed * 1.2f) {
            brake = qMin(1.0f, (state.speed - target.targetSpeed) * 0.05f);
        }
        return brake;
    }

    float calculateSteering(const KsAICarState& state, const KsWaypointAI& target, float dt) {
        float targetHeading = atan2(target.tangent[0], target.tangent[1]);
        float headingError = targetHeading - state.heading;
        while (headingError > PI) headingError -= PI2;
        while (headingError < -PI) headingError += PI2;
        float steerGain = 1.0f / (target.preferredRadius * 0.1f + 0.5f);
        float steering = headingError * steerGain;
        if (state.speed < 10) steering *= 2;
        else steering *= qBound(0.5f, state.speed / 50.0f, 1.5f);
        return qBound(-1.0f, steering, 1.0f);
    }

    int calculateGear(const KsAICarState& state, float acceleration) {
        if (acceleration > 0) {
            if (state.rpm > 7500) return qMin(6, state.gear + 1);
            if (state.rpm > 6000) return state.gear;
            if (state.rpm < 3000 && state.gear > 1) return state.gear - 1;
        } else {
            if (state.rpm < 4000 && state.gear > 1) return state.gear - 1;
            if (state.rpm < 7000) return state.gear;
        }
        return state.gear;
    }
};

class KsAIDatabase {
public:
    QMap<QString, KsAIModel> models;

    void loadDefaults() {
        KsAIModel aggressive;
        aggressive.name = "Aggressive";
        aggressive.profile.aggression = 0.9f;
        aggressive.profile.defensive = 0.3f;
        models["aggressive"] = aggressive;

        KsAIModel defensive;
        defensive.name = "Defensive";
        defensive.profile.aggression = 0.3f;
        defensive.profile.defensive = 0.9f;
        models["defensive"] = defensive;

        KsAIModel qualifier;
        qualifier.name = "Qualifier";
        qualifier.profile.qualifying = 0.95f;
        models["qualifier"] = qualifier;

        KsAIModel starter;
        starter.name = "Beginner";
        starter.profile.aggression = 0.2f;
        models["starter"] = starter;
    }

    KsAIModel* getModel(const QString& name) {
        return models.contains(name) ? &models[name] : nullptr;
    }
};

}

}

#endif
#ifndef KS_KS_AUDIO_H
#define KS_KS_AUDIO_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>

namespace ks {

enum class KsEngineType {
    Inline4 = 0,
    V8 = 1,
    V10 = 2,
    V12 = 3,
    Flat6 = 4,
    V6 = 5,
    Rotary = 6
};

enum class KsExhaustType {
    Single = 0,
    Double = 1,
    Quad = 2,
    Center = 3
};

struct KsEngineSoundParams {
    KsEngineType type;
    float displacement;
    float bore;
    float stroke;
    int cylinders;
    int valves;
    float headerLength;

    float baseVolume;
    float idleVolume;
    float redlineVolume;
    float limiterVolume;

    float idleRpm;
    float redlineRpm;
    float maxRpm;

    float limiterPitches[4];
    float limiterVolumes[4];

    bool antiLag;
    float antiLagGain;

    KsEngineSoundParams()
        : type(KsEngineType::V8), displacement(4.0f), bore(86.0f), stroke(86.0f),
          cylinders(8), valves(32), headerLength(0.3f),
          baseVolume(0.8f), idleVolume(0.6f), redlineVolume(1.0f), limiterVolume(0.9f),
          idleRpm(900), redlineRpm(8000), maxRpm(9000),
          antiLag(false), antiLagGain(0.5f)
    {
        limiterPitches[0] = 2.0f;
        limiterPitches[1] = 3.0f;
        limiterPitches[2] = 4.0f;
        limiterPitches[3] = 5.0f;
        limiterVolumes[0] = 0.8f;
        limiterVolumes[1] = 0.6f;
        limiterVolumes[2] = 0.4f;
        limiterVolumes[3] = 0.2f;
    }
};

struct KsTyreSoundParams {
    float frontWidth;
    float rearWidth;
    float frontCompound;
    float rearCompound;

    float rollVolume;
    float slipVolume;
    float flacnelVolume;

    float rollingResistance;
    float gripFloor;

    float slipThreshold;
    float flatThreshold;

    KsTyreSoundParams()
        : frontWidth(0.25f), rearWidth(0.30f),
          frontCompound(0.5f), rearCompound(0.5f),
          rollVolume(0.5f), slipVolume(0.7f), flacnelVolume(0.3f),
          rollingResistance(0.015f), gripFloor(0.3f),
          slipThreshold(0.05f), flatThreshold(0.1f)
    {}
};

struct KsWindSoundParams {
    float baseVolume;
    float speedVolume;
    float cockpitOpenRatio;

    float basePitch;
    float speedPitch;

    int channels;
    bool doppler;

    KsWindSoundParams()
        : baseVolume(0.3f), speedVolume(0.7f), cockpitOpenRatio(0.0f),
          basePitch(0.5f), speedPitch(0.5f),
          channels(2), doppler(true)
    {}
};

struct KsBrakeSoundParams {
    float volume;
    float ductedVolume;
    float temperatureThreshold;

    float fadeIn;
    float fadeOut;

    KsBrakeSoundParams()
        : volume(0.7f), ductedVolume(0.5f), temperatureThreshold(200.0f),
          fadeIn(0.5f), fadeOut(1.0f)
    {}
};

class KsaudioConfig {
public:
    KsEngineSoundParams engine;
    KsTyreSoundParams tyres;
    KsWindSoundParams wind;
    KsBrakeSoundParams brakes;

    float masterVolume;
    float uiVolume;
    bool enabled;
    bool spacial;

    float minDistance;
    float maxDistance;
    float rolloff;

    KsaudioConfig()
        : masterVolume(1.0f), uiVolume(0.7f), enabled(true), spacial(true),
          minDistance(1.0f), maxDistance(50.0f), rolloff(1.0f)
    {}
};

struct KsBankInfo {
    QString name;
    QString path;
    int size;
    QStringList events;

    KsBankInfo() : size(0) {}
};

class KsAudioInstance {
public:
    QString name;
    QString projectPath;
    QString fevPath;

    QList<KsBankInfo> banks;

    bool init();
    bool loadBank(const QString& path, KsBankInfo& bank);
    bool playEvent(const QString& eventName);
    bool stopEvent(const QString& eventName);
    bool setEventParameter(const QString& eventName, const QString& paramName, float value);

    float getEventParameter(const QString& eventName, const QString& paramName);
    bool isEventPlaying(const QString& eventName);

    void setListenerPosition(float x, float y, float z);
    void setListenerOrientation(float forward[3], float up[3]);

    enum { MAX_LISTENERS = 8 };
    struct Listener {
        float position[3];
        float forward[3];
        float up[3];
    };
    Listener listeners[MAX_LISTENERS];
    int activeListener;

    KsAudioInstance() : activeListener(0) {}
};

const QMap<QString, KsEngineType>& getEngineTypes() {
    static QMap<QString, KsEngineType> types;
    if (types.isEmpty()) {
        types["inline4"] = KsEngineType::Inline4;
        types["v8"] = KsEngineType::V8;
        types["v10"] = KsEngineType::V10;
        types["v12"] = KsEngineType::V12;
        types["flat6"] = KsEngineType::Flat6;
        types["v6"] = KsEngineType::V6;
        types["rotary"] = KsEngineType::Rotary;
    }
    return types;
}

inline QString toString(KsEngineType type) {
    switch (type) {
    case KsEngineType::Inline4: return "Inline-4";
    case KsEngineType::V8: return "V8";
    case KsEngineType::V10: return "V10";
    case KsEngineType::V12: return "V12";
    case KsEngineType::Flat6: return "Flat-6";
    case KsEngineType::V6: return "V6";
    case KsEngineType::Rotary: return "Rotary";
    default: return "Unknown";
    }
}

inline const char* toString(KsExhaustType type) {
    switch (type) {
    case KsExhaustType::Single: return "Single";
    case KsExhaustType::Double: return "Double";
    case KsExhaustType::Quad: return "Quad";
    case KsExhaustType::Center: return "Center";
    default: return "Unknown";
    }
}

inline const QList<QString>& getCarSoundEvents() {
    static QList<QString> events;
    if (events.isEmpty()) {
        events.append("engine_int");
        events.append("engine_ext");
        events.append("turbo");
        events.append("wastegate");
        events.append("blowoff");
        events.append("backfire_ext");
        events.append("backfire_int");
        events.append("upshift");
        events.append("downshift");
        events.append("antiglag");
        events.append("transmission");
        events.append(" tyre_roll_front");
        events.append("rear_tyre_roll");
        events.append("tyreslip_front");
        events.append("tyreslip_rear");
        events.append("gravel");
        events.append("rumblestrips");
        events.append("kerb");
events.append("wind");
        events.append("brake_duct");
    }
    return events;
}

}

}

#endif
#ifndef KS_KS_CONFIG_H
#define KS_KS_CONFIG_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "sdk/SDKBackend.h"

#include "ksAssettoCorsaIni.h"

namespace ks {

namespace kunos {

class KsGameSettings {
public:
    static QString getSettingsPath(KsFolderID folder = KsFolderID::Cfg) {
        return SDKBackend::getFolderPath(folder) + "/settings.ini";
    }

    static void setValue(const QString& key, const QVariant& value,
                    const QString& section = "GENERAL") {
        QSettings settings(getSettingsPath(), QSettings::IniFormat);
        settings.beginGroup(section);
        settings.setValue(key, value);
        settings.endGroup();
    }

    static QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant(),
                          const QString& section = "GENERAL") {
        QSettings settings(getSettingsPath(), QSettings::IniFormat);
        settings.beginGroup(section);
        QVariant value = settings.value(key, defaultValue);
        settings.endGroup();
        return value;
    }

    static void setVideoMode(int width, int height, bool fullscreen) {
        setValue("WIDTH", width, "VIDEO");
        setValue("HEIGHT", height, "VIDEO");
        setValue("FULLSCREEN", fullscreen ? 1 : 0, "VIDEO");
    }

    static void getVideoMode(int& width, int& height, bool& fullscreen) {
        width = getValue("WIDTH", 1920, "VIDEO").toInt();
        height = getValue("HEIGHT", 1080, "VIDEO").toInt();
        fullscreen = getValue("FULLSCREEN", 1, "VIDEO").toInt() == 1;
    }

    static void setQuality(int quality) {
        setValue("QUALITY", quality, "GRAPHICS");
    }

    static int getQuality() {
        return getValue("QUALITY", 2, "GRAPHICS").toInt();
    }

    static void setMSAA(int samples) {
        setValue("MSAA", samples, "GRAPHICS");
    }

    static int getMSAA() {
        return getValue("MSAA", 0, "GRAPHICS").toInt();
    }

    static void setShadowQuality(int quality) {
        setValue("SHADOW_QUALITY", quality, "GRAPHICS");
    }

    static int getShadowQuality() {
        return getValue("SHADOW_QUALITY", 2, "GRAPHICS").toInt();
    }

    static void setSSAO(bool enabled) {
        setValue("SSAO", enabled ? 1 : 0, "GRAPHICS");
    }

    static bool getSSAO() {
        return getValue("SSAO", 1, "GRAPHICS").toInt() == 1;
    }

    static void setBloom(bool enabled) {
        setValue("BLOOM", enabled ? 1 : 0, "GRAPHICS");
    }

    static bool getBloom() {
        return getValue("BLOOM", 1, "GRAPHICS").toInt() == 1;
    }

    static void setMotionBlur(bool enabled) {
        setValue("MOTION_BLUR", enabled ? 1 : 0, "GRAPHICS");
    }

    static bool getMotionBlur() {
        return getValue("MOTION_BLUR", 0, "GRAPHICS").toInt() == 1;
    }

    static void setAudioDevice(const QString& device) {
        setValue("DEVICE", device, "AUDIO");
    }

    static QString getAudioDevice() {
        return getValue("DEVICE", "Default", "AUDIO").toString();
    }

    static void setMasterVolume(float volume) {
        setValue("MASTER", int(volume * 100), "AUDIO");
    }

    static float getMasterVolume() {
        return getValue("MASTER", 80, "AUDIO").toInt() / 100.0f;
    }

    static void setEngineVolume(float volume) {
        setValue("ENGINE", int(volume * 100), "AUDIO");
    }

    static float getEngineVolume() {
        return getValue("ENGINE", 100, "AUDIO").toInt() / 100.0f;
    }

    static void setTyreVolume(float volume) {
        setValue("TYRES", int(volume * 100), "AUDIO");
    }

    static float getTyreVolume() {
        return getValue("TYRES", 80, "AUDIO").toInt() / 100.0f;
    }

    static void setController(const QString& name) {
        setValue("CONTROLLER", name, "CONTROLS");
    }

    static QString getController() {
        return getValue("CONTROLLER", "", "CONTROLS").toString();
    }

    static void setFFBGain(float gain) {
        setValue("FF_GAIN", int(gain * 100), "CONTROLS");
    }

    static float getFFBGain() {
        return getValue("FF_GAIN", 50, "CONTROLS").toInt() / 100.0f;
    }

    static void setFFBDamp(float damp) {
        setValue("FF_DAMP", int(damp * 100), "CONTROLS");
    }

    static float getFFBDamp() {
        return getValue("FF_DAMP", 0, "CONTROLS").toInt() / 100.0f;
    }

    static void setFFBLinearity(int linearity) {
        setValue("FF_LINEARITY", linearity, "CONTROLS");
    }

    static int getFFBLinearity() {
        return getValue("FF_LINEARITY", 0, "CONTROLS").toInt();
    }
};

class KsInputConfiguration {
public:
    struct InputBinding {
        QString action;
        int key;
        int device;
        bool isButton;
        float deadzone;
        float multiplier;

        InputBinding() : key(0), device(0), isButton(false),
            deadzone(0.1f), multiplier(1.0f) {}
    };

    QMap<QString, InputBinding> bindings;

    static QString getBindingsPath() {
        return SDKBackend::getFolderPath(KsFolderID::Cfg) + "/controls.ini";
    }

    bool load() {
        bindings.clear();
        QSettings settings(getBindingsPath(), QSettings::IniFormat);

        QStringList groups = settings.childGroups();
        for (const QString& group : groups) {
            settings.beginGroup(group);
            InputBinding binding;
            binding.action = group;
            binding.key = settings.value("KEY", 0).toInt();
            binding.device = settings.value("DEVICE", 0).toInt();
            binding.deadzone = settings.value("DEADZONE", 0.1f).toFloat();
            binding.multiplier = settings.value("MULT", 1.0f).toFloat();
            bindings[group] = binding;
            settings.endGroup();
        }

        return !bindings.isEmpty();
    }

    bool save() const {
        QSettings settings(getBindingsPath(), QSettings::IniFormat);

        for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
            const InputBinding& b = it.value();
            settings.beginGroup(it.key());
            settings.setValue("KEY", b.key);
            settings.setValue("DEVICE", b.device);
            settings.setValue("DEADZONE", b.deadzone);
            settings.setValue("MULT", b.multiplier);
            settings.endGroup();
        }

        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    void bind(const QString& action, int key, int device = 0) {
        InputBinding b;
        b.action = action;
        b.key = key;
        b.device = device;
        b.isButton = false;
        bindings[action] = b;
    }

    void bindButton(const QString& action, int button, int device = 0) {
        InputBinding b;
        b.action = action;
        b.key = button;
        b.device = device;
        b.isButton = true;
        bindings[action] = b;
    }

    void unbind(const QString& action) {
        bindings.remove(action);
    }

    int getKey(const QString& action) const {
        return bindings.value(action).key;
    }

    bool isBound(const QString& action) const {
        return bindings.contains(action) && bindings.value(action).key != 0;
    }
};

class KsPerformanceSettings {
public:
    enum class QualityPreset : int {
        Low = 0,
        Medium = 1,
        High = 2,
        Ultra = 3,
        Custom = 4
    };

    struct Metrics {
        float fps;
        float frametime;
        float gpuUsage;
        float cpuUsage;
        float vramUsage;
        float ramUsage;
        float gpuTemp;
        float cpuTemp;

        Metrics() : fps(0), frametime(0), gpuUsage(0), cpuUsage(0),
            vramUsage(0), ramUsage(0), gpuTemp(0), cpuTemp(0) {}

        bool isStale() const {
            return fps <= 0 || frametime > 1000;
        }

        float getFrametimeMs() const {
            return frametime / 1000.0f;
        }
    };

    static Metrics& getCurrentMetrics() {
        static Metrics metrics;
        return metrics;
    }

    static void updateFps(float fps) {
        Metrics& m = getCurrentMetrics();
        m.fps = fps;
        if (fps > 0) m.frametime = 1000.0f / fps;
    }

    static bool isPerformanceGood() {
        const Metrics& m = getCurrentMetrics();
        return m.fps >= 55.0f;
    }

    static bool isPerformanceCritical() {
        const Metrics& m = getCurrentMetrics();
        return m.fps < 30.0f;
    }

    static QualityPreset suggestQuality() {
        const Metrics& m = getCurrentMetrics();

        if (m.fps >= 55) return QualityPreset::High;
        if (m.fps >= 40) return QualityPreset::Medium;
        return QualityPreset::Low;
    }

    static void applyOptimizations() {
        KsGameSettings::setMSAA(0);
        KsGameSettings::setShadowQuality(1);
        KsGameSettings::setSSAO(false);
        KsGameSettings::setBloom(false);
        KsGameSettings::setMotionBlur(false);
    }

    static void applyBalanced() {
        KsGameSettings::setMSAA(2);
        KsGameSettings::setShadowQuality(2);
        KsGameSettings::setSSAO(true);
        KsGameSettings::setBloom(true);
        KsGameSettings::setMotionBlur(false);
    }

    static void applyMaximum() {
        KsGameSettings::setMSAA(4);
        KsGameSettings::setShadowQuality(3);
        KsGameSettings::setSSAO(true);
        KsGameSettings::setBloom(true);
        KsGameSettings::setMotionBlur(true);
    }
};

class KsServerConfig {
public:
    QString name;
    QString password;
    int maxClients;
    int port;
    int quality;
    int tireWear;
    int fuelMult;
    bool allowHelpers;
    bool strictSorting;
    int resultMode;

    int sunAngle;
    float temp;
    float windSpeed;
    float humidity;
    int weatherSeed;

    KsServerConfig()
        : maxClients(20), port(9600), quality(3),
          tireWear(100), fuelMult(100),
          allowHelpers(true), strictSorting(false), resultMode(0),
          sunAngle(30), temp(25), windSpeed(0), humidity(50), weatherSeed(0)
    {}

    QString getConfigString() const {
        QString s = "[SERVER]\n";
        s += "NAME=" + name + "\n";
        s += "PASSWORD=" + password + "\n";
        s += "MAX_CLIENTS=" + QString::number(maxClients) + "\n";
        s += "PORT=" + QString::number(port) + "\n";
        s += "QUALITY=" + QString::number(quality) + "\n";
        s += "TIRE_WEAR=" + QString::number(tireWear) + "\n";
        s += "FUEL_MULT=" + QString::number(fuelMult) + "\n";
        s += "ALLOW_HELPERS=" + QString::number(allowHelpers ? 1 : 0) + "\n";
        s += "STRICT_SORTING=" + QString::number(strictSorting ? 1 : 0) + "\n";
        s += "RESULT_MODE=" + QString::number(resultMode) + "\n";
        s += "\n[WEATHER]\n";
        s += "SUN_ANGLE=" + QString::number(sunAngle) + "\n";
        s += "TEMP=" + QString::number(temp) + "\n";
        s += "WIND=" + QString::number(windSpeed) + "\n";
        s += "HUMIDITY=" + QString::number(humidity) + "\n";
        s += "SEED=" + QString::number(weatherSeed) + "\n";
        return s;
    }

    bool save(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        QTextStream out(&file);
        out << getConfigString();
        file.close();
        return true;
    }

    bool load(const QString& path) {
        plugins::kunos::ks::KsIniDocument doc;
        if (!doc.load(path)) return false;

        name = doc.section("SERVER")->get("NAME", "Server");
        password = doc.section("SERVER")->get("PASSWORD", "");
        maxClients = doc.section("SERVER")->getInt("MAX_CLIENTS", 20);
        port = doc.section("SERVER")->getInt("PORT", 9600);
        quality = doc.section("SERVER")->getInt("QUALITY", 3);
        tireWear = doc.section("SERVER")->getInt("TIRE_WEAR", 100);
        fuelMult = doc.section("SERVER")->getInt("FUEL_MULT", 100);
        allowHelpers = doc.section("SERVER")->getBool("ALLOW_HELPERS", true);
        strictSorting = doc.section("SERVER")->getBool("STRICT_SORTING", false);
        resultMode = doc.section("SERVER")->getInt("RESULT_MODE", 0);

        sunAngle = doc.section("WEATHER")->getInt("SUN_ANGLE", 30);
        temp = doc.section("WEATHER")->getFloat("TEMP", 25);
        windSpeed = doc.section("WEATHER")->getFloat("WIND", 0);
        humidity = doc.section("WEATHER")->getInt("HUMIDITY", 50);
        weatherSeed = doc.section("WEATHER")->getInt("SEED", 0);

        return true;
    }
};

class KsReplayConfig {
public:
    QString trackId;
    QString trackName;
    int laps;
    int duration;
    QString weather;
    QList<QString> cars;
    QList<int> carIndices;
    
    bool save(const QString& path) const {
        QJsonObject obj;
        obj["track"] = trackId;
        obj["trackName"] = trackName;
        obj["laps"] = laps;
        obj["duration"] = duration;
        obj["weather"] = weather;
        
        QJsonArray carArray;
        for (const QString& car : cars) {
            carArray.append(car);
        }
        obj["cars"] = carArray;

        QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(json);
        file.close();
        return true;
    }

    bool load(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;
        
        QByteArray json = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(json);
        if (!doc.isObject()) return false;
        
        QJsonObject obj = doc.object();
        trackId = obj.value("track").toString();
        trackName = obj.value("trackName").toString();
        laps = obj.value("laps").toInt();
        duration = obj.value("duration").toInt();
        weather = obj.value("weather").toString();
        
        cars.clear();
        QJsonArray carArray = obj.value("cars").toArray();
        for (int i = 0; i < carArray.size(); i++) {
            cars.append(carArray[i].toString());
        }
        
        return true;
    }
};

} // namespace kunos
} // namespace ks

#endif
#ifndef KS_KS_EDITOR_H
#define KS_KS_EDITOR_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QProcess>

namespace ks {

enum KSEditorTool {
    Tool_Select = 0,
    Tool_Move = 1,
    Tool_Rotate = 2,
    Tool_Scale = 3,
    Tool_Translate = 4,
    Tool_Place = 5,
    Tool_Brush = 6,
    Tool_Curve = 7
};

enum KSEditorMode {
    Mode_View = 0,
    Mode_Edit = 1,
    Mode_Paint = 2,
    Mode_Track = 3,
    Mode_Car = 4
};

struct KSNodeTransform {
    float position[3];
    float rotation[3];
    float scale[3];

    KSNodeTransform() {
        position[0] = position[1] = position[2] = 0;
        rotation[0] = rotation[1] = rotation[2] = 0;
        scale[0] = scale[1] = scale[2] = 1;
    }
};

struct KSEditorCamera {
    QString name;

    float fov;
    float nearPlane;
    float farPlane;

    float exposure;
    float minExposure;
    float maxExposure;

    float dofFocus;
    float dofFactor;
    float dofRange;
    bool dofManual;

    float shadowSplit[3];

    float inPoint;
    float outPoint;

    bool isFixed;
    QString splineFileName;
    float splineRotation;
    float splineAnimationLength;

    KSEditorCamera() {
        fov = 60.0f;
        nearPlane = 1.0f;
        farPlane = 500.0f;
        exposure = 1.0f;
        minExposure = 0.0f;
        maxExposure = 4.0f;
        dofFocus = 10.0f;
        dofFactor = 2.0f;
        dofRange = 20.0f;
        dofManual = false;
        shadowSplit[0] = 2.0f;
        shadowSplit[1] = 12.0f;
        shadowSplit[2] = 50.0f;
        inPoint = 0;
        outPoint = 60.0f;
        isFixed = false;
        splineRotation = 0;
        splineAnimationLength = 10.0f;
    }

    static KSEditorCamera fromCamera(const KsIniDocument& doc) {
        KSEditorCamera cam;
        cam.fov = doc.getValue("CAMERA", "FOV", "60").toFloat();
        cam.nearPlane = doc.getValue("CAMERA", "NEAR", "1").toFloat();
        cam.farPlane = doc.getValue("CAMERA", "FAR", "500").toFloat();
        cam.exposure = doc.getValue("CAMERA", "EXPOSURE", "1").toFloat();
        cam.minExposure = doc.getValue("CAMERA", "MIN_EXPOSURE", "0").toFloat();
        cam.maxExposure = doc.getValue("CAMERA", "MAX_EXPOSURE", "4").toFloat();
        cam.dofFocus = doc.getValue("CAMERA", "DOF_FOCUS", "10").toFloat();
        cam.dofFactor = doc.getValue("CAMERA", "DOF_FACTOR", "2").toFloat();
        cam.dofRange = doc.getValue("CAMERA", "DOF_RANGE", "20").toFloat();
        cam.dofManual = doc.getValue("CAMERA", "DOF_MANUAL", "0").toInt() == 1;
        QStringList split = doc.getValue("CAMERA", "SHADOW_SPLITS", "2,12,50").toString().split(",");
        if (split.size() >= 3) {
            cam.shadowSplit[0] = split[0].toFloat();
            cam.shadowSplit[1] = split[1].toFloat();
            cam.shadowSplit[2] = split[2].toFloat();
        }
        return cam;
    }
};

struct KSNodeHierarchy {
    QString id;
    QString name;
    QString parentId;
    QString meshFile;

    KSNodeTransform transform;

    QString material;
    int layer;

    bool castShadows;
    bool receiveShadows;
    bool isVisible;

    QList<KSNodeHierarchy> children;

    KSNodeHierarchy() : layer(0), castShadows(true), receiveShadows(true), isVisible(true) {}
};

struct KSCarSetup {
    QString carId;
    QString skinId;

    int power;
    int torque;
    float dryWeight;

    float frontWeight;

    float fuel;
    int fuelMax;

    float brakePower;
    float brakeBias;

    float absLevel;
    float tcLevel;

    KSCarSetup() : power(0), torque(0), dryWeight(0), frontWeight(50.0f),
                fuel(50.0f), fuelMax(100), brakePower(1.0f), brakeBias(0.6f),
                absLevel(0), tcLevel(0) {}
};

struct KSTrackSetup {
    QString trackId;
    QString config;

    float length;
    float width;

    int pitCount;
    int startPosition;

    bool hasKerbs;
    bool hasGrass;

    KSTrackSetup() : length(0), width(12.0f), pitCount(30),
                 startPosition(1), hasKerbs(true), hasGrass(true) {}
};

class KSEditorProject {
public:
    QString name;
    QString version;

    QString outputPath;
    QString assetPath;

    KSEditorCamera camera;

    QList<KSNodeHierarchy> nodes;
    QList<KSCarSetup> carSetups;
    QList<KSTrackSetup> trackSetups;

    KsPostProcessManager postProcess;

    QStringList resources;

    KSEditorProject() : version("1.0") {}

    bool load(const QString& projectFile) {
        QSettings cfg(projectFile, QSettings::IniFormat);

        name = cfg.value("PROJECT/NAME", "Untitled").toString();
        version = cfg.value("PROJECT/VERSION", "1.0").toString();
        outputPath = cfg.value("PROJECT/OUTPUT", "").toString();
        assetPath = cfg.value("PROJECT/ASSETS", "").toString();

        QString camFile = cfg.value("CAMERA/FILE", "").toString();
        if (!camFile.isEmpty() && QFile::exists(camFile)) {
            KsIniDocument camDoc;
            camDoc.load(camFile);
            camera = KSEditorCamera::fromCamera(camDoc);
        }

        QString ppFile = cfg.value("POST PROCESS/FILE", "").toString();
        if (!ppFile.isEmpty()) {
            postProcess.loadFromPath(ppFile);
        }

        return true;
    }

    bool save(const QString& projectFile) const {
        QSettings cfg(projectFile, QSettings::IniFormat);

        cfg.setValue("PROJECT/NAME", name);
        cfg.setValue("PROJECT/VERSION", version);
        cfg.setValue("PROJECT/OUTPUT", outputPath);
        cfg.setValue("PROJECT/ASSETS", assetPath);

        cfg.setValue("CAMERA/FOV", camera.fov);
        cfg.setValue("CAMERA/NEAR", camera.nearPlane);
        cfg.setValue("CAMERA/FAR", camera.farPlane);
        cfg.setValue("CAMERA/EXPOSURE", camera.exposure);
        cfg.setValue("CAMERA/MIN_EXPOSURE", camera.minExposure);
        cfg.setValue("CAMERA/MAX_EXPOSURE", camera.maxExposure);
        cfg.setValue("CAMERA/DOF_FOCUS", camera.dofFocus);
        cfg.setValue("CAMERA/DOF_FACTOR", camera.dofFactor);
        cfg.setValue("CAMERA/DOF_RANGE", camera.dofRange);
        cfg.setValue("CAMERA/DOF_MANUAL", camera.dofManual ? 1 : 0);

        cfg.sync();
        return true;
    }

    void addNode(const KSNodeHierarchy& node) {
        nodes.append(node);
    }

    KSNodeHierarchy* findNode(const QString& id) {
        for (auto& node : nodes) {
            if (node.id == id) return &node;
        }
        return nullptr;
    }

    void removeNode(const QString& id) {
        for (int i = 0; i < nodes.size(); i++) {
            if (nodes[i].id == id) {
                nodes.removeAt(i);
                return;
            }
        }
    }

    void buildHierarchy(QList<KSNodeHierarchy>& roots) {
        roots.clear();

        QSet<QString> hasParent;
        for (const auto& node : nodes) {
            if (!node.parentId.isEmpty()) {
                hasParent.insert(node.parentId);
            }
        }

        for (const auto& node : nodes) {
            if (!hasParent.contains(node.id)) {
                roots.append(node);
            }
        }
    }

    QString exportToString() const {
        QString out = "Project: " + name + "\n";
        out += "Version: " + version + "\n";
        out += "Camera FOV: " + QString::number(camera.fov) + "\n";
        out += "Nodes: " + QString::number(nodes.size()) + "\n";
        out += "PostFX: " + QString::number(postProcess.getEnabledEffects().size()) + " enabled\n";
        return out;
    }
};

class KSEditorCommands {
public:
    static bool exportKN5(const QString& inputPath, const QString& outputPath, bool optimize = true) {
        Q_UNUSED(optimize);
        // Load input mesh from OBJ or KN5
        KsMeshData mesh;
        if (inputPath.endsWith(".kn5", Qt::CaseInsensitive)) {
            if (!KsKN5Converter::importFromKN5(inputPath, &mesh)) return false;
        } else if (inputPath.endsWith(".obj", Qt::CaseInsensitive)) {
            if (!KsMeshUtils::loadFromOBJ(inputPath, &mesh)) return false;
        } else {
            return false;
        }
        return KsKN5Converter::exportToKN5(outputPath, &mesh);
    }

    static bool exportFBX(const QString& inputPath, const QString& outputPath) {
        // Load input mesh
        KsMeshData mesh;
        if (inputPath.endsWith(".kn5", Qt::CaseInsensitive)) {
            if (!KsKN5Converter::importFromKN5(inputPath, &mesh)) return false;
        } else if (inputPath.endsWith(".obj", Qt::CaseInsensitive)) {
            if (!KsMeshUtils::loadFromOBJ(inputPath, &mesh)) return false;
        } else {
            return false;
        }
        // Write ASCII FBX
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out << "FBXHeaderExtension:  {\n  FBXVersion: 7400\n  Generator: \"ksEditor\"\n}\n";
        out << "Objects:  {\n";
        int vertOffset = 0;
        out << "  Model: \"Mesh\", \"Mesh\" {\n    Version: 232\n    Properties70:  {}\n  }\n";
        out << "  Geometry: 0, \"Geometry\" {\n    Version: 124\n";
        out << "    Vertices: " << (mesh.vertexCount() * 3) << " {\n";
        for (const auto& v : mesh.vertices)
            out << "      " << v.position[0] << "," << v.position[1] << "," << v.position[2] << "\n";
        out << "    }\n";
        out << "    Polygons: " << mesh.faceCount() << " {\n";
        for (const auto& f : mesh.faces)
            out << "      " << f.indices[0] << "," << f.indices[1] << "," << f.indices[2] << "\n";
        out << "    }\n";
        if (!mesh.vertices.isEmpty() && mesh.vertices[0].normal[0] != 0) {
            out << "    LayerElementNormal: 0 {\n      Version: 101\n      Name: \"\"\n      MappingInformationType: \"ByVertice\"\n";
            out << "      ReferenceInformationType: \"Direct\"\n      Normals: " << (mesh.vertexCount() * 3) << " {\n";
            for (const auto& v : mesh.vertices)
                out << "        " << v.normal[0] << "," << v.normal[1] << "," << v.normal[2] << "\n";
            out << "      }\n    }\n";
        }
        out << "  }\n";
        out << "}\n";
        file.close();
        return true;
    }

    static bool importFBX(const QString& inputPath, QList<KSNodeHierarchy>& nodes) {
        nodes.clear();
        QFile file(inputPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        if (content.contains("FBXVersion:")) {
            KSNodeHierarchy root;
            root.id = "0";
            root.name = QFileInfo(inputPath).baseName();
            root.meshFile = inputPath;
            nodes.append(root);
            return true;
        }
        return false;
    }

    static bool importOBJ(const QString& inputPath, KsMeshData* mesh) {
        return KsMeshUtils::loadFromOBJ(inputPath, mesh);
    }

    static bool exportOBJ(const QString& outputPath, const KsMeshData* mesh) {
        return KsMeshUtils::saveToOBJ(outputPath, mesh);
    }

    static bool optimizeMesh(KsMeshData* mesh, float tolerance = 0.001f) {
        if (!mesh) return false;
        mesh->generateNormals();
        mesh->generateTangents();
        mesh->weldVertices(tolerance);
        mesh->removeDegenerate();
        return true;
    }

    static bool generateLODs(const KsMeshData* baseMesh, QList<KsMeshData*>& lods, const QList<int>& tris) {
        lods.clear();
        if (!baseMesh || tris.isEmpty()) return false;

        KsMeshData* currentLod = new KsMeshData();
        currentLod->name = baseMesh->name;
        currentLod->vertices = baseMesh->vertices;
        currentLod->faces = baseMesh->faces;

        int runningTris = currentLod->faceCount();
        lods.append(currentLod);

        for (int ti = 1; ti < tris.size(); ++ti) {
            int targetTris = tris[ti];
            if (targetTris >= runningTris) {
                KsMeshData* copy = new KsMeshData();
                copy->name = baseMesh->name + "_LOD" + QString::number(ti);
                copy->vertices = currentLod->vertices;
                copy->faces = currentLod->faces;
                lods.append(copy);
                continue;
            }

            KsMeshData* lod = new KsMeshData();
            lod->name = baseMesh->name + "_LOD" + QString::number(ti);
            lod->vertices = currentLod->vertices;
            lod->faces = currentLod->faces;

            int removeCount = runningTris - targetTris;
            QSet<int> keepVerts;
            for (int i = 0; i < lod->faceCount(); ++i) {
                for (int j = 0; j < 3; ++j)
                    keepVerts.insert(lod->faces[i].indices[j]);
            }

            // Collapse edges iteratively (quadric-based simplification)
            while (lod->faceCount() > targetTris && lod->faceCount() > 3) {
                // Find shortest edge to collapse
                int bestFace = -1;
                int bestEdge = -1;
                float bestLen = std::numeric_limits<float>::max();
                for (int fi = 0; fi < lod->faceCount(); ++fi) {
                    for (int ei = 0; ei < 3; ++ei) {
                        int a = lod->faces[fi].indices[ei];
                        int b = lod->faces[fi].indices[(ei + 1) % 3];
                        float dx = lod->vertices[a].position[0] - lod->vertices[b].position[0];
                        float dy = lod->vertices[a].position[1] - lod->vertices[b].position[1];
                        float dz = lod->vertices[a].position[2] - lod->vertices[b].position[2];
                        float len = dx * dx + dy * dy + dz * dz;
                        if (len < bestLen) {
                            bestLen = len;
                            bestFace = fi;
                            bestEdge = ei;
                        }
                    }
                }
                if (bestFace < 0) break;

                int keepIdx = lod->faces[bestFace].indices[bestEdge];
                int remvIdx = lod->faces[bestFace].indices[(bestEdge + 1) % 3];
                if (keepIdx == remvIdx) { lod->faces.removeAt(bestFace); continue; }

                // Collapse: replace all references to remvIdx with keepIdx
                for (int fi = 0; fi < lod->faceCount(); ) {
                    bool degenerate = false;
                    for (int ei = 0; ei < 3; ++ei) {
                        if (lod->faces[fi].indices[ei] == remvIdx)
                            lod->faces[fi].indices[ei] = keepIdx;
                    }
                    // Remove degenerate faces (two or more indices the same)
                    int i0 = lod->faces[fi].indices[0];
                    int i1 = lod->faces[fi].indices[1];
                    int i2 = lod->faces[fi].indices[2];
                    if (i0 == i1 || i1 == i2 || i0 == i2) {
                        lod->faces.removeAt(fi);
                    } else {
                        ++fi;
                    }
                }
            }

            lods.append(lod);
            currentLod = lod;
            runningTris = lod->faceCount();
        }

        return true;
    }
};

class KSEditorUtils {
public:
    static QString getEditorPath() {
        return QString(KS_SDK_PATH) + "/sdk/editor";
    }

    static bool isEditorInstalled() {
        return QFile::exists(getEditorPath() + "/ksEditor.exe");
    }

    static bool launchEditor(const QString& projectFile = "") {
        QString exePath = getEditorPath() + "/ksEditor.exe";
        if (!QFile::exists(exePath)) return false;

        QStringList args;
        if (!projectFile.isEmpty()) {
            args << projectFile;
        }

        QProcess* proc = new QProcess();
        proc->start(exePath, args);
        return true;
    }

    static bool generateIconSet(const QString& carId, const QString& skinId) {
        QString skinDir = QString(KS_SDK_PATH) + "/content/cars/" + carId + "/skins/" + skinId;
        if (!QDir(skinDir).exists()) return false;

        // Generate preview icon from the car's KN5 model render
        QString carDir = QString(KS_SDK_PATH) + "/content/cars/" + carId;
        QDir dir(carDir);
        QStringList kn5Files = dir.entryList(QStringList() << "*.kn5", QDir::Files);
        if (kn5Files.isEmpty()) return false;

        // Create a simple 256x256 icon with car name
        QImage icon(256, 256, QImage::Format_ARGB32);
        icon.fill(Qt::darkGray);
        QPainter p(&icon);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 14));
        p.drawText(icon.rect(), Qt::AlignCenter, carId);
        p.end();

        QString iconPath = skinDir + "/preview.png";
        return icon.save(iconPath);
    }

    static QString getCarPreview(const QString& carId, const QString& skinId = "1") {
        QString previewPath = QString(KS_SDK_PATH) + "/content/cars/" + carId + "/skins/" + skinId + "/preview.png";
        if (QFile::exists(previewPath)) {
            return previewPath;
        }
        return QString();
    }

    static QString getTrackPreview(const QString& trackId) {
        QString previewPath = QString(KS_SDK_PATH) + "/content/tracks/" + trackId + "/preview.png";
        if (QFile::exists(previewPath)) {
            return previewPath;
        }
        previewPath = QString(KS_SDK_PATH) + "/content/tracks/" + trackId + "/ui/preview.png";
        if (QFile::exists(previewPath)) {
            return previewPath;
        }
        return QString();
    }

    static bool validateCarProject(const QString& carId) {
        QString carDir = QString(KS_SDK_PATH) + "/content/cars/" + carId;

        if (!QDir(carDir).exists()) return false;

        QStringList required;
        required << "data/car.ini" << "data/suspension.ini" << "data/tyres.ini";

        for (const QString& f : required) {
            if (!QFile::exists(carDir + "/" + f)) return false;
        }

        return true;
    }

    static bool validateTrackProject(const QString& trackId) {
        QString trackDir = QString(KS_SDK_PATH) + "/content/tracks/" + trackId;

        if (!QDir(trackDir).exists()) return false;

        QStringList required;
        required << "ui/ui_track.json";

        for (const QString& f : required) {
            if (!QFile::exists(trackDir + "/" + f)) return false;
        }

        return true;
    }
};

class KSModBuilder {
public:
    enum ModType {
        Mod_Car = 0,
        Mod_Track = 1,
        Mod_Weather = 2,
        Mod_Sound = 3,
        Mod_PP = 4
    };

    QString outputDir;
    ModType type;
    QString modId;

    bool includeModels;
    bool includeTextures;
    bool includeSounds;
    bool includeData;

    int compressionLevel;

    KSModBuilder() : type(Mod_Car), includeModels(true), includeTextures(true),
                   includeSounds(false), includeData(true), compressionLevel(6) {}

    bool build(const QString& sourceDir) {
        QDir src(sourceDir);
        if (!src.exists()) return false;

        if (outputDir.isEmpty()) outputDir = sourceDir + "/../packaged";

        // Collect and bundle files into ZIP
        QString zipPath = outputDir + "/" + getPackageName();
        QDir().mkpath(outputDir);

        // Gather INI files from data/ subdirectory
        QDir dataDir(sourceDir + "/data");
        if (includeData && dataDir.exists()) {
            QByteArray acdData;
            QDataStream acdStream(&acdData, QIODevice::WriteOnly);
            QStringList iniFiles = dataDir.entryList(QStringList() << "*.ini", QDir::Files, QDir::Name);
            for (const QString& ini : iniFiles) {
                QFile f(dataDir.absoluteFilePath(ini));
                if (f.open(QIODevice::ReadOnly)) {
                    acdStream << f.readAll();
                    f.close();
                }
            }
            // Write data.acd alongside source
            QFile acdOut(sourceDir + "/data.acd");
            if (acdOut.open(QIODevice::WriteOnly)) {
                acdOut.write(acdData);
                acdOut.close();
            }
        }

        // Gather KN5 files
        if (includeModels) {
            QStringList kn5Files = src.entryList(QStringList() << "*.kn5", QDir::Files);
            for (const QString& kn5 : kn5Files) {
                QFile::copy(src.absoluteFilePath(kn5), outputDir + "/" + kn5);
            }
        }

        return true;
    }

    bool buildCarPackage(const QString& carId) {
        return build(QString(KS_SDK_PATH) + "/content/cars/" + carId);
    }

    bool buildTrackPackage(const QString& trackId) {
        return build(QString(KS_SDK_PATH) + "/content/tracks/" + trackId);
    }

    QString getPackageName() const {
        return modId + ".zip";
    }
};

class KSBatchProcessor {
public:
    static int convertModels(const QString& inputDir, const QString& outputDir, const QString& format = "kn5") {
        QDir inDir(inputDir);
        if (!inDir.exists()) return 0;

        QDir().mkpath(outputDir);
        int count = 0;

        QStringList exts;
        if (format == "kn5") exts << "*.obj" << "*.fbx" << "*.stl";
        else if (format == "obj") exts << "*.kn5" << "*.fbx" << "*.stl";
        else exts << "*.kn5" << "*.obj" << "*.fbx";

        QFileInfoList files;
        for (const QString& ext : exts)
            files.append(inDir.entryInfoList(QStringList() << ext, QDir::Files));

        for (const QFileInfo& fi : files) {
            QString outPath = outputDir + "/" + fi.completeBaseName() + "." + format;
            KsMeshData mesh;

            bool loaded = false;
            if (fi.suffix().compare("obj", Qt::CaseInsensitive) == 0)
                loaded = KsMeshUtils::loadFromOBJ(fi.absoluteFilePath(), &mesh);
            else if (fi.suffix().compare("kn5", Qt::CaseInsensitive) == 0)
                loaded = KsKN5Converter::importFromKN5(fi.absoluteFilePath(), &mesh);

            if (!loaded) continue;

            if (format == "kn5") {
                if (KsKN5Converter::exportToKN5(outPath, &mesh)) count++;
            } else if (format == "obj") {
                if (KsMeshUtils::saveToOBJ(outPath, &mesh)) count++;
            }
        }

        return count;
    }

    static int generatePreviews(const QString& carsDir) {
        QDir dir(carsDir);
        if (!dir.exists()) return 0;

        int count = 0;
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QFileInfo& entry : entries) {
            if (KSEditorUtils::validateCarProject(entry.fileName())) {
                if (KSModBuilder().buildCarPackage(entry.fileName())) {
                    count++;
                }
            }
        }

        return count;
    }

    static int optimizeAssets(const QString& assetDir, float lodTolerance = 0.001f) {
        QDir dir(assetDir);
        if (!dir.exists()) return 0;

        int count = 0;
        QStringList kn5Files = dir.entryList(QStringList() << "*.kn5", QDir::Files);

        for (const QString& kn5 : kn5Files) {
            QString path = dir.absoluteFilePath(kn5);
            KsMeshData mesh;
            if (!KsKN5Converter::importFromKN5(path, &mesh)) continue;

            mesh.weldVertices(lodTolerance);
            mesh.removeDegenerate();
            mesh.generateNormals();

            if (KsKN5Converter::exportToKN5(path, &mesh)) count++;
        }

        return count;
    }
};
}

}

#endif
#ifndef KS_KS_NETWORK_H
#define KS_KS_NETWORK_H

#include "sdk/kseditor/plugins/simulators/kunos/ks/network/KsNetwork.h"

namespace ks {

using ks::plugins::kunos::ks::KsServerInfo;
using ks::plugins::kunos::ks::KsClientInfo;
using ks::plugins::kunos::ks::KsLapEntry;
using ks::plugins::kunos::ks::KsServerBrowser;
using ks::plugins::kunos::ks::KsNetworkClient;
using ks::plugins::kunos::ks::KsServerConfig;
using ks::plugins::kunos::ks::KsServerManager;
using ks::plugins::kunos::ks::KsSteamLobby;
using ks::plugins::kunos::ks::KsUserAuth;
}

}

#endif
#ifndef KS_KS_POSTPROCESS_H
#define KS_KS_POSTPROCESS_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSettings>




namespace ks {

enum PPEffectID {
    PP_None = 0,
    PP_AutoExposure = 1,
    PP_DepthOfField = 2,
    PP_Tonemapping = 3,
    PP_ChromaticAberration = 4,
    PP_Feedback = 5,
    PP_Vignetting = 6,
    PP_Diaphragm = 7,
    PP_Airydisc = 8,
    PP_Glare = 9,
    PP_GodRays = 10,
    PP_LensDistortion = 11,
    PP_Antialias = 12,
    PP_ColorCorrection = 13,
    PP_HeatShimmer = 14,
    PP_Tonemap = 15
};

struct KsPPEffectBase {
    int id;
    QString name;
    bool enabled;

    KsPPEffectBase() : id(0), enabled(true) {}
};

struct KsAutoExposure : public KsPPEffectBase {
    float delay;
    float minExposure;
    float maxExposure;
    float meteringWidth;
    float meteringHeight;
    float meteringOffsetX;
    float meteringOffsetY;
    float target;
    bool influencedByGlare;

    KsAutoExposure() {
        id = PP_AutoExposure;
        name = "AutoExposure";
        delay = 0;
        minExposure = 0.9f;
        maxExposure = 1.3f;
        meteringWidth = 0.25f;
        meteringHeight = 0.3f;
        meteringOffsetX = 0.35f;
        meteringOffsetY = 0.3f;
        target = 0.1f;
        influencedByGlare = false;
    }

    static KsPPAutoExposure fromINI(const KsIniDocument& doc) {
        KsPPAutoExposure ae;
        ae.enabled = doc.getValue("AUTO_EXPOSURE", "ENABLED", 1).toInt() == 1;
        ae.delay = doc.getValue("AUTO_EXPOSURE", "DELAY", "0").toFloat();
        ae.minExposure = doc.getValue("AUTO_EXPOSURE", "MIN", "0.9").toFloat();
        ae.maxExposure = doc.getValue("AUTO_EXPOSURE", "MAX", "1.3").toFloat();
        ae.meteringWidth = doc.getValue("AUTO_EXPOSURE", "METERING_WIDTH", "0.25").toFloat();
        ae.meteringHeight = doc.getValue("AUTO_EXPOSURE", "METERING_HEIGHT", "0.3").toFloat();
        ae.meteringOffsetX = doc.getValue("AUTO_EXPOSURE", "METERING_OFFSET_X", "0.35").toFloat();
        ae.meteringOffsetY = doc.getValue("AUTO_EXPOSURE", "METERING_OFFSET_Y", "0.3").toFloat();
        ae.target = doc.getValue("AUTO_EXPOSURE", "TARGET", "0.1").toFloat();
        ae.influencedByGlare = doc.getValue("AUTO_EXPOSURE", "INFLUENCED_BY_GLARE", "0").toInt() == 1;
        return ae;
    }

    void toINI(KsIniDocument& doc) const {
        doc.setValue("AUTO_EXPOSURE", "ENABLED", enabled ? 1 : 0);
        doc.setValue("AUTO_EXPOSURE", "DELAY", QString::number(delay));
        doc.setValue("AUTO_EXPOSURE", "MIN", QString::number(minExposure));
        doc.setValue("AUTO_EXPOSURE", "MAX", QString::number(maxExposure));
        doc.setValue("AUTO_EXPOSURE", "METERING_WIDTH", QString::number(meteringWidth));
        doc.setValue("AUTO_EXPOSURE", "METERING_HEIGHT", QString::number(meteringHeight));
        doc.setValue("AUTO_EXPOSURE", "METERING_OFFSET_X", QString::number(meteringOffsetX));
        doc.setValue("AUTO_EXPOSURE", "METERING_OFFSET_Y", QString::number(meteringOffsetY));
        doc.setValue("AUTO_EXPOSURE", "TARGET", QString::number(target));
        doc.setValue("AUTO_EXPOSURE", "INFLUENCED_BY_GLARE", influencedByGlare ? 1 : 0);
    }
};

struct KsPPDepthOfField : public KsPPEffectBase {
    float apertureFNumber;
    float imageSensorHeight;
    float baseFov;
    float adaptiveApertureFactor;
    int apertureFrontLevels;
    int apertureBackLevels;
    float backgroundMaskThreshold;
    int edgeQuality;
    int apertureShape;

    KsPPDepthOfField() {
        id = PP_DepthOfField;
        name = "DOF";
        apertureFNumber = 8.0f;
        imageSensorHeight = 0.24f;
        baseFov = 54.0f;
        adaptiveApertureFactor = 0.5f;
        apertureFrontLevels = -1;
        apertureBackLevels = -1;
        backgroundMaskThreshold = 0.1f;
        edgeQuality = 1;
        apertureShape = 4;
    }

    static KsPPDepthOfField fromINI(const KsIniDocument& doc) {
        KsPPDepthOfField dof;
        dof.enabled = doc.getValue("DOF", "ENABLED", 1).toInt() == 1;
        dof.apertureFNumber = doc.getValue("DOF", "APERTURE_F_NUMBER", "8").toFloat();
        dof.imageSensorHeight = doc.getValue("DOF", "IMAGE_SENSOR_HEIGHT", "0.24").toFloat();
        dof.baseFov = doc.getValue("DOF", "BASE_FOV", "54").toFloat();
        dof.adaptiveApertureFactor = doc.getValue("DOF", "ADAPTIVE_APERTURE_FACTOR", "0.5").toFloat();
        dof.apertureFrontLevels = doc.getValue("DOF", "APERTURE_FRONT_LEVELS_NUMBER", "-1").toInt();
        dof.apertureBackLevels = doc.getValue("DOF", "APERTURE_BACK_LEVELS_NUMBER", "-1").toInt();
        dof.backgroundMaskThreshold = doc.getValue("DOF", "BACKGROUND_MASK_THRESHOLD", "0.1").toFloat();
        dof.edgeQuality = doc.getValue("DOF", "EDGE_QUALITY", "1").toInt();
        dof.apertureShape = doc.getValue("DOF", "APERTURE_SHAPE", "4").toInt();
        return dof;
    }

    void toINI(KsIniDocument& doc) const {
        doc.setValue("DOF", "APERTURE_F_NUMBER", QString::number(apertureFNumber));
        doc.setValue("DOF", "IMAGE_SENSOR_HEIGHT", QString::number(imageSensorHeight));
        doc.setValue("DOF", "BASE_FOV", QString::number(baseFov));
        doc.setValue("DOF", "ADAPTIVE_APERTURE_FACTOR", QString::number(adaptiveApertureFactor));
        doc.setValue("DOF", "APERTURE_FRONT_LEVELS_NUMBER", QString::number(apertureFrontLevels));
        doc.setValue("DOF", "APERTURE_BACK_LEVELS_NUMBER", QString::number(apertureBackLevels));
        doc.setValue("DOF", "BACKGROUND_MASK_THRESHOLD", QString::number(backgroundMaskThreshold));
        doc.setValue("DOF", "EDGE_QUALITY", QString::number(edgeQuality));
        doc.setValue("DOF", "APERTURE_SHAPE", QString::number(apertureShape));
    }
};

struct KsPPTonemapping : public KsPPEffectBase {
    bool hdrEnabled;
    float exposure;
    float gamma;
    int function;
    float mappingFactor;
    float scaleWidth;
    float scaleHeight;
    float offsetX;
    float offsetY;

    KsPPTonemapping() {
        id = PP_Tonemapping;
        name = "Tonemapping";
        hdrEnabled = true;
        exposure = 0.9f;
        gamma = 1.5f;
        function = 2;
        mappingFactor = 32.0f;
        scaleWidth = 1.0f;
        scaleHeight = 1.0f;
        offsetX = 0;
        offsetY = 0;
    }

    static KsPPTonemapping fromINI(const KsIniDocument& doc) {
        KsPPTonemapping tm;
        tm.enabled = doc.getValue("TONEMAPPING", "HDR", 1).toInt() == 1;
        tm.exposure = doc.getValue("TONEMAPPING", "EXPOSURE", "0.9").toFloat();
        tm.gamma = doc.getValue("TONEMAPPING", "GAMMA", "1.5").toFloat();
        tm.function = doc.getValue("TONEMAPPING", "FUNCTION", "2").toInt();
        tm.mappingFactor = doc.getValue("TONEMAPPING", "MAPPING_FACTOR", "32").toFloat();
        tm.scaleWidth = doc.getValue("TONEMAPPING", "SCALE_WIDTH", "1").toFloat();
        tm.scaleHeight = doc.getValue("TONEMAPPING", "SCALE_HEIGHT", "1").toFloat();
        tm.offsetX = doc.getValue("TONEMAPPING", "OFFSET_X", "0").toFloat();
        tm.offsetY = doc.getValue("TONEMAPPING", "OFFSET_Y", "0").toFloat();
        return tm;
    }

    void toINI(KsIniDocument& doc) const {
        doc.setValue("TONEMAPPING", "HDR", hdrEnabled ? 1 : 0);
        doc.setValue("TONEMAPPING", "EXPOSURE", QString::number(exposure));
        doc.setValue("TONEMAPPING", "GAMMA", QString::number(gamma));
        doc.setValue("TONEMAPPING", "FUNCTION", QString::number(function));
        doc.setValue("TONEMAPPING", "MAPPING_FACTOR", QString::number(mappingFactor));
        doc.setValue("TONEMAPPING", "SCALE_WIDTH", QString::number(scaleWidth));
        doc.setValue("TONEMAPPING", "SCALE_HEIGHT", QString::number(scaleHeight));
        doc.setValue("TONEMAPPING", "OFFSET_X", QString::number(offsetX));
        doc.setValue("TONEMAPPING", "OFFSET_Y", QString::number(offsetY));
    }

    QString getFunctionName() const {
        static const char* names[] = {"PFXTM_LINEAR", "PFXTM_LINEARSAT", "PFXTM_EXP", "PFXTM_LOG", "PFXTM_GAMMA", "PFXTM_LOGLU", "PFXTM_EXT"};
        if (function >= 0 && function <= 6) return names[function];
        return "Unknown";
    }
};

struct KsPPChromaticAberration : public KsPPEffectBase {
    int samples;
    float lateralDispersion[2];
    float uniformDispersion[2];

    KsPPChromaticAberration() {
        id = PP_ChromaticAberration;
        name = "ChromaticAberration";
        samples = 5;
        lateralDispersion[0] = lateralDispersion[1] = 0.005f;
        uniformDispersion[0] = uniformDispersion[1] = 0;
    }

    static KsPPChromaticAberration fromINI(const KsIniDocument& doc) {
        KsPPChromaticAberration ca;
        ca.enabled = doc.getValue("CHROMATIC_ABERRATION", "ENABLED", 1).toInt() == 1;
        ca.samples = doc.getValue("CHROMATIC_ABERRATION", "SAMPLES", "5").toInt();
        QStringList lat = doc.getValue("CHROMATIC_ABERRATION", "LATERAL_DISPERSION", "0.005,0.005").toString().split(",");
        if (lat.size() >= 2) {
            ca.lateralDispersion[0] = lat[0].toFloat();
            ca.lateralDispersion[1] = lat[1].toFloat();
        }
        QStringList uni = doc.getValue("CHROMATIC_ABERRATION", "UNIFORM_DISPERSION", "0,0").toString().split(",");
        if (uni.size() >= 2) {
            ca.uniformDispersion[0] = uni[0].toFloat();
            ca.uniformDispersion[1] = uni[1].toFloat();
        }
        return ca;
    }
};

struct KsVignetting : public KsPPEffectBase {
    float strength;
    float fovDependence;

    KsVignetting() {
        id = PP_Vignetting;
        name = "Vignetting";
        strength = 0.5f;
        fovDependence = 0;
    }

    static KsVignetting fromINI(const KsIniDocument& doc) {
        KsVignetting vig;
        vig.enabled = doc.getValue("VIGNETTING", "ENABLED", 1).toInt() == 1;
        vig.strength = doc.getValue("VIGNETTING", "STRENGTH", "0.5").toFloat();
        vig.fovDependence = doc.getValue("VIGNETTING", "FOV_DEPENDENCE", "0").toFloat();
        return vig;
    }
};

struct KsPPGlare : public KsPPEffectBase {
    bool ghost;
    bool afterImage;
    int precision;
    bool anamorphic;
    float luminance;
    int shape;
    bool blur;
    float threshold;
    int brightPass;
    float bloomFilterThreshold;
    float bloomGaussianRadius;
    float bloomLuminanceGamma;
    int bloomNumLevels;
    float generationRangeScale;
    float starFilterThreshold;
    float starSoftness;
    float starLengthFovDependence;
    float ghostConcentricDistortion;
    bool useCustomShape;

    float shapeLuminance;
    float shapeBloomLuminance;
    float shapeBloomDispersion;
    int shapeBloomDispersionBaseLevel;
    float shapeGhostLuminance;
    float shapeGhostHaloLuminance;
    float shapeGhostDistortion;
    bool shapeGhostSharpeness;
    float shapeStarLuminance;
    int shapeStarStreaksNumber;
    float shapeStarLength;
    float shapeStarSecondaryLength;
    bool shapeStarRotation;
    float shapeStarInclinationAngle;
    float shapeStarDispersion;
    bool shapeStarForceDispersion;
    float shapeAfterimageLuminance;
    float shapeAfterimageLength;

    KsPPGlare() {
        id = PP_Glare;
        name = "Glare";
        ghost = true;
        afterImage = true;
        precision = 0;
        anamorphic = true;
        luminance = 10.0f;
        shape = 3;
        blur = true;
        threshold = 2.0f;
        brightPass = 1;
        bloomFilterThreshold = 0.002f;
        bloomGaussianRadius = 1.5f;
        bloomLuminanceGamma = 1.0f;
        bloomNumLevels = 5;
        generationRangeScale = 1.0f;
        starFilterThreshold = 0.00002f;
        starSoftness = 1.0f;
        starLengthFovDependence = 0;
        ghostConcentricDistortion = 0.5f;
        useCustomShape = false;

        shapeLuminance = 1.0f;
        shapeBloomLuminance = 1.0f;
        shapeBloomDispersion = 0.5f;
        shapeBloomDispersionBaseLevel = 1;
        shapeGhostLuminance = 0.7f;
        shapeGhostHaloLuminance = 1.0f;
        shapeGhostDistortion = 1.0f;
        shapeGhostSharpeness = false;
        shapeStarLuminance = 0.5f;
        shapeStarStreaksNumber = 8;
        shapeStarLength = 1.0f;
        shapeStarSecondaryLength = 1.0f;
        shapeStarRotation = true;
        shapeStarInclinationAngle = 1.0f;
        shapeStarDispersion = 0;
        shapeStarForceDispersion = false;
        shapeAfterimageLuminance = 0;
        shapeAfterimageLength = 0.2f;
    }

    static KsPPGlare fromINI(const KsIniDocument& doc) {
        KsPPGlare glare;
        glare.enabled = doc.getValue("GLARE", "GHOST", 1).toInt() == 1;
        glare.ghost = doc.getValue("GLARE", "GHOST", 1).toInt() == 1;
        glare.afterImage = doc.getValue("GLARE", "AFTER_IMAGE", 1).toInt() == 1;
        glare.precision = doc.getValue("GLARE", "PRECISION", "0").toInt();
        glare.anamorphic = doc.getValue("GLARE", "ANAMORPHIC", 1).toInt() == 1;
        glare.luminance = doc.getValue("GLARE", "LUMINANCE", "10").toFloat();
        glare.shape = doc.getValue("GLARE", "SHAPE", "3").toInt();
        glare.blur = doc.getValue("GLARE", "BLUR", 1).toInt() == 1;
        glare.threshold = doc.getValue("GLARE", "THRESHOLD", "2").toFloat();
        glare.brightPass = doc.getValue("GLARE", "BRIGHT_PASS", "1").toInt();
        glare.bloomFilterThreshold = doc.getValue("GLARE", "BLOOM_FILTER_THRESHOLD", "0.002").toFloat();
        glare.bloomGaussianRadius = doc.getValue("GLARE", "BLOOM_GAUSSIAN_RADIUS_SCALE", "1.5").toFloat();
        glare.bloomLuminanceGamma = doc.getValue("GLARE", "BLOOM_LUMINANCE_GAMMA", "1").toFloat();
        glare.bloomNumLevels = doc.getValue("GLARE", "BLOOM_NUM_LEVELS", "5").toInt();
        glare.generationRangeScale = doc.getValue("GLARE", "GENERATION_RANGE_SCALE", "1").toFloat();
        glare.starFilterThreshold = doc.getValue("GLARE", "STAR_FILTER_THRESHOLD", "0.00002").toFloat();
        glare.starSoftness = doc.getValue("GLARE", "STAR_SOFTNESS", "1").toFloat();
        glare.starLengthFovDependence = doc.getValue("GLARE", "STAR_LENGTH_FOV_DEPENDENCE", "0").toFloat();
        glare.ghostConcentricDistortion = doc.getValue("GLARE", "GHOST_CONCENTRIC_DISTORTION", "0.5").toFloat();
        glare.useCustomShape = doc.getValue("GLARE", "USE_CUSTOM_SHAPE", "0").toInt() == 1;

        glare.shapeLuminance = doc.getValue("GLARE", "SHAPE_LUMINANCE", "1").toFloat();
        glare.shapeBloomLuminance = doc.getValue("GLARE", "SHAPE_BLOOM_LUMINANCE", "1").toFloat();
        glare.shapeBloomDispersion = doc.getValue("GLARE", "SHAPE_BLOOM_DISPERSION", "0.5").toFloat();
        glare.shapeBloomDispersionBaseLevel = doc.getValue("GLARE", "SHAPE_BLOOM_DISPERSION_BASE_LEVEL", "1").toInt();
        glare.shapeGhostLuminance = doc.getValue("GLARE", "SHAPE_GHOST_LUMINANCE", "0.7").toFloat();
        glare.shapeGhostHaloLuminance = doc.getValue("GLARE", "SHAPE_GHOST_HALO_LUMINANCE", "1").toFloat();
        glare.shapeGhostDistortion = doc.getValue("GLARE", "SHAPE_GHOST_DISTORTION", "1").toFloat();
        glare.shapeGhostSharpeness = doc.getValue("GLARE", "SHAPE_GHOST_SHARPENESS", "0").toInt() == 1;
        glare.shapeStarLuminance = doc.getValue("GLARE", "SHAPE_STAR_LUMINANCE", "0.5").toFloat();
        glare.shapeStarStreaksNumber = doc.getValue("GLARE", "SHAPE_STAR_STREAKS_NUMBER", "8").toInt();
        glare.shapeStarLength = doc.getValue("GLARE", "SHAPE_STAR_LENGTH", "1").toFloat();
        glare.shapeStarSecondaryLength = doc.getValue("GLARE", "SHAPE_STAR_SECONDARY_LENGTH", "1").toFloat();
        glare.shapeStarRotation = doc.getValue("GLARE", "SHAPE_STAR_ROTATION", "1").toInt() == 1;
        glare.shapeStarInclinationAngle = doc.getValue("GLARE", "SHAPE_STAR_INCLINATION_ANGLE", "1").toFloat();
        glare.shapeStarDispersion = doc.getValue("GLARE", "SHAPE_STAR_DISPERSION", "0").toFloat();
        glare.shapeStarForceDispersion = doc.getValue("GLARE", "SHAPE_STAR_FORCE_DISPERSION", "0").toInt() == 1;
        glare.shapeAfterimageLuminance = doc.getValue("GLARE", "SHAPE_AFTERIMAGE_LUMINANCE", "0").toFloat();
        glare.shapeAfterimageLength = doc.getValue("GLARE", "SHAPE_AFTERIMAGE_LENGTH", "0.2").toFloat();

        return glare;
    }
};

struct KsPPGodRays : public KsPPEffectBase {
    bool useSunLight;
    float diffractionRing;
    float diffractionRingRadius;
    float diffractionRingAttenuation;
    int diffractionRingSpectrumOrder;
    float diffractionRingOuterColor[4];
    float color[4];
    float length;
    float glareRatio;
    float angleAttenuation;
    float noiseMask;
    float noiseFrequency;
    float depthMaskThreshold;

    KsPPGodRays() {
        id = PP_GodRays;
        name = "GodRays";
        useSunLight = true;
        diffractionRing = 0.25f;
        diffractionRingRadius = 5.0f;
        diffractionRingAttenuation = 0.5f;
        diffractionRingSpectrumOrder = 2;
        diffractionRingOuterColor[0] = diffractionRingOuterColor[1] = diffractionRingOuterColor[2] = diffractionRingOuterColor[3] = 0.5f;
        color[0] = color[1] = color[2] = color[3] = 1.0f;
        length = 2.5f;
        glareRatio = 0.05f;
        angleAttenuation = 2.0f;
        noiseMask = 0.05f;
        noiseFrequency = 0.01f;
        depthMaskThreshold = 0.999f;
    }

    static KsPPGodRays fromINI(const KsIniDocument& doc) {
        KsPPGodRays gr;
        gr.enabled = doc.getValue("GODRAYS", "USE_SUN_LIGHT", 1).toInt() == 1;
        gr.useSunLight = doc.getValue("GODRAYS", "USE_SUN_LIGHT", 1).toInt() == 1;
        gr.diffractionRing = doc.getValue("GODRAYS", "DIFFRACTION_RING", "0.25").toFloat();
        gr.diffractionRingRadius = doc.getValue("GODRAYS", "DIFFRACTION_RING_RADIUS", "5").toFloat();
        gr.diffractionRingAttenuation = doc.getValue("GODRAYS", "DIFFRACTION_RING_ATTENUATION", "0.5").toFloat();
        gr.diffractionRingSpectrumOrder = doc.getValue("GODRAYS", "DIFFRACTION_RING_SPECTRUM_ORDER", "2").toInt();
        QStringList outer = doc.getValue("GODRAYS", "DIFFRACTION_RING_OUTER_COLOR", "0.5,0.5,0.5,0.5").toString().split(",");
        if (outer.size() >= 4) {
            for (int i = 0; i < 4; i++) gr.diffractionRingOuterColor[i] = outer[i].toFloat();
        }
        QStringList col = doc.getValue("GODRAYS", "COLOR", "1,1,1,1").toString().split(",");
        if (col.size() >= 4) {
            for (int i = 0; i < 4; i++) gr.color[i] = col[i].toFloat();
        }
        gr.length = doc.getValue("GODRAYS", "LENGTH", "2.5").toFloat();
        gr.glareRatio = doc.getValue("GODRAYS", "GLARE_RATIO", "0.05").toFloat();
        gr.angleAttenuation = doc.getValue("GODRAYS", "ANGLE_ATTENUATION", "2").toFloat();
        gr.noiseMask = doc.getValue("GODRAYS", "NOISE_MASK", "0.05").toFloat();
        gr.noiseFrequency = doc.getValue("GODRAYS", "NOISE_FREQUENCY", "0.01").toFloat();
        gr.depthMaskThreshold = doc.getValue("GODRAYS", "DEPTH_MASK_THRESHOLD", "0.999").toFloat();
        return gr;
    }
};

struct KsPPColorCorrection : public KsPPEffectBase {
    float hue;
    float saturation;
    float brightness;
    float contrast;
    float sepia;
    float colorTemp;
    float whiteBalance;

    KsPPColorCorrection() {
        id = PP_ColorCorrection;
        name = "Color";
        hue = 10.0f;
        saturation = 0.7f;
        brightness = 1.0f;
        contrast = 1.1f;
        sepia = 0.1f;
        colorTemp = 6500.0f;
        whiteBalance = 6500.0f;
    }

    static KsPPColorCorrection fromINI(const KsIniDocument& doc) {
        KsPPColorCorrection cc;
        cc.enabled = doc.getValue("COLOR", "ENABLED", 1).toInt() == 1;
        cc.hue = doc.getValue("COLOR", "HUE", "10").toFloat();
        cc.saturation = doc.getValue("COLOR", "SATURATION", "0.7").toFloat();
        cc.brightness = doc.getValue("COLOR", "BRIGHTNESS", "1").toFloat();
        cc.contrast = doc.getValue("COLOR", "CONTRAST", "1.1").toFloat();
        cc.sepia = doc.getValue("COLOR", "SEPIA", "0.1").toFloat();
        cc.colorTemp = doc.getValue("COLOR", "COLOR_TEMP", "6500").toFloat();
        cc.whiteBalance = doc.getValue("COLOR", "WHITE_BALANCE", "6500").toFloat();
        return cc;
    }

    void toINI(KsIniDocument& doc) const {
        doc.setValue("COLOR", "ENABLED", enabled ? 1 : 0);
        doc.setValue("COLOR", "HUE", QString::number(hue));
        doc.setValue("COLOR", "SATURATION", QString::number(saturation));
        doc.setValue("COLOR", "BRIGHTNESS", QString::number(brightness));
        doc.setValue("COLOR", "CONTRAST", QString::number(contrast));
        doc.setValue("COLOR", "SEPIA", QString::number(sepia));
        doc.setValue("COLOR", "COLOR_TEMP", QString::number(colorTemp));
        doc.setValue("COLOR", "WHITE_BALANCE", QString::number(whiteBalance));
    }
};

struct KsDOFSettings {
    int frontLevels;
    int backLevels;
    float fNumber;

    KsDOFSettings() : frontLevels(-1), backLevels(-1), fNumber(8.0f) {}
};

class KsPostProcessManager {
public:
    KsPPAutoExposure autoExposure;
    KsPPDepthOfField depthOfField;
    KsPPTonemapping tonemapping;
    KsPPChromaticAberration chromaticAberration;
    KsVignetting vignetting;
    KsPPGlare glare;
    KsPPGodRays godRays;
    KsPPColorCorrection colorCorrection;

    bool loadFromPath(const QString& path) {
        KsIniDocument doc;
        if (!doc.load(path)) return false;

        autoExposure = KsPPAutoExposure::fromINI(doc);
        depthOfField = KsPPDepthOfField::fromINI(doc);
        tonemapping = KsPPTonemapping::fromINI(doc);
        chromaticAberration = KsPPChromaticAberration::fromINI(doc);
        vignetting = KsVignetting::fromINI(doc);
        glare = KsPPGlare::fromINI(doc);
        godRays = KsPPGodRays::fromINI(doc);
        colorCorrection = KsPPColorCorrection::fromINI(doc);

        return true;
    }

    bool loadFromTrack(const QString& trackId) {
        QString path = QString(KS_SDK_PATH) + "/content/showroom/" + trackId + "/ppeffects.ini";
        return loadFromPath(path);
    }

    void saveToPath(const QString& path) const {
        KsIniDocument doc;
        autoExposure.toINI(doc);
        depthOfField.toINI(doc);
        tonemapping.toINI(doc);
        doc.setValue("CHROMATIC_ABERRATION", "ENABLED", chromaticAberration.enabled ? 1 : 0);
        doc.setValue("VIGNETTING", "ENABLED", vignetting.enabled ? 1 : 0);
        doc.setValue("VIGNETTING", "STRENGTH", QString::number(vignetting.strength));
        doc.setValue("VIGNETTING", "FOV_DEPENDENCE", QString::number(vignetting.fovDependence));
        colorCorrection.toINI(doc);
        doc.save(path);
    }

    QStringList getEnabledEffects() const {
        QStringList effects;
        if (autoExposure.enabled) effects << "AutoExposure";
        if (depthOfField.enabled) effects << "DepthOfField";
        if (tonemapping.enabled) effects << "Tonemapping";
        if (chromaticAberration.enabled) effects << "ChromaticAberration";
        if (vignetting.enabled) effects << "Vignetting";
        if (glare.enabled) effects << "Glare";
        if (godRays.enabled) effects << "GodRays";
        if (colorCorrection.enabled) effects << "Color";
        return effects;
    }

    void reset() {
        autoExposure = KsPPAutoExposure();
        depthOfField = KsPPDepthOfField();
        tonemapping = KsPPTonemapping();
        chromaticAberration = KsPPChromaticAberration();
        vignetting = KsVignetting();
        glare = KsPPGlare();
        godRays = KsPPGodRays();
        colorCorrection = KsPPColorCorrection();
    }
};
}

}

#endif
#ifndef KS_KS_RACE_H
#define KS_KS_RACE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QDateTime>
#include <QTime>
#include <algorithm>



#include "ks_track.h"
#include "ks_weather.h"
#include "ks_network.h"
#include "ks_util.h"

namespace ks {

enum KsRaceMode {
    Race_TimeAttack = 0,
    Race_Practice = 1,
    Race_Qualifying = 2,
    Race_Race = 3,
    Race_Hotlap = 4,
    Race_Drift = 5,
    Race_Drag = 6
};

enum KsEventType {
    Event_Sprint = 0,
    Event_Endurance = 1,
    Event_Endurance24 = 2,
    Event_Championship = 3,
    Event_Tourist = 4,
    Event_Events = 5
};

enum KsResultStatus {
    Result_DNF = 0,
    Result_DQ = 1,
    Result_Finished = 2,
    Result_Pole = 3,
    Result_Winner = 4
};

enum KsPitStopAction {
    Pitstop_Stop = 0,
    Pitstop_Fuel = 1,
    Pitstop_Tires = 2,
    Pitstop_Repair = 3
};

enum KsLapInvalidationReason {
    Lap_Valid = 0,
    Lap_CutTrack = 1,
    Lap_ShortCut = 2,
    Lap_SectorCut = 3,
    Lap_WrongWay = 4,
    Lap_PitEntry = 5,
    Lap_Shortcut = 6
};

struct KsLapTime {
    int lapNumber;
    float time;
    float sector1;
    float sector2;
    float sector3;
    int invalidation;
    bool isValid;
    bool isPersonalBest;
    bool isSessionBest;
    bool isPole;

    KsLapTime() : lapNumber(0), time(0), sector1(0), sector2(0), sector3(0)
                , invalidation(0), isValid(true), isPersonalBest(false), isSessionBest(false), isPole(false) {}
};

struct KsLapData {
    int carNumber;
    QString driverName;
    QString teamName;
    QList<KsLapTime> laps;

    int bestLapIndex;
    int lastLapIndex;

    float bestTime;
    float lastTime;

    int totalLaps;
    int completedLaps;

    KsLapData() : bestLapIndex(-1), lastLapIndex(-1), bestTime(1e9f), lastTime(0), totalLaps(0), completedLaps(0) {}

    float getBestLap() const { return bestTime; }
    float getLastLap() const { return lastTime; }

    int getPosition() const { return -1; }
};

struct KsRaceSession {
    int sessionIndex;
    int type;
    int duration;
    int laps;
    QDateTime startTime;

    bool hasStarted;
    bool hasFinished;
    bool isPaused;

    float ambientTemp;
    float trackTemp;
    float humidity;
    float windSpeed;
    float windDirection;

    KsRaceSession()
        : sessionIndex(0), type(0), duration(0), laps(0), hasStarted(false), hasFinished(false), isPaused(false)
        , ambientTemp(25.0f), trackTemp(30.0f), humidity(50.0f), windSpeed(0), windDirection(0) {}
};

struct KsParticipant {
    int carId;
    int raceNumber;
    int teamId;
    int modelId;
    int skinId;
    int driverId;
    int nationality;

    QString carModel;
    QString carSkin;
    QString driverName;
    QString teamName;
    QString shortName;

    int cupCategory;
    int startPosition;
    int finishPosition;

    float bestTime;
    float lastTime;
    int totalLaps;

    bool isPlayer;
    bool isAI;
    bool isConnected;
    bool hasFinished;
    bool isDisqualified;

    KsParticipant()
        : carId(0), raceNumber(0), teamId(0), modelId(0), skinId(0), driverId(0), nationality(0)
        , cupCategory(0), startPosition(0), finishPosition(0)
        , bestTime(1e9f), lastTime(0), totalLaps(0)
        , isPlayer(false), isAI(false), isConnected(true), hasFinished(false), isDisqualified(false) {}
};

struct KsPenalty {
    int carId;
    int lapNumber;
    int reason;
    int penaltyTime;
    bool isDriveThrough;
    bool isServed;

    KsPenalty() : carId(0), lapNumber(0), reason(0), penaltyTime(0), isDriveThrough(false), isServed(false) {}
};

struct KsEventConfig {
    QString name;
    int trackId;
    int trackConfiguration;
    int weatherId;

    int raceMode;
    int eventType;

    int laps;
    int raceDuration;
    int practiceDuration;
    int qualifyingDuration;

    int carSlots;
    int maxClients;

    bool isFixedSetup;
    bool isFuelRate;
    bool isTireRate;
    bool isDamage;
    bool isABSAllowed;
    bool isTCAllowed;

    bool hasFormationLap;
    bool hasRollingStart;
    bool hasMandatoryPitStop;
    bool hasSafetyCar;

    bool allowableCutTrack;
    int invalidLapDelay;

    bool hasUDP;
    bool hasTCP;

    QString serverPassword;
    QString adminPassword;

    KsEventConfig()
        : trackId(0), trackConfiguration(0), weatherId(0), raceMode(0), eventType(0)
        , laps(0), raceDuration(0), practiceDuration(0), qualifyingDuration(0)
        , carSlots(30), maxClients(30)
        , isFixedSetup(true), isFuelRate(true), isTireRate(true), isDamage(true), isABSAllowed(true), isTCAllowed(true)
        , hasFormationLap(true), hasRollingStart(false), hasMandatoryPitStop(false), hasSafetyCar(false)
        , allowableCutTrack(true), invalidLapDelay(0), hasUDP(true), hasTCP(false) {}
};

class KsRaceSessionManager {
public:
    QList<KsRaceSession> sessions;
    QList<KsParticipant> participants;
    QList<KsPenalty> penalties;

    int currentSessionIndex;
    int totalLaps;
    int maxClients;

    QString trackName;
    QString weatherName;

    bool isActive;
    bool hasStarted;

    KsRaceSessionManager()
        : currentSessionIndex(0), totalLaps(0), maxClients(30)
        , isActive(false), hasStarted(false) {}

    void initialize(const KsEventConfig& config) {
        Q_UNUSED(config);
    }

    void startSession(int index) {
        if (index >= 0 && index < sessions.size()) {
            currentSessionIndex = index;
            sessions[index].hasStarted = true;
            hasStarted = true;
        }
    }

    void endSession(int index) {
        if (index >= 0 && index < sessions.size()) {
            sessions[index].hasFinished = true;
        }
    }

    void addParticipant(const KsParticipant& p) {
        participants.append(p);
    }

    void removeParticipant(int carId) {
        for (int i = 0; i < participants.size(); i++) {
            if (participants[i].carId == carId) {
                participants.removeAt(i);
                return;
            }
        }
    }

    int getParticipantCount() const {
        return participants.size();
    }

    KsParticipant* getParticipant(int carId) {
        for (auto& p : participants) {
            if (p.carId == carId) return &p;
        }
        return nullptr;
    }

    KsParticipant* getLeader() {
        if (participants.isEmpty()) return nullptr;

        KsParticipant* leader = nullptr;
        float bestTime = 1e9f;

        for (auto& p : participants) {
            if (p.isConnected && p.bestTime < bestTime) {
                bestTime = p.bestTime;
                leader = &p;
            }
        }

        return leader;
    }

    QList<KsParticipant> getStanding() {
        QList<KsParticipant> standing = participants;
        std::sort(standing.begin(), standing.end(), [](const KsParticipant& a, const KsParticipant& b) {
            if (a.hasFinished != b.hasFinished) return a.hasFinished;
            if (a.bestTime != b.bestTime) return a.bestTime < b.bestTime;
            return a.totalLaps > b.totalLaps;
        });

        for (int i = 0; i < standing.size(); i++) {
            standing[i].finishPosition = i + 1;
        }

        return standing;
    }

    void addPenalty(const KsPenalty& penalty) {
        penalties.append(penalty);
    }

    void clearPenalties() {
        penalties.clear();
    }
};

class KsRaceResult {
public:
    QString eventName;
    QString trackName;
    QString weatherName;

    KsRaceMode raceMode;
    KsEventType eventType;

    QDateTime date;
    QTime duration;

    QList<KsParticipant> results;

    int winnerCarId;
    int poleCarId;

    float fastestLap;
    int fastestLapCar;

    KsRaceResult()
        : raceMode(Race_Race), eventType(Event_Sprint)
        , winnerCarId(-1), poleCarId(-1), fastestLap(1e9f), fastestLapCar(-1) {}

    void addResult(const KsParticipant& p) {
        results.append(p);
    }

    void sortResults() {
        std::sort(results.begin(), results.end(), [](const KsParticipant& a, const KsParticipant& b) {
            if (a.hasFinished != b.hasFinished) return a.hasFinished;
            if (a.totalLaps != b.totalLaps) return a.totalLaps > b.totalLaps;
            if (a.finishPosition != b.finishPosition) return a.finishPosition < b.finishPosition;
            return a.bestTime < b.bestTime;
        });
    }

    void updatePositions() {
        sortResults();
        for (int i = 0; i < results.size(); i++) {
            results[i].finishPosition = i + 1;
        }
    }

    QString toHTML() const {
        QString html = "<html><body>";
        html += QString("<h1>%1</h1>").arg(eventName);
        html += QString("<h2>%1 - %2</h2>").arg(trackName).arg(date.toString());
        html += "<table border='1'>";
        html += "<tr><th>Pos</th><th>#</th><th>Driver</th><th>Team</th><th>Gap</th><th>Laps</th><th>Best</th></tr>";

        for (const auto& p : results) {
            QString posClass = p.finishPosition == 1 ? "winner" : "";
            html += QString("<tr class='%1'><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td></td><td>%6</td><td>%7</td></tr>")
                .arg(posClass)
                .arg(p.finishPosition)
                .arg(p.raceNumber)
                .arg(p.driverName)
                .arg(p.teamName)
                .arg(p.totalLaps)
                .arg(p.bestTime > 0 ? QString::number(p.bestTime, 'f', 3) : "-");
        }

        html += "</table></body></html>";
        return html;
    }

    QString toCSV() const {
        QString csv = "Pos,CarNum,Driver,Team,Laps,Best Time\n";
        for (const auto& p : results) {
            csv += QString("%1,%2,%3,%4,%5,%6\n")
                .arg(p.finishPosition)
                .arg(p.raceNumber)
                .arg(p.driverName)
                .arg(p.teamName)
                .arg(p.totalLaps)
                .arg(p.bestTime > 0 ? QString::number(p.bestTime, 'f', 3) : "-");
        }
        return csv;
    }
};

class KsLeaderboard {
public:
    enum SortBy {
        SortBy_Position = 0,
        SortBy_Time = 1,
        SortBy_Laps = 2,
        SortBy_Gap = 3
    };

    QList<KsParticipant> entries;
    SortBy sortMode;

    bool showClass;

    int topCount;

    KsLeaderboard() : sortMode(SortBy_Position), showClass(true), topCount(3) {}

    void addEntry(const KsParticipant& e) {
        entries.append(e);
    }

    void removeEntry(int carId) {
        for (int i = 0; i < entries.size(); i++) {
            if (entries[i].carId == carId) {
                entries.removeAt(i);
                return;
            }
        }
    }

    void sort() {
        switch (sortMode) {
            case SortBy_Position:
                std::sort(entries.begin(), entries.end(), [](const KsParticipant& a, const KsParticipant& b) {
                    return a.finishPosition < b.finishPosition;
                });
                break;
            case SortBy_Time:
                std::sort(entries.begin(), entries.end(), [](const KsParticipant& a, const KsParticipant& b) {
                    if (!a.hasFinished && !b.hasFinished) return false;
                    if (!a.hasFinished) return false;
                    if (!b.hasFinished) return true;
                    return a.bestTime < b.bestTime;
                });
                break;
            case SortBy_Laps:
                std::sort(entries.begin(), entries.end(), [](const KsParticipant& a, const KsParticipant& b) {
                    return a.totalLaps > b.totalLaps;
                });
                break;
            default:
                break;
        }
    }

    void updatePositions() {
        sort();
        for (int i = 0; i < entries.size(); i++) {
            entries[i].finishPosition = i + 1;
        }
    }

    int getPosition(int carId) const {
        for (int i = 0; i < entries.size(); i++) {
            if (entries[i].carId == carId) {
                return i + 1;
            }
        }
        return -1;
    }

    float getGap(int carId, float leaderTime) const {
        for (int i = 0; i < entries.size(); i++) {
            if (entries[i].carId == carId) {
                return entries[i].bestTime - leaderTime;
            }
        }
        return 0;
    }

    float getInterval(int carId, int carAhead) const {
        for (int i = 0; i < entries.size(); i++) {
            if (entries[i].carId == carId && i > 0) {
                return entries[i].bestTime - entries[i-1].bestTime;
            }
        }
        return 0;
    }

    KsParticipant* getLeader() {
        if (entries.isEmpty()) return nullptr;
        sort();
        return &entries[0];
    }

    QList<KsParticipant> getTop(int count) {
        updatePositions();
        QList<KsParticipant> top;
        for (int i = 0; i < qMin(count, entries.size()); i++) {
            top.append(entries[i]);
        }
        return top;
    }

    void clear() {
        entries.clear();
    }

    QString toJSON() const {
        QString json = "[\n";
        for (int i = 0; i < entries.size(); i++) {
            const auto& e = entries[i];
            json += QString("  {\"position\": %1, \"carId\": %2, \"driver\": \"%3\", \"team\": \"%4\", \"laps\": %5, \"bestTime\": %6}")
                .arg(i + 1).arg(e.carId).arg(e.driverName).arg(e.teamName).arg(e.totalLaps)
                .arg(e.bestTime > 0 ? QString::number(e.bestTime, 'f', 3) : "null");
            if (i < entries.size() - 1) json += ",";
            json += "\n";
        }
        json += "]\n";
        return json;
    }
};

class KsEventManager {
public:
    QList<KsEventConfig> events;
    KsEventConfig currentEvent;

    KsRaceSessionManager sessionManager;
    KsLeaderboard leaderboard;
    KsRaceResult lastResult;

    QString eventFolder;

    bool isHosting;
    bool isClient;

    KsEventManager()
        : isHosting(false), isClient(false) {}

    void loadEvent(const QString& name) {
        Q_UNUSED(name);
    }

    void saveEvent(const KsEventConfig& config) {
        Q_UNUSED(config);
    }

    int createEvent(const KsEventConfig& config) {
        events.append(config);
        return events.size() - 1;
    }

    QList<KsEventConfig> getEvents() const {
        return events;
    }

    void startEvent() {
        sessionManager.initialize(currentEvent);
        isHosting = true;
    }

    void endEvent() {
        isHosting = false;
        sessionManager.hasStarted = false;
    }

    void joinEvent(const QString& host, int port) {
        Q_UNUSED(host); Q_UNUSED(port);
        isClient = true;
    }

    void leaveEvent() {
        isClient = false;
    }

    void updateStanding() {
        leaderboard.clear();
        for (const auto& p : sessionManager.participants) {
            leaderboard.addEntry(p);
        }
        leaderboard.updatePositions();
    }

    void generateResult() {
        lastResult.updatePositions();

        KsParticipant* leader = leaderboard.getLeader();
        if (leader) {
            lastResult.winnerCarId = leader->carId;
        }
    }

    QString exportResults(const QString& format = "json") const {
        if (format == "html") return lastResult.toHTML();
        if (format == "csv") return lastResult.toCSV();
        if (format == "json") return leaderboard.toJSON();
        return QString();
    }
};

class KsClientConnection {
public:
    int socket;
    QString ip;
    int port;

    int carId;
    int raceNumber;

    QString playerName;
    QString teamName;

    bool isConnected;
    bool isVerified;
    bool isAdmin;

    quint64 lastPing;
    quint64 lastUpdate;

    QByteArray receiveBuffer;
    QByteArray sendBuffer;

    KsClientConnection()
        : socket(-1), port(0), carId(0), raceNumber(0)
        , isConnected(false), isVerified(false), isAdmin(false)
        , lastPing(0), lastUpdate(0) {}

    void ping() {
        lastPing = QDateTime::currentMSecsSinceEpoch();
    }

    bool isTimedOut(int timeoutMs = 30000) const {
        return QDateTime::currentMSecsSinceEpoch() - lastUpdate > timeoutMs;
    }

    qint64 getLatency() const {
        if (lastPing == 0) return -1;
        return QDateTime::currentMSecsSinceEpoch() - lastPing;
    }
};

class KsRaceServer {
public:
    enum ServerState {
        State_Idle = 0,
        State_Booking = 1,
        State_Practice = 2,
        State_Qualifying = 3,
        State_Race = 4,
        State_Result = 5
    };

    int tcpPort;
    int udpPort;

    QString serverName;
    QString adminPassword;
    QString serverPassword;

    QMap<int, KsClientConnection> clients;

    KsEventManager eventManager;
    KsRaceSessionManager sessionManager;
    KsLeaderboard leaderboard;

    ServerState state;
    int currentLap;

    bool isPasswordProtected;
    bool isLANOnly;

    int maxClients;
    int currentClients;

    int connectionTimeout;

    KsRaceServer()
        : tcpPort(8877), udpPort(8877)
        , state(State_Idle), currentLap(0)
        , isPasswordProtected(false), isLANOnly(false)
        , maxClients(30), currentClients(0)
        , connectionTimeout(30000) {}

    bool start() {
        return false;
    }

    void stop() {
    }

    bool broadcast(const QByteArray& data) {
        Q_UNUSED(data);
        return false;
    }

    void addClient(const KsClientConnection& client) {
        clients[client.carId] = client;
        currentClients++;
    }

    void removeClient(int carId) {
        clients.remove(carId);
        currentClients--;
    }

    KsClientConnection* getClient(int carId) {
        if (clients.contains(carId)) {
            return &clients[carId];
        }
        return nullptr;
    }

    bool isClientConnected(int carId) const {
        return clients.contains(carId);
    }

    int getClientCount() const {
        return clients.size();
    }

    void pruneInactive() {
        QList<int> toRemove;
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            if (it.value().isTimedOut(connectionTimeout)) {
                toRemove.append(it.key());
            }
        }
        for (int carId : toRemove) {
            clients.remove(carId);
            currentClients--;
        }
    }

    void sendTo(int carId, const QByteArray& data) {
        Q_UNUSED(carId); Q_UNUSED(data);
    }

    void sendToAll(const QByteArray& data) {
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            sendTo(it.key(), data);
        }
    }
};
}

}

#endif
#ifndef KS_KS_RENDERER_H
#define KS_KS_RENDERER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QColor>




namespace ks {

enum KsShaderType {
    Shader_Default = 0,
    Shader_Skinned = 1,
    Shader_Partical = 2,
    Shader_Terrain = 3,
    Shader_Reflective = 4,
    Shader_Transparent = 5,
    Shader_PostProcess = 6
};

enum KsRenderMode {
    Render_Wireframe = 0,
    Render_Solid = 1,
    Render_Textured = 2,
    Render_Lit = 3,
    Render_Shaded = 4
};

struct KsRenderState {
    bool depthTest;
    bool depthWrite;
    bool cullFace;
    bool blend;
    bool wireframe;

    int polyMode;
    int shadeMode;

    float clearColor[4];
    float ambientColor[4];
    float diffuseColor[4];
    float specularColor[4];
    float emissiveColor[4];

    float shininess;
    float opacity;
    float lineWidth;

    KsRenderState()
        : depthTest(true), depthWrite(true), cullFace(true), blend(false), wireframe(false)
        , polyMode(2), shadeMode(1), shininess(32.0f), opacity(1.0f), lineWidth(1.0f)
    {
        clearColor[0] = clearColor[1] = clearColor[2] = 0; clearColor[3] = 1;
        ambientColor[0] = ambientColor[1] = ambientColor[2] = 0.2f; ambientColor[3] = 1;
        diffuseColor[0] = diffuseColor[1] = diffuseColor[2] = 0.8f; diffuseColor[3] = 1;
        specularColor[0] = specularColor[1] = specularColor[2] = 0.5f; specularColor[3] = 1;
        emissiveColor[0] = emissiveColor[1] = emissiveColor[2] = 0; emissiveColor[3] = 1;
    }
};

struct KsLight {
    int id;
    QString name;

    float position[3];
    float direction[3];

    float color[4];
    float intensity;
    float ambient[4];
    float diffuse[4];
    float specular[4];

    float constant;
    float linear;
    float quadratic;

    float cutOff;
    float outerCutOff;

    bool enabled;
    int type;

    KsLight(int lightId = 0)
        : id(lightId), intensity(1.0f), constant(1.0f), linear(0.09f), quadratic(0.032f)
        , cutOff(12.0f), outerCutOff(15.0f), enabled(true), type(0)
    {
        name = QString("Light%1").arg(lightId);
        position[0] = position[1] = position[2] = 0;
        direction[0] = 0; direction[1] = -1; direction[2] = 0;

        color[0] = color[1] = color[2] = 1; color[3] = 1;
        ambient[0] = ambient[1] = ambient[2] = 0.1f; ambient[3] = 1;
        diffuse[0] = diffuse[1] = diffuse[2] = 1; diffuse[3] = 1;
        specular[0] = specular[1] = specular[2] = 1; specular[3] = 1;
    }
};

struct KsViewport {
    int x, y;
    int width, height;

    float minZ, maxZ;

    KsViewport() : x(0), y(0), width(1920), height(1080), minZ(0.1f), maxZ(1000.0f) {}
};

struct KsCamera {
    float position[3];
    float target[3];
    float up[3];

    float fov;
    float aspect;
    float nearPlane;
    float farPlane;

    int projection;

    float matrix[16];

    KsCamera()
        : fov(60.0f), aspect(16.0f/9.0f), nearPlane(0.1f), farPlane(1000.0f), projection(0)
    {
        position[0] = position[1] = 0; position[2] = 5;
        target[0] = target[1] = target[2] = 0;
        up[0] = 0; up[1] = 1; up[2] = 0;

        for (int i = 0; i < 16; i++) matrix[i] = (i % 5 == 0) ? 1 : 0;
    }

    void setPosition(float x, float y, float z) {
        position[0] = x; position[1] = y; position[2] = z;
    }

    void lookAt(float x, float y, float z) {
        target[0] = x; target[1] = y; target[2] = z;
    }

    void move(float dx, float dy, float dz) {
        position[0] += dx; position[1] += dy; position[2] += dz;
        target[0] += dx; target[1] += dy; target[2] += dz;
    }

    void orbit(float angle, float pitch) {
        float radius = sqrt(
            pow(target[0] - position[0], 2) +
            pow(target[1] - position[1], 2) +
            pow(target[2] - position[2], 2)
        );

        float theta = atan2(target[0] - position[0], target[2] - position[2]);
        float phi = acos((target[1] - position[1]) / radius);

        theta += angle;
        phi += pitch;

        position[0] = target[0] + radius * sin(phi) * sin(theta);
        position[1] = target[1] + radius * cos(phi);
        position[2] = target[2] + radius * sin(phi) * cos(theta);
    }

    void zoom(float factor) {
        float dx = target[0] - position[0];
        float dy = target[1] - position[1];
        float dz = target[2] - position[2];

        position[0] = target[0] - dx * factor;
        position[1] = target[1] - dy * factor;
        position[2] = target[2] - dz * factor;
    }

    float getDistance() const {
        return sqrt(
            pow(target[0] - position[0], 2) +
            pow(target[1] - position[1], 2) +
            pow(target[2] - position[2], 2)
        );
    }
};

class KsRenderer {
public:
    KsRenderState state;
    KsViewport viewport;
    KsCamera camera;
    QList<KsLight> lights;

    int maxLights;
    int currentLight;

    QString errorLog;

    KsRenderer() : maxLights(8), currentLight(-1) {
        lights.append(KsLight(0));
        lights[0].position[1] = 5;
    }

    void initialize() {
        state.depthTest = true;
        state.depthWrite = true;
        state.cullFace = true;
        state.blend = false;
    }

    void cleanup() {
        for (auto& light : lights) {
            light.enabled = false;
        }
    }

    int addLight(const KsLight& light) {
        if (lights.size() >= maxLights) return -1;

        KsLight newLight = light;
        newLight.id = lights.size();
        lights.append(newLight);
        return newLight.id;
    }

    void removeLight(int id) {
        if (id >= 0 && id < lights.size()) {
            lights[id].enabled = false;
        }
    }

    void setLightPosition(int id, float x, float y, float z) {
        if (id >= 0 && id < lights.size()) {
            lights[id].position[0] = x;
            lights[id].position[1] = y;
            lights[id].position[2] = z;
        }
    }

    void setLightColor(int id, float r, float g, float b) {
        if (id >= 0 && id < lights.size()) {
            lights[id].color[0] = r;
            lights[id].color[1] = g;
            lights[id].color[2] = b;
        }
    }

    void setAmbient(float r, float g, float b) {
        state.ambientColor[0] = r;
        state.ambientColor[1] = g;
        state.ambientColor[2] = b;
    }

    void setClearColor(float r, float g, float b, float a = 1.0f) {
        state.clearColor[0] = r;
        state.clearColor[1] = g;
        state.clearColor[2] = b;
        state.clearColor[3] = a;
    }

    void setBackground(const QColor& color) {
        setClearColor(
            color.redF(),
            color.greenF(),
            color.blueF(),
            color.alphaF()
        );
    }

    void clear(int flags) {
        Q_UNUSED(flags);
    }

    void begin(KsRenderMode mode) {
        Q_UNUSED(mode);
    }

    void end() {
    }

    void drawPoint(float x, float y, float z, const QColor& color = Qt::white) {
        Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z); Q_UNUSED(color);
    }

    void drawLine(float x1, float y1, float z1, float x2, float y2, float z2, const QColor& color = Qt::white) {
        Q_UNUSED(x1); Q_UNUSED(y1); Q_UNUSED(z1); Q_UNUSED(x2); Q_UNUSED(y2); Q_UNUSED(z2); Q_UNUSED(color);
    }

    void drawTriangle(const float v0[3], const float v1[3], const float v2[3], const QColor& color = Qt::white) {
        Q_UNUSED(v0); Q_UNUSED(v1); Q_UNUSED(v2); Q_UNUSED(color);
    }

    void drawQuad(const float v0[3], const float v1[3], const float v2[3], const float v3[3], const QColor& color = Qt::white) {
        Q_UNUSED(v0); Q_UNUSED(v1); Q_UNUSED(v2); Q_UNUSED(v3); Q_UNUSED(color);
    }

    void drawBox(float width, float height, float depth, const QColor& color = Qt::white) {
        Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(depth); Q_UNUSED(color);
    }

    void drawSphere(float radius, const QColor& color = Qt::white) {
        Q_UNUSED(radius); Q_UNUSED(color);
    }

    void drawGrid(int size, int divisions, const QColor& color = Qt::white) {
        Q_UNUSED(size); Q_UNUSED(divisions); Q_UNUSED(color);
    }

    void drawAxes(float size = 1.0f) {
        Q_UNUSED(size);
    }

    void drawMesh(KsMeshData* mesh) {
        Q_UNUSED(mesh);
    }

    void drawText(const QString& text, float x, float y, const QColor& color = Qt::white) {
        Q_UNUSED(text); Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(color);
    }

    void swapBuffers() {
    }

    QString getLastError() const {
        return errorLog;
    }
};

class KsPostProcess {
public:
    enum Effect {
        Effect_None = 0,
        Effect_Bloom = 1,
        Effect_DOF = 2,
        Effect_MotionBlur = 3,
        Effect_FXAA = 4,
        Effect_SSAO = 5,
        Effect_ColorCorrection = 6,
        Effect_Vignette = 7
    };

    QMap<Effect, bool> effects;

    float bloomThreshold;
    float bloomIntensity;
    float bloomRadius;

    float dofFocus;
    float dofAperture;
    float dofMaxBlur;

    float motionBlurStrength;

    float ssaoRadius;
    float ssaoIntensity;

    float saturation;
    float contrast;
    float brightness;
    float gamma;

    float vignetteIntensity;
    float vignetteRadius;
    float vignetteSoftness;

    KsPostProcess()
        : bloomThreshold(0.8f), bloomIntensity(1.0f), bloomRadius(5.0f)
        , dofFocus(5.0f), dofAperture(2.4f), dofMaxBlur(2.0f)
        , motionBlurStrength(0.5f)
        , ssaoRadius(0.5f), ssaoIntensity(1.0f)
        , saturation(1.0f), contrast(1.0f), brightness(0.0f), gamma(2.2f)
        , vignetteIntensity(0.0f), vignetteRadius(0.8f), vignetteSoftness(0.4f)
    {
        effects[Effect_Bloom] = false;
        effects[Effect_DOF] = false;
        effects[Effect_MotionBlur] = false;
        effects[Effect_FXAA] = false;
        effects[Effect_SSAO] = false;
        effects[Effect_ColorCorrection] = false;
        effects[Effect_Vignette] = false;
    }

    void enable(Effect effect) {
        effects[effect] = true;
    }

    void disable(Effect effect) {
        effects[effect] = false;
    }

    bool isEnabled(Effect effect) const {
        return effects.value(effect, false);
    }

    void toggle(Effect effect) {
        effects[effect] = !effects[effect];
    }

    void setBloom(float threshold, float intensity, float radius) {
        bloomThreshold = threshold;
        bloomIntensity = intensity;
        bloomRadius = radius;
        effects[Effect_Bloom] = true;
    }

    void setDOF(float focus, float aperture, float maxBlur) {
        dofFocus = focus;
        dofAperture = aperture;
        dofMaxBlur = maxBlur;
        effects[Effect_DOF] = true;
    }

    void setSSAO(float radius, float intensity) {
        ssaoRadius = radius;
        ssaoIntensity = intensity;
        effects[Effect_SSAO] = true;
    }

    void setColorCorrection(float sat, float cont, float bright, float g = 2.2f) {
        saturation = sat;
        contrast = cont;
        brightness = bright;
        gamma = g;
        effects[Effect_ColorCorrection] = true;
    }

    void setVignette(float intensity, float radius, float softness) {
        vignetteIntensity = intensity;
        vignetteRadius = radius;
        vignetteSoftness = softness;
        effects[Effect_Vignette] = true;
    }

    void reset() {
        for (auto it = effects.begin(); it != effects.end(); ++it) {
            it.value() = false;
        }
    }
};

classKsSkybox {
public:
    QString name;
    QString cubemap[6];

    float tint[4];
    float brightness;
    float horizon;
    float exposure;

    float sunPosition[3];
    float sunColor[4];
    float sunSize;
    float sunIntensity;

    float moonPosition[3];
    float moonColor[4];
    float moonSize;

    bool procedural;
    bool animatedClouds;

   KsSkybox()
        : brightness(1.0f), horizon(0.0f), exposure(1.0f)
        , sunSize(0.05f), sunIntensity(1.0f)
        , moonSize(0.02f)
        , procedural(true), animatedClouds(true)
    {
        name = "Default";

        tint[0] = tint[1] = tint[2] = 1; tint[3] = 1;

        sunPosition[0] = 0; sunPosition[1] = 0.5f; sunPosition[2] = 1;
        sunColor[0] = sunColor[1] = 1; sunColor[2] = 0.9f; sunColor[3] = 1;

        moonPosition[0] = 0; moonPosition[1] = 0.5f; moonPosition[2] = -1;
        moonColor[0] = moonColor[1] = moonColor[2] = 0.9f; moonColor[3] = 1;
    }

    void setSun(float azimuth, float elevation, float size, float intensity) {
        float radAz = azimuth * PI / 180.0f;
        float radEl = elevation * PI / 180.0f;

        sunPosition[0] = cos(radEl) * sin(radAz);
        sunPosition[1] = sin(radEl);
        sunPosition[2] = cos(radEl) * cos(radAz);

        sunSize = size;
        sunIntensity = intensity;
    }

    void setMoon(float azimuth, float elevation, float size) {
        float radAz = azimuth * PI / 180.0f;
        float radEl = elevation * PI / 180.0f;

        moonPosition[0] = cos(radEl) * sin(radAz);
        moonPosition[1] = sin(radEl);
        moonPosition[2] = cos(radEl) * cos(radAz);

        moonSize = size;
    }

    void setTime(float hours, float minutes) {
        Q_UNUSED(hours); Q_UNUSED(minutes);
    }

    float getSunAzimuth() const {
        return atan2(sunPosition[0], sunPosition[2]) * 180.0f / PI;
    }

    float getSunElevation() const {
        return asin(sunPosition[1]) * 180.0f / PI;
    }
};

namespace RenderUtils {

inline float* multiplyMatrix(float out[16], const float a[16], const float b[16]) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out[row * 4 + col] =
                a[row * 4 + 0] * b[0 * 4 + col] +
                a[row * 4 + 1] * b[1 * 4 + col] +
                a[row * 4 + 2] * b[2 * 4 + col] +
                a[row * 4 + 3] * b[3 * 4 + col];
        }
}

}

#endif
#ifndef KS_KS_REPLAY_H
#define KS_KS_REPLAY_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QPair>
#include <QDateTime>
#include <QFile>
#include <QDataStream>
#include <QIODevice>

#include "sdk/SDKBackend.h"


namespace ks {

struct KsReplayHeader {
    quint32 magic;
    quint32 version;
    quint32 headerSize;

    QString trackId;
    QString trackName;
    QString layout;
    float trackLength;

    quint32 carCount;
    QStringList carIds;

    quint32 lapCount;
    quint32 frameCount;
    float duration;

    QDateTime recordingDate;
    QString recordingSoftware;
    QString recorderName;

    KsReplayHeader() : magic(0x41435250), version(2), headerSize(0),
        carCount(0), lapCount(0), frameCount(0), duration(0) {}
};

struct KsReplayCarInfo {
    int index;
    quint32 color;
    QString carName;
    QString skin;
    QString driverName;
    quint32 guid;

    int finishPosition;
    float bestLap;
    int totalLaps;
    quint32 gaps[10];

    KsReplayCarInfo() : index(0), color(0), guid(0),
        finishPosition(0), bestLap(0), totalLaps(0) {}
};

struct KsReplayFrame {
    quint32 timestamp;
    quint16 flags;

    struct CarData {
        float position[3];
        float rotation[3];
        float speed;
        float heading;

        float throttle;
        float brake;
        float steering;
        int gear;
        float rpm;

        float slipAngle[4];
        float slipRatio[4];

        float wheels[4][3];
    } cars[24];

    KsReplayFrame() : timestamp(0), flags(0) {}
};

class KsReplayFile {
public:
    KsReplayHeader header;
    QList<KsReplayCarInfo> carInfo;
    QList<KsReplayFrame> frames;

    QFile* file;
    QDataStream* stream;

    bool isOpen() const { return file && file->isOpen(); }
    bool isWriting() const { return m_writing; }
    qint64 position() const { return file ? file->pos() : 0; }

    bool create(const QString& path) {
        file = new QFile(path);
        if (!file->open(QIODevice::WriteOnly)) {
            delete file;
            file = nullptr;
            return false;
        }

        stream = new QDataStream(file);
        m_writing = true;

        return writeHeader();
    }

    bool open(const QString& path) {
        file = new QFile(path);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            file = nullptr;
            return false;
        }

        stream = new QDataStream(file);
        m_writing = false;

        return readHeader();
    }

    void close() {
        if (stream) {
            if (m_writing) {
                writeFinalFrames();
            }
            delete stream;
            stream = nullptr;
        }
        if (file) {
            file->close();
            delete file;
            file = nullptr;
        }
    }

    bool writeFrame(const KsReplayFrame& frame) {
        if (!stream || !m_writing) return false;
        *stream << frame.timestamp;
        *stream << frame.flags;

        for (int c = 0; c < header.carCount; c++) {
            const auto& car = frame.cars[c];
            *stream << car.position[0] << car.position[1] << car.position[2];
            *stream << car.rotation[0] << car.rotation[1] << car.rotation[2];
            *stream << car.speed;
            *stream << car.heading;
            *stream << car.throttle << car.brake << car.steering;
            *stream << car.gear;
            *stream << car.rpm;
        }

        frames.append(frame);
        return true;
    }

    bool readFrame(KsReplayFrame& frame) {
        if (!stream || m_writing) return false;
        if (stream->atEnd()) return false;

        *stream >> frame.timestamp;
        *stream >> frame.flags;

        for (int c = 0; c < header.carCount; c++) {
            auto& car = frame.cars[c];
            *stream >> car.position[0] >> car.position[1] >> car.position[2];
            *stream >> car.rotation[0] >> car.rotation[1] >> car.rotation[2];
            *stream >> car.speed;
            *stream >> car.heading;
            *stream >> car.throttle >> car.brake >> car.steering;
            *stream >> car.gear;
            *stream >> car.rpm;
        }

        return true;
    }

    bool stepForward(int frames) {
        KsReplayFrame frame;
        for (int i = 0; i < frames; i++) {
            if (!readFrame(frame)) return false;
        }
        return true;
    }

    bool seekTo(quint32 timestamp) {
        if (!file) return false;
        qint64 startPos = header.headerSize;
        file->seek(startPos);

        KsReplayFrame frame;
        while (readFrame(frame)) {
            if (frame.timestamp >= timestamp) {
                return true;
            }
        }
        return false;
    }

    bool seekToPercent(float percent) {
        if (!file || frames.empty()) return false;
        int targetFrame = int(frames.size() * qBound(0.0f, percent, 1.0f));
        return seekToFrame(targetFrame);
    }

    bool seekToFrame(int frameIndex) {
        if (!file || frameIndex < 0) return false;
        qint64 startPos = header.headerSize;
        qint64 frameSize = estimateFrameSize();
        file->seek(startPos + frameIndex * frameSize);
        return true;
    }

    bool getFrameAt(int index, KsReplayFrame& frame) const {
        if (index < 0 || index >= frames.size()) return false;
        frame = frames[index];
        return true;
    }

    QList<int> getLapStarts() const {
        QList<int> lapStarts;
        if (frames.empty()) return lapStarts;

        int currentGear = frames.first().cars[0].gear;
        int lastGear = currentGear;

        for (int i = 1; i < frames.size(); i++) {
            currentGear = frames[i].cars[0].gear;
            if (currentGear < lastGear && lastGear == 1) {
                lapStarts.append(i);
            }
            lastGear = currentGear;
        }

        return lapStarts;
    }

    float getLapTime(int lapIndex) const {
        QList<int> lapStarts = getLapStarts();
        if (lapIndex < 0 || lapIndex >= lapStarts.size()) return 0;

        int start = lapStarts[lapIndex];
        int end = (lapIndex + 1 < lapStarts.size()) ? lapStarts[lapIndex + 1] : frames.size();

        if (start >= frames.size() || end > frames.size()) return 0;
        if (start >= end) return 0;

        return (frames[end - 1].timestamp - frames[start].timestamp) / 1000.0f;
    }

    QDateTime getRecordingDate() const { return header.recordingDate; }
    QString getTrackName() const { return header.trackName; }
    QStringList getCarIds() const { return header.carIds; }
    int getCarCount() const { return header.carCount; }
    int getFrameCount() const { return header.frameCount; }
    float getDuration() const { return header.duration; }

private:
    bool m_writing;

    bool writeHeader() {
        if (!stream) return false;

        *stream << header.magic;
        *stream << header.version;
        *stream << header.trackId;
        *stream << header.trackName;
        *stream << header.layout;
        *stream << header.trackLength;
        *stream << header.carCount;
        *stream << header.carIds;
        *stream << header.lapCount;
        *stream << header.duration;
        *stream << static_cast<qint64>(header.recordingDate.toSecsSinceEpoch());

        header.headerSize = file->pos();
        return true;
    }

    bool readHeader() {
        if (!stream) return false;

        quint32 magic;
        *stream >> magic;
        if (magic != header.magic) return false;

        *stream >> header.version;
        *stream >> header.trackId;
        *stream >> header.trackName;
        *stream >> header.layout;
        *stream >> header.trackLength;
        *stream >> header.carCount;
        *stream >> header.carIds;
        *stream >> header.lapCount;
        *stream >> header.duration;

        qint64 timestamp;
        *stream >> timestamp;
        header.recordingDate = QDateTime::fromSecsSinceEpoch(timestamp);

        return true;
    }

    void writeFinalFrames() {
        header.frameCount = frames.size();
        header.duration = frames.isEmpty() ? 0 :
            (frames.last().timestamp - frames.first().timestamp) / 1000.0f;
        header.lapCount = getLapStarts().size();
    }

    qint64 estimateFrameSize() const {
        qint64 base = sizeof(quint32) + sizeof(quint16);
        base += header.carCount * (sizeof(float) * 20 + sizeof(int) * 2);
        return base;
    }
};

class KsReplayManager {
public:
    KsReplayFile currentReplay;
    QString replayFolder;

    bool loadReplay(const QString& path) {
        return currentReplay.open(path);
    }

    bool startRecording(const QString& trackId, const QStringList& carIds) {
        replayFolder = SDKBackend::getFolderPath(KsFolderID::Replays);

        currentReplay.header.trackId = trackId;
        currentReplay.header.carIds = carIds;
        currentReplay.header.carCount = carIds.size();
        currentReplay.header.recordingDate = QDateTime::currentDateTime();
        currentReplay.header.recordingSoftware = "ksEditor";

        QString fileName = QString("%1_%2.acr")
            .arg(trackId)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

        QString path = replayFolder + "/" + fileName;
        return currentReplay.create(path);
    }

    bool stopRecording() {
        currentReplay.close();
        return true;
    }

    QStringList getReplayList() const {
        QStringList replays;
        QDir dir(replayFolder);

        if (!dir.exists()) return replays;

        return dir.entryList(QStringList() << "*.acr", QDir::Files);
    }

    bool deleteReplay(const QString& name) {
        QString path = replayFolder + "/" + name;
        return QFile::remove(path);
    }

    bool exportToVideo(const QString& replayPath, const QString& outputPath,
                      int width, int height, float fps) {
        if (!QFile::exists(replayPath)) return false;

        // Open replay file and read frames
        KsReplayFile replay;
        if (!replay.open(replayPath)) return false;

        // Collect all frames first for seeking
        QVector<KsReplayFrame> allFrames;
        KsReplayFrame frame;
        while (replay.readFrame(frame)) {
            allFrames.append(frame);
        }
        replay.close();

        if (allFrames.isEmpty()) return false;

        // Configure video encoder
        ks::VideoEncoderConfig cfg;
        cfg.width = width;
        cfg.height = height;
        cfg.fps = fps;
        cfg.codec = ks::VideoEncoderConfig::Codec::H264;
        cfg.quality = ks::VideoEncoderConfig::Quality::High;
        cfg.bitrate = width * height > (1920 * 1080) ? 15000000 : 8000000;

        ks::VideoEncoder encoder;
        encoder.setConfig(cfg);

        if (!encoder.open(outputPath)) {
            return false;
        }

        // Render each frame as a simple telemetry overlay
        // (In production, this would render from the 3D viewport)
        int totalFrames = allFrames.size();
        for (int i = 0; i < totalFrames; i++) {
            QImage frameImg(width, height, QImage::Format_RGB888);
            frameImg.fill(Qt::black);

            QPainter painter(&frameImg);
            painter.setRenderHint(QPainter::Antialiasing);

            // Draw telemetry data as visual overlay
            const KsReplayFrame& f = allFrames[i];

            // Background gradient
            QLinearGradient grad(0, 0, 0, height);
            grad.setColorAt(0, QColor(20, 20, 40));
            grad.setColorAt(1, QColor(10, 10, 20));
            painter.fillRect(0, 0, width, height, grad);

            // Title bar
            painter.fillRect(0, 0, width, 40, QColor(30, 120, 200, 200));
            painter.setPen(Qt::white);
            painter.setFont(QFont("Consolas", 14, QFont::Bold));
            painter.drawText(10, 28, QString("REPLAY - %1").arg(QFileInfo(replayPath).baseName()));

            // Telemetry panel
            int panelY = 60;
            painter.setFont(QFont("Consolas", 16));

            auto drawStat = [&](const QString& label, const QString& value, int y) {
                painter.setPen(QColor(150, 180, 220));
                painter.drawText(30, y, label);
                painter.setPen(Qt::white);
                painter.drawText(width / 2, y, value);
            };

            if (!f.cars.isEmpty()) {
                const auto& car = f.cars.first();
                drawStat("TIME", QString("%1:%2")
                    .arg(int(f.timestamp) / 60, 2, 10, QChar('0'))
                    .arg(int(f.timestamp) % 60, 2, 10, QChar('0')), panelY);
                panelY += 30;
                drawStat("SPEED", QString("%1 km/h").arg(car.speed * 3.6, 0, 'f', 1), panelY);
                panelY += 30;
                drawStat("RPM", QString::number(int(car.rpm)), panelY);
                panelY += 30;
                drawStat("GEAR", QString::number(car.gear), panelY);
                panelY += 30;
                drawStat("THROTTLE", QString("%1%").arg(car.throttle * 100, 0, 'f', 0), panelY);
                panelY += 30;
                drawStat("BRAKE", QString("%1%").arg(car.brake * 100, 0, 'f', 0), panelY);
            }

            // Progress bar
            float progress = float(i) / float(totalFrames);
            painter.fillRect(0, height - 8, width, 8, QColor(40, 40, 60));
            painter.fillRect(0, height - 8, int(width * progress), 8, QColor(30, 120, 200));

            painter.end();

            if (!encoder.encodeFrame(frameImg)) {
                return false;
            }
        }

        return encoder.close();
    }

    bool exportToData(const QString& replayPath, const QString& outputPath) {
        KsReplayFile replay;
        if (!replay.open(replayPath)) return false;

        KsTelemetryBuffer buffer;
        buffer.trackId = replay.getTrackName();

        KsReplayFrame frame;
        while (replay.readFrame(frame)) {
            KsTelemetryFrame tf;
            tf.timestamp = frame.timestamp;
            for (int c = 0; c < replay.getCarCount(); c++) {
                tf.speedKmh = frame.cars[c].speed;
                tf.rpm = frame.cars[c].rpm;
                tf.throttle = frame.cars[c].throttle;
                tf.brake = frame.cars[c].brake;
                tf.gear = frame.cars[c].gear;
            }
            buffer.addFrame(tf);
        }

        bool ok = buffer.saveToFile(outputPath);
        replay.close();
        return ok;
    }
};

inline QString formatReplayTime(float seconds) {
    if (seconds <= 0) return "00:00.000";

    int mins = int(seconds) / 60;
    int secs = int(seconds) % 60;
    int ms = int(seconds * 1000) % 1000;

    return QString("%1:%2.%3")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}
}

}

#endif
#ifndef KS_KS_SCRIPT_H
#define KS_KS_SCRIPT_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QTextStream>

#include "sdk/SDKBackend.h"

namespace ks {

enum class KsScriptType {
    App = 0,
    Track = 1,
    Car = 2,
    PostProcess = 3,
    Background = 4,
    WeatherFX = 5,
    Camera = 6,
    Tools = 7
};

class KsLuaGenerator {
public:
    QString code;
    int indentLevel;
    QString indentStr;

    KsLuaGenerator() : indentLevel(0), indentStr("  ") {}

    void reset() {
        code.clear();
        indentLevel = 0;
    }

    QString indent() const {
        QString s;
        for (int i = 0; i < indentLevel; i++) {
            s += indentStr;
        }
        return s;
    }

    void write(const QString& line) {
        code += indent() + line + "\n";
    }

    void write(const QString& line, const QString& arg) {
        code += indent() + line + "(" + arg + ")\n";
    }

    void writeComment(const QString& comment) {
        code += indent() + "-- " + comment + "\n";
    }

    void writeBlockStart(const QString& line) {
        code += indent() + line + " do\n";
        indentLevel++;
    }

    void writeBlockEnd(const QString& end) {
        indentLevel--;
        code += indent() + end + "\n";
    }

    void writeFunction(const QString& name, const QString& params = "") {
        code += indent() + "function " + name + "(" + params + ")\n";
        indentLevel++;
    }

    void writeEndFunction() {
        indentLevel--;
        code += indent() + "end\n";
    }

    void writeLocal(const QString& var, const QString& value) {
        code += indent() + "local " + var + " = " + value + "\n";
    }

    void writeReturn(const QString& value) {
        code += indent() + "return " + value + "\n";
    }

    void writeIf(const QString& condition) {
        code += indent() + "if " + condition + " then\n";
        indentLevel++;
    }

    void writeElseif(const QString& condition) {
        indentLevel--;
        code += indent() + "elseif " + condition + " then\n";
        indentLevel++;
    }

    void writeElse() {
        indentLevel--;
        code += indent() + "else\n";
        indentLevel++;
    }

    void writeEndIf() {
        indentLevel--;
        code += indent() + "end\n";
    }

    void writeFor(const QString& var, const QString& start, const QString& end) {
        code += indent() + "for " + var + " = " + start + ", " + end + " do\n";
        indentLevel++;
    }

    void writeForEach(const QString& var, const QString& list) {
        code += indent() + "for _, " + var + " in pairs(" + list + ") do\n";
        indentLevel++;
    }

    void writeEndFor() {
        indentLevel--;
        code += indent() + "end\n";
    }

    void writeWhile(const QString& condition) {
        code += indent() + "while " + condition + " do\n";
        indentLevel++;
    }

    void writeEndWhile() {
        indentLevel--;
        code += indent() + "end\n";
    }

    void writeTable(const QString& name) {
        code += indent() + name + " = {\n";
        indentLevel++;
    }

    void writeTableKey(const QString& key, const QString& value) {
        code += indent() + "[" + key + "] = " + value + ",\n";
    }

    void writeTableValue(const QString& value) {
        code += indent() + value + ",\n";
    }

    void writeEndTable(int fields = 0) {
        indentLevel--;
        if (fields == 0) {
            code += indent() + "}\n";
        } else {
            code += indent() + "},\n";
}
    static QString generateApp(const QString& appName, const QString& author) {
        KsLuaGenerator gen;
        gen.writeComment("Assetto Corsa App: " + appName);
        gen.writeComment("Author: " + author);
        gen.writeComment("Generated by ksEditor");
        gen.write("");

        gen.write("local ac = ac");

        gen.writeFunction("ac.onStart", "");
        gen.writeComment("Called when the app is loaded");
        gen.writeEndFunction();
        gen.write("");

        gen.writeFunction("ac.onUpdate", "dt");
        gen.writeComment("Called every frame");
        gen.writeEndFunction();
        gen.write("");

        gen.writeFunction("ac.onDraw", "");
        gen.writeComment("Called every frame for UI rendering");

        gen.writeLocal("ui", "ac");
        gen.writeBlockStart("if ui");

        gen.writeEndIf();
        gen.writeEndFunction();

        return gen.code;
    }

    static QString generateTrackScript(const QString& trackName) {
        KsLuaGenerator gen;
        gen.writeComment("Assetto Corsa Track Script: " + trackName);
        gen.write("");
        gen.writeFunction("function script.onLoad", "(data)");
        gen.writeEndFunction();
        gen.write("");
        gen.writeFunction("function script.onUpdate", "(dt)");
        gen.writeEndFunction();
        gen.write("");
        gen.writeFunction("function script.onDraw", "(x, y, w, h)");
        gen.writeEndFunction();

        return gen.code;
    }

    static QString generateCarPhysics(const QString& carName) {
        KsLuaGenerator gen;
        gen.writeComment("Assetto Corsa Car Physics Script: " + carName);
        gen.write("");
        gen.write("localacar = ac.car");
        gen.write("local mes = mes");

        gen.writeFunction("function script.physics.onUpdate", "(dt, car)");
        gen.writeLocal("mb = car:masses()");
        gen.writeLocal("speed = car:speed()");
        gen.writeLocal("rpm = car:rpm()");
        gen.writeEndFunction();

        return gen.code;
    }

    static QString generatePostProcessFilter(const QString& filterName) {
        KsLuaGenerator gen;
        gen.writeComment("Assetto Corsa Post-Process Filter: " + filterName);
        gen.write("");

        gen.write("local pp = require('pp')");
        gen.write("local tex = pp:shader()");

        gen.writeFunction("function filter.onShader", "pipeline, name");
        gen.write("pp:addShader(name, tex)");
        gen.writeEndFunction();

        gen.write("");

        gen.writeFunction("function filter.onUpdate", "dtd");
        gen.writeEndFunction();

        return gen.code;
    }

    static QString generateBackground(const QString& name, const QString& skybox) {
        KsLuaGenerator gen;
        gen.writeComment("Assetto Corsa Background: " + name);
        gen.write("local ac = ac");
        gen.write("local bg = background");
        gen.write("");

        gen.writeFunction("function script.onLoad", "(data)");
        gen.write("bg:setSkybox('" + skybox + "')");
        gen.writeEndFunction();

        gen.write("");

        gen.writeFunction("script.onUpdate", "dt");

        gen.writeEndFunction();

        return gen.code;
    }
};

class KsPythonGenerator {
public:
    QString code;
    int indentLevel;
    QString indentStr;

    KsPythonGenerator() : indentLevel(0), indentStr("    ") {}

    void reset() {
        code.clear();
        indentLevel = 0;
    }

    QString indent() const {
        QString s;
        for (int i = 0; i < indentLevel; i++) {
            s += indentStr;
        }
        return s;
    }

    void write(const QString& line) {
        code += indent() + line + "\n";
    }

    void writeComment(const QString& comment) {
        code += indent() + "# " + comment + "\n";
    }

    void writeClass(const QString& name, const QString& base = "") {
        if (base.isEmpty()) {
            code += indent() + "class " + name + ":\n";
        } else {
            code += indent() + "class " + name + "(" + base + "):\n";
        }
        indentLevel++;
    }

    void writeFunction(const QString& name, const QString& params = "self") {
        code += indent() + "def " + name + "(" + params + "):\n";
        indentLevel++;
    }

    void writeEndFunction() {
        indentLevel--;
    }

    void writeImport(const QString& module) {
        code += "import " + module + "\n";
    }

    void writeFromImport(const QString& module, const QString& item) {
        code += "from " + module + " import " + item + "\n";
    }

    static QString generatePythonApp(const QString& appName) {
        KsPythonGenerator gen;

        gen.writeComment("Assetto Corsa Python App: " + appName);
        gen.write("");
        gen.writeImport("ac");
        gen.writeImport("ui");
        gen.write("");

        gen.writeClass(appName, "ac.PythonApp");
        gen.writeFunction("__init__", "self");
        gen.write("super().__init__()");
        gen.writeEndFunction();
        gen.write("");

        gen.writeFunction("onStart", "self");
        gen.writeEndFunction();
        gen.write("");

        gen.writeFunction("onUpdate", "self", "dt");
        gen.writeEndFunction();
        gen.write("");

        gen.writeFunction("onDraw", "self");
        gen.writeEndFunction();

        return gen.code;
    }
};

class KsJSONGenerator {
public:
    static QString generateCarJSON(const QString& carId, const QString& model, const QString& author,
                             const QString& country, int year) {
        QString json = "{\n";
        json += "  \"id\": \"" + carId + "\",\n";
        json += "  \"model\": \"" + model + "\",\n";
        json += "  \"author\": \"" + author + "\",\n";
        json += "  \"country\": \"" + country + "\",\n";
        json += "  \"year\": " + QString::number(year) + ",\n";
        json += "  \"power\": 0,\n";
        json += "  \"torque\": 0,\n";
        json += "  \"weight\": 0,\n";
        json += "  \"tags\": []\n";
        json += "}\n";
        return json;
    }

    static QString generateTrackJSON(const QString& trackId, const QString& name,
                                   const QString& country, float length) {
        QString json = "{\n";
        json += "  \"id\": \"" + trackId + "\",\n";
        json += "  \"name\": \"" + name + "\",\n";
        json += "  \"country\": \"" + country + "\",\n";
        json += "  \"length\": " + QString::number(length, 'f', 3) + ",\n";
        json += "  \"pits\": 0,\n";
        json += "  \"tags\": []\n";
        json += "}\n";
        return json;
    }

    static QString generateUIJSON(const QString& name, const QString& author,
                                  const QString& description) {
        QString json = "{\n";
        json += "  \"name\": \"" + name + "\",\n";
        json += "  \"author\": \"" + author + "\",\n";
        json += "  \"description\": \"" + description + "\",\n";
        json += "  \"version\": \"1.0\",\n";
        json += "  \"tags\": []\n";
        json += "}\n";
        return json;
    }
};

}
}

}

#endif
#ifndef KS_KS_SHOWROOM_H
#define KS_KS_SHOWROOM_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QIODevice>
#include <QSettings>
#include <QDateTime>


#include "ks_audio.h"


#include "ks_renderer.h"

namespace ks {

// ============================================================================
// AUDIO MODULE
// ============================================================================

enum KsEngineSoundMode {
    Engine_Idle = 0,
    Engine_Start = 1,
    Engine_Rev = 2,
    Engine_WOT = 3,
    Engine_Coil = 4,
    Engine_EngineBrake = 5
};

enum KsDynoMode {
    Dyno_Steady = 0,
    Dyno_Sweep = 1,
    Dyno_Load = 2,
    Dyno_Ramp = 3
};

enum KsExhaustMode {
    Exhaust_Quiet = 0,
    Exhaust_Sport = 1,
    Exhaust_Race = 2,
    Exhaust_Open = 3
};

struct KsEngineAudioConfig {
    float idleRpm;
    float maxRpm;
    float redlineRpm;

    float idleVolume;
    float revVolume;
    float wotVolume;

    float idlePitch;
    float revPitch;
    float wotPitch;

    float throttleSensitivity;
    float loadResponse;

    float gearRatios[8];
    int gearCount;

    float finalDrive;

    KsEngineAudioConfig() {
        idleRpm = 800.0f;
        maxRpm = 8000.0f;
        redlineRpm = 7000.0f;

        idleVolume = 0.3f;
        revVolume = 0.7f;
        wotVolume = 1.0f;

        idlePitch = 0.5f;
        revPitch = 1.0f;
        wotPitch = 1.5f;

        throttleSensitivity = 2.0f;
        loadResponse = 1.0f;

        gearCount = 6;
        for (int i = 0; i < 8; i++) gearRatios[i] = 0;
        gearRatios[0] = 3.5f;
        gearRatios[1] = 2.1f;
        gearRatios[2] = 1.4f;
        gearRatios[3] = 1.0f;
        gearRatios[4] = 0.8f;
        gearRatios[5] = 0.6f;

        finalDrive = 3.7f;
    }

    static KsEngineAudioConfig fromCar(const QString& carId) {
        KsEngineAudioConfig cfg;
        QString carDir = QString(KS_SDK_PATH) + "/content/cars/" + carId;

        KsIniDocument carIni;
        if (carIni.load(carDir + "/data/car.ini")) {
            cfg.idleRpm = carIni.getValue("ENGINE", "IDLE_RPM", "800").toFloat();
            cfg.maxRpm = carIni.getValue("SPECS", "MAX_RPM", "8000").toFloat();
            cfg.redlineRpm = carIni.getValue("SPECS", "RED_LINE_RPM", "7000").toFloat();
        }

        KsIniDocument engineIni;
        if (engineIni.load(carDir + "/data/engine.ini")) {
            QStringList gears = engineIni.getValue("GEARS", "GEARS", "3.5,2.1,1.4,1.0,0.8,0.6").toString().split(",");
            cfg.gearCount = qMin(gears.size(), 8);
            for (int i = 0; i < cfg.gearCount; i++) {
                cfg.gearRatios[i] = gears[i].toFloat();
            }
        }

        return cfg;
    }
};

struct KsExhaustConfig {
    float volume;
    float pitch;
    float crackleVolume;
    float crackleDensity;

    QString exhaustType;
    int configuration;

    float backPressure;

    bool hasAntiLag;
    bool hasPopValve;

    KsExhaustConfig() {
        volume = 0.7f;
        pitch = 0.8f;
        crackleVolume = 0.5f;
        crackleDensity = 0.3f;

        exhaustType = "Sport";
        configuration = 2;
        backPressure = 1.0f;

        hasAntiLag = false;
        hasPopValve = false;
    }

    static KsExhaustConfig fromCar(const QString& carId) {
        KsExhaustConfig cfg;
        QString carDir = QString(KS_SDK_PATH) + "/content/cars/" + carId;

        QDir dataDir(carDir + "/data");
        if (dataDir.exists()) {
            QFileInfoList inis = dataDir.entryInfoList(QStringList() << "*.ini", QDir::Files);
            for (const QFileInfo& ini : inis) {
                QString name = ini.baseName().toLower();
                if (name.contains("exhaust")) {
                    KsIniDocument doc;
                    if (doc.load(ini.absoluteFilePath())) {
                        cfg.volume = doc.getValue("AUDIO", "VOLUME", "0.7").toFloat();
                        cfg.pitch = doc.getValue("AUDIO", "PITCH", "0.8").toFloat();
                        cfg.hasAntiLag = doc.getValue("AUDIO", "ANTILAG", "0").toInt() == 1;
                        cfg.hasPopValve = doc.getValue("AUDIO", "POPVALVE", "0").toInt() == 1;
                    }
                }
            }
        }

        return cfg;
    }
};

class KsEngineSoundPlayer {
public:
    KsEngineAudioConfig config;
    KsExhaustConfig exhaust;

    float currentRpm;
    float targetRpm;
    float currentThrottle;
    float targetThrottle;

    float currentLoad;
    float targetLoad;

    int currentGear;

    float volume;
    float pitch;

    bool isPlaying;
    bool isLooping;

    float playTime;

    KsEngineSoundPlayer() {
        currentRpm = 800;
        targetRpm = 800;
        currentThrottle = 0;
        targetThrottle = 0;
        currentLoad = 0;
        targetLoad = 0;
        currentGear = 1;
        volume = 0.5f;
        pitch = 0.5f;
        isPlaying = false;
        isLooping = true;
        playTime = 0;
    }

    void loadCar(const QString& carId) {
        config = KsEngineAudioConfig::fromCar(carId);
        exhaust = KsExhaustConfig::fromCar(carId);
    }

    void setThrottle(float throttle) {
        targetThrottle = qBound(0.0f, throttle, 1.0f);
    }

    void setLoad(float load) {
        targetLoad = qBound(0.0f, load, 1.0f);
    }

    void setRPM(float rpm) {
        targetRpm = qBound(config.idleRpm, rpm, config.maxRpm);
    }

    void setGear(int gear) {
        currentGear = qBound(1, gear, config.gearCount);
    }

    void update(float dt) {
        float interpolation = config.throttleSensitivity * dt;

        currentThrottle = currentThrottle + (targetThrottle - currentThrottle) * interpolation;
        currentLoad = currentLoad + (targetLoad - currentLoad) * interpolation;

        currentRpm = currentRpm + (targetRpm - currentRpm) * interpolation * 0.5f;

        float rpmNorm = (currentRpm - config.idleRpm) / (config.maxRpm - config.idleRpm);
        rpmNorm = qBound(0.0f, rpmNorm, 1.0f);

        float throttleNorm = currentThrottle;
        float loadNorm = currentLoad;

        volume = config.idleVolume + (config.revVolume - config.idleVolume) * rpmNorm;
        volume += (config.wotVolume - config.revVolume) * throttleNorm * rpmNorm;

        pitch = config.idlePitch + (config.revPitch - config.idlePitch) * rpmNorm;
        pitch += (config.wotPitch - config.revPitch) * throttleNorm * rpmNorm;

        if (currentThrottle < 0.1f && currentRpm > config.idleRpm + 500) {
            volume *= 0.7f;
            pitch *= 0.8f;
        }

        if (isPlaying) {
            playTime += dt;
        }
    }

    float getTorque(float rpm) const {
        float normalized = (rpm - config.idleRpm) / (config.maxRpm - config.idleRpm);
        normalized = qBound(0.0f, normalized, 1.0f);

        float torque = 1.0f - pow(normalized, 2.0f);
        torque *= (1.0f - currentLoad * 0.3f);

        return torque;
    }

    float getPower(float rpm) const {
        float torque = getTorque(rpm);
        float hp = torque * rpm / 5252.0f;
        return hp;
    }

    float getDbVolume() const {
        float db = 20.0f * log10(volume + 0.001f);
        return qBound(-60.0f, db, 0.0f);
    }

    QString getStatus() const {
        return QString("RPM: %1 THROTTLE: %2 GEAR: %3 VOL: %4 dB")
            .arg(int(currentRpm))
            .arg(int(currentThrottle * 100), 3, 10, QChar('0'))
            .arg(currentGear)
            .arg(int(getDbVolume()));
    }
};

class KsDynoTest {
public:
    KsDynoMode mode;

    float startRpm;
    float endRpm;
    float stepRpm;

    float rampRate;
    float holdTime;

    float currentTestRpm;
    float testTime;

    float testRpmValues[100];
    float testTorqueValues[100];
    float testPowerValues[100];
    int testPointCount;

    bool isRunning;
    bool isComplete;

    float peakTorque;
    float peakTorqueRpm;
    float peakPower;
    float peakPowerRpm;

    KsDynoTest() {
        mode = Dyno_Steady;
        startRpm = 1000;
        endRpm = 7000;
        stepRpm = 500;
        rampRate = 500;
        holdTime = 2.0f;

        currentTestRpm = startRpm;
        testTime = 0;
        testPointCount = 0;
        isRunning = false;
        isComplete = false;

        peakTorque = 0;
        peakTorqueRpm = 0;
        peakPower = 0;
        peakPowerRpm = 0;
    }

    void reset() {
        testPointCount = 0;
        currentTestRpm = startRpm;
        testTime = 0;
        isRunning = false;
        isComplete = false;
        peakTorque = 0;
        peakTorqueRpm = 0;
        peakPower = 0;
        peakPowerRpm = 0;
    }

    void start() {
        reset();
        isRunning = true;
    }

    void stop() {
        isRunning = false;
    }

    void update(float dt, KsEngineSoundPlayer* engine) {
        if (!isRunning || !engine) return;

        testTime += dt;

        switch (mode) {
            case Dyno_Steady:
                if (currentTestRpm <= endRpm) {
                    engine->setRPM(currentTestRpm);
                    engine->setThrottle(1.0f);
                    engine->setLoad(0.0f);
                    engine->update(dt);

                    if (testTime >= holdTime) {
                        recordPoint(engine);
                        currentTestRpm += stepRpm;
                        testTime = 0;
                    }
                } else {
                    isComplete = true;
                    isRunning = false;
                }
                break;

            case Dyno_Sweep:
                engine->setThrottle(1.0f);
                engine->setLoad(0.0f);
                currentTestRpm += rampRate * dt;
                if (currentTestRpm > endRpm) {
                    currentTestRpm = endRpm;
                    isComplete = true;
                    isRunning = false;
                }
                engine->setRPM(currentTestRpm);
                engine->update(dt);
                recordPoint(engine);
                break;

            case Dyno_Load:
                if (testTime < 3.0f) {
                    engine->setThrottle(0.5f);
                } else if (testTime < 5.0f) {
                    engine->setThrottle(1.0f);
                } else {
                    testTime = 0;
                    currentTestRpm += stepRpm;
                    if (currentTestRpm > endRpm) {
                        isComplete = true;
                        isRunning = false;
                    }
                }
                engine->setRPM(currentTestRpm);
                engine->update(dt);
                recordPoint(engine);
                break;

            default:
                break;
        }
    }

    void recordPoint(KsEngineSoundPlayer* engine) {
        if (testPointCount >= 100) return;

        testRpmValues[testPointCount] = currentTestRpm;
        testTorqueValues[testPointCount] = engine->getTorque(currentTestRpm);
        testPowerValues[testPointCount] = engine->getPower(currentTestRpm);

        if (testTorqueValues[testPointCount] > peakTorque) {
            peakTorque = testTorqueValues[testPointCount];
            peakTorqueRpm = currentTestRpm;
        }

        if (testPowerValues[testPointCount] > peakPower) {
            peakPower = testPowerValues[testPointCount];
            peakPowerRpm = currentTestRpm;
        }

        testPointCount++;
    }

    QString getResults() const {
        QString out = "Dyno Test Results:\n";
        out += QString("Peak Torque: %1 lb-ft @ %2 RPM\n")
            .arg(peakTorque, 0, 'f', 1).arg(int(peakTorqueRpm));
        out += QString("Peak Power: %1 HP @ %2 RPM\n")
            .arg(peakPower, 0, 'f', 1).arg(int(peakPowerRpm));
        out += QString("Test Points: %1\n").arg(testPointCount);
        return out;
    }

    QString getCurveData() const {
        QString out;
        for (int i = 0; i < testPointCount; i++) {
            out += QString("%1,%2,%3\n")
                .arg(int(testRpmValues[i]))
                .arg(testTorqueValues[i], 0, 'f', 2)
                .arg(testPowerValues[i], 0, 'f', 2);
        }
        return out;
    }

    void exportCSV(const QString& path) const {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream out(&file);
            out << "RPM,Torque,Power\n";
            out << getCurveData();
            file.close();
        }
    }
};

class KsShowroomAudioController {
public:
    KsEngineSoundPlayer enginePlayer;
    KsDynoTest dynoTest;

    QStringList availableSounds;
    QStringList availableExhausts;

    float masterVolume;
    float uiVolume;

    bool isEnginePlaying;
    bool isDynoRunning;

    KsShowroomAudioController() {
        masterVolume = 1.0f;
        uiVolume = 0.5f;
        isEnginePlaying = false;
        isDynoRunning = false;
    }

    void initializeCar(const QString& carId) {
        enginePlayer.loadCar(carId);
        loadSoundBanks(carId);
    }

    void loadSoundBanks(const QString& carId) {
        availableSounds.clear();
        availableExhausts.clear();

        QString carDir = QString(KS_SDK_PATH) + "/content/cars/" + carId + "/audio";
        if (!QDir(carDir).exists()) return;

        QDir audioDir(carDir);
        availableSounds = audioDir.entryList(QStringList() << "*.bank", QDir::Files);

        QString exhDir = carDir + "/exhaust";
        if (QDir(exhDir).exists()) {
            availableExhausts = QDir(exhDir).entryList(QStringList() << "*.bank", QDir::Files);
        }
    }

    void playEngineStart() {
        isEnginePlaying = true;
        enginePlayer.targetRpm = enginePlayer.config.idleRpm * 2;
    }

    void playEngineRev(float throttle) {
        enginePlayer.setThrottle(throttle);
    }

    void stopEngine() {
        isEnginePlaying = false;
    }

    void startDynoTest(KsDynoMode mode = Dyno_Steady) {
        dynoTest.mode = mode;
        dynoTest.start();
        isDynoRunning = true;
    }

    void stopDynoTest() {
        dynoTest.stop();
        isDynoRunning = false;
    }

    void update(float dt) {
        if (isDynoRunning) {
            dynoTest.update(dt, &enginePlayer);
            if (dynoTest.isComplete) {
                isDynoRunning = false;
            }
        }

        enginePlayer.update(dt);
    }

    void playSound(const QString& soundId) {
        Q_UNUSED(soundId);
    }

    void setMasterVolume(float vol) {
        masterVolume = qBound(0.0f, vol, 1.0f);
    }

    QString getEngineStatus() const {
        return enginePlayer.getStatus();
    }

    QString getDynoStatus() const {
        if (isDynoRunning) {
            return QString("Dyno Running: %1 RPM").arg(int(dynoTest.currentTestRpm));
        } else if (dynoTest.isComplete) {
            return "Dyno Complete";
        }
        return "Dyno Ready";
    }
};

class KsShowroomAudioGUI {
public:
    QStringList buttonSounds;
    QStringList buttonSoundsPressed;

    bool throttlePressed;
    bool brakePressed;

    KsShowroomAudioGUI() {
        throttlePressed = false;
        brakePressed = false;
        loadButtonSounds();
    }

    void loadButtonSounds() {
        QString guiPath = QString(KS_SDK_PATH) + "/content/gui/showroom";
        buttonSounds.append(guiPath + "/bar.png");
    }

    bool handleThrottleInput(bool pressed) {
        throttlePressed = pressed;
        return true;
    }

    bool handleBrakeInput(bool pressed) {
        brakePressed = pressed;
        return true;
    }
};

// ============================================================================
// VIDEO SHOWROOM MODULE
// ============================================================================

enum KsShowroomMode {
    Showroom_CarView = 0,
    Showroom_Preview = 1,
    Showroom_Compare = 2,
    Showroom_Paint = 3
};

enum KsCameraPreset {
    Camera_Front3Quarter = 0,
    Camera_Rear3Quarter = 1,
    Camera_Front = 2,
    Camera_Rear = 3,
    Camera_Side = 4,
    Camera_Driver = 5,
    Camera_Hood = 6,
    Camera_Aero = 7,
    Camera_Custom = 8
};

enum KsEnvironment {
    Env_Showroom = 0,
    Env_StudioWhite = 1,
    Env_ASR = 2,
    Env_Hangar = 3,
    Env_Industrial = 4,
    Env_Beach = 5,
    Env_Ferrari = 6,
    Env_ATPPreviews = 7
};

enum KsAnimationType {
    Anim_None = 0,
    Anim_Rotate = 1,
    Anim_Bounce = 2,
    Anim_Sway = 3,
    Anim_Carousel = 4
};

struct KsShowroomSettings {
    QString carId;
    QString skinId;
    QString trackId;

    float cameraDistance;
    float cameraHeight;
    float cameraFov;
    float cameraExposure;

    float sunAngle;
    int shadowSplits[3];

    float nearPlane;
    float farPlane;

    float minExposure;
    float maxExposure;

    float rotationSpeed;
    float animationMultiplier;

    KsShowroomSettings()
        : trackId("showroom")
        , cameraDistance(6.0f), cameraHeight(1.5f)
        , cameraFov(30.0f), cameraExposure(30.0f)
        , sunAngle(-50.0f)
        , nearPlane(0.01f), farPlane(200.0f)
        , minExposure(0.2f), maxExposure(10000.0f)
        , rotationSpeed(1.0f), animationMultiplier(0.15f)
    {
        shadowSplits[0] = 2;
        shadowSplits[1] = 12;
        shadowSplits[2] = 50;
    }
};

struct KsCameraPosition {
    float position[3];
    float target[3];
    float roll;
    float fov;
    float exposure;

    bool useCustom;

    KsCameraPosition()
        : roll(0), fov(0), exposure(0), useCustom(false)
    {
        position[0] = position[1] = position[2] = 0;
        target[0] = 0; target[1] = 0.6f; target[2] = 0;
    }
};

class KsShowroomEnvironment {
public:
    QString id;
    QString name;

    QString modelPath;
    QString bankPath;
    QString trackWavPath;
    QString previewPath;

    QString colorCurvesIni;
    QString ppEffectsIni;
    QString settingsIni;

    bool hasAudio;
    bool hasPostProcess;

    QStringList uiFiles;

    KsShowroomEnvironment()
        : hasAudio(true), hasPostProcess(true) {}

    static QList<KsShowroomEnvironment> getAvailable() {
        QList<KsShowroomEnvironment> envs;

        QString basePath = QString(KS_SDK_PATH) + "/content/showroom";
        QDir baseDir(basePath);

        if (!baseDir.exists()) return envs;

        QFileInfoList entries = baseDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            QString envPath = entry.absoluteFilePath();
            QDir envDir(envPath);

            KsShowroomEnvironment env;
            env.id = entry.fileName();
            env.name = entry.fileName();

            if (QFileInfo(envPath + "/" + env.id + ".kn5").exists()) {
                env.modelPath = envPath + "/" + env.id + ".kn5";
            }
            if (QFileInfo(envPath + "/" + env.id + ".bank").exists()) {
                env.bankPath = envPath + "/" + env.id + ".bank";
            }
            if (QFileInfo(envPath + "/track.wav").exists()) {
                env.trackWavPath = envPath + "/track.wav";
            }
            if (QFileInfo(envPath + "/preview.jpg").exists()) {
                env.previewPath = envPath + "/preview.jpg";
            }
            if (QFileInfo(envPath + "/colorCurves.ini").exists()) {
                env.colorCurvesIni = envPath + "/colorCurves.ini";
            }
            if (QFileInfo(envPath + "/ppeffects.ini").exists()) {
                env.ppEffectsIni = envPath + "/ppeffects.ini";
            }
            if (QFileInfo(envPath + "/settings.ini").exists()) {
                env.settingsIni = envPath + "/settings.ini";
            }

            QDir uiDir(envPath + "/ui");
            if (uiDir.exists()) {
                env.uiFiles = uiDir.entryList(QStringList() << "*.json", QDir::Files);
            }

            env.hasAudio = !env.bankPath.isEmpty();
            env.hasPostProcess = !env.ppEffectsIni.isEmpty();

            envs.append(env);
        }

        return envs;
    }
};

class KsShowroomCar {
public:
    QString carId;
    QString carName;
    QString brand;

    QString modelPath;
    QString skinPath;
    QStringList availableSkins;

    bool isLoaded;
    bool hasLights;
    bool hasAnimations;

    float yaw;
    float pitch;
    float distance;
    float height;

    KsShowroomCar()
        : yaw(0), pitch(0), distance(6.0f), height(1.5f)
        , isLoaded(false), hasLights(false), hasAnimations(false) {}

    static QList<KsShowroomCar> getCars() {
        QList<KsShowroomCar> cars;

        QString carsPath = QString(KS_SDK_PATH) + "/content/cars";
        QDir carsDir(carsPath);

        if (!carsDir.exists()) return cars;

        QFileInfoList entries = carsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            QString carDataPath = entry.absoluteFilePath() + "/data";
            QDir dataDir(carDataPath);

            if (!dataDir.exists()) continue;

            KsShowroomCar car;
            car.carId = entry.fileName();
            car.carName = entry.fileName();
            car.modelPath = entry.absoluteFilePath() + "/" + entry.fileName() + ".kn5";

            QFileInfo carIni(carDataPath + "/car.ini");
            if (carIni.exists()) {
                KsIniDocument doc;
                if (doc.load(carIni.absoluteFilePath())) {
                    car.brand = doc.getValue("General", "brand", "").toString();
                }
            }

            QDir skinsDir(entry.absoluteFilePath() + "/skins");
            if (skinsDir.exists()) {
                QStringList skins = skinsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                car.availableSkins = skins;
            }

            car.isLoaded = QFileInfo(car.modelPath).exists();

            cars.append(car);
        }

        return cars;
    }

    QStringList getAvailableSkins() const {
        return availableSkins;
    }
};

class KsShowroomState {
public:
    KsShowroomMode mode;
    KsEnvironment environment;
    KsAnimationType animation;

    KsShowroomCar car;
    QString currentSkin;

    KsCameraPosition cameraPos;
    KsCameraPreset cameraPreset;

    float rotationAngle;
    float animationTime;
    float zoomLevel;

    bool autoRotate;
    bool showGrid;
    bool showAxes;
    bool showWireframe;
    bool showLights;

    bool allowSkinSelect;
    bool allowCarChange;

    float exposure;
    float ambientTemp;
    float trackTemp;

    KsShowroomState()
        : mode(Showroom_CarView)
        , environment(Env_Showroom)
        , animation(Anim_Rotate)
        , cameraPreset(Camera_Front3Quarter)
        , rotationAngle(0)
        , animationTime(0)
        , zoomLevel(1.0f)
        , autoRotate(true)
        , showGrid(true)
        , showAxes(false)
        , showWireframe(false)
        , showLights(true)
        , allowSkinSelect(true)
        , allowCarChange(true)
        , exposure(30.0f)
        , ambientTemp(25.0f)
        , trackTemp(30.0f) {}
};

class KsShowroomController {
public:
    KsShowroomState state;
    KsShowroomSettings settings;

    QList<KsShowroomCar> availableCars;
    QList<KsShowroomEnvironment> availableEnvs;

    QString screenshotPath;
    bool screenshotRequested;

    KsShowroomController()
        : screenshotRequested(false) {
        loadCars();
        loadEnvironments();
    }

    void loadCars() {
        availableCars = KsShowroomCar::getCars();
    }

    void loadEnvironments() {
        availableEnvs = KsShowroomEnvironment::getAvailable();
    }

    bool loadCar(const QString& carId) {
        for (const auto& car : availableCars) {
            if (car.carId == carId) {
                state.car = car;
                return true;
            }
        }
        return false;
    }

    bool loadSkin(const QString& skinId) {
        if (state.car.availableSkins.contains(skinId)) {
            state.currentSkin = skinId;
            return true;
        }
        return false;
    }

    void setEnvironment(KsEnvironment env) {
        state.environment = env;
    }

    void setCameraPreset(KsCameraPreset preset) {
        state.cameraPreset = preset;
        updatePresetCamera(preset);
    }

    void updatePresetCamera(KsCameraPreset preset) {
        switch (preset) {
            case Camera_Front3Quarter:
                state.cameraPos.position[0] = -3.5f;
                state.cameraPos.position[1] = 1.2f;
                state.cameraPos.position[2] = 4.5f;
                break;
            case Camera_Rear3Quarter:
                state.cameraPos.position[0] = 3.5f;
                state.cameraPos.position[1] = 1.2f;
                state.cameraPos.position[2] = -4.5f;
                break;
            case Camera_Front:
                state.cameraPos.position[0] = 0;
                state.cameraPos.position[1] = 0.8f;
                state.cameraPos.position[2] = 3.0f;
                break;
            case Camera_Rear:
                state.cameraPos.position[0] = 0;
                state.cameraPos.position[1] = 0.8f;
                state.cameraPos.position[2] = -3.0f;
                break;
            case Camera_Side:
                state.cameraPos.position[0] = 4.0f;
                state.cameraPos.position[1] = 1.0f;
                state.cameraPos.position[2] = 0;
                break;
            case Camera_Driver:
                state.cameraPos.position[0] = 0.3f;
                state.cameraPos.position[1] = 1.0f;
                state.cameraPos.position[2] = 0.8f;
                break;
            case Camera_Hood:
                state.cameraPos.position[0] = 0;
                state.cameraPos.position[1] = 1.1f;
                state.cameraPos.position[2] = 1.8f;
                break;
            case Camera_Aero:
                state.cameraPos.position[0] = -2.0f;
                state.cameraPos.position[1] = 0.4f;
                state.cameraPos.position[2] = 1.5f;
                break;
            default:
                break;
        }

        updateCameraTarget();
    }

    void updateCameraTarget() {
        state.cameraPos.target[0] = 0;
        state.cameraPos.target[1] = 0.6f;
        state.cameraPos.target[2] = 0;
    }

    void rotate(float delta) {
        state.rotationAngle += delta * settings.rotationSpeed;

        float rad = state.rotationAngle * DEG2RAD;
        float dist = state.car.distance * state.zoomLevel;

        state.cameraPos.position[0] = -sin(rad) * dist;
        state.cameraPos.position[1] = state.car.height * state.zoomLevel;
        state.cameraPos.position[2] = cos(rad) * dist;

        updateCameraTarget();
    }

    void zoom(float delta) {
        state.zoomLevel = qBound(0.1f, state.zoomLevel + delta, 5.0f);

        float rad = state.rotationAngle * DEG2RAD;
        float dist = state.car.distance * state.zoomLevel;

        state.cameraPos.position[0] = -sin(rad) * dist;
        state.cameraPos.position[1] = state.car.height * state.zoomLevel;
        state.cameraPos.position[2] = cos(rad) * dist;
    }

    void orbit(float dx, float dy) {
        float rad = state.rotationAngle * DEG2RAD;
        float dist = state.car.distance * state.zoomLevel;

        float angle = atan2(state.cameraPos.position[0], state.cameraPos.position[2]);
        angle += dx * 0.01f;

        state.cameraPos.position[0] = -sin(angle) * dist;
        state.cameraPos.position[1] = qBound(0.1f, state.cameraPos.position[1] + dy * 0.01f, 5.0f);
        state.cameraPos.position[2] = cos(angle) * dist;

        state.rotationAngle = angle * RAD2DEG;
    }

    void update(float dt) {
        if (state.autoRotate) {
            rotate(dt * settings.rotationSpeed);
        }

        if (state.animation != Anim_None) {
            updateAnimation(dt);
        }
    }

    void updateAnimation(float dt) {
        state.animationTime += dt;

        switch (state.animation) {
            case Anim_Rotate:
                rotate(dt * settings.animationMultiplier);
                break;
            case Anim_Bounce: {
                float bounce = sin(state.animationTime * 2.0f) * 0.1f;
                float center[3] = {0, 0, 0};
                Q_UNUSED(center);
                break;
            }
            case Anim_Sway: {
                float sway = sin(state.animationTime * 1.5f) * 0.15f;
                float center[3] = {0, 0, 0};
                Q_UNUSED(center);
                break;
            }
            case Anim_Carousel:
                rotate(dt * settings.animationMultiplier * 0.5f);
                break;
            default:
                break;
        }
    }

    void takeScreenshot(const QString& path = "") {
        if (!path.isEmpty()) {
            screenshotPath = path;
        } else {
            QDateTime now = QDateTime::currentDateTime();
            screenshotPath = "screenshot_" + now.toString("yyyyMMdd_HHmmss") + ".png";
        }
        screenshotRequested = true;
    }

    bool loadStartConfig(const QString& carId = "", const QString& skinId = "1") {
        QString configPath = QString(KS_SDK_PATH) + "/cfg/showroom_start.ini";

        QSettings cfg(configPath, QSettings::IniFormat);

        if (!carId.isEmpty()) {
            settings.carId = carId;
        } else {
            settings.carId = cfg.value("SHOWROOM/CAR", "tatuusfa1").toString();
        }

        settings.trackId = cfg.value("SHOWROOM/TRACK", "showroom").toString();
        state.currentSkin = cfg.value("SHOWROOM/SKIN", skinId).toString();

        state.cameraPos.useCustom = cfg.value("PREVIEW_MODE/USE_CUSTOM_CAMERA", 0).toInt() == 1;
        if (state.cameraPos.useCustom) {
            QStringList pos = cfg.value("PREVIEW_MODE/CUSTOM_CAMERA_POSITION", "0,0.775145,-6.12493").toString().split(",");
            if (pos.size() >= 3) {
                state.cameraPos.position[0] = pos[0].toFloat();
                state.cameraPos.position[1] = pos[1].toFloat();
                state.cameraPos.position[2] = pos[2].toFloat();
            }
            updateCameraTarget();

            state.cameraPos.roll = cfg.value("PREVIEW_MODE/CUSTOM_CAMERA_ROLL", "0").toFloat();
            state.cameraPos.exposure = cfg.value("PREVIEW_MODE/CUSTOM_CAMERA_EXPOSURE", "94.5").toFloat();
        }

        settings.animationMultiplier = cfg.value("ANIMATION/MUL", "0.15").toFloat();
        settings.rotationSpeed = cfg.value("SETTINGS/ROTATION_SPEED", "1.0").toFloat();
        settings.cameraDistance = cfg.value("SETTINGS/CAMERA_DISTANCE", "6").toFloat();
        settings.cameraHeight = cfg.value("SETTINGS/CAMERA_HEIGHT", "1.5").toFloat();
        settings.cameraFov = cfg.value("SETTINGS/CAMERA_FOV", "30").toFloat();
        settings.cameraExposure = cfg.value("SETTINGS/CAMERA_EXPOSURE", "30").toFloat();
        settings.sunAngle = cfg.value("SETTINGS/SUN_ANGLE", "-50").toFloat();

        state.autoRotate = cfg.value("SETTINGS/ROTATION_SPEED", "1.0").toFloat() > 0;

        state.car.distance = settings.cameraDistance;
        state.car.height = settings.cameraHeight;
        state.cameraPos.fov = settings.cameraFov;
        state.exposure = settings.cameraExposure;

        return loadCar(settings.carId);
    }

    void saveStartConfig() const {
        QString configPath = QString(KS_SDK_PATH) + "/cfg/showroom_start.ini";
        QSettings cfg(configPath, QSettings::IniFormat);

        cfg.setValue("SHOWROOM/CAR", settings.carId);
        cfg.setValue("SHOWROOM/SKIN", 1);
        cfg.setValue("SHOWROOM/TRACK", settings.trackId);
        cfg.setValue("SHOWROOM/ALLOW_SELECT_SKIN", 1);
        cfg.setValue("SHOWROOM/SELECTED_SKIN", 1);

        if (state.cameraPos.useCustom) {
            cfg.setValue("PREVIEW_MODE/LOOK_AT", "0,0.6,0");
            QString pos = QString("%1,%2,%3")
                .arg(state.cameraPos.position[0], 0, 'f', 6)
                .arg(state.cameraPos.position[1], 0, 'f', 6)
                .arg(state.cameraPos.position[2], 0, 'f', 6);
            cfg.setValue("PREVIEW_MODE/CUSTOM_CAMERA_POSITION", pos);
            cfg.setValue("PREVIEW_MODE/USE_CUSTOM_CAMERA", 1);
            cfg.setValue("PREVIEW_MODE/CUSTOM_CAMERA_ROLL", state.cameraPos.roll);
            cfg.setValue("PREVIEW_MODE/CUSTOM_CAMERA_EXPOSURE", state.cameraPos.exposure);
        }

        cfg.setValue("ANIMATION/MUL", settings.animationMultiplier);
        cfg.setValue("SETTINGS/ROTATION_SPEED", settings.rotationSpeed);
        cfg.setValue("SETTINGS/CAMERA_DISTANCE", settings.cameraDistance);
        cfg.setValue("SETTINGS/CAMERA_HEIGHT", settings.cameraHeight);
        cfg.setValue("SETTINGS/CAMERA_FOV", settings.cameraFov);
        cfg.setValue("SETTINGS/CAMERA_EXPOSURE", settings.cameraExposure);
        cfg.setValue("SETTINGS/SUN_ANGLE", settings.sunAngle);

        cfg.sync();
    }
};

class KsShowroomGUI {
public:
    enum ButtonId {
        Button_RotateLeft = 0,
        Button_RotateRight = 1,
        Button_ZoomIn = 2,
        Button_ZoomOut = 3,
        Button_Change = 4,
        Button_Select = 5,
        Button_Screenshot = 6,
        Button_EnterCar = 7,
        Button_Help = 8
    };

    QStringList buttonImages_ON;
    QStringList buttonImages_OFF;

    QString mockupImage;

    bool isVisible;
    bool isHelpVisible;

    KsShowroomGUI()
        : isVisible(true), isHelpVisible(false)
    {
        QString guiPath = QString(KS_SDK_PATH) + "/content/gui/showroom";

        buttonImages_OFF.append(guiPath + "/rollLeft.png");
        buttonImages_OFF.append(guiPath + "/rollRight.png");
        buttonImages_OFF.append(guiPath + "/zoomMore.png");
        buttonImages_OFF.append(guiPath + "/zoomLess.png");
        buttonImages_OFF.append(guiPath + "/change_OFF.png");
        buttonImages_OFF.append(guiPath + "/select_button_OFF.png");
        buttonImages_OFF.append(guiPath + "/screenshot_OFF.png");
        buttonImages_OFF.append(guiPath + "/enterCar_OFF.png");
        buttonImages_OFF.append(guiPath + "/help_OFF.png");

        buttonImages_ON.append(guiPath + "/rollRight.png");
        buttonImages_ON.append(guiPath + "/rollLeft.png");
        buttonImages_ON.append(guiPath + "/zoomMore.png");
        buttonImages_ON.append(guiPath + "/zoomLess.png");
        buttonImages_ON.append(guiPath + "/change_ON.png");
        buttonImages_ON.append(guiPath + "/select_button_ON.png");
        buttonImages_ON.append(guiPath + "/screenshot_ON.png");
        buttonImages_ON.append(guiPath + "/enterCar_ON.png");
        buttonImages_ON.append(guiPath + "/help_ON.png");

        mockupImage = guiPath + "/showroom_mockup02.jpg";
    }

    bool handleButtonClick(ButtonId id) {
        return false;
    }

    bool handleButtonHover(ButtonId id) {
        return false;
    }
};

} // namespace ks

#endif // KS_KS_SHOWROOM_H
#ifndef KS_KS_TELEMETRY_H
#define KS_KS_TELEMETRY_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QMap>
#include <QPair>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QIODevice>

#include "sdk/SDKBackend.h"


namespace ks {

struct KsTelemetryFrame {
    qint64 timestamp;

    float position[3];
    float velocity[3];
    float acceleration[3];
    float rotation[3];
    float angularVelocity[3];

    float speedKmh;
    float speedMs;
    float heading;
    float pitch;
    float roll;

    float throttle;
    float brake;
    float steeringWheel;
    float steeringAckerman;

    int gear;
    float rpm;
    float engineTorque;

    float oilTemp;
    float waterTemp;
    float oilPressure;
    float boost;

    float fuelLevel;
    float fuelPerSec;

    float tyreTemp[4];
    float tyrePressure[4];
    float tyreWear[4];
    float slipAngle[4];
    float slipRatio[4];
    float normalForce[4];

    float brakeTemp[4];
    float brakePressure[4];

    float drs;
    float abs;
    float tc;
    float esc;

    float lapDistance;
    int currentWaypoint;

    KsTelemetryFrame() : timestamp(0), speedKmh(0), speedMs(0),
        heading(0), pitch(0), roll(0), throttle(0), brake(0),
        steeringWheel(0), gear(0), rpm(0), oilTemp(90), waterTemp(90),
        oilPressure(3), boost(0), fuelLevel(50), drs(0),
        abs(0), tc(0), esc(0), lapDistance(0), currentWaypoint(0) {
        for (int i = 0; i < 4; i++) {
            tyreTemp[i] = 90;
            tyrePressure[i] = 32;
            tyreWear[i] = 0;
            slipAngle[i] = 0;
            slipRatio[i] = 0;
            normalForce[i] = 2500;
            brakeTemp[i] = 200;
            brakePressure[i] = 0;
        }
    }
};

class KsTelemetryBuffer {
public:
    QList<KsTelemetryFrame> frames;
    QString carId;
    QString trackId;
    QString layoutId;
    qint64 startTime;
    qint64 endTime;

    int lapStart;
    int lapEnd;
    int lapCount;

    float bestLapTime;
    int bestLapIndex;
    float lastLapTime;
    int lastLapIndex;

    KsTelemetryBuffer()
        : startTime(0), endTime(0), lapStart(0), lapEnd(0),
          lapCount(0), bestLapTime(0), bestLapIndex(-1),
          lastLapTime(0), lastLapIndex(-1)
    {}

    void clear() {
        frames.clear();
        lapCount = 0;
        bestLapTime = lastLapTime = 0;
        bestLapIndex = lastLapIndex = -1;
    }

    bool isEmpty() const {
        return frames.isEmpty();
    }

    int size() const {
        return frames.size();
    }

    float getDuration() const {
        return (endTime - startTime) / 1000.0f;
    }

    float getFrameRate() const {
        if (frames.size() < 2) return 0;
        return frames.size() / getDuration();
    }

    KsTelemetryFrame* getFrame(int index) {
        if (index >= 0 && index < frames.size()) {
            return &frames[index];
        }
        return nullptr;
    }

    const KsTelemetryFrame* getFrame(int index) const {
        if (index >= 0 && index < frames.size()) {
            return &frames[index];
        }
        return nullptr;
    }

    void setMetadata(const QString& car, const QString& track, const QString& layout) {
        carId = car;
        trackId = track;
        layoutId = layout;
        startTime = QDateTime::currentMSecsSinceEpoch();
    }

    void addFrame(const KsTelemetryFrame& frame) {
        frames.append(frame);
        endTime = frame.timestamp;
    }

    int findLap(int startWaypoint, int lapCount) const {
        int lapStart = -1;

        for (int i = 0; i < frames.size() - 1; i++) {
            int wp = frames[i].currentWaypoint;
            float lastWp = frames[i + 1].currentWaypoint;

            if (wp == startWaypoint && lastWp != startWaypoint) {
                lapStart = i;
                break;
            }
        }

        return lapStart;
    }

    void calculateLaps(int startWaypoint, int finishWaypoint) {
        lapCount = 0;
        int lapStart = -1;

        for (int i = 0; i < frames.size() - 1; i++) {
            int wp = frames[i].currentWaypoint;

            if (wp == startWaypoint && lapStart < 0) {
                lapStart = i;
            } else if (wp == finishWaypoint && lapStart >= 0) {
                lapCount++;
                float lapTime = (frames[i].timestamp - frames[lapStart].timestamp) / 1000.0f;

                if (bestLapTime <= 0 || lapTime < bestLapTime) {
                    bestLapTime = lapTime;
                    bestLapIndex = lapStart;
                }

                lastLapTime = lapTime;
                lastLapIndex = lapStart;

                lapStart = -1;
            }
        }
    }

    float getSpeedAt(int index) const {
        if (index >= 0 && index < frames.size()) {
            return frames[index].speedKmh;
        }
        return 0;
    }

    float getRpmAt(int index) const {
        if (index >= 0 && index < frames.size()) {
            return frames[index].rpm;
        }
        return 0;
    }

    float getThrottleAt(int index) const {
        if (index >= 0 && index < frames.size()) {
            return frames[index].throttle;
        }
        return 0;
    }

    float getBrakeAt(int index) const {
        if (index >= 0 && index < frames.size()) {
            return frames[index].brake;
        }
        return 0;
    }

    float getMinSpeed() const {
        float minSpeed = 1e9f;
        for (const auto& f : frames) {
            if (f.speedKmh < minSpeed && f.speedKmh > 0) {
                minSpeed = f.speedKmh;
            }
        }
        return minSpeed == 1e9f ? 0 : minSpeed;
    }

    float getMaxSpeed() const {
        float maxSpeed = 0;
        for (const auto& f : frames) {
            if (f.speedKmh > maxSpeed) {
                maxSpeed = f.speedKmh;
            }
        }
        return maxSpeed;
    }

    float getMaxRpm() const {
        float maxRpm = 0;
        for (const auto& f : frames) {
            if (f.rpm > maxRpm) {
                maxRpm = f.rpm;
            }
        }
        return maxRpm;
    }

    float getAvgThrottle() const {
        float sum = 0;
        int count = 0;
        for (const auto& f : frames) {
            sum += f.throttle;
            count++;
        }
        return count > 0 ? sum / count : 0;
    }

    float getAvgBrake() const {
        float sum = 0;
        int count = 0;
        for (const auto& f : frames) {
            sum += f.brake;
            count++;
        }
        return count > 0 ? sum / count : 0;
    }

    float getFuelUsed() const {
        if (frames.isEmpty()) return 0;
        return frames.first().fuelLevel - frames.last().fuelLevel;
    }
};

struct KsLapAnalysis {
    float lapTime;
    float maxSpeed;
    float avgSpeed;
    float maxRpm;
    float maxThrottle;
    float maxBrake;
    float fuelUsed;
    float tyreWear[4];

    int cornerCount;
    float maxLateralG;
    float maxLongitudinalG;
    float avgLateralG;
    float avgLongitudinalG;

    float sectorTimes[3];
    int sectorWaypoints[3];

    int gearChanges;
    float shiftTime;
    float shiftQuality;

    KsLapAnalysis() : lapTime(0), maxSpeed(0), avgSpeed(0), maxRpm(0),
        maxThrottle(0), maxBrake(0), fuelUsed(0), cornerCount(0),
        maxLateralG(0), maxLongitudinalG(0), avgLateralG(0),
        avgLongitudinalG(0), gearChanges(0), shiftTime(0), shiftQuality(0) {
        for (int i = 0; i < 4; i++) tyreWear[i] = 0;
        for (int i = 0; i < 3; i++) {
            sectorTimes[i] = 0;
            sectorWaypoints[i] = 0;
        }
    }
};

class KsTelemetryAnalyzer {
public:
    static KsLapAnalysis analyze(const KsTelemetryBuffer& buffer, int startIndex, int endIndex) {
        KsLapAnalysis analysis;

        if (startIndex < 0 || endIndex >= buffer.size() || startIndex >= endIndex) {
            return analysis;
        }

        analysis.lapTime = (buffer.frames[endIndex].timestamp -
                          buffer.frames[startIndex].timestamp) / 1000.0f;

        float totalSpeed = 0;
        int speedCount = 0;

        for (int i = startIndex; i < endIndex; i++) {
            const KsTelemetryFrame& f = buffer.frames[i];

            if (f.speedKmh > analysis.maxSpeed) {
                analysis.maxSpeed = f.speedKmh;
            }
            totalSpeed += f.speedKmh;
            speedCount++;

            if (f.rpm > analysis.maxRpm) {
                analysis.maxRpm = f.rpm;
            }

            if (f.throttle > analysis.maxThrottle) {
                analysis.maxThrottle = f.throttle;
            }

            if (f.brake > analysis.maxBrake) {
                analysis.maxBrake = f.brake;
            }
        }

        analysis.avgSpeed = speedCount > 0 ? totalSpeed / speedCount : 0;
        analysis.fuelUsed = buffer.frames[startIndex].fuelLevel -
                         buffer.frames[endIndex].fuelLevel;

        for (int i = 0; i < 4; i++) {
            analysis.tyreWear[i] = buffer.frames[endIndex].tyreWear[i] -
                                 buffer.frames[startIndex].tyreWear[i];
        }

        return analysis;
    }

    static KsLapAnalysis analyzeLap(const KsTelemetryBuffer& buffer, int lapIndex) {
        int start = buffer.findLap(0, 0);
        int end = buffer.size() - 1;
        return analyze(buffer, start, end);
    }

    static QList<float> getSpeedTrace(const KsTelemetryBuffer& buffer, int start, int end) {
        QList<float> trace;

        if (start < 0 || end >= buffer.size()) {
            start = 0;
            end = buffer.size() - 1;
        }

        for (int i = start; i <= end; i++) {
            trace.append(buffer.frames[i].speedKmh);
        }

        return trace;
    }

    static QList<float> getRpmTrace(const KsTelemetryBuffer& buffer, int start, int end) {
        QList<float> trace;

        if (start < 0 || end >= buffer.size()) {
            start = 0;
            end = buffer.size() - 1;
        }

        for (int i = start; i <= end; i++) {
            trace.append(buffer.frames[i].rpm);
        }

        return trace;
    }

    static QMap<int, float> getCornerSpeeds(const KsTelemetryBuffer& buffer, int waypointStart) {
        QMap<int, float> corners;

        if (buffer.size() < 2) return corners;

        int minWaypoint = waypointStart;
        int maxWaypoint = waypointStart;

        for (const auto& f : buffer.frames) {
            if (f.currentWaypoint < minWaypoint) minWaypoint = f.currentWaypoint;
            if (f.currentWaypoint > maxWaypoint) maxWaypoint = f.currentWaypoint;
        }

        for (int wp = minWaypoint; wp <= maxWaypoint; wp++) {
            float minSpeed = 1e9f;
            float maxSpeed = 0;

            for (const auto& f : buffer.frames) {
                if (f.currentWaypoint == wp) {
                    if (f.speedKmh < minSpeed) minSpeed = f.speedKmh;
                    if (f.speedKmh > maxSpeed) maxSpeed = f.speedKmh;
                }
            }

            if (minSpeed < 1e9f) {
                corners[wp] = maxSpeed > minSpeed ? maxSpeed : minSpeed;
            }
        }

        return corners;
    }

    static float calculateGForce(float speed, float radius) {
        if (radius <= 0 || speed <= 0) return 0;
        float speedMs = speed / 3.6f;
        return (speedMs * speedMs) / (radius * 9.81f);
    }

    static QList<float> getGForceTrace(const KsTelemetryBuffer& buffer, const QList<float>& radii) {
        QList<float> gForces;

        int wp = 0;
        for (const auto& f : buffer.frames) {
            float radius = wp < radii.size() ? radii[wp] : 0;
            gForces.append(calculateGForce(f.speedKmh, radius));
            if (f.currentWaypoint != buffer.frames[wp].currentWaypoint) {
                wp++;
            }
        }

        return gForces;
    }

    static void detectGearChanges(const KsTelemetryBuffer& buffer, QList<QPair<int, int>>& changes) {
        changes.clear();

        for (int i = 1; i < buffer.size(); i++) {
            int gear = buffer.frames[i].gear;
            int prevGear = buffer.frames[i - 1].gear;

            if (gear != prevGear && gear > 0 && prevGear > 0) {
                changes.append(std::make_pair(i, gear > prevGear ? gear : -gear));
            }
        }
    }

    static void detectBrakingZones(const KsTelemetryBuffer& buffer, QList<QPair<int, int>>& zones) {
        zones.clear();

        int start = -1;
        for (int i = 1; i < buffer.size(); i++) {
            float brake = buffer.frames[i].brake;
            float prevBrake = buffer.frames[i - 1].brake;

            if (brake > 0.5f && prevBrake <= 0.5f) {
                start = i;
            } else if ((brake <= 0.1f || i == buffer.size() - 1) && start >= 0) {
                zones.append(std::make_pair(start, i));
                start = -1;
            }
        }
    }

    static void detectThrottleZones(const KsTelemetryBuffer& buffer, QList<QPair<int, int>>& zones) {
        zones.clear();

        int start = -1;
        for (int i = 1; i < buffer.size(); i++) {
            float throttle = buffer.frames[i].throttle;
            float prevThrottle = buffer.frames[i - 1].throttle;

            if (throttle > 0.9f && prevThrottle <= 0.9f) {
                start = i;
            } else if ((throttle <= 0.1f || i == buffer.size() - 1) && start >= 0) {
                zones.append(std::make_pair(start, i));
                start = -1;
            }
        }
    }
};

class KsTelemetryRecorder {
public:
    KsTelemetryBuffer buffer;
    bool isRecording;
    int maxFrames;

    KsTelemetryRecorder() : isRecording(false), maxFrames(100000) {}

    void startRecording(const QString& car, const QString& track, const QString& layout, int max = 100000) {
        buffer.clear();
        buffer.setMetadata(car, track, layout);
        maxFrames = max;
        isRecording = true;
    }

    void stopRecording() {
        isRecording = false;
    }

    bool record(const KsTelemetryFrame& frame) {
        if (!isRecording) return false;
        if (buffer.size() >= maxFrames) return false;

        buffer.addFrame(frame);
        return true;
    }

    bool saveToFile(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }

        QTextStream out(&file);
        out << "# Assetto Corsa Telemetry Data\n";
        out << "# Car: " << buffer.carId << "\n";
        out << "# Track: " << buffer.trackId << "\n";
        out << "# Layout: " << buffer.layoutId << "\n";
        out << "# Start: " << buffer.startTime << "\n";
        out << "# End: " << buffer.endTime << "\n";
        out << "# Frames: " << buffer.size() << "\n";
        out << "#\n";
        out << "# Timestamp,Speed,RPM,Throttle,Brake,Gear,SlipFL,SlipFR,SlipRL,SlipRR\n";

        for (const auto& f : buffer.frames) {
            out << f.timestamp << ","
               << f.speedKmh << ","
               << f.rpm << ","
               << f.throttle << ","
               << f.brake << ","
               << f.gear << ","
               << f.slipAngle[0] << ","
               << f.slipAngle[1] << ","
               << f.slipAngle[2] << ","
               << f.slipAngle[3] << "\n";
        }

        file.close();
        return true;
    }

    bool loadFromFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }

        buffer.clear();

        QTextStream in(&file);
        QString line;

        while (!in.atEnd()) {
            line = in.readLine();

            if (line.startsWith("# Car:")) {
                buffer.carId = line.mid(6).trimmed();
            } else if (line.startsWith("# Track:")) {
                buffer.trackId = line.mid(8).trimmed();
            } else if (line.startsWith("# Layout:")) {
                buffer.layoutId = line.mid(10).trimmed();
            } else if (!line.startsWith("#") && !line.startsWith("Timestamp,")) {
                QStringList parts = line.split(",");
                if (parts.size() >= 6) {
                    KsTelemetryFrame frame;
                    frame.timestamp = parts[0].toLongLong();
                    frame.speedKmh = parts[1].toFloat();
                    frame.rpm = parts[2].toFloat();
                    frame.throttle = parts[3].toFloat();
                    frame.brake = parts[4].toFloat();
                    frame.gear = parts[5].toInt();

                    buffer.addFrame(frame);
                }
            }
        }

        file.close();
        return true;
    }

    QStringList exportToCSV(const QString& path) const {
        QStringList errors;

        if (!saveToFile(path)) {
            errors.append("Failed to save telemetry to: " + path);
        }

        return errors;
    }
};

class KsLiveTelemetry {
public:
    static KsTelemetryFrame captureCurrentFrame() {
        KsTelemetryFrame frame;
        frame.timestamp = QDateTime::currentMSecsSinceEpoch();

        return frame;
    }

    static void updatePhysics(KsTelemetryFrame& frame) {
    }

    static void updateCar(KsTelemetryFrame& frame) {
    }

    static void updateTrack(KsTelemetryFrame& frame) {
    }
};

}

}

#endif

#ifndef KS_KS_UI_H
#define KS_KS_UI_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QRect>
#include <QRectF>

#if QT_VERSION >= 0x060000
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QImage>
#else
#include <QColor>
#include <QFont>
#include <QImage>
#endif

#ifdef QT_WIDGETS_LIB
#include <QWidget>
#endif



namespace ks {

enum KsUIEvent {
    UI_None = 0,
    UI_MouseDown = 1,
    UI_MouseUp = 2,
    UI_MouseMove = 3,
    UI_MouseWheel = 4,
    UI_KeyDown = 5,
    UI_KeyUp = 6,
    UI_GainFocus = 7,
    UI_LoseFocus = 8
};

enum KsKeyCode {
    Key_Escape = 0,
    Key_1 = 1, Key_2 = 2, Key_3 = 3, Key_4 = 4, Key_5 = 5,
    Key_A = 10, Key_B = 11, Key_C = 12,
    Key_Return = 20, Key_Space = 21, Key_Tab = 22,
    Key_Shift = 30, Key_Control = 31, Key_Alt = 32,
    Key_Left = 40, Key_Right = 41, Key_Up = 42, Key_Down = 43,
    Key_F1 = 50, Key_F2 = 51, Key_F3 = 52, Key_F4 = 53
};

enum KsWidgetType {
    Widget_None = 0,
    Widget_Window = 1,
    Widget_Button = 2,
    Widget_Label = 3,
    Widget_Image = 4,
    Widget_List = 5,
    Widget_Slider = 6,
    Widget_TextField = 7,
    Widget_Checkbox = 8,
    Widget_RadioButton = 9,
    Widget_ComboBox = 10,
    Widget_Tab = 11,
    Widget_ListBox = 12,
    Widget_ListBoxItem = 13,
    Widget_Timer = 14,
    Widget_ProgressBar = 15
};

enum KsAnchor {
    Anchor_Left = 1,
    Anchor_Right = 2,
    Anchor_Top = 4,
    Anchor_Bottom = 8,
    Anchor_Center = 16
};

struct KsFontDef {
    QString fontName;
    int size;
    int weight;
    bool italic;
    bool underline;
    bool bold;

    KsFontDef() : size(16), weight(400), italic(false), underline(false), bold(false) {}
};

struct KsWidgetStyle {
    QColor backgroundColor;
    QColor foregroundColor;
    QColor borderColor;
    QColor disabledColor;
    QColor highlightColor;
    QColor selectionColor;

    int borderWidth;
    int margin;
    int padding;
    int spacing;

    KsFontDef font;

    int alignment;

    KsWidgetStyle()
        : borderWidth(1), margin(0), padding(4), spacing(2), alignment(0) {
        backgroundColor = QColor(50, 50, 50);
        foregroundColor = QColor(255, 255, 255);
        borderColor = QColor(100, 100, 100);
        disabledColor = QColor(128, 128, 128);
        highlightColor = QColor(0, 120, 215);
        selectionColor = QColor(0, 100, 200);
    }
};

class KsWidget {
public:
    int id;
    int type;

    QString name;
    QString text;

    float x, y;
    float width, height;

    int anchor;

    bool visible;
    bool enabled;
    bool focused;
    bool hovered;

    bool isMouseOver;
    bool isPressed;
    bool isChecked;

    KsWidgetStyle style;

    QMap<QString, QString> attributes;

    QList<KsWidget*> children;

    KsWidget* parent;

    QWidget* qtWidget;

    KsWidget() : id(0), type(Widget_None), x(0), y(0), width(100), height(30)
             , anchor(Anchor_Left | Anchor_Top)
             , visible(true), enabled(true), focused(false), hovered(false)
             , isMouseOver(false), isPressed(false), isChecked(false), parent(nullptr), qtWidget(nullptr) {}

    virtual ~KsWidget() {
        for (auto child : children) {
            delete child;
        }
    }

    void setPosition(float px, float py) {
        x = px;
        y = py;
    }

    void setSize(float w, float h) {
        width = w;
        height = h;
    }

    void setGeometry(float px, float py, float w, float h) {
        x = px; y = py; width = w; height = h;
    }

    QRectF getGeometry() const {
        return QRectF(x, y, width, height);
    }

    void show() { visible = true; }
    void hide() { visible = false; }

    void enable() { enabled = true; }
    void disable() { enabled = false; }

    void setFocus() { focused = true; }
    void clearFocus() { focused = false; }

    virtual void update() {}
    virtual void repaint() {}

    virtual bool handleEvent(KsUIEvent event, int param1, int param2) {
        Q_UNUSED(event); Q_UNUSED(param1); Q_UNUSED(param2);
        return false;
    }

    bool contains(float px, float py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    KsWidget* findChild(const QString& childName) {
        for (auto child : children) {
            if (child->name == childName) return child;
            KsWidget* found = child->findChild(childName);
            if (found) return found;
        }
        return nullptr;
    }

    void addChild(KsWidget* child) {
        child->parent = this;
        children.append(child);
    }

    void removeChild(KsWidget* child) {
        children.removeOne(child);
        child->parent = nullptr;
    }
};

class KsLabel : public KsWidget {
public:
    QString labelText;
    int alignment;

    bool wordWrap;
    bool autoSize;

    KsLabel() {
        type = Widget_Label;
        alignment = 0;
        wordWrap = false;
        autoSize = true;
    }

    void setText(const QString& text) {
        labelText = text;
    }

    QString getText() const {
        return labelText;
    }
};

class KsButton : public KsWidget {
public:
    enum ButtonStyle {
        Style_Normal = 0,
        Style_Toggle = 1,
        Style_Check = 2,
        Style_Radio = 3,
        Style_Image = 4
    };

    QString buttonText;
    QString command;

    int buttonStyle;

    QImage upImage;
    QImage downImage;
    QImage hoverImage;
    QImage disabledImage;

    bool toggle;
    bool isDown;

    QList<QString> shortcuts;

    KsButton() {
        type = Widget_Button;
        buttonStyle = Style_Normal;
        toggle = false;
        isDown = false;
    }

    void setText(const QString& text) {
        buttonText = text;
    }

    void press() {
        if (toggle) {
            isDown = !isDown;
        }
    }

    void release() {
        if (!toggle) {
            isDown = false;
        }
    }

    bool handleEvent(KsUIEvent event, int param1, int param2) override {
        if (event == UI_MouseDown) {
            if (contains(param1, param2)) {
                press();
                return true;
            }
        } else if (event == UI_MouseUp) {
            release();
        }
        return false;
    }
};

class KsImage : public KsWidget {
public:
    QString imagePath;
    QImage image;

    int imageMode;

    float scaleX;
    float scaleY;

    bool preserveAspect;

    KsImage() {
        type = Widget_Image;
        imageMode = 0;
        scaleX = scaleY = 1.0f;
        preserveAspect = true;
    }

    void load(const QString& path) {
        imagePath = path;
        image.load(path);
    }

    void setImage(const QImage& img) {
        image = img;
    }

    void setScale(float sx, float sy) {
        scaleX = sx;
        scaleY = sy;
    }
};

class KsListBox : public KsWidget {
public:
    struct ListItem {
        QString text;
        int id;
        QImage icon;
        bool enabled;
        bool selected;

        ListItem() : id(0), enabled(true), selected(false) {}
    };

    QList<ListItem> items;

    int selectedIndex;
    int mouseOverIndex;

    bool multiSelect;

    KsListBox() {
        type = Widget_ListBox;
        selectedIndex = -1;
        mouseOverIndex = -1;
        multiSelect = false;
    }

    void addItem(const QString& text, int id = -1) {
        ListItem item;
        item.text = text;
        item.id = id >= 0 ? id : items.size();
        items.append(item);
    }

    void insertItem(int index, const QString& text, int id = -1) {
        ListItem item;
        item.text = text;
        item.id = id >= 0 ? id : items.size();
        items.insert(index, item);
    }

    void removeItem(int index) {
        if (index >= 0 && index < items.size()) {
            items.removeAt(index);
        }
    }

    void clear() {
        items.clear();
        selectedIndex = -1;
    }

    int getItemCount() const {
        return items.size();
    }

    int getSelectedIndex() const {
        return selectedIndex;
    }

    void setSelectedIndex(int index) {
        if (index >= 0 && index < items.size()) {
            selectedIndex = index;
            items[index].selected = true;
        }
    }

    QString getSelectedText() const {
        if (selectedIndex >= 0 && selectedIndex < items.size()) {
            return items[selectedIndex].text;
        }
        return QString();
    }

    int getSelectedId() const {
        if (selectedIndex >= 0 && selectedIndex < items.size()) {
            return items[selectedIndex].id;
        }
        return -1;
    }

    bool handleEvent(KsUIEvent event, int param1, int param2) override {
        if (event == UI_MouseMove) {
            mouseOverIndex = getItemAt(param1, param2);
            isMouseOver = mouseOverIndex >= 0;
        } else if (event == UI_MouseDown) {
            int clickedIndex = getItemAt(param1, param2);
            if (clickedIndex >= 0) {
                setSelectedIndex(clickedIndex);
                return true;
            }
        }
        return false;
    }

    int getItemAt(float mx, float my) const {
        float itemHeight = height / qMax(1, qMin(items.size(), 10));
        for (int i = 0; i < qMin(items.size(), 10); i++) {
            if (my >= y + i * itemHeight && my < y + (i + 1) * itemHeight) {
                return i;
            }
        }
        return -1;
    }
};

class KsComboBox : public KsWidget {
public:
    QList<QString> items;
    int selectedIndex;

    bool isOpen;

    KsComboBox() {
        type = Widget_ComboBox;
        selectedIndex = -1;
        isOpen = false;
    }

    void addItem(const QString& text) {
        items.append(text);
    }

    void removeItem(int index) {
        if (index >= 0 && index < items.size()) {
            items.removeAt(index);
        }
    }

    void setCurrentIndex(int index) {
        if (index >= 0 && index < items.size()) {
            selectedIndex = index;
        }
    }

    QString currentText() const {
        if (selectedIndex >= 0 && selectedIndex < items.size()) {
            return items[selectedIndex];
        }
        return QString();
    }
};

class KsSlider : public KsWidget {
public:
    enum Orientation {
        Orient_Horizontal = 0,
        Orient_Vertical = 1
    };

    float minimum;
    float maximum;
    float value;
    float step;

    int orientation;

    bool showValue;
    bool logarithmic;

    float thumbWidth;

    KsSlider() {
        type = Widget_Slider;
        minimum = 0;
        maximum = 100;
        value = 50;
        step = 1;
        orientation = Orient_Horizontal;
        showValue = true;
        logarithmic = false;
        thumbWidth = 20;
    }

    void setRange(float min, float max) {
        minimum = min;
        maximum = max;
    }

    void setValue(float val) {
        value = qBound(minimum, val, maximum);
    }

    float getValue() const {
        return value;
    }

    void setPosition(float pos) {
        float range = maximum - minimum;
        value = minimum + pos * range;
    }

    float getNormalizedPosition() const {
        float range = maximum - minimum;
        if (range <= 0) return 0;
        return (value - minimum) / range;
    }

    bool handleEvent(KsUIEvent event, int param1, int param2) override {
        if (!contains(param1, param2)) return false;

        if (event == UI_MouseDown) {
            isPressed = true;
            return true;
        } else if (event == UI_MouseMove && isPressed) {
            float pos;
            if (orientation == Orient_Horizontal) {
                pos = float(param1 - x) / width;
            } else {
                pos = float(param2 - y) / height;
            }
            setPosition(pos);
            update();
            return true;
        } else if (event == UI_MouseUp) {
            isPressed = false;
        }

        return false;
    }
};

class KsProgressBar : public KsWidget {
public:
    float minimum;
    float maximum;
    float value;

    bool showText;
    bool vertical;

    QColor fillColor;
    QColor backgroundColor;

    KsProgressBar() {
        type = Widget_ProgressBar;
        minimum = 0;
        maximum = 100;
        value = 0;
        showText = true;
        vertical = false;
        fillColor = QColor(0, 120, 215);
        backgroundColor = QColor(50, 50, 50);
    }

    void setRange(float min, float max) {
        minimum = min;
        maximum = max;
    }

    void setValue(float val) {
        value = qBound(minimum, val, maximum);
    }

    void increment(float delta = 1) {
        value = qMin(value + delta, maximum);
    }

    float getPercentage() const {
        float range = maximum - minimum;
        if (range <= 0) return 0;
        return (value - minimum) / range * 100;
    }
};

class KsTextField : public KsWidget {
public:
    QString text;
    QString placeholder;

    bool password;
    bool readOnly;
    bool multiLine;

    int maxLength;

    KsWidgetStyle textStyle;

    KsTextField() {
        type = Widget_TextField;
        password = false;
        readOnly = false;
        multiLine = false;
        maxLength = 0;
    }

    void setText(const QString& t) {
        if (maxLength > 0 && t.length() > maxLength) {
            text = t.left(maxLength);
        } else {
            text = t;
        }
    }

    QString getText() const {
        return text;
    }

    void clear() {
        text.clear();
    }

    bool handleEvent(KsUIEvent event, int param1, int param2) override {
        Q_UNUSED(param1); Q_UNUSED(param2);
        if (event == UI_GainFocus) {
            focused = true;
            return true;
        } else if (event == UI_LoseFocus) {
            focused = false;
        }
        return false;
    }
};

class KsDialog : public KsWidget {
public:
    enum DialogResult {
        Result_None = 0,
        Result_Accept = 1,
        Result_Reject = 2,
        Result_Close = 3
    };

    bool modal;
    bool resizable;
    bool closeable;

    bool hasTitleBar;
    bool hasBorder;

    float minWidth;
    float minHeight;
    float maxWidth;
    float maxHeight;

    int dialogResult;

    QList<KsWidget*> dialogItems;

    KsDialog() {
        type = Widget_Window;
        modal = true;
        resizable = true;
        closeable = true;
        hasTitleBar = true;
        hasBorder = true;
        minWidth = 200;
        minHeight = 150;
        maxWidth = 2000;
        maxHeight = 2000;
        dialogResult = Result_None;
    }

    void accept() {
        dialogResult = Result_Accept;
    }

    void reject() {
        dialogResult = Result_Reject;
    }

    void done(int result) {
        dialogResult = result;
    }

    int exec() {
        return dialogResult;
    }

    void open() {
        visible = true;
    }

    void close() {
        visible = false;
    }
};

class KsFormLayout {
public:
    struct FormRow {
        QString label;
        KsWidget* widget;
    };

    QList<FormRow> rows;

    int labelWidth;
    int spacing;
    int margin;

    KsFormLayout() : labelWidth(100), spacing(10), margin(10) {}

    void addRow(const QString& label, KsWidget* widget) {
        FormRow row;
        row.label = label;
        row.widget = widget;
        rows.append(row);
    }

    void insertRow(int index, const QString& label, KsWidget* widget) {
        FormRow row;
        row.label = label;
        row.widget = widget;
        rows.insert(index, row);
    }

    void removeRow(int index) {
        if (index >= 0 && index < rows.size()) {
            rows.removeAt(index);
        }
    }

    void clear() {
        rows.clear();
    }

    KsWidget* getWidget(int index) {
        if (index >= 0 && index < rows.size()) {
            return rows[index].widget;
        }
        return nullptr;
    }

    KsWidget* getWidget(const QString& label) {
        for (const auto& row : rows) {
            if (row.label == label) return row.widget;
        }
        return nullptr;
    }
};

class KsUIManager {
public:
    QList<KsWidget*> widgets;
    KsWidget* focusedWidget;
    KsWidget* hoveredWidget;

    QList<KsWidget*> focusChain;

    int nextId;

    KsWidgetStyle defaultStyle;

    float dpiScale;

    float screenWidth;
    float screenHeight;

    KsUIManager() : focusedWidget(nullptr), hoveredWidget(nullptr), nextId(0), dpiScale(1.0f), screenWidth(1920), screenHeight(1080) {}

    KsWidget* createWidget(int type) {
        KsWidget* widget = new KsWidget();
        widget->id = nextId++;
        widget->type = type;
        widget->style = defaultStyle;
        widgets.append(widget);
        return widget;
    }

    KsLabel* createLabel(const QString& text = "") {
        KsLabel* label = new KsLabel();
        label->id = nextId++;
        label->type = Widget_Label;
        label->labelText = text;
        label->style = defaultStyle;
        widgets.append(label);
        return label;
    }

    KsButton* createButton(const QString& text = "") {
        KsButton* button = new KsButton();
        button->id = nextId++;
        button->type = Widget_Button;
        button->buttonText = text;
        button->style = defaultStyle;
        widgets.append(button);
        return button;
    }

    KsImage* createImage() {
        KsImage* image = new KsImage();
        image->id = nextId++;
        image->type = Widget_Image;
        image->style = defaultStyle;
        widgets.append(image);
        return image;
    }

    KsListBox* createListBox() {
        KsListBox* list = new KsListBox();
        list->id = nextId++;
        list->type = Widget_ListBox;
        list->style = defaultStyle;
        widgets.append(list);
        return list;
    }

    KsComboBox* createComboBox() {
        KsComboBox* combo = new KsComboBox();
        combo->id = nextId++;
        combo->type = Widget_ComboBox;
        combo->style = defaultStyle;
        widgets.append(combo);
        return combo;
    }

    KsSlider* createSlider() {
        KsSlider* slider = new KsSlider();
        slider->id = nextId++;
        slider->type = Widget_Slider;
        slider->style = defaultStyle;
        widgets.append(slider);
        return slider;
    }

    KsProgressBar* createProgressBar() {
        KsProgressBar* bar = new KsProgressBar();
        bar->id = nextId++;
        bar->type = Widget_ProgressBar;
        bar->style = defaultStyle;
        widgets.append(bar);
        return bar;
    }

    KsTextField* createTextField() {
        KsTextField* field = new KsTextField();
        field->id = nextId++;
        field->type = Widget_TextField;
        field->style = defaultStyle;
        widgets.append(field);
        return field;
    }

    KsDialog* createDialog() {
        KsDialog* dialog = new KsDialog();
        dialog->id = nextId++;
        dialog->type = Widget_Window;
        dialog->style = defaultStyle;
        widgets.append(dialog);
        return dialog;
    }

    void removeWidget(KsWidget* widget) {
        widgets.removeOne(widget);
        delete widget;
    }

    void clear() {
        for (auto w : widgets) {
            delete w;
        }
        widgets.clear();
        focusedWidget = nullptr;
        hoveredWidget = nullptr;
    }

    KsWidget* getWidgetAt(float x, float y) {
        for (int i = widgets.size() - 1; i >= 0; i--) {
            if (widgets[i]->visible && widgets[i]->contains(x, y)) {
                return widgets[i];
            }
        }
        return nullptr;
    }

    bool handleMouseMove(int x, int y) {
        KsWidget* widget = getWidgetAt(x, y);
        if (widget != hoveredWidget) {
            if (hoveredWidget) {
                hoveredWidget->hovered = false;
                hoveredWidget->isMouseOver = false;
            }
            hoveredWidget = widget;
            if (hoveredWidget) {
                hoveredWidget->hovered = true;
                hoveredWidget->isMouseOver = true;
            }
        }

        if (focusedWidget) {
            focusedWidget->handleEvent(UI_MouseMove, x, y);
        }
        return widget != nullptr;
    }

    bool handleMouseDown(int x, int y) {
        KsWidget* widget = getWidgetAt(x, y);
        if (widget) {
            if (focusedWidget && focusedWidget != widget) {
                focusedWidget->clearFocus();
            }
            focusedWidget = widget;
            focusedWidget->setFocus();
            return widget->handleEvent(UI_MouseDown, x, y);
        }
        return false;
    }

    bool handleMouseUp(int x, int y) {
        if (focusedWidget) {
            focusedWidget->handleEvent(UI_MouseUp, x, y);
        }
        return true;
    }

    bool handleKeyDown(int keyCode) {
        if (focusedWidget) {
            focusedWidget->handleEvent(UI_KeyDown, keyCode, 0);
        }
        return focusedWidget != nullptr;
    }

    void update() {
        for (auto w : widgets) {
            if (w->visible) {
                w->update();
            }
        }
    }

    void render() {
        for (auto w : widgets) {
            if (w->visible) {
                w->repaint();
            }
        }
    }
};
}

}

#endif
#ifndef KS_KS_UTIL_H
#define KS_KS_UTIL_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QPoint>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QUrl>
#include <QDateTime>
#include <QFileInfo>

#if QT_VERSION >= 0x060000
#include <QtCore/QTimer>
#include <QtCore/QRandomGenerator>
#else
#include <QTimer>
#include <QDesktopServices>
#endif


#include "sdk/SDKBackend.h"

namespace ks {

class KsOfflineTools {
public:
    static bool launchKs(const QString& parameters = "") {
        QString ksPath = SDKBackend::getFolderPath(KsFolderID::Root) + "/acs.exe";
        return QProcess::startDetached(ksPath, parameters.split(" "));
    }

    static bool launchKsShowroom(const QString& carId = "") {
        QString showPath = SDKBackend::getFolderPath(KsFolderID::Root) + "/acShowroom.exe";
        if (!carId.isEmpty()) {
            showPath += " -car=" + carId;
        }
        return QProcess::startDetached(showPath);
    }

    static bool launchContentManager() {
        QString cmPath = SDKBackend::getFolderPath(KsFolderID::Root) + "/Content Manager.exe";
        return QProcess::startDetached(cmPath);
    }

    static bool openFolder(KsFolderID folder) {
        QString path = SDKBackend::getFolderPath(folder);
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }

    static bool openCarFolder(const QString& carId) {
        QString path = SDKBackend::getFolderPath(KsFolderID::ContentCars) + "/" + carId;
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }

    static bool openTrackFolder(const QString& trackId) {
        QString path = SDKBackend::getFolderPath(KsFolderID::ContentTracks) + "/" + trackId;
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }

    static bool openLogFolder() {
        return openFolder(KsFolderID::Logs);
    }

    static bool browseWeb(const QString& url) {
        return QDesktopServices::openUrl(QUrl(url));
    }
};

class KsCalculators {
public:
    static float calculateGearRatios(float finalDrive, const float gearRatios[7]) {
        return finalDrive * gearRatios[0];
    }

    static float calculateSpeed(float rpm, float finalDrive, float gearRatio, float wheelRadius) {
        return (rpm * wheelRadius * PI2) / (60 * finalDrive * gearRatio) * 3.6f;
    }

    static float calculateTorqueAtWheels(float engineTorque, float finalDrive, float efficiency) {
        return engineTorque * finalDrive * efficiency;
    }

    static float calculateCornerSpeed(float cornerRadius, float maxLateralG) {
        return sqrt(maxLateralG * cornerRadius * 9.81f) * 3.6f;
    }

    static float calculateBrakingDistance(float speed, float deceleration) {
        float speedMs = speed / 3.6f;
        return (speedMs * speedMs) / (2 * deceleration);
    }

    static float calculateStoppingDistance(float speed, float reactionTime, float deceleration) {
        float dist = (speed / 3.6f) * reactionTime;
        dist += calculateBrakingDistance(speed, deceleration);
        return dist;
    }

    static float calculateDownforce(float speed, float aoa, float area, float cl) {
        float speedMs = speed / 3.6f;
        return 0.5f * AIR_DENSITY * speedMs * speedMs * aoa * area * cl;
    }

    static float calculateDrag(float speed, float cd, float area) {
        float speedMs = speed / 3.6f;
        return 0.5f * AIR_DENSITY * speedMs * speedMs * cd * area;
    }

    static float calculateFuelConsumption(float power, float bsfc) {
        return power * bsfc * 0.0001f;
    }

    static float calculateLapsPerTank(float tankCapacity, float fuelPerLap) {
        return fuelPerLap > 0 ? tankCapacity / fuelPerLap : 0;
    }

    static float calculateRequiredFuel(float targetLaps, float fuelPerLap, float safetyMargin) {
        return targetLaps * fuelPerLap * (1 + safetyMargin);
    }
};

class KsCrashReporter {
public:
    static QStringList findCrashLogs() {
        QString logFolder = SDKBackend::getFolderPath(KsFolderID::Logs);
        QDir dir(logFolder);
        QStringList logs;

        if (dir.exists()) {
            logs = dir.entryList(QStringList() << "*.log", QDir::Files);
        }

        return logs;
    }

    static QStringList gatherLogFiles() {
        QStringList logs;
        QString logFolder = SDKBackend::getFolderPath(KsFolderID::Logs);
        QDir dir(logFolder);

        if (dir.exists()) {
            QStringList files = dir.entryList(QStringList() << "*.log", QDir::Files);
            for (const QString& f : files) {
                QString fullPath = logFolder + "/" + f;
                if (QFileInfo(fullPath).size() > 0) {
                    logs.append(fullPath);
                }
            }
        }

        return logs;
    }

    static bool hasRecentCrash(int hours = 24) {
        QDateTime now = QDateTime::currentDateTime();
        QStringList logs = findCrashLogs();

        for (const QString& l : logs) {
            if (l.contains("crash", Qt::CaseInsensitive) ||
                l.contains("error", Qt::CaseInsensitive)) {

                QString path = SDKBackend::getFolderPath(KsFolderID::Logs) + "/" + l;
                QFileInfo info(path);
                QDateTime modified = info.lastModified();

                if (modified.secsTo(now) < hours * 3600) {
                    return true;
                }
            }
        }

        return false;
    }
};

class KsAssetChecker {
public:
    struct CheckResult {
        QString item;
        QString severity;
        QString message;
    };

    static QList<CheckResult> checkCar(const QString& carId) {
        QList<CheckResult> results;

        QString carPath = SDKBackend::getFolderPath(KsFolderID::ContentCars) + "/" + carId;
        QDir carDir(carPath);

        if (!carDir.exists()) {
            results.append({"Folder", "Error", "Car folder not found"});
            return results;
        }

        QStringList models = carDir.entryList(QStringList() << "*.kn5", QDir::Files);
        if (models.isEmpty()) {
            results.append({"Models", "Warning", "No KN5 model files found"});
        }

        QString dataPath = carPath + "/data";
        QDir dataDir(dataPath);
        if (!dataDir.exists()) {
            results.append({"Data", "Warning", "No data folder found"});
        } else {
            if (!QFile(dataPath + "/car.ini").exists()) {
                results.append({"car.ini", "Error", "car.ini not found"});
            }
            if (!QFile(dataPath + "/tyres.ini").exists()) {
                results.append({"tyres.ini", "Warning", "tyres.ini not found"});
            }
        }

        return results;
    }

    static QList<CheckResult> checkTrack(const QString& trackId) {
        QList<CheckResult> results;

        QString trackPath = SDKBackend::getFolderPath(KsFolderID::ContentTracks) + "/" + trackId;
        QDir trackDir(trackPath);

        if (!trackDir.exists()) {
            results.append({"Folder", "Error", "Track folder not found"});
            return results;
        }

        QStringList models = trackDir.entryList(QStringList() << "*.kn5", QDir::Files);
        if (models.isEmpty()) {
            results.append({"Models", "Warning", "No KN5 model files found"});
        }

        return results;
    }
};

class KsFileWatcher {
public:
    QMap<QString, QDateTime> watchedFiles;
    int checkInterval;
    QTimer* timer;

    KsFileWatcher() : checkInterval(5000) {}

    void addPath(const QString& path) {
        QFileInfo info(path);
        if (info.exists()) {
            watchedFiles[path] = info.lastModified();
        }
    }

    QList<QString> checkChanges() {
        QList<QString> changed;

        for (auto it = watchedFiles.begin(); it != watchedFiles.end(); ++it) {
            QFileInfo info(it.key());
            if (info.exists() && info.lastModified() != it.value()) {
                changed.append(it.key());
                it.value() = info.lastModified();
            }
        }

        return changed;
    }
};

class KsBackupManager {
public:
    static bool createBackup(const QString& carId) {
        QString carPath = SDKBackend::getFolderPath(KsFolderID::ContentCars) + "/" + carId;
        QString backupPath = carPath + "_backup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

        QDir source(carPath);
        if (!source.exists()) return false;

        return source.rename(carPath, backupPath);
    }

    static bool restoreBackup(const QString& backupId) {
        return false;
    }

    static QStringList findBackups(const QString& carId) {
        QString carsPath = SDKBackend::getFolderPath(KsFolderID::ContentCars);
        QDir dir(carsPath);

        if (!dir.exists()) return QStringList();

        QStringList backups;
        QStringList entries = dir.entryList(QDir::Dirs);

        for (const QString& e : entries) {
            if (e.startsWith(carId + "_backup_")) {
                backups.append(e);
            }
        }

        return backups;
    }

    static bool deleteBackup(const QString& backupId) {
        QString carsPath = SDKBackend::getFolderPath(KsFolderID::ContentCars);
        QString path = carsPath + "/" + backupId;
        return QDir(path).removeRecursively();
    }
};

inline QSize calculatePreviewSize(int videoWidth, int videoHeight, int maxWidth, int maxHeight) {
    float aspect = float(videoWidth) / float(videoHeight);

    int newWidth = maxWidth;
    int newHeight = int(newWidth / aspect);

    if (newHeight > maxHeight) {
        newHeight = maxHeight;
        newWidth = int(newHeight * aspect);
    }

    return QSize(newWidth, newHeight);
}

inline QString formatFileSize(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}
}

}

#endif
#ifndef KS_KS_WEATHER_H
#define KS_KS_WEATHER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QRandomGenerator>

#include "sdk/SDKBackend.h"



namespace ks {

enum class KsWeatherCondition {
    Clear = 0,
    LightClouds = 1,
    Cloudy = 2,
    Overcast = 3,
    Fog = 10,
    LightRain = 20,
    Rain = 21,
    HeavyRain = 22,
    Drizzle = 23,
    LightSnow = 30,
    Snow = 31,
    HeavySnow = 32,
    Thunderstorm = 40,
    Sandstorm = 50
};

struct KsWeatherParams {
    KsWeatherCondition condition;

    float temperature;
    float humidity;
    float pressure;
    float windSpeed;
    float windDirection;

    float cloudCover;
    float cloudHeight;
    float rainIntensity;
    float thunderProbability;

    float fogDensity;
    float fogHeight;

    float solarAngle;
    float solarIntensity;
    float ambientIntensity;

    KsWeatherParams()
        : condition(KsWeatherCondition::Clear),
          temperature(20), humidity(50), pressure(1013),
          windSpeed(0), windDirection(0),
          cloudCover(0), cloudHeight(2000), rainIntensity(0), thunderProbability(0),
          fogDensity(0), fogHeight(0),
          solarAngle(30), solarIntensity(1), ambientIntensity(0.8)
    {}
};

class KsWeatherSystem {
public:
    static QString getWeatherFolder(KsWeatherCondition condition) {
        switch (condition) {
        case KsWeatherCondition::Clear: return "sol_00_clear";
        case KsWeatherCondition::LightClouds: return "sol_01_few_clouds";
        case KsWeatherCondition::Cloudy: return "sol_02_scattered_clouds";
        case KsWeatherCondition::Overcast: return "sol_03_broken_clouds";
        case KsWeatherCondition::Fog: return "sol_04_fog";
        case KsWeatherCondition::LightRain: return "sol_10_lightrain";
        case KsWeatherCondition::Rain: return "sol_11_rain";
        case KsWeatherCondition::HeavyRain: return "sol_12_heavyrain";
        case KsWeatherCondition::LightSnow: return "sol_20_lightsnow";
        case KsWeatherCondition::Snow: return "sol_21_snow";
        case KsWeatherCondition::Thunderstorm: return "sol_30_thunderstorm";
        default: return "sol_00_clear";
        }
    }

    static QStringList getAvailableWeathers() {
        QStringList weathers;
        weathers.append("sol_00_clear");
        weathers.append("sol_01_few_clouds");
        weathers.append("sol_02_scattered_clouds");
        weathers.append("sol_03_broken_clouds");
        weathers.append("sol_04_fog");
        weathers.append("sol_10_lightrain");
        weathers.append("sol_11_rain");
        weathers.append("sol_12_heavyrain");
        weathers.append("sol_20_lightsnow");
        weathers.append("sol_21_snow");
        weathers.append("sol_30_thunderstorm");
        return weathers;
    }

    static bool loadWeather(const QString& weatherId, KsWeatherParams& params) {
        QString path = QString(KS_SDK_PATH) + "/content/weather/" + weatherId + "/weather.ini";
        KsIniDocument doc;
        if (!doc.load(path)) return false;

        KsIniSection* weather = doc.section("WEATHER");
        if (!weather) return false;

        params.humidity = weather->getFloat("HUMIDITY", 50);
        params.pressure = weather->getFloat("PRESSURE", 1013);
        params.windSpeed = weather->getFloat("WIND", 0);

        KsIniSection* graphics = doc.section("GRAPHICS");
        if (graphics) {
            params.cloudCover = graphics->getFloat("CLOUD_COVER", 0);
            params.rainIntensity = graphics->getFloat("RAIN_INTENSITY", 0);
        }

        return true;
    }

    static bool generateRandomWeather(KsWeatherParams& params, int seed = -1) {
        if (seed < 0) {
            seed = QRandomGenerator::global()->bounded(0, 100);
        }

        QRandomGenerator gen(seed);

        int condRoll = gen.bounded(100);
        if (condRoll < 40) {
            params.condition = KsWeatherCondition::Clear;
        } else if (condRoll < 70) {
            params.condition = KsWeatherCondition::LightClouds;
        } else if (condRoll < 85) {
            params.condition = KsWeatherCondition::Cloudy;
        } else if (condRoll < 90) {
            params.condition = KsWeatherCondition::LightRain;
        } else if (condRoll < 95) {
            params.condition = KsWeatherCondition::Rain;
        } else {
            params.condition = KsWeatherCondition::Thunderstorm;
        }

        params.temperature = gen.bounded(5, 35);
        params.humidity = gen.bounded(20, 90);
        params.pressure = gen.bounded(980, 1040);
        params.windSpeed = gen.bounded(0, 30);

        if (params.windSpeed > 0) {
            params.windDirection = gen.bounded(0, 360);
        }

        return true;
    }

    static QString toString(KsWeatherCondition condition) {
        switch (condition) {
        case KsWeatherCondition::Clear: return "Clear";
        case KsWeatherCondition::LightClouds: return "Light Clouds";
        case KsWeatherCondition::Cloudy: return "Cloudy";
        case KsWeatherCondition::Overcast: return "Overcast";
        case KsWeatherCondition::Fog: return "Fog";
        case KsWeatherCondition::LightRain: return "Light Rain";
        case KsWeatherCondition::Rain: return "Rain";
        case KsWeatherCondition::HeavyRain: return "Heavy Rain";
        case KsWeatherCondition::Drizzle: return "Drizzle";
        case KsWeatherCondition::LightSnow: return "Light Snow";
        case KsWeatherCondition::Snow: return "Snow";
        case KsWeatherCondition::HeavySnow: return "Heavy Snow";
        case KsWeatherCondition::Thunderstorm: return "Thunderstorm";
        case KsWeatherCondition::Sandstorm: return "Sandstorm";
        default: return "Unknown";
        }
    }

    static KsWeatherCondition fromString(const QString& name) {
        QString n = name.toLower();
        if (n.contains("clear")) return KsWeatherCondition::Clear;
        if (n.contains("cloud") && n.contains("light")) return KsWeatherCondition::LightClouds;
        if (n.contains("cloud")) return KsWeatherCondition::Cloudy;
        if (n.contains("fog")) return KsWeatherCondition::Fog;
        if (n.contains("rain") && n.contains("light")) return KsWeatherCondition::LightRain;
        if (n.contains("rain") && n.contains("heavy")) return KsWeatherCondition::HeavyRain;
        if (n.contains("rain")) return KsWeatherCondition::Rain;
        if (n.contains("snow") && n.contains("light")) return KsWeatherCondition::LightSnow;
        if (n.contains("snow") && n.contains("heavy")) return KsWeatherCondition::HeavySnow;
        if (n.contains("snow")) return KsWeatherCondition::Snow;
        if (n.contains("thunder")) return KsWeatherCondition::Thunderstorm;
        return KsWeatherCondition::Clear;
    }
};

class KsWeatherTransition {
public:
    KsWeatherCondition from;
    KsWeatherCondition to;
    float duration;
    float elapsed;

    bool isTransitioning() const { return elapsed < duration; }
    float getProgress() const { return duration > 0 ? elapsed / duration : 1.0f; }

    void startTransition(KsWeatherCondition from, KsWeatherCondition to, float durationMinutes) {
        this->from = from;
        this->to = to;
        duration = durationMinutes * 60.0f;
        elapsed = 0;
    }

    void update(float dt) {
        if (isTransitioning()) {
            elapsed += dt;
        }
    }

    KsWeatherCondition getCurrent() const {
        float t = getProgress();
        if (t >= 1.0f) return to;

        if (t < 0.5f) {
            return from;
        }
        return to;
    }
};

class KsTrackWeather {
public:
    QString trackId;
    KsWeatherParams params;
    KsWeatherTransition transition;

    bool loadTrackWeather() {
        return loadWeatherForTrack(trackId, params);
    }

    static bool loadWeatherForTrack(const QString& trackId, KsWeatherParams& params) {
        QString trackPath = SDKBackend::getTrackPath(trackId);
        QString weatherIni = trackPath + "/weather.ini";

        KsIniDocument doc;
        if (!doc.load(weatherIni)) return false;

        return parseWeatherParams(doc, params);
    }

    static bool parseWeatherParams(const KsIniDocument& doc, KsWeatherParams& params) {
        KsIniSection* weather = doc.section("WEATHER");
        if (!weather) return false;

        params.temperature = weather->getFloat("TEMP", 20);
        params.humidity = weather->getFloat("HUMIDITY", 50);
        params.windSpeed = weather->getFloat("WIND_SPEED", 0);
        params.windDirection = weather->getFloat("WIND_DIR", 0);

        KsIniSection* graphics = doc.section("GRAPHICS");
        if (graphics) {
            params.cloudCover = graphics->getFloat("CLOUDCover", 0);
            params.rainIntensity = graphics->getFloat("RAIN_INTENSITY", 0);
            params.fogDensity = graphics->getFloat("FOG_DENSITY", 0);
        }

        return true;
    }

    void applyDynamicChanges(float dt) {
        if (transition.isTransitioning()) {
            transition.update(dt);
        } else {
            params.temperature += (QRandomGenerator::global()->generateFloat() - 0.5f) * 0.1f * dt;
            params.humidity += (QRandomGenerator::global()->generateFloat() - 0.5f) * 0.5f * dt;
            params.windSpeed += (QRandomGenerator::global()->generateFloat() - 0.5f) * 0.2f * dt;

            params.temperature = qBound(-10.0f, params.temperature, 45.0f);
            params.humidity = qBound(0.0f, params.humidity, 100.0f);
            params.windSpeed = qBound(0.0f, params.windSpeed, 50.0f);
        }
    }
};

class KsWeatherManager {
public:
    QMap<QString, KsWeatherParams> trackWeathers;
    QString currentWeather;
    bool dynamicWeather;

    KsWeatherManager() : dynamicWeather(false) {}

    void init() {
        QStringList weathers = KsWeatherSystem::getAvailableWeathers();
        for (const QString& w : weathers) {
            KsWeatherParams params;
            if (KsWeatherSystem::loadWeather(w, params)) {
                trackWeathers[w] = params;
            }
        }
    }

    QStringList getWeathers() const {
        return trackWeathers.keys();
    }

    const KsWeatherParams* getWeather(const QString& id) const {
        return trackWeathers.value(id);
    }

    bool setWeather(const QString& weatherId) {
        if (!trackWeathers.contains(weatherId)) return false;
        currentWeather = weatherId;
        return true;
    }

    void enableDynamic(bool enable) {
        dynamicWeather = enable;
    }

    QString predictWeatherNext(int hours) const {
        int seed = QDateTime::currentSecsSinceEpoch() / 3600 + hours;
        KsWeatherParams pred;
        KsWeatherSystem::generateRandomWeather(pred, seed);
        return KsWeatherSystem::toString(pred.condition);
    }
};

inline float calculateHeatIndex(float temp, float humidity) {
    float hi = 0.5f * (temp + 61.0f + (temp - 68.0f) * 1.2f + humidity * 0.094f);
    if (hi >= 80) {
        hi = -42.379f + 2.04901523f * temp + 10.14333127f * humidity
            - 0.22475541f * temp * humidity - 0.00683783f * temp * temp
            - 0.05481717f * humidity * humidity + 0.00122874f * temp * temp * humidity
            + 0.00085282f * temp * humidity * humidity - 0.00000199f * temp * temp * humidity * humidity;
    }
    return hi;
}

inline float calculateWindChill(float temp, float windSpeed) {
    if (temp > 10 || windSpeed < 1.3f) return temp;
    return 13.12f + 0.6215f * temp - 11.37f * pow(windSpeed, 0.16f) + 0.3965f * temp * pow(windSpeed, 0.16f);
}

inline float calculateDensityAltitude(float temp, float pressure, float dewpoint) {
    float rh = pow(113e-3, (1 + 0.00004f * pressure) * (dewpoint / (temp - 29)));
    float vp = 6.11f * rh * pow(10, (7.5f * dewpoint / (237.3 + dewpoint)));
    return (1 - pow(vp / pressure, 0.286f)) * 288.8f;
}

}

}

#endif

