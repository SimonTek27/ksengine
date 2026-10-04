#include <QtTest/QtTest>

#include "engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/modules/modellingEditor/SkinWrapSystem.h"

using namespace ks;

namespace {
const float EPS = 1e-3f;

bool near3(const QVector3D& a, const QVector3D& b, float eps = EPS)
{
    return (a - b).length() < eps;
}

// Unit cube cage (edges on +/- 0.5 planes) with correct triangle orientation.
void cubeCage(QVector<QVector3D>& verts, QVector<uint32_t>& idx)
{
    verts.resize(8);
    const float s = 0.5f;
    verts[0] = QVector3D(-s, -s, -s);
    verts[1] = QVector3D( s, -s, -s);
    verts[2] = QVector3D( s,  s, -s);
    verts[3] = QVector3D(-s,  s, -s);
    verts[4] = QVector3D(-s, -s,  s);
    verts[5] = QVector3D( s, -s,  s);
    verts[6] = QVector3D( s,  s,  s);
    verts[7] = QVector3D(-s,  s,  s);
    idx = {
        0,1,2, 0,2,3,
        4,6,5, 4,7,6,
        0,4,5, 0,5,1,
        1,5,6, 1,6,2,
        2,6,7, 2,7,3,
        3,7,4, 3,4,0
    };
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

class TestSkinWrapSystem : public QObject
{
    Q_OBJECT

private slots:
    void wrapFollowsCage();
    void wrapRigidTranslation();
    void bookkeepingAddRemoveDisable();
    void serializationRoundTrip();
};

void TestSkinWrapSystem::wrapFollowsCage()
{
    // A single skin vertex sitting exactly on the +X face of the cube cage
    // (x=+0.5, so the captured world offset is ~0). Deform: translate +3 X
    // then scale X by 2 (applied about origin): face center x=0.5 -> 1 -> +3
    // = 4, offset ~0 preserved -> final x = 4.
    QVector<QVector3D> skinVerts; skinVerts.append(QVector3D(0.5f, 0, 0));
    QVector<QVector3D> cageVerts;
    QVector<uint32_t> cageIdx;
    cubeCage(cageVerts, cageIdx);

    QMatrix4x4 identity;
    SkinWrapBinding b;
    QVERIFY(SkinWrapSystem::captureGeometry(skinVerts, identity, cageVerts, cageIdx,
                                            identity, b, 7, "cage"));
    QVERIFY(b.isValid(1));
    QVERIFY(near3(b.worldOffset[0], QVector3D(0, 0, 0), 1e-3f));

    QMatrix4x4 cageWorld;
    cageWorld.translate(3, 0, 0);
    cageWorld.scale(2, 1, 1);

    QVector<QVector3D> out = skinVerts;
    SkinWrapSystem::applyGeometry(cageVerts, cageIdx, cageWorld, identity, b, out);
    QVERIFY(near3(out[0], QVector3D(4, 0, 0), 1e-2f));
}

void TestSkinWrapSystem::wrapRigidTranslation()
{
    QVector<QVector3D> skinVerts; skinVerts.append(QVector3D(0, 0, 0));
    QVector<QVector3D> cageVerts;
    QVector<uint32_t> cageIdx;
    cubeCage(cageVerts, cageIdx);

    QMatrix4x4 identity;
    SkinWrapBinding b;
    QVERIFY(SkinWrapSystem::captureGeometry(skinVerts, identity, cageVerts, cageIdx,
                                            identity, b, 7, "cage"));

    QMatrix4x4 cageWorld;
    cageWorld.translate(10, 0, 0);
    QVector<QVector3D> out = skinVerts;
    SkinWrapSystem::applyGeometry(cageVerts, cageIdx, cageWorld, identity, b, out);
    QVERIFY(near3(out[0], QVector3D(10, 0, 0), 1e-2f));
}

void TestSkinWrapSystem::bookkeepingAddRemoveDisable()
{
    SceneFixture fx;
    SceneObject* skin = fx.create("skin");
    SceneObject* cage = fx.create("cage");

    SkinWrapSystem sys;
    QVERIFY(sys.add(skin->id(), cage->id(), "cage"));
    QCOMPARE(sys.count(skin->id()), 1);
    QCOMPARE(sys.wrappedObjectIds().size(), 1);
    QVERIFY(!sys.forObject(skin->id()).constFirst().isValid(1)); // still pending

    QVERIFY(sys.setEnabled(skin->id(), 0, false));
    // No mesh attached -> evaluate must be a no-op returning 0.
    QCOMPARE(sys.evaluate(skin, &fx.graph), 0);

    QVERIFY(sys.remove(skin->id(), 0));
    QCOMPARE(sys.count(skin->id()), 0);
}

void TestSkinWrapSystem::serializationRoundTrip()
{
    QVector<QVector3D> skinVerts; skinVerts.append(QVector3D(0, 0, 0));
    QVector<QVector3D> cageVerts;
    QVector<uint32_t> cageIdx;
    cubeCage(cageVerts, cageIdx);

    QMatrix4x4 identity;
    SkinWrapBinding b;
    QVERIFY(SkinWrapSystem::captureGeometry(skinVerts, identity, cageVerts, cageIdx,
                                            identity, b, 7, "cage"));

    const QVariant v = b.toVariant();
    SkinWrapBinding restored;
    restored.fromVariant(v);
    QCOMPARE(restored.cageId, 7);
    QCOMPARE(restored.cageName, QStringLiteral("cage"));
    QCOMPARE(restored.cageTri.size(), 1);
    QCOMPARE(restored.bary.size(), 1);
    QCOMPARE(restored.worldOffset.size(), 1);
    QVERIFY(restored.isValid(1));

    // Round-trip again to be sure serialization is stable.
    const QVariant v2 = restored.toVariant();
    QVERIFY(v2.toJsonObject() == v.toJsonObject());
}

QTEST_MAIN(TestSkinWrapSystem)

#include "test_SkinWrapSystem.moc"
