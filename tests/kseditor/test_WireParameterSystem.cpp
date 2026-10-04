#include <QtTest/QtTest>

#include "engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/modules/modellingEditor/WireParameterSystem.h"
#include "sdk/kseditor/modules/modellingEditor/ExpressionEvaluator.h"

using namespace ks;

namespace {
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

class TestWireParameterSystem : public QObject
{
    Q_OBJECT

private slots:
    void wiresPositionChannel();
    void scaleAndOffset();
    void disabledBindingIgnored();
    void wireListAndRemove();
    void serializationRoundTrip();
    void expressionBinding();
    void expressionEvaluatesCorrectly();
};

void TestWireParameterSystem::wiresPositionChannel()
{
    SceneFixture fx;
    SceneObject* driver = fx.create("driver");
    SceneObject* driven = fx.create("driven");
    fx.graph.updateAllTransforms();

    WireParameterSystem sys;
    QVERIFY(sys.add(driver->id(), "driver", "position.x",
                    driven->id(), "driven", "position.y", 1.0f, 0.0f));
    driver->setPosition(QVector3D(7, 0, 0));
    QCOMPARE(sys.evaluate(&fx.graph), 1);
    // driven.position.y follows driver.position.x because the scale map is 1:1.
    QVERIFY(qAbs(driven->position().y() - 7.0f) < 1e-4f);
}

void TestWireParameterSystem::scaleAndOffset()
{
    SceneFixture fx;
    SceneObject* driver = fx.create("driver");
    SceneObject* driven = fx.create("driven");
    fx.graph.updateAllTransforms();

    WireParameterSystem sys;
    QVERIFY(sys.add(driver->id(), "driver", "position.x",
                    driven->id(), "driven", "scale.z", 2.0f, 0.5f));
    driver->setPosition(QVector3D(3, 0, 0));
    sys.evaluate(&fx.graph);
    QVERIFY(qAbs(driven->scale().z() - (3.0f * 2.0f + 0.5f)) < 1e-4f);
}

void TestWireParameterSystem::disabledBindingIgnored()
{
    SceneFixture fx;
    SceneObject* driver = fx.create("driver");
    SceneObject* driven = fx.create("driven");
    fx.graph.updateAllTransforms();

    WireParameterSystem sys;
    QVERIFY(sys.add(driver->id(), "driver", "position.x",
                    driven->id(), "driven", "position.y", 1.0f, 0.0f));
    QVERIFY(sys.setEnabled(driven->id(), 0, false));
    driver->setPosition(QVector3D(9, 0, 0));
    sys.evaluate(&fx.graph);
    QVERIFY(qAbs(driven->position().y() - 0.0f) < 1e-4f);
}

void TestWireParameterSystem::wireListAndRemove()
{
    SceneFixture fx;
    SceneObject* driver = fx.create("driver");
    SceneObject* driven = fx.create("driven");
    SceneObject* other = fx.create("other");
    fx.graph.updateAllTransforms();

    WireParameterSystem sys;
    QVERIFY(sys.add(driver->id(), "driver", "position.x", driven->id(), "driven", "position.z", 1.0f, 0.0f));
    QVERIFY(sys.add(other->id(), "other", "position.y", driven->id(), "driven", "position.x", 1.0f, 0.0f));

    // Two bindings both drive `driven`.
    QCOMPARE(sys.forObject(driven->id()).size(), 2);
    // `driver` drives one binding.
    QCOMPARE(sys.drivenBy(driver->id()).size(), 1);

    QVERIFY(sys.remove(driven->id(), 0));
    QCOMPARE(sys.forObject(driven->id()).size(), 1);

    sys.clearDriver(other->id());
    QCOMPARE(sys.forObject(driven->id()).size(), 0);
}

void TestWireParameterSystem::serializationRoundTrip()
{
    SceneFixture fx;
    SceneObject* driver = fx.create("driver");
    SceneObject* driven = fx.create("driven");
    fx.graph.updateAllTransforms();

    WireParameterSystem sys;
    QVERIFY(sys.add(driver->id(), "driver", "position.x",
                    driven->id(), "driven", "scale.z", 3.0f, 1.25f));
    const auto b = sys.forObject(driven->id()).constFirst();
    WireBinding restored;
    restored.fromVariant(b.toVariant());
    QCOMPARE(restored.driverProp, QStringLiteral("position.x"));
    QCOMPARE(restored.drivenProp, QStringLiteral("scale.z"));
    QVERIFY(qAbs(restored.scale - 3.0f) < 1e-4f);
    QVERIFY(qAbs(restored.offset - 1.25f) < 1e-4f);
    QVERIFY(restored.enabled);
}

void TestWireParameterSystem::expressionBinding()
{
    SceneFixture fx;
    SceneObject* driver = fx.create("driver");
    SceneObject* driven = fx.create("driven");
    fx.graph.updateAllTransforms();

    WireParameterSystem sys;
    QVERIFY(sys.add(driver->id(), "driver", "position.x",
                    driven->id(), "driven", "position.y", 1.0f, 0.0f));
    // Non-linear mapping: driven.y = sin(x) * 2.
    QVERIFY(sys.setExpression(driven->id(), 0, "sin(x) * 2"));
    driver->setPosition(QVector3D(1.5707963f, 0, 0));  // pi/2
    QCOMPARE(sys.evaluate(&fx.graph), 1);
    QVERIFY(qAbs(driven->position().y() - 2.0f) < 1e-3f);

    // Clearing the expression restores the legacy affine map (x * 3 + 1).
    QVERIFY(sys.setExpression(driven->id(), 0, QString()));
    QVERIFY(sys.setParams(driven->id(), 0, 3.0f, 1.0f));
    driver->setPosition(QVector3D(4, 0, 0));
    sys.evaluate(&fx.graph);
    QVERIFY(qAbs(driven->position().y() - 13.0f) < 1e-3f);
}

void TestWireParameterSystem::expressionEvaluatesCorrectly()
{
    using expr::ExpressionEvaluator;
    bool ok = false;
    QVERIFY(qFuzzyCompare(ExpressionEvaluator::evaluate("2 + 3 * 4", 0, &ok), 14.0));
    QVERIFY(ok);
    ok = false;
    QVERIFY(qFuzzyCompare(ExpressionEvaluator::evaluate("sqrt(16) + pow(2, 3)", 0, &ok), 12.0));
    QVERIFY(ok);
    ok = false;
    QVERIFY(qFuzzyCompare(ExpressionEvaluator::evaluate("smoothstep(0, 1, x)", 0.5, &ok), 0.5));
    QVERIFY(ok);
    ok = true;
    ExpressionEvaluator::evaluate("2 +", 0, &ok);
    QVERIFY(!ok);
}

QTEST_MAIN(TestWireParameterSystem)

#include "test_WireParameterSystem.moc"
