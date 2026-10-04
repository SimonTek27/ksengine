#pragma once

#include <QObject>
#include <QImage>
#include <QColor>
#include <QVector>
#include <QMap>
#include <QRect>
#include <QPoint>
#include <QVector3D>
#include <QQuaternion>
#include <QMatrix4x4>
#include <QMatrix3x3>
#include <QVariant>

namespace ks {
namespace animation {

// ============================================================================
// Animation State Machine
// ============================================================================

class StateMachine : public QObject {
    Q_OBJECT
public:
    struct State {
        QString id;
        QString name;
        QMap<QString, QVariant> properties;
    };

    struct Transition {
        QString id;
        QString fromState;
        QString toState;
        QString condition;
        double duration = 0.3;
        QString easing = "linear";
    };

    explicit StateMachine(QObject* parent = nullptr);
    ~StateMachine() override = default;

    void addState(const State& state);
    void removeState(const QString& stateId);
    void addTransition(const Transition& transition);
    void removeTransition(const QString& transitionId);

    void start(const QString& initialState = QString());
    void stop();
    void setState(const QString& stateId);
    QString currentState() const { return m_currentState; }

    void update(double deltaTime);
    void evaluateTransitions();

    QMap<QString, State> states() const { return m_states; }
    QVector<Transition> transitions() const { return m_transitions; }

signals:
    void stateChanged(const QString& fromState, const QString& toState);
    void transitionStarted(const QString& fromState, const QString& toState);
    void transitionFinished(const QString& fromState, const QString& toState);

private:
    QMap<QString, State> m_states;
    QVector<Transition> m_transitions;
    QString m_currentState;
    QString m_targetState;
    double m_transitionProgress = 0.0;
    double m_transitionDuration = 0.0;
    bool m_running = false;

    void startTransition(const QString& toState, double duration, const QString& easing);
    double applyEasing(double t, const QString& easing) const;
};

// ============================================================================
// Animation Curves
// ============================================================================

class AnimationCurve : public QObject {
    Q_OBJECT
public:
    struct Keyframe {
        double time = 0.0;
        QVariant value;
        QString interpolation = "linear"; // linear, cubic, bezier
        double inTangent = 0.0;
        double outTangent = 0.0;
    };

    explicit AnimationCurve(QObject* parent = nullptr);
    ~AnimationCurve() override = default;

    enum ExtrapolationMode {
        Constant,
        Linear,
        Cycle,
        CycleOffset,
        Bounce
    };

    void addKeyframe(const Keyframe& kf);
    void removeKeyframe(double time);
    void clearKeyframes();

    QVariant evaluate(double time) const;
    QVector<Keyframe> keyframes() const { return m_keyframes; }

    void setExtrapolationMode(ExtrapolationMode pre, ExtrapolationMode post);

signals:
    void keyframesChanged();

private:
    QVector<Keyframe> m_keyframes;
    ExtrapolationMode m_preExtrapolation = Constant;
    ExtrapolationMode m_postExtrapolation = Constant;

    QVariant interpolate(const Keyframe& a, const Keyframe& b, double t) const;
    QVariant interpolateDouble(double a, double b, double t, const QString& interpolation) const;
    QColor interpolateColor(const QColor& a, const QColor& b, double t) const;
    QVector3D interpolateVector3D(const QVector3D& a, const QVector3D& b, double t) const;
    QQuaternion interpolateQuaternion(const QQuaternion& a, const QQuaternion& b, double t) const;
};

// ============================================================================
// Blend Tree
// ============================================================================

class BlendTree : public QObject {
    Q_OBJECT
public:
    enum BlendType {
        Blend1D,
        Blend2D,
        Direct
    };

    struct BlendNode {
        QString name;
        BlendType type = Blend1D;
        QVector<double> thresholds; // for 1D
        QVector<QPointF> positions; // for 2D
        QVector<QString> animations; // animation clip names
        double weight = 1.0;
    };

    explicit BlendTree(QObject* parent = nullptr);
    ~BlendTree() override = default;

