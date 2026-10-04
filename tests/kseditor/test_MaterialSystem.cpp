#include "engine/material/MaterialSystem.h"
#include <QtTest/QtTest>

using namespace ks;

class TestMaterialSystem : public QObject {
    Q_OBJECT

private slots:
    void testMaterialDefaults();
    void testPBRDefaults();
    void testTextureSlots();
    void testMaterialCategory();
    void testTransparency();
    void testTextureTypes();
    void testMaterialName();
};

void TestMaterialSystem::testMaterialDefaults()
{
    Material mat;
    QVERIFY(mat.name.isEmpty());
    QCOMPARE(mat.isTransparent, false);
    QCOMPARE(mat.twoSided, false);
    QVERIFY(mat.textures.isEmpty());
}

void TestMaterialSystem::testPBRDefaults()
{
    PBRParams pbr;
    QCOMPARE(pbr.baseColor, QVector3D(0.8f, 0.8f, 0.8f));
    QCOMPARE(pbr.metallic, 0.0f);
    QCOMPARE(pbr.roughness, 0.5f);
    QCOMPARE(pbr.alpha, 1.0f);
    QCOMPARE(pbr.clearcoat, 0.0f);
    QCOMPARE(pbr.sheen, 0.0f);
    QCOMPARE(pbr.ior, 1.5f);
    QCOMPARE(pbr.subsurface, 0.0f);
}

void TestMaterialSystem::testTextureSlots()
{
    Material mat("TestMat");
    QVERIFY(mat.textures.isEmpty());

    mat.setTexture(TextureType::BaseColor, "textures/diffuse.png");
    QCOMPARE(mat.textures.size(), 1);
    QVERIFY(mat.textures.contains(TextureType::BaseColor));
    QCOMPARE(mat.textures[TextureType::BaseColor].texturePath, QString("textures/diffuse.png"));

    TextureSlot* slot = mat.getTexture(TextureType::BaseColor);
    QVERIFY(slot != nullptr);
    QCOMPARE(slot->type, TextureType::BaseColor);

    mat.removeTexture(TextureType::BaseColor);
    QVERIFY(mat.textures.isEmpty());
}

void TestMaterialSystem::testMaterialCategory()
{
    Material mat("Metal");
    mat.category = "Metals";
    QCOMPARE(mat.category, QString("Metals"));
}

void TestMaterialSystem::testTransparency()
{
    Material mat("Glass");
    mat.isTransparent = true;
    mat.twoSided = true;
    QVERIFY(mat.isTransparent);
    QVERIFY(mat.twoSided);
}

void TestMaterialSystem::testTextureTypes()
{
    Material mat("FullPBR");
    mat.setTexture(TextureType::BaseColor, "diffuse.png");
    mat.setTexture(TextureType::Normal, "normal.png");
    mat.setTexture(TextureType::Roughness, "rough.png");
    mat.setTexture(TextureType::Metallic, "metal.png");
    mat.setTexture(TextureType::Emission, "emit.png");
    mat.setTexture(TextureType::AmbientOcclusion, "ao.png");
    mat.setTexture(TextureType::Height, "height.png");

    QCOMPARE(mat.textures.size(), 7);
    mat.clearTextures();
    QVERIFY(mat.textures.isEmpty());
}

void TestMaterialSystem::testMaterialName()
{
    Material mat("MyMaterial");
    QCOMPARE(mat.name, QString("MyMaterial"));
}

QTEST_MAIN(TestMaterialSystem)
#include "test_MaterialSystem.moc"
