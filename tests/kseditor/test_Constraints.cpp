#include <QtTest/QtTest>

#include <cmath>

#include "engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/modules/modellingEditor/ConstraintSystem.h"

using namespace ks;

namespace {
const float EPS = 1e-3f;

bool near3(const QVector3D& a, const QVector3D& b, float eps = EPS)
{
    return (a - b).length() < eps;
}

// SceneGraph::~SceneGraph -> deleteRecursive(root) does not unlink children from
// the root list, so the root's ~SceneObject would qDeleteAll already-freed children.
// Explicitly delete created objects through SceneGraph::deleteObject() (which
// removeChild()s first) so teardown is safe.
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

class TestConstraints : public QObject
{
    Q_OBJECT

private slots:
    void pointConstraint();
    void orientationConstraint();
    void pathConstraint();
    void pathFollowAlignsTangent();
    void attachmentConstraint();
    void linkConstraintMaintainsRelative();
    void springEasesTowardTarget();
    void serializationRoundTrip();
};

void TestConstraints::pointConstraint()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    target->setPosition(QVector3D(5, 6, 7));
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Point, target->id(), "target",
            QVector3D(1, 2, 3));
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    QVERIFY(near3(obj->position(), QVector3D(6, 8, 10)));
}

void TestConstraints::orientationConstraint()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    obj->setPosition(QVector3D(1, 2, 3));
    target->setPosition(QVector3D(0, 0, 0));
    target->setRotationEuler(QVector3D(0, 0, 1.5707963f)); // 90 deg about Z
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Orientation, target->id(), "target");
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    // Pure orientation: position stays, rotation follows target.
    QVERIFY(near3(obj->position(), QVector3D(1, 2, 3)));
    QVERIFY(std::fabs(obj->rotationEuler().z() - 1.5707963f) < EPS);
}

void TestConstraints::pathConstraint()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    target->setPosition(QVector3D(10, 0, 0));
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Path, target->id(), "spline");
    const int idx = 0;
    sys.setPath(obj->id(), idx, { QVector3D(0, 0, 0), QVector3D(0, 0, 10) });
    sys.setParam(obj->id(), idx, 0.5f);
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    // Target-local sample (0,0,5) mapped through target world = (10,0,5).
    QVERIFY(near3(obj->position(), QVector3D(10, 0, 5)));
}

void TestConstraints::pathFollowAlignsTangent()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    target->setPosition(QVector3D(0, 0, 0));
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Path, target->id(), "spline");
    const int idx = 0;
    // Path runs along +X so 'follow' must rotate the object (non-trivial).
    sys.setPath(obj->id(), idx, { QVector3D(0, 0, 0), QVector3D(10, 0, 0) });
    sys.setParam(obj->id(), idx, 0.5f);
    sys.setFollow(obj->id(), idx, true);
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    // Follow keeps the object on the path midpoint and applies a rotation.
    QVERIFY(near3(obj->position(), QVector3D(5, 0, 0)));
    QVERIFY(obj->rotationEuler().length() > 1e-3f); // follow engaged a rotation
}

void TestConstraints::attachmentConstraint()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("meshTarget");
    target->setPosition(QVector3D(10, 0, 0));
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Attachment, target->id(), "meshTarget");
    const int idx = 0;
    sys.setPath(obj->id(), idx, { QVector3D(0, 0, 0), QVector3D(5, 5, 5) });
    sys.setParam(obj->id(), idx, 1.0f);
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    QVERIFY(near3(obj->position(), QVector3D(15, 5, 5)));
}

void TestConstraints::linkConstraintMaintainsRelative()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    target->setPosition(QVector3D(5, 0, 0));
    obj->setPosition(QVector3D(10, 0, 0));
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Link, target->id(), "target");
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    QVERIFY(near3(obj->position(), QVector3D(10, 0, 0))); // still locked

    target->setPosition(QVector3D(7, 0, 0));
    fx.graph.updateAllTransforms();
    QCOMPARE(sys.evaluate(obj, &fx.graph), 1);
    // Object keeps its captured +5 relative offset along X.
    QVERIFY(near3(obj->position(), QVector3D(12, 0, 0)));
}

void TestConstraints::springEasesTowardTarget()
{
    SceneFixture fx;
    SceneObject* obj = fx.create("obj");
    SceneObject* target = fx.create("target");
    target->setPosition(QVector3D(10, 0, 0));
    fx.graph.updateAllTransforms();

    ConstraintSystem sys;
    sys.add(obj->id(), (int)ConstraintType::Spring, target->id(), "target");
    const int idx = 0;
    sys.setSpringParams(obj->id(), idx, 25.0f, 1.0f);
    for (int i = 0; i < 400; ++i) {
        target->setPosition(QVector3D(10, 0, 0));
        fx.graph.updateAllTransforms();
        sys.evaluate(obj, &fx.graph);
    }
    // Converged close to the target (stiff, lightly damped).
    QVERIFY(obj->position().x() > 8.5f);
    QVERIFY(obj->position().y() < EPS);
    QVERIFY(obj->position().z() < EPS);
}

void TestConstraints::serializationRoundTrip()
{
    ConstraintDef src;
    src.type = 4;
    src.targetId = 7;
    src.targetName = "spline";
    src.offset = QVector3D(1, 2, 3);
    src.offsetRot = QVector3D(0, 0, 0);
    src.param = 0.75f;
    src.stiffness = 42.0f;
    src.damping = 3.5f;
    src.follow = true;
    src.enabled = false;
    src.path = { QVector3D(0, 0, 0), QVector3D(0, 0, 10), QVector3D(0, 0, 20) };

    ConstraintDef dst;
    dst.fromVariant(src.toVariant());

    QCOMPARE(dst.type, src.type);
    QCOMPARE(dst.targetId, src.targetId);
    QCOMPARE(dst.targetName, src.targetName);
    QVERIFY(near3(dst.offset, src.offset));
    QVERIFY(near3(dst.offsetRot, src.offsetRot));
    QVERIFY(std::fabs(dst.param - src.param) < EPS);
    QVERIFY(std::fabs(dst.stiffness - src.stiffness) < EPS);
    QVERIFY(std::fabs(dst.damping - src.damping) < EPS);
    QCOMPARE(dst.follow, src.follow);
    QCOMPARE(dst.enabled, src.enabled);
    QCOMPARE(dst.path.size(), src.path.size());
    for (int i = 0; i < dst.path.size(); ++i)
        QVERIFY(near3(dst.path[i], src.path[i]));
}

QTEST_APPLESS_MAIN(TestConstraints)
#include "test_Constraints.moc"