    void addNode(const BlendNode& node);
    void removeNode(const QString& name);
    void setParameter(const QString& param, double value);
    void setParameter2D(const QString& param, double x, double y);

    QVector<QPair<QString, double>> evaluate() const; // returns animation -> weight

signals:
    void weightsChanged();

private:
    QMap<QString, BlendNode> m_nodes;
    QMap<QString, double> m_parameters;
    QMap<QString, QPointF> m_parameters2D;

    double calculateBlend1D(const BlendNode& node, double param) const;
    QPair<double, double> calculateBlend2D(const BlendNode& node, double x, double y) const;
};

// ============================================================================
// Inverse Kinematics
// ============================================================================

class IKSolver {
    public:
    struct Joint {
        QString name;
        QVector3D position;
        QVector3D rotation;
        QVector3D minLimits;
        QVector3D maxLimits;
        QString parent;
    };

    struct Chain {
        QString name;
        QVector<QString> joints; // ordered from root to tip
        QVector3D targetPosition;
        QVector3D targetRotation;
        double maxIterations = 10;
        double tolerance = 0.001;
    };

    explicit IKSolver();
    ~IKSolver() = default;

    void addJoint(const Joint& joint);
    void removeJoint(const QString& name);
    void addChain(const Chain& chain);
    void removeChain(const QString& name);

    void solve(double deltaTime);
    void setTarget(const QString& chainName, const QVector3D& position, const QVector3D& rotation = QVector3D());
    
    QMap<QString, Joint> joints() const;
    QMap<QString, Chain> chains() const { return m_chains; }

private:
    struct JointData {
        Joint joint;
        QVector3D worldPos;
        QQuaternion worldRot;
        QMatrix4x4 localToWorld;
    };

    QMap<QString, JointData> m_joints;
    QMap<QString, Chain> m_chains;
    QMap<QString, QVector<QString>> m_children;

    void updateWorldTransforms();
    void computeWorldTransform(const QString& jointName, const QMatrix4x4& parentMatrix,
                               const QQuaternion& parentRotation, const QVector3D& parentPos);
    bool solveFABRIK(const QString& chainName);
    bool solveCCD(const QString& chainName);
    void applyLimits(const QString& jointName, QQuaternion& rotation) const;
    QMatrix4x4 computeLocalToWorld(const QString& jointName) const;
};

// ============================================================================
// Physics-driven Animation
// ============================================================================

class PhysicsAnimation {
    public:
    struct Body {
        QString name;
        QVector3D position;
        QVector3D velocity;
        QVector3D angularVelocity;
        QQuaternion rotation;
        double mass = 1.0;
        QMatrix3x3 inertiaTensor;
        bool kinematic = false;
    };

    struct Constraint {
        enum Type { Fixed, Hinge, Slider, ConeTwist, Generic6DOF };
        Type type = Fixed;
        QString bodyA;
        QString bodyB;
        QVector3D pivotA;
        QVector3D pivotB;
        QVector3D axis;
        QVector3D limitsMin;
        QVector3D limitsMax;
        double stiffness = 1.0;
        double damping = 0.1;
    };

    struct Spring {
        QString bodyA;
        QString bodyB;
        QVector3D anchorA;
        QVector3D anchorB;
        double restLength = 1.0;
        double stiffness = 100.0;
        double damping = 1.0;
    };

    explicit PhysicsAnimation();
    ~PhysicsAnimation() = default;

    void addBody(const Body& body);
    void removeBody(const QString& name);
    void addConstraint(const Constraint& constraint);
    void removeConstraint(const QString& name);
    void addSpring(const Spring& spring);

    void step(double deltaTime);
    void setGravity(const QVector3D& gravity) { m_gravity = gravity; }
    QVector3D gravity() const { return m_gravity; }

    QMap<QString, Body> bodies() const { return m_bodies; }
    QVector<Constraint> constraints() const { return m_constraints; }
    QVector<Spring> springs() const { return m_springs; }

