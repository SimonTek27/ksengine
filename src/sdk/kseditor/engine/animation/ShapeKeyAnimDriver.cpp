#include "ShapeKeyAnimDriver.h"
#include "../mesh/MeshOperations.h"
#include "../mesh/ShapeKeyData.h"

namespace ks {

ShapeKeyAnimDriver::ShapeKeyAnimDriver(QObject* parent) : QObject(parent)
{
}

void ShapeKeyAnimDriver::setTargetMesh(MeshData* mesh)
{
    m_mesh = mesh;
    if (mesh) {
        clearChannels();
        QStringList names = ShapeKeyManager::getShapeKeyNames(*mesh);
        for (const auto& name : names) {
            if (name == "Basis") continue;
            addChannel(name);
        }
    }
}

void ShapeKeyAnimDriver::addChannel(const QString& shapeKeyName)
{
    if (!m_mesh) return;
    int idx = ShapeKeyManager::getShapeKeyIndexByName(*m_mesh, shapeKeyName);
    if (idx < 0) return;

    ShapeKeyAnimChannel ch;
    ch.shapeKeyName = shapeKeyName;
    ch.targetIndex = idx;
    m_channels.append(ch);
    emit channelAdded(shapeKeyName);
}

void ShapeKeyAnimDriver::removeChannel(const QString& shapeKeyName)
{
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].shapeKeyName == shapeKeyName) {
            m_channels.removeAt(i);
            emit channelRemoved(shapeKeyName);
            return;
        }
    }
}

void ShapeKeyAnimDriver::clearChannels()
{
    m_channels.clear();
}

QStringList ShapeKeyAnimDriver::channelNames() const
{
    QStringList names;
    for (const auto& ch : m_channels) names << ch.shapeKeyName;
    return names;
}

void ShapeKeyAnimDriver::setKeyframe(const QString& shapeKeyName, int frame, float value, Keyframe::Interpolation interp)
{
    for (auto& ch : m_channels) {
        if (ch.shapeKeyName == shapeKeyName) {
            ch.curve.addKeyframe({frame, value, interp});
            return;
        }
    }
}

void ShapeKeyAnimDriver::removeKeyframe(const QString& shapeKeyName, int frame)
{
    for (auto& ch : m_channels) {
        if (ch.shapeKeyName == shapeKeyName) {
            ch.curve.removeKeyframe(frame);
            return;
        }
    }
}

void ShapeKeyAnimDriver::clearKeyframes(const QString& shapeKeyName)
{
    for (auto& ch : m_channels) {
        if (ch.shapeKeyName == shapeKeyName) {
            ch.curve.keyframes.clear();
            return;
        }
    }
}

void ShapeKeyAnimDriver::evaluateAtFrame(int frame)
{
    if (!m_mesh) return;
    for (const auto& ch : m_channels) {
        float weight = ch.curve.evaluate(static_cast<float>(frame));
        applyShapeKeyWeight(ch.shapeKeyName, weight);
    }
    emit evaluationApplied();
}

void ShapeKeyAnimDriver::evaluateAtTime(float timeSeconds, int fps)
{
    evaluateAtFrame(qRound(timeSeconds * fps));
}

void ShapeKeyAnimDriver::connectToTimeline(AnimationTimeline* timeline)
{
    m_timeline = timeline;
}

void ShapeKeyAnimDriver::disconnectFromTimeline()
{
    m_timeline = nullptr;
}

void ShapeKeyAnimDriver::applyShapeKeyWeight(const QString& name, float weight)
{
    if (!m_mesh) return;
    int idx = ShapeKeyManager::getShapeKeyIndexByName(*m_mesh, name);
    if (idx >= 0) {
        ShapeKeyManager::setShapeKeyWeight(*m_mesh, idx, weight);
    }
    emit weightChanged(name, weight);
}

void ShapeKeyAnimDriver::onFrameChanged(int frame)
{
    evaluateAtFrame(frame);
}

} // namespace ks
