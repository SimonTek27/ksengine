#include <QtTest/QtTest>

#include <cmath>

#include "engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/modules/modellingEditor/ControllerSystem.h"

using namespace ks;

namespace {
const float EPS = 3e-2f; // noise is not exact; springs oscillate

bool near3(const QVector3D& a, const QVector3D& b, float eps)
{
    return (a - b).length() < eps;
}

struct SceneFixture {
    SceneGraph graph;
    QVector<SceneObject*> objects;

    SceneObject* create(const QString& name)
    {
        SceneObject* o = graph.createObject(name);
        objects.append(o);
        return o;
    }

    ~SceneFixture()
    {
        for (SceneObject* o : objects)
            graph.deleteObject(o);
        objects.clear();
    }

    SceneFixture() = default;
    SceneFixture(SceneFixture&&) = delete;
    SceneFixture& operator=(SceneFixture&&) = delete;
};
} // namespace

class TestControllerSystem : public QObject
{
    Q_OBJECT

private slots:
    void noiseDrivesChannel();
    void noiseStaysWithinAmplitude();
    void springDecaysToBase();
    void lookAtRotates();
    void attachmentFollowsVertex();
    void serializationRoundTrip();
};

void TestControllerSystem::noiseDrivesChannel()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    target->setPosition(QVector3D(0, 5, 0));
    obj->setPosition(QVector3D(0, 0, 0));
    fx.graph.updateAllTransforms();

    ControllerSystem sys;
    sys.add(obj->id(), (int)ControllerType::Noise, target->id(), "target",
            "position.y", /*base*/ 2.0f, /*amplitude*/ 3.0f, 1.0f, 0.0f, 50.0f, 2.0f);
    QCOMPARE(sys.evaluate(obj, &fx.graph, 0.0f), 1);
    const float v0 = obj->position().y();
    QCOMPARE(sys.evaluate(obj, &fx.graph, 1.7f), 1);
    const float v1 = obj->position().y();
    // Noise output varies in time, within [base-amp, base+amp].
    QVERIFY(v0 >= -1.0f - EPS && v0 <= 5.0f + EPS);
    QVERIFY(v1 >= -1.0f - EPS && v1 <= 5.0f + EPS);
    QVERIFY(qAbs(v1 - v0) > 1e-6f); // different times -> different samples
    // Same time -> identical (deterministic).
    QCOMPARE(sys.evaluate(obj, &fx.graph, 1.7f), 1);
    QVERIFY(qAbs(obj->position().y() - v1) < 1e-6f);
}

void TestControllerSystem::noiseStaysWithinAmplitude()
{
    ControllerSystem sys;
    bool ok = true;
    for (int i = 0; i < 200; ++i) {
        float n = ControllerSystem::valueNoise(i * 0.37f);
        if (n < -1e-9f || n > 1.0f + 1e-9f) ok = false;
    }
    QVERIFY(ok);
}

void TestControllerSystem::springDecaysToBase()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    obj->setPosition(QVector3D(0, 10, 0));
    fx.graph.updateAllTransforms();

    ControllerSystem sys;
    sys.add(obj->id(), (int)ControllerType::Spring, target->id(), "target",
            "position.y", /*base*/ 0.0f, /*amplitude*/ 10.0f, 1.0f, 0.0f,
            /*stiffness*/ 50.0f, /*damping*/ 4.0f);
    // Eval far in the future: the damped sine has decayed to (near) base.
    QCOMPARE(sys.evaluate(obj, &fx.graph, 100.0f), 1);
    QVERIFY(qAbs(obj->position().y() - 0.0f) < 1e-2f);
}

void TestControllerSystem::lookAtRotates()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    obj->setPosition(QVector3D(0, 0, 0));
    target->setPosition(QVector3D(5, 0, 0)); // off the +Z aim axis -> real rotation
    fx.graph.updateAllTransforms();

    ControllerSystem sys;
    sys.add(obj->id(), (int)ControllerType::LookAt, target->id(), "target",
            "rotation.z", 0.0f, 1.0f, 1.0f, 0.0f, 50.0f, 2.0f);
    QCOMPARE(sys.evaluate(obj, &fx.graph, 0.0f), 1);
    // A rotation was applied (position preserved, orientation changed away from identity).
    QVERIFY(near3(obj->position(), QVector3D(0, 0, 0), 1e-3f));
    QVERIFY(obj->rotationEuler().length() > 0.01f);
}

void TestControllerSystem::attachmentFollowsVertex()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");

    // Pure helper path (no SceneMesh needed).
    QVector<QVector3D> localVerts;
    localVerts.append(QVector3D(0, 0, 0));
    localVerts.append(QVector3D(1, 0, 0));
    localVerts.append(QVector3D(0, 1, 0));
    QMatrix4x4 world;
    world.translate(5, 0, 0); // target sits at +5 X in world space
    QVector3D wp;
    QVERIFY(ControllerSystem::vertexWorldPos(localVerts, world, 1, wp));
    QVERIFY(near3(wp, QVector3D(6, 0, 0), 1e-4f)); // vertex 1 local (1,0,0) -> (6,0,0)

    // Controller path: attaching to a target that has no mesh is a no-op.
    ControllerSystem sys;
    sys.add(obj->id(), (int)ControllerType::Attachment, target->id(), "target",
            "position.x", 0.0f, 1.0f, 1.0f, 0.0f, 50.0f, 2.0f);
    sys.setAttachment(obj->id(), 0, 1, QVector3D(0, 0, 0));
    // Without a mesh on the target the Attachment solver is a no-op.
    QCOMPARE(sys.evaluate(obj, &fx.graph, 0.0f), 0);
    QVERIFY(near3(obj->position(), QVector3D(0, 0, 0), 1e-4f));
}

void TestControllerSystem::serializationRoundTrip()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");

    ControllerSystem sys;
    sys.add(obj->id(), (int)ControllerType::Noise, target->id(), "noisetarget",
            "position.z", 3.0f, 4.0f, 2.0f, 0.5f, 50.0f, 2.0f);
    sys.add(obj->id(), (int)ControllerType::Spring, target->id(), "springtarget",
            "rotation.z", 1.0f, 2.0f, 3.0f, 0.0f, 80.0f, 5.0f);

    ControllerSystem sys2;
    SceneObject* obj2 = fx.create("obj2");
    SceneObject* target2 = fx.create("target2");
    Q_UNUSED(target2);
    for (const auto& c : sys.forObject(obj->id())) {
        QVariant v = c.toVariant();
        ControllerDef restored;
        restored.fromVariant(v);
        sys2.add(obj2->id(), restored.type, target->id(), restored.targetName,
                 restored.channel, restored.base, restored.amplitude, restored.frequency,
                 restored.phase, restored.stiffness, restored.damping);
    }

    QCOMPARE(sys2.count(obj2->id()), 2);
    QCOMPARE(sys2.evaluate(obj2, &fx.graph, 1.0f), 2);
    const auto c0 = sys2.forObject(obj2->id()).at(0);
    QCOMPARE(c0.channel, QStringLiteral("position.z"));
    QVERIFY(qAbs(c0.base - 3.0f) < 1e-4f);
    QVERIFY(qAbs(c0.amplitude - 4.0f) < 1e-4f);
    const auto c1 = sys2.forObject(obj2->id()).at(1);
    QVERIFY(qAbs(c1.stiffness - 80.0f) < 1e-4f);
    QVERIFY(qAbs(c1.damping - 5.0f) < 1e-4f);
}

QTEST_MAIN(TestControllerSystem)

#include "test_ControllerSystem.moc"
