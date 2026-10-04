#include "SkeletonSystem.h"
#include <QQueue>
#include <algorithm>
#include <cmath>

namespace {

QQuaternion quatSlerp(const QQuaternion &q1, const QQuaternion &q2, float t) {
    float dot = QQuaternion::dotProduct(q1, q2);
    float sign = 1.0f;
    if (dot < 0.0f) { dot = -dot; sign = -1.0f; }
    if (dot > 0.9999f) return (q1 * (1.0f - t) + q2 * t * sign).normalized();
    float angle = std::acos(dot);
    float sinAngle = std::sin(angle);
    float a1 = std::sin((1.0f - t) * angle) / sinAngle;
    float a2 = std::sin(t * angle) / sinAngle * sign;
    return q1 * a1 + q2 * a2;
}

} // anonymous namespace

namespace ks {

Skeleton::Skeleton() : name("Skeleton") {}

Skeleton::Skeleton(const QString& n) : name(n) {}

int Skeleton::addBone(const QString& boneName, int parentIndex) {
    if (boneIndexMap.contains(boneName)) return -1;

    Bone bone;
    bone.name = boneName;
    bone.parentIndex = parentIndex;

    int index = bones.size();
    bones.append(bone);
    boneIndexMap[boneName] = index;

    if (parentIndex >= 0 && parentIndex < bones.size()) {
        bones[parentIndex].children.append(index);
    }

    return index;
}

int Skeleton::findBone(const QString& name) const {
    auto it = boneIndexMap.find(name);
    if (it != boneIndexMap.end()) return it.value();
    return -1;
}

void Skeleton::removeBone(int index) {
    if (index < 0 || index >= bones.size()) return;

    QString boneName = bones[index].name;
    int parentIdx = bones[index].parentIndex;

    if (parentIdx >= 0) {
        bones[parentIdx].children.removeAll(index);
    }

    QVector<int> toRemove;
    QQueue<int> queue;
    queue.append(index);

    while (!queue.isEmpty()) {
        int current = queue.dequeue();
        toRemove.append(current);
        for (int child : bones[current].children) {
            queue.append(child);
        }
    }

    for (int i = toRemove.size() - 1; i >= 0; --i) {
        int removeIdx = toRemove[i];
        bones.removeAt(removeIdx);
        boneIndexMap.remove(bones[removeIdx].name);
    }

    updateBoneIndexMap();
}

void Skeleton::setBoneParent(int boneIndex, int newParentIndex) {
    if (boneIndex < 0 || boneIndex >= bones.size()) return;

    int oldParent = bones[boneIndex].parentIndex;
    if (oldParent >= 0) {
        bones[oldParent].children.removeAll(boneIndex);
    }

    bones[boneIndex].parentIndex = newParentIndex;

    if (newParentIndex >= 0 && newParentIndex < bones.size()) {
        bones[newParentIndex].children.append(boneIndex);
    }
}

void Skeleton::updateHierarchy() {
    for (auto& bone : bones) {
        bone.children.clear();
    }

    for (int i = 0; i < bones.size(); ++i) {
        int parent = bones[i].parentIndex;
        if (parent >= 0 && parent < bones.size()) {
            bones[parent].children.append(i);
        }
    }
}

QMatrix4x4 Skeleton::getBoneLocalMatrix(int index) const {
    if (index < 0 || index >= bones.size()) return QMatrix4x4();

    const Bone& bone = bones[index];
    QMatrix4x4 m;
    m.setToIdentity();
    m.translate(bone.head);
    m.rotate(bone.rotation);
    m.scale(bone.scale);
    return m;
}

QMatrix4x4 Skeleton::getBoneWorldMatrix(int index) const {
    QMatrix4x4 world;
    world.setToIdentity();

    QVector<int> ancestors = getBoneAncestors(index);
    for (int i = 0; i < ancestors.size(); ++i) {
        world *= getBoneLocalMatrix(ancestors[i]);
    }

    return world * getBoneLocalMatrix(index);
}

void Skeleton::computeWorldMatrices() {
    for (auto& bone : bones) {
        if (bone.parentIndex < 0) {
            bone.worldMatrix = bone.localMatrix;
        } else {
            bone.worldMatrix = bones[bone.parentIndex].worldMatrix * bone.localMatrix;
        }
    }
}

void Skeleton::computeInverseBindMatrices() {
    computeWorldMatrices();
    m_invBindMatrices.clear();
    for (int i = 0; i < bones.size(); ++i) {
        m_invBindMatrices[i] = bones[i].worldMatrix.inverted();
    }
}

QVector<int> Skeleton::getBoneChildren(int index) const {
    if (index < 0 || index >= bones.size()) return QVector<int>();
    return bones[index].children;
}

QVector<int> Skeleton::getBoneAncestors(int index) const {
    QVector<int> ancestors;
    int current = bones[index].parentIndex;
    while (current >= 0) {
        ancestors.append(current);
        current = bones[current].parentIndex;
    }
    return ancestors;
}

QVector3D Skeleton::getBonePosition(int index) const {
    if (index < 0 || index >= bones.size()) return QVector3D();
    return bones[index].head;
}

QQuaternion Skeleton::getBoneRotation(int index) const {
    if (index < 0 || index >= bones.size()) return QQuaternion();
    return bones[index].rotation;
}

void Skeleton::setBonePosition(int index, const QVector3D& pos) {
    if (index < 0 || index >= bones.size()) return;
    bones[index].head = pos;
}

void Skeleton::setBoneRotation(int index, const QQuaternion& rot) {
    if (index < 0 || index >= bones.size()) return;
    bones[index].rotation = rot;
}

bool Skeleton::isAncestor(int boneIndex, int potentialAncestor) const {
    int current = bones[boneIndex].parentIndex;
    while (current >= 0) {
        if (current == potentialAncestor) return true;
        current = bones[current].parentIndex;
    }
    return false;
}

void Skeleton::updateBoneIndexMap() {
    boneIndexMap.clear();
    for (int i = 0; i < bones.size(); ++i) {
        boneIndexMap[bones[i].name] = i;
    }
}

void VertexWeights::normalize() {
    float total = 0.0f;
    for (const auto& w : weights) {
        total += w.weight;
    }
    if (total > 0.0001f) {
        for (auto& w : weights) {
            w.weight /= total;
        }
    }
}

void VertexWeights::clamp(int maxInfluences) {
    if (weights.size() <= maxInfluences) return;

    std::sort(weights.begin(), weights.end());
    weights.resize(maxInfluences);
    normalize();
}

float VertexWeights::getWeight(int boneIndex) const {
    for (const auto& w : weights) {
        if (w.boneIndex == boneIndex) return w.weight;
    }
    return 0.0f;
}

void VertexWeights::setWeight(int boneIndex, float weight) {
    for (auto& w : weights) {
        if (w.boneIndex == boneIndex) {
            w.weight = weight;
            normalize();
            return;
        }
    }

    if (weight > 0.0001f) {
        weights.append({boneIndex, weight});
        normalize();
    }
}

void VertexWeights::addSkinWeight(int boneIndex, float weight) {
    for (auto& w : weights) {
        if (w.boneIndex == boneIndex) {
            w.weight += weight;
            normalize();
            return;
        }
    }
    if (weight > 0.0001f) {
        weights.append({boneIndex, weight});
        normalize();
    }
}

void SkinningData::addSkinWeight(int vertexIndex, int boneIndex, float weight) {
    SkinnedVertex sv;
    sv.vertexIndex = vertexIndex;
    sv.weights.addSkinWeight(boneIndex, weight);
}

void SkinningData::normalizeAllWeights() {
    for (auto& sv : vertices) {
        sv.weights.normalize();
    }
}

void SkinningData::computeFromSkeleton(const Skeleton& skeleton) {
    boneNames.clear();
    inverseBindMatrices.clear();
    // Use a mutable copy to compute world matrices
    Skeleton& skel = const_cast<Skeleton&>(skeleton);
    skel.computeWorldMatrices();
    for (int i = 0; i < skeleton.bones.size(); ++i) {
        boneNames.append(skeleton.bones[i].name);
        QMatrix4x4 invBind = skeleton.bones[i].worldMatrix.inverted();
        inverseBindMatrices[i] = invBind;
    }
}

float AnimationCurve::evaluate(float time) const {
    if (keyframes.isEmpty()) return 0.0f;
    if (keyframes.size() == 1) return keyframes[0].position.y();

    const AnimCurveKeyframe* kf0 = nullptr;
    const AnimCurveKeyframe* kf1 = nullptr;

    for (int i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].time <= time) {
            kf0 = &keyframes[i];
        } else {
            kf1 = &keyframes[i];
            break;
        }
    }

    if (!kf0 && kf1) return kf1->position.y();
    if (kf0 && !kf1) return kf0->position.y();
    if (!kf0 && !kf1) return 0.0f;

    float t = (time - kf0->time) / (kf1->time - kf0->time);

    if (kf0->interpolation == "LINEAR") {
        return kf0->position.y() + (kf1->position.y() - kf0->position.y()) * t;
    }

    return kf0->position.y();
}

