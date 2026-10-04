#include "AudioSpatial.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QtMath>
#include <QDebug>

namespace ks { namespace audio {

// ============================================================================
// SpeakerInfo
// ============================================================================

SpeakerInfo SpeakerInfo::forPosition(SpeakerPosition pos)
{
    SpeakerInfo info;
    info.position = pos;

    switch (pos) {
    case SpeakerPosition::FrontLeft:      info.azimuth = -30.0f; info.elevation = 0.0f; break;
    case SpeakerPosition::FrontCenter:    info.azimuth = 0.0f;   info.elevation = 0.0f; break;
    case SpeakerPosition::FrontRight:     info.azimuth = 30.0f;  info.elevation = 0.0f; break;
    case SpeakerPosition::SideLeft:       info.azimuth = -90.0f; info.elevation = 0.0f; break;
    case SpeakerPosition::SideRight:      info.azimuth = 90.0f;  info.elevation = 0.0f; break;
    case SpeakerPosition::BackLeft:       info.azimuth = -150.0f; info.elevation = 0.0f; break;
    case SpeakerPosition::BackCenter:     info.azimuth = 180.0f; info.elevation = 0.0f; break;
    case SpeakerPosition::BackRight:      info.azimuth = 150.0f; info.elevation = 0.0f; break;
    case SpeakerPosition::LFE:            info.azimuth = 0.0f;   info.elevation = -90.0f; break;
    case SpeakerPosition::TopFrontLeft:   info.azimuth = -30.0f; info.elevation = 45.0f; break;
    case SpeakerPosition::TopFrontRight:  info.azimuth = 30.0f;  info.elevation = 45.0f; break;
    case SpeakerPosition::TopBackLeft:    info.azimuth = -150.0f; info.elevation = 45.0f; break;
    case SpeakerPosition::TopBackRight:   info.azimuth = 150.0f; info.elevation = 45.0f; break;
    }

    return info;
}

// ============================================================================
// SpatialSource
// ============================================================================

QJsonObject SpatialSource::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["x"] = static_cast<double>(position.x());
    obj["y"] = static_cast<double>(position.y());
    obj["z"] = static_cast<double>(position.z());
    obj["vx"] = static_cast<double>(velocity.x());
    obj["vy"] = static_cast<double>(velocity.y());
    obj["vz"] = static_cast<double>(velocity.z());
    obj["volume"] = static_cast<double>(volume);
    obj["spread"] = static_cast<double>(spread);
    obj["distanceFactor"] = static_cast<double>(distanceFactor);
    obj["rolloffFactor"] = static_cast<double>(rolloffFactor);
    obj["innerAngle"] = static_cast<double>(innerAngle);
    obj["outerAngle"] = static_cast<double>(outerAngle);
    obj["outerGain"] = static_cast<double>(outerGain);
    obj["dopplerFactor"] = static_cast<double>(dopplerFactor);
    obj["isEmitting"] = isEmitting;
    return obj;
}

void SpatialSource::fromJson(const QJsonObject& obj)
{
    id = obj["id"].toString();
    name = obj["name"].toString();
    position = QVector3D(static_cast<float>(obj["x"].toDouble()),
                         static_cast<float>(obj["y"].toDouble()),
                         static_cast<float>(obj["z"].toDouble()));
    velocity = QVector3D(static_cast<float>(obj["vx"].toDouble()),
                         static_cast<float>(obj["vy"].toDouble()),
                         static_cast<float>(obj["vz"].toDouble()));
    volume = static_cast<float>(obj["volume"].toDouble(1.0));
    spread = static_cast<float>(obj["spread"].toDouble(0.0));
    distanceFactor = static_cast<float>(obj["distanceFactor"].toDouble(1.0));
    rolloffFactor = static_cast<float>(obj["rolloffFactor"].toDouble(1.0));
    innerAngle = static_cast<float>(obj["innerAngle"].toDouble(360.0));
    outerAngle = static_cast<float>(obj["outerAngle"].toDouble(360.0));
    outerGain = static_cast<float>(obj["outerGain"].toDouble(0.0));
    dopplerFactor = static_cast<float>(obj["dopplerFactor"].toDouble(1.0));
    isEmitting = obj["isEmitting"].toBool(true);
}

// ============================================================================
// SpatialListener
// ============================================================================

QJsonObject SpatialListener::toJson() const
{
    QJsonObject obj;
    obj["px"] = static_cast<double>(position.x());
    obj["py"] = static_cast<double>(position.y());
    obj["pz"] = static_cast<double>(position.z());
    obj["fx"] = static_cast<double>(forward.x());
    obj["fy"] = static_cast<double>(forward.y());
    obj["fz"] = static_cast<double>(forward.z());
    obj["ux"] = static_cast<double>(up.x());
    obj["uy"] = static_cast<double>(up.y());
    obj["uz"] = static_cast<double>(up.z());
    obj["vx"] = static_cast<double>(velocity.x());
    obj["vy"] = static_cast<double>(velocity.y());
    obj["vz"] = static_cast<double>(velocity.z());
    return obj;
}

void SpatialListener::fromJson(const QJsonObject& obj)
{
    position = QVector3D(static_cast<float>(obj["px"].toDouble()),
                         static_cast<float>(obj["py"].toDouble()),
                         static_cast<float>(obj["pz"].toDouble()));
    forward = QVector3D(static_cast<float>(obj["fx"].toDouble()),
                        static_cast<float>(obj["fy"].toDouble()),
                        static_cast<float>(obj["fz"].toDouble()));
    up = QVector3D(static_cast<float>(obj["ux"].toDouble()),
                   static_cast<float>(obj["uy"].toDouble()),
                   static_cast<float>(obj["uz"].toDouble()));
    velocity = QVector3D(static_cast<float>(obj["vx"].toDouble()),
                         static_cast<float>(obj["vy"].toDouble()),
                         static_cast<float>(obj["vz"].toDouble()));
}

// ============================================================================
// SpatialBus
// ============================================================================

SpatialBus SpatialBus::createStereo(const QString& name)
{
    SpatialBus bus;
    bus.name = name;
    bus.layout = ChannelLayout::Stereo;
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontLeft));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontRight));
    return bus;
}

