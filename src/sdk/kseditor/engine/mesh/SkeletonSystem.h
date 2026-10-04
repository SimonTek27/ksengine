#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <QVariant>

namespace ks {

struct Bone {
    QString name;
    int parentIndex = -1;
    QVector<int> children;

    QVector3D head;
    QVector3D tail;
    QQuaternion rotation;
    QVector3D scale = {1, 1, 1};

    QMatrix4x4 localMatrix;
    QMatrix4x4 worldMatrix;

    float length() const { return (tail - head).length(); }
    QVector3D direction() const { return (tail - head).normalized(); }

    QVector3D getLocalPosition() const { return head; }
    void setLocalPosition(const QVector3D& pos) {
        QVector3D delta = pos - head;
        head = pos;
        tail += delta;
    }

    QQuaternion getLocalRotation() const { return rotation; }
    void setLocalRotation(const QQuaternion& rot) {
        rotation = rot;
        QMatrix4x4 m;
        m.rotate(rot);
        tail = head + m.mapVector({0, length(), 0});
    }
};

struct PoseBone {
    QString name;
    int boneIndex = -1;

    QQuaternion rotation;
    QVector3D position;
    QVector3D scale = {1, 1, 1};

    QVector3D location;
    QQuaternion quat;
    QVector4D euler;

    float lockLocationX = 0, lockLocationY = 0, lockLocationZ = 0;
    float lockRotationX = 0, lockRotationY = 0, lockRotationZ = 0;

    bool hide = false;
    bool select = false;
    bool isIK = false;
    int ikChainLength = 0;
    int ikCount = 0;
    float ikStretch = 0.0f;
};

struct SkeletonKeyframe {
    float time;
    QVector3D position;
    QQuaternion rotation;
    QVector3D scale;
    QString interpolation = "LINEAR";
};

struct Action {
    QString name;
    float startFrame = 0.0f;
    float endFrame = 0.0f;
    float fps = 24.0f;

    QMap<QString, QVector<SkeletonKeyframe>> tracks;
    QMap<QString, QVariantMap> fcurves;
};

class Skeleton {
public:
    Skeleton();
    explicit Skeleton(const QString& name);

    QString name;
    QVector<Bone> bones;
    QMap<QString, int> boneIndexMap;

    int addBone(const QString& name, int parentIndex = -1);
    int findBone(const QString& name) const;
    void removeBone(int index);

    void setBoneParent(int boneIndex, int newParentIndex);
    void updateHierarchy();

    QMatrix4x4 getBoneLocalMatrix(int index) const;
    QMatrix4x4 getBoneWorldMatrix(int index) const;

    void computeWorldMatrices();
    void computeInverseBindMatrices();

    QVector<int> getBoneChildren(int index) const;
    QVector<int> getBoneAncestors(int index) const;

    QVector3D getBonePosition(int index) const;
    QQuaternion getBoneRotation(int index) const;
    void setBonePosition(int index, const QVector3D& pos);
    void setBoneRotation(int index, const QQuaternion& rot);

    bool isAncestor(int boneIndex, int potentialAncestor) const;

    QVector<Skeleton*> children;
    Skeleton* parent = nullptr;

    QMap<int, QMatrix4x4> inverseBindMatrices() const { return m_invBindMatrices; }

private:
    void updateBoneIndexMap();
    QMap<int, QMatrix4x4> m_invBindMatrices;
};

struct SkinWeight {
    int boneIndex;
    float weight;
    bool operator<(const SkinWeight& other) const { return weight > other.weight; }
};

struct VertexWeights {
    QVector<SkinWeight> weights;

    void normalize();
    void clamp(int maxInfluences = 4);
    float getWeight(int boneIndex) const;
    void setWeight(int boneIndex, float weight);
    void addSkinWeight(int boneIndex, float weight);
};

struct SkinnedVertex {
    int vertexIndex;
    VertexWeights weights;
};

struct SkinningData {
    QVector<SkinnedVertex> vertices;
    QMap<int, QMatrix4x4> inverseBindMatrices;
    QStringList boneNames;

    void addSkinWeight(int vertexIndex, int boneIndex, float weight);
    void normalizeAllWeights();
    void computeFromSkeleton(const Skeleton& skeleton);
};

struct AnimCurveKeyframe {
    float time;
    QVector3D position;
    QQuaternion rotation;
    QVector3D scale = {1, 1, 1};
    QString interpolation = "LINEAR";
};

class AnimationCurve {
public:
    QString dataPath;
    int arrayIndex = 0;

    QVector<AnimCurveKeyframe> keyframes;

    float evaluate(float time) const;
    QVector3D evaluateVec3(float time) const;
    QQuaternion evaluateQuat(float time) const;

    void addKeyframe(const AnimCurveKeyframe& kf);
    void removeKeyframe(int index);
    void clear();

    AnimCurveKeyframe* getKeyframeBefore(float time);
    AnimCurveKeyframe* getKeyframeAfter(float time);
};

class Animator {
public:
    Animator();
    explicit Animator(Skeleton* skeleton);

    Skeleton* skeleton = nullptr;
    QMap<QString, Action> actions;
    QString currentAction;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool isPlaying = false;
    bool loop = true;

    float startFrame = 0.0f;
    float endFrame = 100.0f;
    float frameRate = 24.0f;

    QMap<QString, AnimationCurve> curves;

    void play();
    void pause();
    void stop();
    void seek(float time);
    void update(float deltaTime);

    void setAction(const QString& actionName);
    Action* getAction(const QString& name) const;
    Action* getCurrentAction() const;

    void addKeyframe(const QString& boneName, const SkeletonKeyframe& keyframe);
    void removeKeyframe(const QString& boneName, float time);

    QMap<QString, PoseBone> getPosedBones(float time) const;
    QMap<QString, QMatrix4x4> getBoneMatrices(float time) const;
};

struct RigifyConfig {
    bool autoGenerateUI = true;
    bool useTorsoIK = true;
    bool useArms = true;
    bool useLegs = true;
    bool useSpine = true;
    bool useNeck = true;
    bool useFace = true;

    int spineCount = 3;
    int fingerCount = 5;
    int fingerSegments = 3;
};

class RigifyGenerator {
public:
    static Skeleton* generateHumanoid(const RigifyConfig& config);
    static Skeleton* generateQuadruped(const QString& type);
    static Skeleton* generateBiped();
    static Skeleton* generateFKChain(int boneCount, const QVector3D& direction);
    static Skeleton* generateIKChain(int boneCount, float length);

    static Skeleton* addFKIKControl(Skeleton* rig, int chainStart, int chainEnd);
    static Skeleton* addPoleVector(Skeleton* rig, int boneIndex);
    static Skeleton* addSplines(Skeleton* rig, const QVector<int>& bones);
};

}