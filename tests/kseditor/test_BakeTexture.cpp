#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>

#include "modules/modellingEditor/TextureBaker.h"
#include "modules/modellingEditor/Geometry3D.h"

using namespace ks;

namespace {
// 1x1 quad on the Z=0 plane, full 0..1 UVs, +Z normals.
graphics::Mesh3D* makeQuad()
{
    graphics::Mesh3D* m = new graphics::Mesh3D();
    m->setVertices({ QVector3D(-1, -1, 0), QVector3D(1, -1, 0),
                     QVector3D(1, 1, 0), QVector3D(-1, 1, 0) });
    m->setNormals({ QVector3D(0, 0, 1), QVector3D(0, 0, 1),
                    QVector3D(0, 0, 1), QVector3D(0, 0, 1) });
    m->setIndices({ 0, 1, 2, 0, 2, 3 });
    m->setUVs({ QVector2D(0, 0), QVector2D(1, 0),
                QVector2D(1, 1), QVector2D(0, 1) });
    return m;
}

bool nearRgb(const QColor& a, const QColor& b, int tol)
{
    return std::abs(a.red() - b.red()) <= tol
        && std::abs(a.green() - b.green()) <= tol
        && std::abs(a.blue() - b.blue()) <= tol;
}
} // namespace

class TestBakeTexture : public QObject
{
    Q_OBJECT

private slots:
    void bakeTypesNamed();
    void bakeDiffuseFillsBaseColor();
    void bakeNormalFillsWorldSpaceNormals();
    void bakeAOOccupiesUVSpace();
    void bakeSaveToFile();
    void bakeRobustWithoutNormals();
};

void TestBakeTexture::bakeTypesNamed()
{
    QCOMPARE(io::TextureBaker::textureTypeName(io::TextureBaker::Diffuse), QString("diffuse"));
    QCOMPARE(io::TextureBaker::textureTypeName(io::TextureBaker::Normal), QString("normal"));
    QCOMPARE(io::TextureBaker::textureTypeName(io::TextureBaker::AO), QString("ao"));
    QCOMPARE(io::TextureBaker::textureTypeName(io::TextureBaker::Emission), QString("emission"));
}

void TestBakeTexture::bakeDiffuseFillsBaseColor()
{
    graphics::Mesh3D* mesh = makeQuad();
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(64, 64);
    baker.setBaseColor(QColor(180, 40, 200));

    baker.bake(io::TextureBaker::Diffuse);
    QImage img = baker.getBakedTexture(io::TextureBaker::Diffuse);
    QCOMPARE(img.size(), QSize(64, 64));
    QVERIFY(nearRgb(img.pixelColor(0, 0), QColor(180, 40, 200), 0));
    QVERIFY(nearRgb(img.pixelColor(63, 63), QColor(180, 40, 200), 0));
    delete mesh;
}

void TestBakeTexture::bakeNormalFillsWorldSpaceNormals()
{
    graphics::Mesh3D* mesh = makeQuad();
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(128, 128);

    baker.bake(io::TextureBaker::Normal);
    QImage img = baker.getBakedTexture(io::TextureBaker::Normal);
    QCOMPARE(img.size(), QSize(128, 128));
    // +Z normal encoded as n*0.5+0.5 -> (0.5,0.5,1.0).
    const QColor c = img.pixelColor(64, 64);
    QVERIFY(std::abs(c.red() - 128) <= 2);
    QVERIFY(std::abs(c.green() - 128) <= 2);
    QVERIFY(std::abs(c.blue() - 255) <= 2);
    delete mesh;
}

void TestBakeTexture::bakeAOOccupiesUVSpace()
{
    graphics::Mesh3D* mesh = makeQuad();
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(64, 64);

    baker.bake(io::TextureBaker::AO);
    QImage img = baker.getBakedTexture(io::TextureBaker::AO);
    QCOMPARE(img.size(), QSize(64, 64));
    // An open quad occludes nothing -> white everywhere.
    QVERIFY(nearRgb(img.pixelColor(32, 32), Qt::white, 2));
    delete mesh;
}

void TestBakeTexture::bakeSaveToFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("diffuse.png");

    graphics::Mesh3D* mesh = makeQuad();
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(32, 32);
    baker.setBaseColor(QColor(0, 120, 255));
    baker.addBakeTarget(io::TextureBaker::Diffuse, path);
    baker.bake(io::TextureBaker::Diffuse);

    QVERIFY(QFile::exists(path));
    QImage loaded(path);
    QCOMPARE(loaded.size(), QSize(32, 32));
    QVERIFY(nearRgb(loaded.pixelColor(16, 16), QColor(0, 120, 255), 0));
    delete mesh;
}

void TestBakeTexture::bakeRobustWithoutNormals()
{
    graphics::Mesh3D* mesh = makeQuad();
    mesh->setNormals({}); // only positions + uvs
    io::TextureBaker baker;
    baker.setSourceMesh(mesh);
    baker.setTargetResolution(32, 32);

    baker.bake(io::TextureBaker::Normal);
    QImage normal = baker.getBakedTexture(io::TextureBaker::Normal);
    QCOMPARE(normal.size(), QSize(32, 32));
    QVERIFY(std::abs(normal.pixelColor(16, 16).blue() - 255) <= 2);

    baker.bake(io::TextureBaker::AO);
    QImage ao = baker.getBakedTexture(io::TextureBaker::AO);
    QCOMPARE(ao.size(), QSize(32, 32));
    QVERIFY(nearRgb(ao.pixelColor(16, 16), Qt::white, 2));
    delete mesh;
}

QTEST_APPLESS_MAIN(TestBakeTexture)
#include "test_BakeTexture.moc"
