#ifndef KSCAR_AUDIOENGINE_H
#define KSCAR_AUDIOENGINE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include "sdk/kseditor/engine/Audio/AudioTypes.h"

namespace ks { namespace audio {

class KsCarAudioEngine : public QObject {
    Q_OBJECT

public:
    static KsCarAudioEngine* instance();

    bool initialize(const QString& acRootPath = QString());
    void shutdown();

    void loadCarSoundBank(const QString& carDirectory, const QString& carId);
    void setEngineParameters(float rpm, float throttle);
    void setTurbo(float boost);
    void setLimiter(float decay);
    void setCameraMode(bool external);
    void setDoorOpen(bool open);
    void setHorn(bool active);
    void setListenerPosition(float x, float y, float z, float fx, float fy, float fz, float ux, float uy, float uz);
    void setCarPosition(float x, float y, float z, float fx, float fy, float fz, float ux, float uy, float uz, float vx, float vy, float vz);
    void setEnginePosition(float x, float y, float z, float fx, float fy, float fz, float ux, float uy, float uz, float vx, float vy, float vz);
    void update();

    bool isInitialized() const { return m_initialized; }

signals:
    void initialized(bool success);
    void soundLoaded(const QString& name);

private:
    explicit KsCarAudioEngine(QObject* parent = nullptr);
    static KsCarAudioEngine* s_instance;

    bool m_initialized = false;
    QString m_acRoot;
    QString m_carDirectory;
    QString m_carId;
    bool m_engineExternal = false;

    // Engine state
    float m_rpm = 800.0f;
    float m_speed = 0.0f;
    float m_load = 0.0f;
    int m_gear = 0;
    float m_idleRpm = 800.0f;
    float m_maxRpm = 7500.0f;
    float m_carX = 0.0f, m_carY = 0.0f, m_carZ = 0.0f;

    audio::KSAudioEngine* m_engine = nullptr;

    int m_engineIntInstance = -1;
    int m_engineExtInstance = -1;
    int m_turboInstance = -1;
    int m_limiterInstance = -1;
    int m_doorInstance = -1;
    int m_hornInstance = -1;
    bool m_doorOpen = false;
    bool m_hornActive = false;

    void updateEngineSound();
    QString eventPath(const QString& eventName) const;
};

} } // namespace ks::audio

#endif // KSCAR_AUDIOENGINE_H