QVector3D AnimationCurve::evaluateVec3(float time) const {
    return QVector3D(evaluate(time), evaluate(time), evaluate(time));
}

QQuaternion AnimationCurve::evaluateQuat(float time) const {
    if (keyframes.isEmpty()) return QQuaternion();
    if (keyframes.size() == 1) return keyframes[0].rotation;

    const AnimCurveKeyframe* kf0 = nullptr;
    const AnimCurveKeyframe* kf1 = nullptr;

    for (int i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].time <= time) {
            kf0 = &keyframes[i];
        } else {
            kf1 = &keyframes[i];
            break;
        }
    }

    if (!kf0) return keyframes[0].rotation;
    if (!kf1) return kf0->rotation;

    float t = (time - kf0->time) / (kf1->time - kf0->time);
    return quatSlerp(kf0->rotation, kf1->rotation, t);
}

void AnimationCurve::addKeyframe(const AnimCurveKeyframe& kf) {
    keyframes.append(kf);
    std::sort(keyframes.begin(), keyframes.end(), [](const AnimCurveKeyframe& a, const AnimCurveKeyframe& b) {
        return a.time < b.time;
    });
}

void AnimationCurve::removeKeyframe(int index) {
    if (index >= 0 && index < keyframes.size()) {
        keyframes.removeAt(index);
    }
}

