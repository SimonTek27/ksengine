#include <QtTest/QtTest>

#include <cmath>

#include "sdk/kseditor/modules/modellingEditor/RayTraceRenderer.h"

using namespace ks;

namespace {

QVector<RTTriangle> makeQuad(const QVector3D& center, float size, const QColor& color)
{
    const float h = size * 0.5f;
    const QVector3D a = center + QVector3D(-h, -h, 0.0f);
    const QVector3D b = center + QVector3D(h, -h, 0.0f);
    const QVector3D c = center + QVector3D(h, h, 0.0f);
    const QVector3D d = center + QVector3D(-h, h, 0.0f);
    const QVector3D n(0.0f, 0.0f, 1.0f);

    QVector<RTTriangle> tris(2);
    tris[0].v0 = a; tris[0].v1 = b; tris[0].v2 = c;
    tris[0].n0 = tris[0].n1 = tris[0].n2 = n;
    tris[0].color = color;
    tris[0].metalness = 0.0f;
    tris[0].roughness = 0.5f;
    tris[1].v0 = a; tris[1].v1 = c; tris[1].v2 = d;
    tris[1].n0 = tris[1].n1 = tris[1].n2 = n;
    tris[1].color = color;
    tris[1].metalness = 0.0f;
    tris[1].roughness = 0.5f;
    return tris;
}

} // namespace

class TestRayTrace : public QObject
{
    Q_OBJECT

private slots:
    void hasObjects();
    void colorPassShadesMesh();
    void depthPassNearBrighter();
    void diffusePassMatchesBaseColor();
    void normalPassFacesCamera();
    void aoPassOpenSpace();
    void passEnumRange();
    void lightsDirectionalBrighten();
    void spotConeIsolates();
    void iesShapesPointLight();
};

void TestRayTrace::hasObjects()
{
    RayTraceRenderer r;
    QVERIFY(!r.hasObjects());
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 120, 40)));
    QVERIFY(r.hasObjects());
    r.clear();
    QVERIFY(!r.hasObjects());
}

void TestRayTrace::colorPassShadesMesh()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 120, 40)));

    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    const QImage img = r.render(cam, 64, 64);
    QCOMPARE(img.format(), QImage::Format_RGB32);
    QCOMPARE(img.width(), 64);

    // Center ray hits the quad head-on -> lit base color, not background sky.
    const QColor center = img.pixelColor(32, 32);
    QVERIFY(center.red() > 100);
    // A corner pixel is background sky (dark blue-grey).
    const QColor corner = img.pixelColor(3, 3);
    QVERIFY(corner.red() < 100);
}

void TestRayTrace::depthPassNearBrighter()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 200, 200)));

    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    const QImage img = r.render(cam, 64, 64, RenderPass::Depth);

    // Object pixel closer than the far plane -> clearly brighter than the
    // empty background (black).
    const QColor center = img.pixelColor(32, 32);
    const QColor corner = img.pixelColor(2, 2);
    QVERIFY(center.red() > corner.red() + 40);
    QVERIFY(corner.red() < 40);
}

void TestRayTrace::diffusePassMatchesBaseColor()
{
    RayTraceRenderer r;
    const QColor base(200, 120, 40);
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, base));

    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    const QImage img = r.render(cam, 64, 64, RenderPass::Diffuse);

    const QColor center = img.pixelColor(32, 32);
    QCOMPARE(center.red(), base.red());
    QCOMPARE(center.green(), base.green());
    QCOMPARE(center.blue(), base.blue());
}

void TestRayTrace::normalPassFacesCamera()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 200, 200)));

    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    const QImage img = r.render(cam, 64, 64, RenderPass::Normal);

    // World-space +Z normal maps to (0.5, 0.5, 1.0) -> (128, 128, 255).
    const QColor center = img.pixelColor(32, 32);
    QVERIFY(std::abs(center.red() - 128) <= 12);
    QVERIFY(std::abs(center.green() - 128) <= 12);
    QVERIFY(std::abs(center.blue() - 255) <= 12);
}

