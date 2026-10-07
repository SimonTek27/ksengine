#pragma once
#include <QVector3D>
#include <QtGlobal>
#include <cmath>

namespace ks::physics {

struct DraftState {
    float dragReduction = 0.0f;
    float downforceLoss = 0.0f;
};

inline DraftState computeDraft(const QVector3D& ego, const QVector3D& egoFwd,
    const QVector3D& leader, float leaderWidth = 1.9f) {
    DraftState s;
    QVector3D d = ego - leader;
    float dist = d.length();
    if (dist < 0.5f || dist > 60.0f) return s;
    QVector3D fwd = egoFwd.normalized();
    float along = QVector3D::dotProduct(d, fwd);
    if (along > 0) return s;
    float behind = -along;
    QVector3D lat = d - fwd * along;
    float lateral = lat.length();
    float wakeWidth = leaderWidth * (0.6f + behind * 0.06f);
    if (lateral > wakeWidth) return s;
    float proximity = 1.0f - behind / 60.0f;
    float centerFactor = 1.0f - (lateral / wakeWidth) * 0.7f;
    s.dragReduction = qBound(0.0f, 0.32f * proximity * centerFactor, 0.32f);
    s.downforceLoss = qBound(0.0f, 0.28f * proximity * centerFactor, 0.28f);
    return s;
}

} // namespace ks::physics