void AnimationCurve::clear() {
    keyframes.clear();
}

AnimCurveKeyframe* AnimationCurve::getKeyframeBefore(float time) {
    for (int i = keyframes.size() - 1; i >= 0; --i) {
        if (keyframes[i].time <= time) return &keyframes[i];
    }
    return nullptr;
}

AnimCurveKeyframe* AnimationCurve::getKeyframeAfter(float time) {
    for (const auto& kf : keyframes) {
        if (kf.time > time) return const_cast<AnimCurveKeyframe*>(&kf);
    }
    return nullptr;
}

Animator::Animator() {}

Animator::Animator(Skeleton* skel) : skeleton(skel) {}

void Animator::play() { isPlaying = true; }

void Animator::pause() { isPlaying = false; }

void Animator::stop() {
    isPlaying = false;
    currentTime = startFrame;
}

void Animator::seek(float time) {
    currentTime = qBound(startFrame, time, endFrame);
}

void Animator::update(float deltaTime) {
    if (!isPlaying) return;

    currentTime += deltaTime * playbackSpeed * frameRate;

    if (currentTime >= endFrame) {
        if (loop) {
            currentTime = startFrame;
        } else {
            currentTime = endFrame;
            isPlaying = false;
        }
    }

    if (!skeleton) return;
    auto poses = getPosedBones(currentTime);
    for (int i = 0; i < skeleton->bones.size(); ++i) {
        Bone& bone = skeleton->bones[i];
        if (poses.contains(bone.name)) {
            const PoseBone& p = poses[bone.name];
            bone.setLocalPosition(p.position);
            bone.setLocalRotation(p.rotation);
        }
    }
    skeleton->computeWorldMatrices();
}

void Animator::setAction(const QString& actionName) {
    if (actions.contains(actionName)) {
        currentAction = actionName;
    }
}

Action* Animator::getAction(const QString& name) const {
    auto it = actions.find(name);
    if (it != actions.end()) {
        return const_cast<Action*>(&it.value());
    }
    return nullptr;
}

Action* Animator::getCurrentAction() const {
    return getAction(currentAction);
}

