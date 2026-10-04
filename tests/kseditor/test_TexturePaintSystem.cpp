#include <QtTest/QtTest>
#include "engine/material/TexturePaintSystem.h"
#include <QImage>
using namespace ks;

class TestTexturePaintSystem : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testAddLayer();
    void testRemoveLayer();
    void testLayerCount();
    void testSetCurrentLayer();
    void testLayerOpacity();
    void testLayerVisibility();
    void testSetCanvasSize();
    void testClearCanvas();
    void testBrushDefaults();
    void testFloodFill();
    void testGradientFill();
    void testBlurFilter();
    void testInvertFilter();
    void testCompositeAll();

private:
    TexturePaintSystem* m_system = nullptr;
};

void TestTexturePaintSystem::initTestCase()
{
    m_system = new TexturePaintSystem(this);
    m_system->setCanvasSize(64, 64);
}

void TestTexturePaintSystem::testAddLayer()
{
    int count = m_system->layerCount();
    int idx = m_system->addLayer("TestLayer");
    QCOMPARE(m_system->layerCount(), count + 1);
    QVERIFY(idx >= 0);
    const auto* layer = m_system->layer(idx);
    QVERIFY(layer != nullptr);
    QCOMPARE(layer->name, QString("TestLayer"));
}

void TestTexturePaintSystem::testRemoveLayer()
{
    int count = m_system->layerCount();
    int idx = m_system->addLayer("RemoveMe");
    QCOMPARE(m_system->layerCount(), count + 1);
    QVERIFY(m_system->removeLayer(idx));
    QCOMPARE(m_system->layerCount(), count);
}

void TestTexturePaintSystem::testLayerCount()
{
    QVERIFY(m_system->layerCount() >= 1);
}

void TestTexturePaintSystem::testSetCurrentLayer()
{
    int idx = m_system->addLayer("CurrentTest");
    m_system->setCurrentLayer(idx);
    QCOMPARE(m_system->currentLayer(), idx);
}

void TestTexturePaintSystem::testLayerOpacity()
{
    int idx = m_system->addLayer("OpacityTest");
    m_system->setLayerOpacity(idx, 0.5f);
    const auto* layer = m_system->layer(idx);
    QVERIFY(layer != nullptr);
    QVERIFY(qAbs(layer->opacity - 0.5f) < 0.001f);
}

void TestTexturePaintSystem::testLayerVisibility()
{
    int idx = m_system->addLayer("VisibilityTest");
    m_system->setLayerVisible(idx, false);
    const auto* layer = m_system->layer(idx);
    QVERIFY(layer != nullptr);
    QVERIFY(!layer->visible);
}

void TestTexturePaintSystem::testSetCanvasSize()
{
    m_system->setCanvasSize(128, 64);
    QCOMPARE(m_system->canvasSize(), QSize(128, 64));
    m_system->setCanvasSize(64, 64);
}

void TestTexturePaintSystem::testClearCanvas()
{
    m_system->clearCanvas(Qt::red);
    QImage composite = m_system->compositeAll();
    QVERIFY(!composite.isNull());
    QCOMPARE(composite.size(), QSize(64, 64));
}

void TestTexturePaintSystem::testBrushDefaults()
{
    const auto& brush = m_system->brush();
    QCOMPARE(brush.type, PaintBrush::Circle);
    QVERIFY(brush.size > 0);
    QVERIFY(brush.strength > 0);
}

void TestTexturePaintSystem::testFloodFill()
{
    m_system->clearCanvas(Qt::white);
    m_system->floodFill(QPoint(32, 32), QColor(255, 0, 0));
    QImage composite = m_system->compositeAll();
    QVERIFY(!composite.isNull());
}

void TestTexturePaintSystem::testGradientFill()
{
    m_system->clearCanvas(Qt::transparent);
    m_system->gradientFill(QPoint(0, 0), QPoint(63, 63),
                           QColor(255, 0, 0), QColor(0, 0, 255));
    QImage composite = m_system->compositeAll();
    QVERIFY(!composite.isNull());
}

void TestTexturePaintSystem::testBlurFilter()
{
    m_system->clearCanvas(Qt::white);
    m_system->applyBlur(QRect(0, 0, 64, 64), 3.0f);
    QImage composite = m_system->compositeAll();
    QVERIFY(!composite.isNull());
}

void TestTexturePaintSystem::testInvertFilter()
{
    m_system->clearCanvas(Qt::red);
    m_system->applyInvert(QRect(0, 0, 64, 64));
    QImage composite = m_system->compositeAll();
    QVERIFY(!composite.isNull());
}

void TestTexturePaintSystem::testCompositeAll()
{
    QImage composite = m_system->compositeAll();
    QVERIFY(!composite.isNull());
    QCOMPARE(composite.format(), QImage::Format_ARGB32_Premultiplied);
}

QTEST_MAIN(TestTexturePaintSystem)
#include "test_TexturePaintSystem.moc"
