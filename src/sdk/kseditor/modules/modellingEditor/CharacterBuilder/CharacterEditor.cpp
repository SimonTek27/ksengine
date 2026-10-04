#include "CharacterEditor.h"
#include "sdk/kseditor/engine/mesh/WeightPainting.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/modules/modellingEditor/3DModelingQmlBridge.h"
#include <QDebug>
#include <QtMath>
#include <cmath>

namespace ks {

CharacterEditor* CharacterEditor::s_instance = nullptr;

CharacterEditor::CharacterEditor(QObject* parent)
    : QObject(parent)
{
}

CharacterEditor* CharacterEditor::instance()
{
    if (!s_instance) {
        s_instance = new CharacterEditor();
    }
    return s_instance;
}

void CharacterEditor::createHumanoidSkeleton(float height)
{
    m_skeleton.name = "Humanoid";
    m_skeleton.bones.clear();
    m_nextBoneId = 0;
    
    float scale = height / 1.8f;
    
    int pelvis = addBone("Pelvis", -1, QVector3D(0, 0.9f * scale, 0));
    int spine = addBone("Spine", pelvis, QVector3D(0, 1.0f * scale, 0));
    int chest = addBone("Chest", spine, QVector3D(0, 1.2f * scale, 0));
    int neck = addBone("Neck", chest, QVector3D(0, 1.35f * scale, 0));
    int head = addBone("Head", neck, QVector3D(0, 1.5f * scale, 0));
    
    int lShoulder = addBone("L_Shoulder", chest, QVector3D(-0.15f * scale, 1.3f * scale, 0));
    int lUpperArm = addBone("L_UpperArm", lShoulder, QVector3D(-0.25f * scale, 1.3f * scale, 0));
    int lForearm = addBone("L_Forearm", lUpperArm, QVector3D(-0.45f * scale, 1.3f * scale, 0));
    int lHand = addBone("L_Hand", lForearm, QVector3D(-0.65f * scale, 1.3f * scale, 0));
    
    int rShoulder = addBone("R_Shoulder", chest, QVector3D(0.15f * scale, 1.3f * scale, 0));
    int rUpperArm = addBone("R_UpperArm", rShoulder, QVector3D(0.25f * scale, 1.3f * scale, 0));
    int rForearm = addBone("R_Forearm", rUpperArm, QVector3D(0.45f * scale, 1.3f * scale, 0));
    int rHand = addBone("R_Hand", rForearm, QVector3D(0.65f * scale, 1.3f * scale, 0));
    
    int lHip = addBone("L_Hip", pelvis, QVector3D(-0.1f * scale, 0.9f * scale, 0));
    int lThigh = addBone("L_Thigh", lHip, QVector3D(-0.1f * scale, 0.5f * scale, 0));
    int lShin = addBone("L_Shin", lThigh, QVector3D(-0.1f * scale, 0.1f * scale, 0));
    int lFoot = addBone("L_Foot", lShin, QVector3D(-0.1f * scale, 0.0f * scale, 0.05f * scale));
    
    int rHip = addBone("R_Hip", pelvis, QVector3D(0.1f * scale, 0.9f * scale, 0));
    int rThigh = addBone("R_Thigh", rHip, QVector3D(0.1f * scale, 0.5f * scale, 0));
    int rShin = addBone("R_Shin", rThigh, QVector3D(0.1f * scale, 0.1f * scale, 0));
    int rFoot = addBone("R_Foot", rShin, QVector3D(0.1f * scale, 0.0f * scale, 0.05f * scale));
    
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

void CharacterEditor::createQuadrupedSkeleton(float height, float length)
{
    m_skeleton.name = "Quadruped";
    m_skeleton.bones.clear();
    m_nextBoneId = 0;
    
    float scale = height / 1.5f;
    
    int spine = addBone("Spine", -1, QVector3D(0, 0.8f * scale, 0));
    int neck = addBone("Neck", spine, QVector3D(0.3f * scale * length, 0.9f * scale, 0));
    int head = addBone("Head", neck, QVector3D(0.6f * scale * length, 0.9f * scale, 0));
    
    int frontLHip = addBone("FL_Hip", spine, QVector3D(0.25f * scale * length, 0.6f * scale, -0.15f * scale));
    int frontLThigh = addBone("FL_Thigh", frontLHip, QVector3D(0.25f * scale * length, 0.2f * scale, -0.15f * scale));
    int frontLShin = addBone("FL_Shin", frontLThigh, QVector3D(0.25f * scale * length, 0.0f * scale, -0.15f * scale));
    int frontLFoot = addBone("FL_Foot", frontLShin, QVector3D(0.25f * scale * length, 0.0f * scale, -0.17f * scale));
    
    int frontRHip = addBone("FR_Hip", spine, QVector3D(0.25f * scale * length, 0.6f * scale, 0.15f * scale));
    int frontRThigh = addBone("FR_Thigh", frontRHip, QVector3D(0.25f * scale * length, 0.2f * scale, 0.15f * scale));
    int frontRShin = addBone("FR_Shin", frontRThigh, QVector3D(0.25f * scale * length, 0.0f * scale, 0.15f * scale));
    int frontRFoot = addBone("FR_Foot", frontRShin, QVector3D(0.25f * scale * length, 0.0f * scale, 0.17f * scale));
    
    int backLHip = addBone("BL_Hip", spine, QVector3D(-0.25f * scale * length, 0.6f * scale, -0.15f * scale));
    int backLThigh = addBone("BL_Thigh", backLHip, QVector3D(-0.25f * scale * length, 0.2f * scale, -0.15f * scale));
    int backLShin = addBone("BL_Shin", backLThigh, QVector3D(-0.25f * scale * length, 0.0f * scale, -0.15f * scale));
    int backLFoot = addBone("BL_Foot", backLShin, QVector3D(-0.25f * scale * length, 0.0f * scale, -0.17f * scale));
    
    int backRHip = addBone("BR_Hip", spine, QVector3D(-0.25f * scale * length, 0.6f * scale, 0.15f * scale));
    int backRThigh = addBone("BR_Thigh", backRHip, QVector3D(-0.25f * scale * length, 0.2f * scale, 0.15f * scale));
    int backRShin = addBone("BR_Shin", backRThigh, QVector3D(-0.25f * scale * length, 0.0f * scale, 0.15f * scale));
    int backRFoot = addBone("BR_Foot", backRShin, QVector3D(-0.25f * scale * length, 0.0f * scale, 0.17f * scale));
    
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

int CharacterEditor::addBone(const QString& name, int parentIndex, const QVector3D& position)
{
    Bone bone;
    bone.name = name;
    bone.parentIndex = parentIndex;
    bone.head = position;
    bone.tail = position + QVector3D(0, 0.1f, 0);
    bone.length = bone.tail - bone.head;
    bone.rotation = QQuaternion();
    
    int index = m_skeleton.bones.size();
    m_skeleton.bones.append(bone);
    m_skeleton.updateMatrices();
    
    return index;
}

void CharacterEditor::removeBone(int index)
{
    if (index < 0 || index >= m_skeleton.bones.size()) return;
    
    for (int i = 0; i < m_skeleton.bones.size(); ++i) {
        if (m_skeleton.bones[i].parentIndex == index) {
            m_skeleton.bones[i].parentIndex = m_skeleton.bones[index].parentIndex;
        }
    }
    
    m_skeleton.bones.removeAt(index);
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

void CharacterEditor::setBoneParent(int boneIndex, int newParentIndex)
{
    if (boneIndex < 0 || boneIndex >= m_skeleton.bones.size()) return;
    if (newParentIndex == boneIndex) return;
    
    int oldParent = m_skeleton.bones[boneIndex].parentIndex;
    if (newParentIndex >= 0) {
        for (int i = newParentIndex; i >= 0; i = m_skeleton.bones[i].parentIndex) {
            if (i == boneIndex) return;
        }
    }
    
    m_skeleton.bones[boneIndex].parentIndex = newParentIndex;
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

void CharacterEditor::selectBone(int index)
{
    if (m_selectedBone >= 0 && m_selectedBone < m_skeleton.bones.size()) {
        m_skeleton.bones[m_selectedBone].selected = false;
    }
    
    m_selectedBone = index;
    
    if (index >= 0 && index < m_skeleton.bones.size()) {
        m_skeleton.bones[index].selected = true;
    }
    
    emit boneSelected(index);
}

void CharacterEditor::deselectAll()
{
    for (Bone& bone : m_skeleton.bones) {
        bone.selected = false;
    }
    m_selectedBone = -1;
    emit boneSelected(-1);
}

QVector<int> CharacterEditor::selectedBones() const
{
    QVector<int> result;
    for (int i = 0; i < m_skeleton.bones.size(); ++i) {
        if (m_skeleton.bones[i].selected) {
            result.append(i);
        }
    }
    return result;
}

void CharacterEditor::moveBone(int index, const QVector3D& delta)
{
    if (index < 0 || index >= m_skeleton.bones.size()) return;
    
    Bone& bone = m_skeleton.bones[index];
    bone.head += delta;
    bone.tail += delta;
    bone.length = bone.tail - bone.head;
    
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

void CharacterEditor::rotateBone(int index, const QQuaternion& rotation)
{
    if (index < 0 || index >= m_skeleton.bones.size()) return;
    
    Bone& bone = m_skeleton.bones[index];
    QVector3D localTail = bone.tail - bone.head;
    localTail = rotation.rotatedVector(localTail);
    bone.tail = bone.head + localTail;
    bone.rotation = rotation * bone.rotation;
    bone.length = bone.tail - bone.head;
    
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

QVector<QVector3D> CharacterEditor::getBoneWorldPositions() const
{
    QVector<QVector3D> positions;
    positions.reserve(m_skeleton.bones.size());
    
    for (const Bone& bone : m_skeleton.bones) {
        QVector3D worldPos = (bone.worldMatrix * QVector4D(bone.head, 1.0f)).toVector3D();
        positions.append(worldPos);
    }
    
    return positions;
}

QVector<QPair<QVector3D, QVector3D>> CharacterEditor::getBoneSegments() const
{
    QVector<QPair<QVector3D, QVector3D>> segments;
    segments.reserve(m_skeleton.bones.size());
    
    for (const Bone& bone : m_skeleton.bones) {
        QVector3D worldHead = (bone.worldMatrix * QVector4D(bone.head, 1.0f)).toVector3D();
        QVector3D worldTail = (bone.worldMatrix * QVector4D(bone.tail, 1.0f)).toVector3D();
        segments.append(std::make_pair(worldHead, worldTail));
    }
    
    return segments;
}

void CharacterEditor::addPose(const QString& name)
{
    CharacterPose pose;
    pose.name = name;
    
    for (int i = 0; i < m_skeleton.bones.size(); ++i) {
        pose.boneRotations[i] = m_skeleton.bones[i].rotation;
        pose.boneTranslations[i] = m_skeleton.bones[i].head;
    }
    
    m_poses[name] = pose;
}

void CharacterEditor::applyPose(const QString& name)
{
    if (!m_poses.contains(name)) return;
    
    const CharacterPose& pose = m_poses[name];
    
    for (auto it = pose.boneRotations.begin(); it != pose.boneRotations.end(); ++it) {
        if (it.key() < m_skeleton.bones.size()) {
            m_skeleton.bones[it.key()].rotation = it.value();
        }
    }
    
    for (auto it = pose.boneTranslations.begin(); it != pose.boneTranslations.end(); ++it) {
        if (it.key() < m_skeleton.bones.size()) {
            QVector3D delta = it.value() - m_skeleton.bones[it.key()].head;
            m_skeleton.bones[it.key()].head = it.value();
            m_skeleton.bones[it.key()].tail += delta;
        }
    }
    
    m_skeleton.updateMatrices();
    emit skeletonModified();
}

void CharacterEditor::removePose(const QString& name)
{
    m_poses.remove(name);
}

CharacterPose CharacterEditor::getCurrentPose() const
{
    CharacterPose pose;
    
    for (int i = 0; i < m_skeleton.bones.size(); ++i) {
        pose.boneRotations[i] = m_skeleton.bones[i].rotation;
        pose.boneTranslations[i] = m_skeleton.bones[i].head;
    }
    
    return pose;
}

void CharacterEditor::bindToMesh(const QString& meshId)
{
    // Get scene and find the mesh
    auto* scene = KSModelerQml::instance().sceneGraph();
    if (!scene || m_skeleton.bones.isEmpty()) return;

    SceneObject* obj = nullptr;
    auto allObjs = scene->allObjects();
    for (auto* o : allObjs) {
        if (o && o->name() == meshId) { obj = o; break; }
    }
    if (!obj || !obj->mesh()) return;

    // Extract mesh geometry
    auto& verts = obj->mesh()->geometry().vertices;
    auto& idxs = obj->mesh()->geometry().indices;
    QVector<QVector3D> positions;
    QVector<QVector<int>> faces;
    for (const auto& sv : verts)
        positions.append(QVector3D(sv.position.x(), sv.position.y(), sv.position.z()));
    for (int i = 0; i + 2 < idxs.size(); i += 3) {
        QVector<int> tri;
        tri.append((int)idxs[i]);
        tri.append((int)idxs[i+1]);
        tri.append((int)idxs[i+2]);
        faces.append(tri);
    }

    // Get bone world positions and names for mapping
    QVector<QVector3D> boneWorldPos = getBoneWorldPositions();

    // Auto-calculate weights using HeatDiffusion method
    QVector<WeightVertex> autoWv = AutoWeightCalculator::calculateAutoWeights(
        positions, faces, boneWorldPos,
        AutoWeightCalculator::Method::HeatDiffusion, 10);

    // Convert to CharacterEditor format
    m_meshPositions = positions;
    QVector<QVector<VertexWeight>> charWeights;
    charWeights.resize(positions.size());

    for (int vi = 0; vi < autoWv.size() && vi < charWeights.size(); ++vi) {
        for (auto it = autoWv[vi].weights.begin(); it != autoWv[vi].weights.end(); ++it) {
            VertexWeight vw;
            vw.boneIndex = it.key();
            vw.weight = it.value();
            charWeights[vi].append(vw);
        }
    }

    WeightOptimization::normalizeAllWeights(autoWv);
    m_meshWeights[meshId] = charWeights;
    emit weightsModified();
}

void CharacterEditor::paintWeights(int boneIndex, const QVector3D& center, float radius, float strength)
{
    for (auto& weights : m_meshWeights) {
        for (int i = 0; i < weights.size(); ++i) {
            for (auto& vw : weights[i]) {
                if (vw.boneIndex == boneIndex) {
                    float dist = (m_meshPositions[i] - center).length();
                    if (dist < radius) {
                        float influence = (1.0f - dist / radius) * strength;
                        vw.weight = qBound(0.0f, vw.weight + influence, 1.0f);
                    }
                }
            }
        }
    }
    emit weightsModified();
}

void CharacterEditor::smoothWeights(int boneIndex, float radius)
{
    for (auto& weights : m_meshWeights) {
        for (int i = 0; i < weights.size(); ++i) {
            for (auto& vw : weights[i]) {
                if (vw.boneIndex == boneIndex) {
                    float sum = vw.weight;
                    int count = 1;
                    for (int j = qMax(0, i - 1); j < qMin(weights.size(), i + 2); ++j) {
                        for (const auto& nw : weights[j]) {
                            if (nw.boneIndex == boneIndex) {
                                sum += nw.weight;
                                count++;
                            }
                        }
                    }
                    vw.weight = sum / count;
                }
            }
        }
    }
    emit weightsModified();
}

void CharacterEditor::normalizeWeights()
{
    for (auto& weights : m_meshWeights) {
        for (auto& vertexWeight : weights) {
            float total = 0.0f;
            for (const VertexWeight& w : vertexWeight) {
                total += w.weight;
            }
            if (total > 0.0f) {
                for (VertexWeight& w : vertexWeight) {
                    w.weight /= total;
                }
            }
        }
    }
    emit weightsModified();
}

void CharacterEditor::mirrorWeights(int axis, float tolerance)
{
    for (auto& weights : m_meshWeights) {
        // Find the mirror vertex for each vertex (reflection across the plane
        // perpendicular to the given axis, flipping the axis coordinate).
        QVector<int> mirrorOf(weights.size(), -1);
        for (int i = 0; i < weights.size() && i < m_meshPositions.size(); ++i) {
            if (mirrorOf[i] >= 0) continue;
            QVector3D pi = m_meshPositions[i];
            for (int j = i + 1; j < weights.size() && j < m_meshPositions.size(); ++j) {
                const QVector3D pj = m_meshPositions[j];
                const float ref = qAbs(pj[axis] + pi[axis]);
                const bool sameOthers = qAbs(pj[(axis + 1) % 3] - pi[(axis + 1) % 3]) < tolerance
                                         && qAbs(pj[(axis + 2) % 3] - pi[(axis + 2) % 3]) < tolerance;
                if (sameOthers && ref < tolerance) {
                    mirrorOf[i] = j;
                    mirrorOf[j] = i;
                    break;
                }
            }
        }
        for (int i = 0; i < weights.size(); ++i) {
            const int j = mirrorOf[i];
            if (j < 0) continue;
            if (weights[j].isEmpty()) continue;
            // Symmetric copy: both vertices end up with mirrored weights.
            weights[i] = weights[j];
            weights[j] = weights[i];
        }
    }
    emit weightsModified();
}

QVector<VertexWeight> CharacterEditor::getVertexWeights(int vertexIndex) const
{
    for (const auto& weights : m_meshWeights) {
        if (vertexIndex < weights.size()) {
            return weights[vertexIndex];
        }
    }
    return QVector<VertexWeight>();
}

void CharacterEditor::setVertexWeights(int vertexIndex, const QVector<VertexWeight>& weights)
{
    for (auto& meshWeights : m_meshWeights) {
        if (vertexIndex < meshWeights.size()) {
            meshWeights[vertexIndex] = weights;
        }
    }
    emit weightsModified();
}

QVector3D SkeletonTool::calculateBoneAxis(const Bone& bone)
{
    QVector3D axis = bone.tail - bone.head;
    if (axis.length() < 0.0001f) {
        return QVector3D(0, 1, 0);
    }
    return axis.normalized();
}

QQuaternion SkeletonTool::lookAt(const QVector3D& from, const QVector3D& to, const QVector3D& up)
{
    QVector3D forward = (to - from).normalized();
    QVector3D right = QVector3D::crossProduct(up, forward).normalized();
    QVector3D newUp = QVector3D::crossProduct(forward, right);
    
    QMatrix4x4 mat;
    mat.setColumn(0, QVector4D(right, 0));
    mat.setColumn(1, QVector4D(newUp, 0));
    mat.setColumn(2, QVector4D(forward, 0));
    mat.setColumn(3, QVector4D(0, 0, 0, 1));
    
    return QQuaternion::fromRotationMatrix(mat.normalMatrix());
}

QVector3D SkeletonTool::quaternionToEuler(const QQuaternion& q)
{
    double test = q.scalar() * q.y() + q.z() * q.x();
    
    if (test > 0.499) {
        return QVector3D(90, 0, 180);
    }
    if (test < -0.499) {
        return QVector3D(-90, 0, -180);
    }
    
    double sqx = q.x() * q.x();
    double sqy = q.y() * q.y();
    double sqz = q.z() * q.z();
    
    double yaw = qRadiansToDegrees(qAtan2(2.0 * q.y() * q.scalar() - 2.0 * q.x() * q.z(), 1.0 - 2.0 * sqy - 2.0 * sqz));
    double pitch = qRadiansToDegrees(qAsin(2.0 * test));
    double roll = qRadiansToDegrees(qAtan2(2.0 * q.x() * q.y() + 2.0 * q.z() * q.scalar(), 1.0 - 2.0 * sqx - 2.0 * sqz));
    
    return QVector3D(pitch, yaw, roll);
}

QQuaternion SkeletonTool::eulerToQuaternion(const QVector3D& euler)
{
    QQuaternion qYaw = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), euler.y());
    QQuaternion qPitch = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), euler.x());
    QQuaternion qRoll = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), euler.z());
    
    return qYaw * qPitch * qRoll;
}

float SkeletonTool::distanceToLine(const QVector3D& point, const QVector3D& lineStart, const QVector3D& lineEnd)
{
    QVector3D lineDir = lineEnd - lineStart;
    float lineLength = lineDir.length();
    
    if (lineLength < 0.0001f) {
        return (point - lineStart).length();
    }
    
    lineDir.normalize();
    QVector3D v = point - lineStart;
    float t = QVector3D::dotProduct(v, lineDir);
    t = qBound(0.0f, t, lineLength);
    
    QVector3D closest = lineStart + lineDir * t;
    return (point - closest).length();
}

QVector3D SkeletonTool::closestPointOnLine(const QVector3D& point, const QVector3D& lineStart, const QVector3D& lineEnd)
{
    QVector3D lineDir = lineEnd - lineStart;
    float lineLength = lineDir.length();
    
    if (lineLength < 0.0001f) {
        return lineStart;
    }
    
    lineDir.normalize();
    QVector3D v = point - lineStart;
    float t = QVector3D::dotProduct(v, lineDir);
    t = qBound(0.0f, t, lineLength);
    
    return lineStart + lineDir * t;
}

bool SkeletonTool::solveTwoBoneIK(const QVector3D& root, const QVector3D& midTarget,
                                  const QVector3D& endTarget, float upperLen, float lowerLen,
                                  QVector3D& outMid, QVector3D& outEnd)
{
    QVector3D toEnd = endTarget - root;
    float dist = toEnd.length();

    if (dist > upperLen + lowerLen) {
        outMid = root + toEnd.normalized() * upperLen;
        outEnd = outMid + toEnd.normalized() * lowerLen;
        return false;
    }
    
    if (dist < qAbs(upperLen - lowerLen)) {
        QVector3D dir = (endTarget - root).normalized();
        outMid = root + dir * upperLen;
        outEnd = outMid + dir * lowerLen;
        return true;
    }
    
    float cosAngle = (upperLen * upperLen + dist * dist - lowerLen * lowerLen) / (2.0f * upperLen * dist);
    cosAngle = qBound(-1.0f, cosAngle, 1.0f);
    float angle = qAcos(cosAngle);
    
    QVector3D dirToEnd = toEnd.normalized();
    QVector3D perpendicular = QVector3D::crossProduct(QVector3D(0, 1, 0), dirToEnd).normalized();
    if (perpendicular.length() < 0.001f) {
        perpendicular = QVector3D::crossProduct(dirToEnd, QVector3D(1, 0, 0)).normalized();
    }
    
    outMid = root + dirToEnd * upperLen * qCos(angle) + perpendicular * upperLen * qSin(angle);
    outEnd = endTarget;
    
    return true;
}

QVector<QVector3D> SkeletonTool::interpolatePath(const QVector<QVector3D>& from, 
                                                  const QVector<QVector3D>& to, float t)
{
    QVector<QVector3D> result;
    int maxSize = qMax(from.size(), to.size());
    
    for (int i = 0; i < maxSize; ++i) {
        QVector3D fromPoint = (i < from.size()) ? from[i] : (from.isEmpty() ? QVector3D() : from.last());
        QVector3D toPoint = (i < to.size()) ? to[i] : (to.isEmpty() ? QVector3D() : to.last());
        result.append(fromPoint + (toPoint - fromPoint) * t);
    }
    
    return result;
}

void SkeletonTool::applyFK(CharacterSkeleton& skeleton, int boneIndex, const QVector3D& rotation)
{
    if (boneIndex < 0 || boneIndex >= skeleton.bones.size()) return;
    
    QQuaternion quat = eulerToQuaternion(rotation);
    skeleton.bones[boneIndex].rotation = quat * skeleton.bones[boneIndex].rotation;
    skeleton.updateMatrices();
}

void SkeletonTool::applyIK(CharacterSkeleton& skeleton, int rootBone, int endBone, 
                           const QVector3D& target, bool stretch)
{
    if (rootBone < 0 || endBone <= rootBone || endBone >= skeleton.bones.size()) return;
    
    QVector3D root = skeleton.getBonePosition(rootBone);
    
    QVector3D mid, end;
    float upperLen = (skeleton.bones[rootBone + 1].head - root).length();
    float lowerLen = (skeleton.bones[endBone].head - skeleton.bones[rootBone + 1].head).length();
    
    solveTwoBoneIK(root, QVector3D(), target, upperLen, lowerLen, mid, end);
    
    QVector3D newUpper = mid - root;
    QVector3D oldUpper = skeleton.bones[rootBone + 1].head - root;
    
    if (oldUpper.length() > 0.001f) {
        QQuaternion rotUpper = QQuaternion::fromAxisAndAngle(
            QVector3D::crossProduct(oldUpper, newUpper).normalized(),
            qAcos(QVector3D::dotProduct(oldUpper.normalized(), newUpper.normalized()))
        );
        skeleton.bones[rootBone + 1].rotation = rotUpper * skeleton.bones[rootBone + 1].rotation;
    }
    
    QVector3D newLower = end - mid;
    QVector3D oldLower = skeleton.bones[endBone].head - skeleton.bones[rootBone + 1].head;
    
    if (oldLower.length() > 0.001f) {
        QQuaternion rotLower = QQuaternion::fromAxisAndAngle(
            QVector3D::crossProduct(oldLower, newLower).normalized(),
            qAcos(QVector3D::dotProduct(oldLower.normalized(), newLower.normalized()))
        );
        skeleton.bones[endBone].rotation = rotLower * skeleton.bones[endBone].rotation;
    }
    
    if (stretch) {
        float totalLen = upperLen + lowerLen;
        float distToTarget = (target - root).length();
        float stretchFactor = qMin(1.5f, distToTarget / totalLen);
        
        skeleton.bones[rootBone + 1].tail = skeleton.bones[rootBone + 1].head + 
            (skeleton.bones[rootBone + 1].tail - skeleton.bones[rootBone + 1].head) * stretchFactor;
    }
    
    skeleton.updateMatrices();
}

} // namespace ks