SpatialBus SpatialBus::create5_1(const QString& name)
{
    SpatialBus bus;
    bus.name = name;
    bus.layout = ChannelLayout::Surround_5_1;
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontLeft));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontCenter));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontRight));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::SideLeft));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::SideRight));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::LFE));
    return bus;
}

SpatialBus SpatialBus::create7_1(const QString& name)
{
    SpatialBus bus;
    bus.name = name;
    bus.layout = ChannelLayout::Surround_7_1;
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontLeft));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontCenter));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontRight));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::SideLeft));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::SideRight));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::BackLeft));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::BackRight));
    bus.speakers.append(SpeakerInfo::forPosition(SpeakerPosition::LFE));
    return bus;
}

void SpatialBus::initializeSpeakers()
{
    speakers.clear();
    switch (layout) {
    case ChannelLayout::Mono:
        speakers.append(SpeakerInfo::forPosition(SpeakerPosition::FrontCenter));
        break;
    case ChannelLayout::Stereo:
    case ChannelLayout::Stereo_1:
        *this = createStereo(name);
        break;
    case ChannelLayout::Surround_5_1:
        *this = create5_1(name);
        break;
    case ChannelLayout::Surround_7_1:
    case ChannelLayout::Atmos_7_1_4:
        *this = create7_1(name);
        break;
    }
}

QJsonObject SpatialBus::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["layout"] = static_cast<int>(layout);
    obj["volume"] = static_cast<double>(volume);
    obj["muted"] = muted;
    obj["panningLaw"] = panningLaw;
    obj["spread"] = static_cast<double>(spread);
    obj["occlusionEnabled"] = occlusionEnabled;
    obj["occlusionFactor"] = static_cast<double>(occlusionFactor);
    obj["occlusionLowPass"] = static_cast<double>(occlusionLowPass);
    obj["reverbSend"] = static_cast<double>(reverbSend);
    return obj;
}