    // Ragdoll helpers
    static QVector<Body> createRagdoll(const QVector3D& rootPos, double height, double mass);
    static QVector<Constraint> createRagdollConstraints(const QVector<Body>& bodies);

private:
    void integrateBodies(double dt);
    void solveConstraints(double dt);
    void solveSprings(double dt);
    void applyGravity(double dt);

    QMap<QString, Body> m_bodies;
    QVector<Constraint> m_constraints;
    QVector<Spring> m_springs;
    QVector3D m_gravity = QVector3D(0, -9.81, 0);
    double m_timeStep = 1.0 / 60.0;
    int m_solverIterations = 10;
};

// ============================================================================
// Animation System
// ============================================================================

class AnimationSystem : public QObject {
    Q_OBJECT
public:
    struct Clip {
        QString name;
        double duration = 0.0;
        double frameRate = 30.0;
        bool loop = true;
        QMap<QString, AnimationCurve> curves;
        QMap<QString, BlendTree> blendTrees;
    };

    struct AnimationState {
        QString clipName;
        double time = 0.0;
        double speed = 1.0;
        double weight = 1.0;
        bool playing = false;
        bool loop = true;
        double blendTime = 0.2;
        double blendProgress = 1.0;
        QString nextState;
    };

    struct Layer {
        QString name;
        int priority = 0;
        double weight = 1.0;
        QMap<QString, AnimationState> states; // state name -> state
        QString currentState;
        QString nextState;
        double blendProgress = 1.0;
    };

    explicit AnimationSystem(QObject* parent = nullptr);
    ~AnimationSystem() override = default;

    void addClip(const Clip& clip);
    void removeClip(const QString& name);
    void addLayer(const Layer& layer);
    void removeLayer(const QString& name);

    void play(const QString& layerName, const QString& clipName, double blendTime = 0.2);
    void stop(const QString& layerName);
    void setLayerWeight(const QString& layerName, double weight);
    void setClipSpeed(const QString& layerName, double speed);
    void setClipTime(const QString& layerName, double time);

    void update(double deltaTime);

    QMap<QString, QVector3D> evaluateBonePositions() const;
    QMap<QString, QQuaternion> evaluateBoneRotations() const;
    QMap<QString, QVector3D> evaluateBoneScales() const;

    const Clip* clip(const QString& name) const;
    const Layer* layer(const QString& name) const;

signals:
    void clipFinished(const QString& layerName, const QString& clipName);
    void stateChanged(const QString& layerName, const QString& state);

private:
    QMap<QString, Clip> m_clips;
    QMap<QString, Layer> m_layers;

    void updateLayer(Layer& layer, double deltaTime);
    void blendStates(Layer& layer, double deltaTime);
    void evaluateClip(const Clip& clip, double time, 
                      QMap<QString, QVector3D>& positions,
                      QMap<QString, QQuaternion>& rotations,
                      QMap<QString, QVector3D>& scales) const;
};

} // namespace animation
} // namespace ks

Q_DECLARE_METATYPE(ks::animation::StateMachine::State)
Q_DECLARE_METATYPE(ks::animation::StateMachine::Transition)
Q_DECLARE_METATYPE(ks::animation::AnimationCurve::Keyframe)
Q_DECLARE_METATYPE(ks::animation::AnimationCurve::ExtrapolationMode)
Q_DECLARE_METATYPE(ks::animation::BlendTree::BlendNode)
Q_DECLARE_METATYPE(ks::animation::BlendTree::BlendType)
Q_DECLARE_METATYPE(ks::animation::IKSolver::Joint)
Q_DECLARE_METATYPE(ks::animation::IKSolver::Chain)
Q_DECLARE_METATYPE(ks::animation::PhysicsAnimation::Body)
Q_DECLARE_METATYPE(ks::animation::PhysicsAnimation::Constraint)
Q_DECLARE_METATYPE(ks::animation::PhysicsAnimation::Spring)
Q_DECLARE_METATYPE(ks::animation::AnimationSystem::Clip)
Q_DECLARE_METATYPE(ks::animation::AnimationSystem::Layer)
Q_DECLARE_METATYPE(ks::animation::AnimationSystem::AnimationState)