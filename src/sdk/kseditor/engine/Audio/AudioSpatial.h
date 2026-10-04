#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QJsonObject>
#include <QMap>

namespace ks { namespace audio {

// ============================================================================
// Channel layouts
// ============================================================================

enum class ChannelLayout {
    Mono,           // 1.0
    Stereo,         // 2.0
    Stereo_1,       // 2.1
    Surround_5_1,   // 5.1
    Surround_7_1,   // 7.1
    Atmos_7_1_4     // 7.1.4 Dolby Atmos
};

enum class SpeakerPosition {
    FrontLeft, FrontCenter, FrontRight,
    SideLeft, SideRight,
    BackLeft, BackCenter, BackRight,
    LFE,
    TopFrontLeft, TopFrontRight,
    TopBackLeft, TopBackRight
};

struct SpeakerInfo {
    SpeakerPosition position;
    float azimuth = 0.0f;     // degrees, 0=front, positive=right
    float elevation = 0.0f;   // degrees, 0=ear level
    float distance = 1.0f;    // relative distance
    float volume = 1.0f;      // channel volume

    static SpeakerInfo forPosition(SpeakerPosition pos);
};

// ============================================================================
// 3D spatial audio source
// ============================================================================

struct SpatialSource {
    QString id;
    QString name;
    QVector3D position;
    QVector3D velocity;
    float volume = 1.0f;
    float spread = 0.0f;        // 0..1, how wide the source is
    float distanceFactor = 1.0f;
    float rolloffFactor = 1.0f;
    float innerAngle = 360.0f;  // cone inner angle in degrees
    float outerAngle = 360.0f;  // cone outer angle in degrees
    float outerGain = 0.0f;     // gain outside the cone
    bool isEmitting = true;

    // Doppler settings
    float dopplerFactor = 1.0f;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
};

// ============================================================================
// Listener (game camera / player position)
// ============================================================================

struct SpatialListener {
    QVector3D position;
    QVector3D forward{0, 0, 1};
    QVector3D up{0, 1, 0};
    QVector3D velocity;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
};

// ============================================================================
// Spatial bus — 5.1/7.1 bus authoring with panning, doppler, occlusion
// ============================================================================

struct SpatialBus {
    QString id;
    QString name;
    ChannelLayout layout = ChannelLayout::Stereo;
    QVector<SpeakerInfo> speakers;
    float volume = 1.0f;
    bool muted = false;

    // Panning law
    QString panningLaw = "equal_power";  // "equal_power", "linear", "none"
    float spread = 0.0f;  // 0=point source, 1=full spread

    // Occlusion
    bool occlusionEnabled = false;
    float occlusionFactor = 0.0f;   // 0=none, 1=fully occluded
    float occlusionLowPass = 5000.0f; // Hz, low-pass filter when occluded

    // Reverb send
    float reverbSend = 0.0f;  // 0..1

    static SpatialBus createStereo(const QString& name);
    static SpatialBus create5_1(const QString& name);
    static SpatialBus create7_1(const QString& name);

    void initializeSpeakers();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);
};

// ============================================================================
// Spatial surround system — manages sources, listener, buses
// ============================================================================

class SpatialSurroundSystem : public QObject {
    Q_OBJECT
public:
    explicit SpatialSurroundSystem(QObject* parent = nullptr);

    // Listener
    void setListener(const SpatialListener& listener);
    const SpatialListener& listener() const { return m_listener; }

    // Sources
    void addSource(const SpatialSource& source);
    void removeSource(const QString& sourceId);
    void updateSource(const QString& sourceId, const QVector3D& position);
    SpatialSource* getSource(const QString& sourceId);
    QVector<SpatialSource*> getAllSources() const;

    // Buses
    void addBus(const SpatialBus& bus);
    void removeBus(const QString& busId);
    SpatialBus* getBus(const QString& busId);
    QVector<SpatialBus*> getAllBuses() const;

    // Processing
    // Calculate per-speaker gains for a source given the listener position
    QMap<SpeakerPosition, float> calculatePanning(const SpatialSource& source,
                                                   const SpatialBus& bus) const;

    // Calculate doppler shift
    float calculateDoppler(const SpatialSource& source) const;

    // Calculate distance attenuation
    float calculateAttenuation(const SpatialSource& source) const;

    // Calculate occlusion effect
    float calculateOcclusion(const SpatialSource& source, const SpatialBus& bus) const;

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void sourceAdded(const QString& sourceId);
    void sourceRemoved(const QString& sourceId);
    void sourceMoved(const QString& sourceId, const QVector3D& position);
    void listenerChanged();
    void busAdded(const QString& busId);
    void busRemoved(const QString& busId);

private:
    SpatialListener m_listener;
    QVector<SpatialSource> m_sources;
    QVector<SpatialBus> m_buses;

    // Helper: convert azimuth to speaker gains using panning law
    void applyPanningLaw(float azimuth, float spread, const QVector<SpeakerInfo>& speakers,
                         QMap<SpeakerPosition, float>& gains) const;
};

}} // namespace ks::audio