void SpatialBus::fromJson(const QJsonObject& obj)
{
    id = obj["id"].toString();
    name = obj["name"].toString();
    layout = static_cast<ChannelLayout>(obj["layout"].toInt(static_cast<int>(ChannelLayout::Stereo)));
    volume = static_cast<float>(obj["volume"].toDouble(1.0));
    muted = obj["muted"].toBool(false);
    panningLaw = obj["panningLaw"].toString("equal_power");
    spread = static_cast<float>(obj["spread"].toDouble(0.0));
    occlusionEnabled = obj["occlusionEnabled"].toBool(false);
    occlusionFactor = static_cast<float>(obj["occlusionFactor"].toDouble(0.0));
    occlusionLowPass = static_cast<float>(obj["occlusionLowPass"].toDouble(5000.0));
    reverbSend = static_cast<float>(obj["reverbSend"].toDouble(0.0));
    initializeSpeakers();
}

// ============================================================================
// SpatialSurroundSystem
// ============================================================================

SpatialSurroundSystem::SpatialSurroundSystem(QObject* parent)
    : QObject(parent) {}

void SpatialSurroundSystem::setListener(const SpatialListener& listener)
{
    m_listener = listener;
    emit listenerChanged();
}

void SpatialSurroundSystem::addSource(const SpatialSource& source)
{
    m_sources.append(source);
    emit sourceAdded(source.id);
}

void SpatialSurroundSystem::removeSource(const QString& sourceId)
{
    for (int i = m_sources.size() - 1; i >= 0; --i) {
        if (m_sources[i].id == sourceId) {
            m_sources.removeAt(i);
            emit sourceRemoved(sourceId);
            return;
        }
    }
}

void SpatialSurroundSystem::updateSource(const QString& sourceId, const QVector3D& position)
{
    for (auto& source : m_sources) {
        if (source.id == sourceId) {
            source.position = position;
            emit sourceMoved(sourceId, position);
            return;
        }
    }
}

SpatialSource* SpatialSurroundSystem::getSource(const QString& sourceId)
{
    for (auto& source : m_sources) {
        if (source.id == sourceId) return &source;
    }
    return nullptr;
}

QVector<SpatialSource*> SpatialSurroundSystem::getAllSources() const
{
    QVector<SpatialSource*> result;
    for (auto& source : const_cast<QVector<SpatialSource>&>(m_sources)) {
        result.append(&source);
    }
    return result;
}

void SpatialSurroundSystem::addBus(const SpatialBus& bus)
{
    m_buses.append(bus);
    emit busAdded(bus.id);
}

void SpatialSurroundSystem::removeBus(const QString& busId)
{
    for (int i = m_buses.size() - 1; i >= 0; --i) {
        if (m_buses[i].id == busId) {
            m_buses.removeAt(i);
            emit busRemoved(busId);
            return;
        }
    }
}

SpatialBus* SpatialSurroundSystem::getBus(const QString& busId)
{
    for (auto& bus : m_buses) {
        if (bus.id == busId) return &bus;
    }
    return nullptr;
}

QVector<SpatialBus*> SpatialSurroundSystem::getAllBuses() const
{
    QVector<SpatialBus*> result;
    for (auto& bus : const_cast<QVector<SpatialBus>&>(m_buses)) {
        result.append(&bus);
    }
    return result;
}

QMap<SpeakerPosition, float> SpatialSurroundSystem::calculatePanning(
    const SpatialSource& source, const SpatialBus& bus) const
{
    QMap<SpeakerPosition, float> gains;

    // Calculate source direction relative to listener
    QVector3D toSource = source.position - m_listener.position;
    float distance = toSource.length();

    if (distance < 0.001f) {
        // Source is at listener position — equal power to all speakers
        for (const auto& speaker : bus.speakers) {
            gains[speaker.position] = 1.0f / qSqrt(static_cast<float>(bus.speakers.size()));
        }
        return gains;
    }

    // Normalize
    QVector3D dir = toSource.normalized();

    // Calculate azimuth (angle from front)
    float azimuth = qAtan2(dir.x(), dir.z()) * 180.0f / M_PI;

    // Apply panning
    applyPanningLaw(azimuth, source.spread, bus.speakers, gains);

    // Apply distance attenuation
    float attenuation = calculateAttenuation(source);
    for (auto it = gains.begin(); it != gains.end(); ++it) {
        it.value() *= attenuation * source.volume;
    }

    return gains;
}

