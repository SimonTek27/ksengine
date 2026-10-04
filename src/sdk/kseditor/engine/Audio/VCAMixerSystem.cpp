#include "VCAMixerSystem.h"
#include <QUuid>

namespace ks { namespace audio {

void VCAMixerSystem::createVCA(const QString& name)
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    VCA vca;
    vca.id = id;
    vca.name = name;
    vca.volume = 1.0f;
    vca.muted = false;
    vca.solo = false;
    m_vcas[id] = vca;
    emit vcaCreated(id);
}

void VCAMixerSystem::deleteVCA(const QString& vcaId)
{
    if (m_vcas.contains(vcaId)) {
        m_vcas.remove(vcaId);
        emit vcaDeleted(vcaId);
    }
}

VCAMixerSystem::VCA VCAMixerSystem::getVCA(const QString& vcaId) const
{
    return m_vcas.value(vcaId);
}

void VCAMixerSystem::assignToVCA(const QString& channelId, const QString& vcaId)
{
    if (m_vcas.contains(vcaId)) {
        if (!m_vcas[vcaId].assignedChannels.contains(channelId)) {
            m_vcas[vcaId].assignedChannels.append(channelId);
        }
    }
}

void VCAMixerSystem::unassignFromVCA(const QString& channelId, const QString& vcaId)
{
    if (m_vcas.contains(vcaId)) {
        m_vcas[vcaId].assignedChannels.removeAll(channelId);
    }
}

void VCAMixerSystem::setChannelVolume(const QString& channelId, float volume)
{
    if (!m_channels.contains(channelId)) {
        MixerChannel ch;
        ch.id = channelId;
        ch.name = channelId;
        ch.volume = 1.0f;
        ch.pan = 0.0f;
        ch.muted = false;
        ch.solo = false;
        m_channels[channelId] = ch;
    }
    m_channels[channelId].volume = qBound(0.0f, volume, 1.0f);
    emit volumeChanged(channelId, m_channels[channelId].volume);
}

float VCAMixerSystem::getChannelVolume(const QString& channelId) const
{
    if (m_channels.contains(channelId)) {
        return m_channels[channelId].volume;
    }
    return 1.0f;
}

void VCAMixerSystem::setChannelPan(const QString& channelId, float pan)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].pan = qBound(-1.0f, pan, 1.0f);
    }
}

void VCAMixerSystem::setChannelMute(const QString& channelId, bool mute)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].muted = mute;
    }
}

void VCAMixerSystem::setChannelSolo(const QString& channelId, bool solo)
{
    if (m_channels.contains(channelId)) {
        m_channels[channelId].solo = solo;
    }
}

void VCAMixerSystem::saveSnapshot(const QString& name)
{
    MixerSnapshot snapshot;
    snapshot.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    snapshot.name = name;
    snapshot.fadeTime = 0;

    for (const auto& ch : m_channels) {
        snapshot.channelVolumes[ch.id] = ch.volume;
        snapshot.mutes[ch.id] = ch.muted;
        snapshot.solos[ch.id] = ch.solo;
    }
    for (const auto& vca : m_vcas) {
        snapshot.vcaVolumes[vca.id] = vca.volume;
    }

    m_snapshots.append(snapshot);
    emit snapshotCreated(snapshot.id);
}

void VCAMixerSystem::loadSnapshot(const QString& snapshotId)
{
    for (const auto& snapshot : m_snapshots) {
        if (snapshot.id == snapshotId) {
            for (auto it = snapshot.channelVolumes.constBegin(); it != snapshot.channelVolumes.constEnd(); ++it) {
                if (m_channels.contains(it.key())) {
                    m_channels[it.key()].volume = it.value();
                }
            }
            for (auto it = snapshot.mutes.constBegin(); it != snapshot.mutes.constEnd(); ++it) {
                if (m_channels.contains(it.key())) {
                    m_channels[it.key()].muted = it.value();
                }
            }
            for (auto it = snapshot.solos.constBegin(); it != snapshot.solos.constEnd(); ++it) {
                if (m_channels.contains(it.key())) {
                    m_channels[it.key()].solo = it.value();
                }
            }
            for (auto it = snapshot.vcaVolumes.constBegin(); it != snapshot.vcaVolumes.constEnd(); ++it) {
                if (m_vcas.contains(it.key())) {
                    m_vcas[it.key()].volume = it.value();
                }
            }
            emit snapshotLoaded(snapshotId);
            return;
        }
    }
}

}} // ks::audio