void Animator::addKeyframe(const QString& boneName, const SkeletonKeyframe& keyframe) {
    Action* action = getCurrentAction();
    if (!action) return;

    if (!action->tracks.contains(boneName)) {
        action->tracks[boneName] = QVector<SkeletonKeyframe>();
    }
    action->tracks[boneName].append(keyframe);
}

void Animator::removeKeyframe(const QString& boneName, float time) {
    Action* action = getCurrentAction();
    if (!action || !action->tracks.contains(boneName)) return;

    auto& keyframes = action->tracks[boneName];
    for (int i = 0; i < keyframes.size(); ++i) {
        if (qAbs(keyframes[i].time - time) < 0.001f) {
            keyframes.removeAt(i);
            return;
        }
    }
}

QMap<QString, PoseBone> Animator::getPosedBones(float time) const {
    QMap<QString, PoseBone> poses;

    if (!skeleton) return poses;

    Action* action = getCurrentAction();
    if (!action) return poses;

    for (auto& bone : skeleton->bones) {
        PoseBone pose;
        pose.name = bone.name;
        pose.boneIndex = skeleton->findBone(bone.name);

        if (action->tracks.contains(bone.name)) {
            const auto& keyframes = action->tracks[bone.name];
            for (int i = 0; i < keyframes.size() - 1; ++i) {
                if (keyframes[i].time <= time && keyframes[i + 1].time >= time) {
                    float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);
                    pose.rotation = quatSlerp(keyframes[i].rotation, keyframes[i + 1].rotation, t);
                    pose.position = keyframes[i].position + (keyframes[i + 1].position - keyframes[i].position) * t;
                    pose.scale = keyframes[i].scale + (keyframes[i + 1].scale - keyframes[i].scale) * t;
                    break;
                }
            }
        } else {
            pose.rotation = bone.rotation;
            pose.position = bone.head;
            pose.scale = bone.scale;
        }

        poses[bone.name] = pose;
    }

    return poses;
}

QMap<QString, QMatrix4x4> Animator::getBoneMatrices(float time) const {
    QMap<QString, QMatrix4x4> matrices;

    auto poses = getPosedBones(time);

    if (skeleton) {
        for (auto& bone : skeleton->bones) {
            PoseBone* pose = nullptr;
            if (poses.contains(bone.name)) {
                pose = &poses[bone.name];
            }

            QMatrix4x4 m;
            m.setToIdentity();
            m.translate(pose ? pose->position : bone.head);
            m.rotate(pose ? pose->rotation : bone.rotation);
            m.scale(pose ? pose->scale : bone.scale);

            matrices[bone.name] = m;
        }
    }

    return matrices;
}

Skeleton* RigifyGenerator::generateHumanoid(const RigifyConfig& config) {
    auto* rig = new Skeleton("Humanoid");

    int root = rig->addBone("root");
    int spine = rig->addBone("spine", root);
    int chest = rig->addBone("chest", spine);

    if (config.useTorsoIK) {
        rig->addBone("spine_01", spine);
        rig->addBone("spine_02", rig->findBone("spine_01"));
        rig->addBone("spine_03", rig->findBone("spine_02"));
    }

    if (config.useNeck) {
        rig->addBone("neck", chest);
        rig->addBone("head", rig->findBone("neck"));
    }

    if (config.useArms) {
        int shoulderL = rig->addBone("shoulder.L", chest);
        int upperArmL = rig->addBone("upper_arm.L", shoulderL);
        rig->addBone("forearm.L", upperArmL);
        rig->addBone("hand.L", rig->findBone("forearm.L"));

        int shoulderR = rig->addBone("shoulder.R", chest);
        rig->addBone("upper_arm.R", shoulderR);
        rig->addBone("forearm.R", rig->findBone("upper_arm.R"));
        rig->addBone("hand.R", rig->findBone("forearm.R"));
    }

    if (config.useLegs) {
        rig->addBone("thigh.L", root);
        rig->addBone("shin.L", rig->findBone("thigh.L"));
        rig->addBone("foot.L", rig->findBone("shin.L"));

        rig->addBone("thigh.R", root);
        rig->addBone("shin.R", rig->findBone("thigh.R"));
        rig->addBone("foot.R", rig->findBone("thigh.R"));
    }

    return rig;
}