float SpatialSurroundSystem::calculateDoppler(const SpatialSource& source) const
{
    QVector3D toSource = source.position - m_listener.position;
    float distance = toSource.length();
    if (distance < 0.001f) return 1.0f;

    QVector3D dir = toSource.normalized();

    // Relative velocity along the source-listener axis
    float sourceRadialVel = QVector3D::dotProduct(source.velocity, dir);
    float listenerRadialVel = QVector3D::dotProduct(m_listener.velocity, dir);

    float speedOfSound = 343.3f;  // m/s
    float factor = source.dopplerFactor;
    float velocityDiff = listenerRadialVel - sourceRadialVel;

    float doppler = speedOfSound / (speedOfSound - velocityDiff * factor);
    return qBound(0.1f, doppler, 10.0f);
}

float SpatialSurroundSystem::calculateAttenuation(const SpatialSource& source) const
{
    float distance = (source.position - m_listener.position).length() * source.distanceFactor;
    if (distance < 0.001f) return 1.0f;

    // Inverse distance with rolloff
    float minDist = 1.0f;
    float rolloff = source.rolloffFactor;

    if (distance <= minDist) return 1.0f;

    float attenuation = minDist / (minDist + rolloff * (distance - minDist));
    return qBound(0.0f, attenuation, 1.0f);
}

float SpatialSurroundSystem::calculateOcclusion(const SpatialSource& source,
                                                  const SpatialBus& bus) const
{
    if (!bus.occlusionEnabled) return 1.0f;
    return 1.0f - bus.occlusionFactor;
}

void SpatialSurroundSystem::applyPanningLaw(
    float azimuth, float spread,
    const QVector<SpeakerInfo>& speakers,
    QMap<SpeakerPosition, float>& gains) const
{
    for (const auto& speaker : speakers) {
        float angleDiff = qAbs(azimuth - speaker.azimuth);
        if (angleDiff > 180.0f) angleDiff = 360.0f - angleDiff;

        float gain = 0.0f;

        if (speaker.position == SpeakerPosition::LFE) {
            // LFE always gets full signal (bass management)
            gain = 1.0f;
        } else if (spread >= 1.0f) {
            // Full spread: equal power to all speakers
            gain = 1.0f;
        } else {
            // Calculate based on equal power panning law (default)
            // Equal power panning: cos^2 law
            float normalizedAngle = angleDiff / (speaker.azimuth != 0.0f ? qAbs(speaker.azimuth) : 90.0f);
            normalizedAngle = qBound(0.0f, normalizedAngle, 1.0f);
            gain = qCos(normalizedAngle * static_cast<float>(M_PI) / 2.0f);
            gain = gain * gain;  // cos^2 for equal power

            // Apply spread: blend towards equal power
            float equalPowerGain = 1.0f / qSqrt(static_cast<float>(speakers.size()));
            gain = gain * (1.0f - spread) + equalPowerGain * spread;
        }

        gains[speaker.position] = qBound(0.0f, gain, 1.0f);
    }
}

QJsonObject SpatialSurroundSystem::toJson() const
{
    QJsonObject root;
    root["listener"] = m_listener.toJson();

    QJsonArray sourcesArr;
    for (const auto& source : m_sources) {
        sourcesArr.append(source.toJson());
    }
    root["sources"] = sourcesArr;

    QJsonArray busesArr;
    for (const auto& bus : m_buses) {
        busesArr.append(bus.toJson());
    }
    root["buses"] = busesArr;

    return root;
}

void SpatialSurroundSystem::fromJson(const QJsonObject& obj)
{
    m_listener.fromJson(obj["listener"].toObject());

    m_sources.clear();
    QJsonArray sourcesArr = obj["sources"].toArray();
    for (const auto& s : sourcesArr) {
        SpatialSource source;
        source.fromJson(s.toObject());
        m_sources.append(source);
    }

    m_buses.clear();
    QJsonArray busesArr = obj["buses"].toArray();
    for (const auto& b : busesArr) {
        SpatialBus bus;
        bus.fromJson(b.toObject());
        m_buses.append(bus);
    }
}

}} // namespace ks::audio
