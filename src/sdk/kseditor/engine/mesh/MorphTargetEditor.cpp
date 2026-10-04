#include "MorphTargetEditor.h"
#include <cmath>
#include <algorithm>
#include <QSet>

MorphTargetEditor::MorphTargetEditor(QObject* parent) : QObject(parent) {}

MorphTargetEditor::~MorphTargetEditor() = default;

int MorphTargetEditor::addMorphTarget(const QString& name, int vertexCount) {
    MorphTarget target(name, QVector<QVector3D>(vertexCount, QVector3D(0, 0, 0)));
    m_targets.append(target);
    if (m_targets.size() == 1) {
        setCurrentTarget(0);
    }
    emit targetAdded(m_targets.size() - 1);
    emit targetsChanged();
    return m_targets.size() - 1;
}

bool MorphTargetEditor::removeMorphTarget(int index) {
    if (index < 0 || index >= m_targets.size()) return false;
    if (m_targets.size() <= 1) return false; // Keep at least one
    
    m_targets.removeAt(index);
    
    // Adjust current target index if needed
    if (m_currentTarget >= m_targets.size()) {
        m_currentTarget = m_targets.size() - 1;
    }
    if (m_currentTarget < 0) m_currentTarget = 0;
    
    emit targetRemoved(index);
    emit targetsChanged();
    emit currentTargetChanged(m_currentTarget);
    return true;
}

bool MorphTargetEditor::renameMorphTarget(int index, const QString& newName) {
    if (index < 0 || index >= m_targets.size()) return false;
    m_targets[index].name = newName;
    emit targetRenamed(index, newName);
    return true;
}

void MorphTargetEditor::setMorphTargetWeight(int index, float weight) {
    if (index < 0 || index >= m_targets.size()) return;
    m_targets[index].weight = qBound(0.0f, weight, 1.0f);
    emit targetWeightChanged(index, m_targets[index].weight);
}

float MorphTargetEditor::morphTargetWeight(int index) const {
    if (index < 0 || index >= m_targets.size()) return 0.0f;
    return m_targets[index].weight;
}

QString MorphTargetEditor::currentTargetName() const {
    if (m_currentTarget < 0 || m_currentTarget >= m_targets.size()) return "";
    return m_targets[m_currentTarget].name;
}

QStringList MorphTargetEditor::targetNames() const {
    QStringList names;
    for (const auto& target : m_targets) {
        names.append(target.name);
    }
    return names;
}

void MorphTargetEditor::setCurrentTarget(int index) {
    if (index < 0 || index >= m_targets.size()) return;
    if (m_currentTarget != index) {
        m_currentTarget = index;
        emit currentTargetChanged(index);
    }
}