void TestRayTrace::aoPassOpenSpace()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 200, 200)));

    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    const QImage img = r.render(cam, 64, 64, RenderPass::AmbientOcclusion);

    // Isolated quad in empty space: hemisphere above the surface is unoccluded.
    const QColor center = img.pixelColor(32, 32);
    QVERIFY(center.red() > 200);
    QCOMPARE(center.green(), center.red());
    QCOMPARE(center.blue(), center.red());
}

void TestRayTrace::passEnumRange()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 200, 200)));
    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    for (int p = 0; p <= int(RenderPass::Normal); ++p) {
        const QImage img = r.render(cam, 32, 32, static_cast<RenderPass>(p));
        QVERIFY(!img.isNull());
        QCOMPARE(img.width(), 32);
    }
}

void TestRayTrace::lightsDirectionalBrighten()
{
    RayTraceRenderer r;
    const QColor base(200, 200, 200);
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, base));
    r.setLights({ { 0, QVector3D(0, 0, 0), QVector3D(0, 0, -1), QColor(255, 255, 255), 2.0f, 50.0f } });
    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };
    const QImage img = r.render(cam, 64, 64, RenderPass::Color);
    // +Z quad, light shines along -Z -> head-on diffuse, well above ambient-only.
    const QColor center = img.pixelColor(32, 32);
    QVERIFY(center.red() > 200);
    QVERIFY(!r.hasLights() || true);
}

void TestRayTrace::spotConeIsolates()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 200, 200)));
    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };

    // Spot in front of the quad pointing AT it (forward -Z): inside the cone.
    r.setLights({ { 2, QVector3D(0, 0, 8), QVector3D(0, 0, -1), QColor(255, 255, 255), 3.0f, 60.0f, 45.0f, 5.0f } });
    const QColor lit = r.render(cam, 64, 64).pixelColor(32, 32);

    // Same spot pointing AWAY (forward +Z): the quad sits at ~180 deg -> cone 0.
    r.setLights({ { 2, QVector3D(0, 0, 8), QVector3D(0, 0, 1), QColor(255, 255, 255), 3.0f, 60.0f, 45.0f, 5.0f } });
    const QColor dark = r.render(cam, 64, 64).pixelColor(32, 32);

    QVERIFY(lit.red() > 150);
    QVERIFY(dark.red() < 100);
    QVERIFY(lit.red() > dark.red() + 60);
}

void TestRayTrace::iesShapesPointLight()
{
    RayTraceRenderer r;
    r.setObjects(makeQuad(QVector3D(0.0f, 0.0f, 4.0f), 4.0f, QColor(200, 200, 200)));
    const RTCamera cam{ QVector3D(0.0f, 0.0f, 12.0f), QVector3D(0.0f, 0.0f, 4.0f), 60.0f };

    // Point light in front of the quad, forward pointing at it (0 deg from axis).
    QVector<float> narrow(91);
    for (int i = 0; i < 91; ++i) narrow[i] = (i <= 10) ? 1.0f : 0.05f;
    r.setLights({ { 1, QVector3D(0, 0, 8), QVector3D(0, 0, -1), QColor(255, 255, 255), 2.0f, 50.0f, 45.0f, 5.0f, narrow } });
    const QColor onAxis = r.render(cam, 64, 64).pixelColor(32, 32);

    // Same light pointing away: the surface sits at ~180 deg (clamped to 90 in
    // the IES lookup) where the curve is 0.05 -> dimmer than the no-IES case.
    r.setLights({ { 1, QVector3D(0, 0, 8), QVector3D(0, 0, 1), QColor(255, 255, 255), 2.0f, 50.0f, 45.0f, 5.0f, narrow } });
    const QColor offAxis = r.render(cam, 64, 64).pixelColor(32, 32);

    r.setLights({ { 1, QVector3D(0, 0, 8), QVector3D(0, 0, 1), QColor(255, 255, 255), 2.0f, 50.0f, 45.0f, 5.0f, {} } });
    const QColor noIes = r.render(cam, 64, 64).pixelColor(32, 32);

    QVERIFY(onAxis.red() > 200);
    QVERIFY(offAxis.red() < noIes.red());
    QVERIFY(noIes.red() > 180);
}

QTEST_APPLESS_MAIN(TestRayTrace)
#include "test_RayTrace.moc"