Skeleton* RigifyGenerator::generateQuadruped(const QString& type) {
    auto* rig = new Skeleton("Quadruped");
    int root = rig->addBone("root");

    if (type == "horse") {
        rig->addBone("spine", root);
        rig->addBone("neck", rig->findBone("spine"));
        rig->addBone("head", rig->findBone("neck"));

        rig->addBone("front_leg.L.upper", rig->findBone("spine"));
        rig->addBone("front_leg.L.lower", rig->findBone("front_leg.L.upper"));
        rig->addBone("front_leg.L.foot", rig->findBone("front_leg.L.lower"));

        rig->addBone("back_leg.L.upper", root);
        rig->addBone("back_leg.L.lower", rig->findBone("back_leg.L.upper"));
        rig->addBone("back_leg.L.foot", rig->findBone("back_leg.L.lower"));
    }

    return rig;
}

Skeleton* RigifyGenerator::generateBiped() {
    RigifyConfig config;
    config.useTorsoIK = true;
    config.useArms = true;
    config.useLegs = true;
    config.useSpine = true;
    config.useNeck = true;
    return generateHumanoid(config);
}

Skeleton* RigifyGenerator::generateFKChain(int boneCount, const QVector3D& direction) {
    auto* rig = new Skeleton("FKChain");
    int prev = -1;

    for (int i = 0; i < boneCount; ++i) {
        int bone = rig->addBone(QString("bone_%1").arg(i), prev);
        if (prev >= 0) {
            rig->bones[prev].tail = QVector3D(0, 0.1f, 0);
        }
        prev = bone;
    }

    return rig;
}

Skeleton* RigifyGenerator::generateIKChain(int boneCount, float length) {
    auto* rig = new Skeleton("IKChain");
    int prev = -1;
    float segLen = length / qMax(1, boneCount);
    for (int i = 0; i < boneCount; ++i) {
        int bone = rig->addBone(QString("ik_bone_%1").arg(i), prev);
        rig->bones[bone].tail = QVector3D(0, segLen, 0);
        prev = bone;
    }
    return rig;
}

Skeleton* RigifyGenerator::addFKIKControl(Skeleton* rig, int chainStart, int chainEnd) {
    if (!rig || chainStart < 0 || chainEnd >= rig->bones.size()) return rig;
    // Add a control bone at chainEnd's tail position
    QVector3D endPos = rig->bones[chainEnd].tail;
    int ctrlBone = rig->addBone(rig->bones[chainEnd].name + "_ctrl", chainEnd);
    rig->bones[ctrlBone].head = endPos;
    rig->bones[ctrlBone].tail = endPos + QVector3D(0, 0.5f, 0);
    // Add a FK-oriented bone at chain start
    int fkBone = rig->addBone(rig->bones[chainStart].name + "_fk", -1);
    rig->bones[fkBone].head = rig->bones[chainStart].head;
    rig->bones[fkBone].tail = rig->bones[chainStart].head + QVector3D(0, 0.3f, 0);
    return rig;
}

Skeleton* RigifyGenerator::addPoleVector(Skeleton* rig, int boneIndex) {
    if (!rig || boneIndex < 0 || boneIndex >= rig->bones.size()) return rig;
    // Create a pole-target bone offset to the side
    QVector3D polePos = rig->bones[boneIndex].head + QVector3D(1, 0, 1);
    int poleBone = rig->addBone(rig->bones[boneIndex].name + "_pole", boneIndex);
    rig->bones[poleBone].head = polePos;
    rig->bones[poleBone].tail = polePos + QVector3D(0, 0.2f, 0);
    return rig;
}

Skeleton* RigifyGenerator::addSplines(Skeleton* rig, const QVector<int>& bones) {
    if (!rig || bones.isEmpty()) return rig;
    // Add B-spline control bones along the chain
    for (int i = 0; i < bones.size(); ++i) {
        int bi = bones[i];
        if (bi < 0 || bi >= rig->bones.size()) continue;
        QVector3D pos = rig->bones[bi].head;
        int splineBone = rig->addBone(rig->bones[bi].name + "_spline", bi);
        rig->bones[splineBone].head = pos + QVector3D(0.5f * (i % 3 - 1), 0.5f, 0);
        rig->bones[splineBone].tail = rig->bones[splineBone].head + QVector3D(0, 0.2f, 0);
    }
    return rig;
}

}