int MorphTargetEditor::sculptBrushToTarget(int targetIndex, const QVector3D& center, float radius, float strength, int mode,
    const QVector3D& drag, const QVector3D& previousCenter,
    float falloffPower, const QSet<int>* pinned) {
    if (targetIndex < 0 || targetIndex >= m_targets.size()) return 0;
    if (m_targets[targetIndex].enabled == false) return 0;
    
    MorphTarget& target = m_targets[targetIndex];
    int affected = 0;

    // Compute the flatten plane normal if needed (mode 3)
    QVector3D flattenNormal;
    QVector3D flattenPoint;
    if (mode == 3 && !m_basePositions.isEmpty()) {
        // Average normal within radius for flatten plane
        QVector3D avgNormal;
        int count = 0;
        for (int vi = 0; vi < m_basePositions.size() && vi < target.positionDeltas.size(); ++vi) {
            QVector3D worldPos = m_basePositions[vi] + target.positionDeltas[vi];
            float d = (worldPos - center).length();
            if (d <= radius) {
                if (vi < m_baseNormals.size()) avgNormal += m_baseNormals[vi];
                flattenPoint += worldPos;
                count++;
            }
        }
        if (count > 0) {
            flattenNormal = avgNormal.normalized();
            flattenPoint /= float(count);
        } else {
            flattenNormal = QVector3D(0, 1, 0);
            flattenPoint = center;
        }
    }

    // Compute smooth brush neighbor averages if needed (mode 1)
    QVector<QVector3D> smoothDeltas;
    if (mode == 1) {
        smoothDeltas.resize(target.positionDeltas.size());
        for (int vi = 0; vi < target.positionDeltas.size(); ++vi) {
            QVector3D avgDelta;
            int neighborCount = 0;
            if (vi < m_adjacency.size()) {
                for (int ni : m_adjacency[vi]) {
                    if (ni >= 0 && ni < target.positionDeltas.size()) {
                        avgDelta += target.positionDeltas[ni];
                        neighborCount++;
                    }
                }
            }
            smoothDeltas[vi] = (neighborCount > 0) ? avgDelta / float(neighborCount) : target.positionDeltas[vi];
        }
    }
    
    // Apply brush to this morph target's position deltas
    for (int vi = 0; vi < target.positionDeltas.size(); ++vi) {
        // Skip pinned vertices
        if (pinned && pinned->contains(vi)) continue;
        
        // Compute real distance from brush center
        float d;
        QVector3D vertexNormal;
        if (!m_basePositions.isEmpty() && vi < m_basePositions.size()) {
            QVector3D worldPos = m_basePositions[vi] + target.positionDeltas[vi];
            d = (worldPos - center).length();
            vertexNormal = (vi < m_baseNormals.size()) ? m_baseNormals[vi] : QVector3D(0, 1, 0);
        } else {
            // Fallback: use vertex index as a rough position (legacy behavior)
            d = (QVector3D(float(vi % 100) - 50.0f, 0.0f, float(vi / 100) - 50.0f) - center).length();
            vertexNormal = QVector3D(0, 1, 0);
        }
        if (d > radius) continue;
        
        float t = 1.0f - (d / radius);
        float falloff;
        if (qAbs(falloffPower - 2.0f) < 1e-4f)
            falloff = t * t * (3.0f - 2.0f * t);
        else
            falloff = t > 0.0f ? qPow(t, qBound(0.25f, falloffPower, 8.0f)) : 0.0f;
        if (falloff <= 0.001f) continue;
        affected++;
        
        // Apply based on mode
        switch (mode) {
            case 0: // draw - add displacement along normal
                target.positionDeltas[vi] += vertexNormal * (strength * falloff);
                break;
            case 1: // smooth - average with neighbors
                if (vi < smoothDeltas.size()) {
                    target.positionDeltas[vi] = target.positionDeltas[vi] * (1.0f - strength * falloff) +
                                                 smoothDeltas[vi] * (strength * falloff);
                }
                break;
            case 2: { // grab - pull toward center (use drag direction)
                QVector3D grabDir = drag.normalized();
                target.positionDeltas[vi] += grabDir * (strength * falloff);
                break;
            }
            case 3: // flatten toward plane
                if (!flattenNormal.isNull()) {
                    QVector3D worldPos = m_basePositions[vi] + target.positionDeltas[vi];
                    QVector3D toPlane = flattenPoint - worldPos;
                    float projDist = QVector3D::dotProduct(toPlane, flattenNormal);
                    target.positionDeltas[vi] += flattenNormal * (projDist * strength * falloff);
                }
                break;
            case 4: // crease - sharpen along normal (inverse of smooth at edges)
                target.positionDeltas[vi] += vertexNormal * (strength * falloff * 0.5f);
                break;
            case 5: // inflate - push outward along normal with center boost
                target.positionDeltas[vi] += vertexNormal * (strength * falloff * (1.0f + falloff));
                break;
            case 6: { // pinch - pull toward brush center
                QVector3D worldPos = m_basePositions[vi] + target.positionDeltas[vi];
                QVector3D toCenter = center - worldPos;
                target.positionDeltas[vi] += toCenter * (strength * falloff * 0.3f);
                break;
            }
            case 7: // smear - follow cursor movement (drag vector)
                target.positionDeltas[vi] += drag * (strength * falloff * 0.5f);
                break;
            case 8: // negate - inverse of draw
                target.positionDeltas[vi] -= vertexNormal * (strength * falloff * 1.5f);
                break;
            default:
                break;
        }
    }
    
    emit sculptUpdated();
    return affected;
}

void MorphTargetEditor::setBaseMeshData(const QVector<QVector3D>& positions, const QVector<QVector3D>& normals,
                                         const QVector<QVector<int>>& adjacency) {
    m_basePositions = positions;
    m_baseNormals = normals;
    m_adjacency = adjacency;
}

QVector3D MorphTargetEditor::getVertexPosition(int targetIndex, int vertexIndex) const {
    if (targetIndex < 0 || targetIndex >= m_targets.size()) return QVector3D();
    const MorphTarget& target = m_targets[targetIndex];
    if (vertexIndex < 0 || vertexIndex >= target.positionDeltas.size()) return QVector3D();
    if (!m_basePositions.isEmpty() && vertexIndex < m_basePositions.size()) {
        return m_basePositions[vertexIndex] + target.positionDeltas[vertexIndex];
    }
    return target.positionDeltas[vertexIndex];
}