#include <QtTest>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>

#include "sdk/kseditor/modules/cockpitInstruments/CockpitInstruments.h"


class TestCockpitInstruments : public QObject {
    Q_OBJECT

private slots:
    void test_defaultState();
    void test_addElement();
    void test_removeElement();
    void test_updateElement();
    void test_clearElements();
    void test_displaySettings();
    void test_validateConfig();
    void test_exportAsImage();
    void test_exportAsImageWithElements();
    void test_saveLoadJson();
    void test_saveLoadIni();
    void test_saveLoadLua();
    void test_animationConfig();
    void test_physicsValues();
    void test_exportImageProgressRing();
    void test_exportImageAnimatedText();
    void test_multipleElements();
    void test_getElementById();
};

void TestCockpitInstruments::test_defaultState() {
    ksCockpitInstruments editor;
    QCOMPARE(editor.getDisplaySize(), QSize(800, 480));
    QCOMPARE(editor.getDisplayName(), QString());
    QCOMPARE(editor.getAllElements().size(), 0);
    QVERIFY(editor.getErrors().isEmpty());
}

void TestCockpitInstruments::test_addElement() {
    ksCockpitInstruments editor;

    DisplayElement elem;
    elem.id = "test_speed";
    elem.type = ElementType::TEXT;
    elem.source = DataSource::SPEED;
    elem.position = QPoint(50, 50);
    elem.size = QSize(200, 60);
    elem.color = Qt::white;
    elem.fontSize = 48;

    editor.addElement(elem);
    QCOMPARE(editor.getAllElements().size(), 1);

    DisplayElement* retrieved = editor.getElement("test_speed");
    QVERIFY(retrieved != nullptr);
    QCOMPARE(retrieved->type, ElementType::TEXT);
    QCOMPARE(retrieved->source, DataSource::SPEED);
}

void TestCockpitInstruments::test_removeElement() {
    ksCockpitInstruments editor;

    DisplayElement elem;
    elem.id = "to_remove";
    editor.addElement(elem);
    QCOMPARE(editor.getAllElements().size(), 1);

    editor.removeElement("to_remove");
    QCOMPARE(editor.getAllElements().size(), 0);
    QVERIFY(editor.getElement("to_remove") == nullptr);
}

void TestCockpitInstruments::test_updateElement() {
    ksCockpitInstruments editor;

    DisplayElement elem;
    elem.id = "updatable";
    elem.type = ElementType::TEXT;
    elem.color = Qt::red;
    editor.addElement(elem);

    DisplayElement updated;
    updated.id = "updatable";
    updated.type = ElementType::DIGIT_GROUP;
    updated.color = Qt::green;
    updated.source = DataSource::GEAR;
    editor.updateElement("updatable", updated);

    DisplayElement* retrieved = editor.getElement("updatable");
    QVERIFY(retrieved != nullptr);
    QCOMPARE(retrieved->type, ElementType::DIGIT_GROUP);
    QCOMPARE(retrieved->source, DataSource::GEAR);
    QCOMPARE(retrieved->color, QColor(Qt::green));
}

void TestCockpitInstruments::test_clearElements() {
    ksCockpitInstruments editor;

    DisplayElement e1, e2;
    e1.id = "one"; e2.id = "two";
    editor.addElement(e1);
    editor.addElement(e2);
    QCOMPARE(editor.getAllElements().size(), 2);

    editor.clearElements();
    QCOMPARE(editor.getAllElements().size(), 0);
}

void TestCockpitInstruments::test_displaySettings() {
    ksCockpitInstruments editor;

    editor.setDisplayName("Test Display");
    QCOMPARE(editor.getDisplayName(), "Test Display");

    editor.setDisplaySize(QSize(1024, 600));
    QCOMPARE(editor.getDisplaySize(), QSize(1024, 600));

    editor.setBackgroundColor(QColor("#1a1a2e"));
    QCOMPARE(editor.getBackgroundColor(), QColor("#1a1a2e"));

    editor.setBackgroundImage("/fake/path.png");
    QCOMPARE(editor.getBackgroundImage(), "/fake/path.png");
}

void TestCockpitInstruments::test_validateConfig() {
    ksCockpitInstruments editor;

    // Empty config should be valid
    QVERIFY(editor.validateConfig());

    // Invalid (negative position) should fail
    DisplayElement elem;
    elem.id = "bad";
    elem.position = QPoint(-10, 0);
    editor.addElement(elem);
    QVERIFY(!editor.validateConfig());
    QVERIFY(!editor.getErrors().isEmpty());
}

void TestCockpitInstruments::test_exportAsImage() {
    ksCockpitInstruments editor;
    editor.setDisplaySize(QSize(200, 100));
    editor.setBackgroundColor(Qt::black);

    QString path = QDir::tempPath() + "/ks_display_test.png";
    QVERIFY(editor.exportAsImage(path));
    QVERIFY(QFile::exists(path));

    QImage img(path);
    QCOMPARE(img.width(), 200);
    QCOMPARE(img.height(), 100);

    QFile::remove(path);
}

void TestCockpitInstruments::test_exportAsImageWithElements() {
    ksCockpitInstruments editor;
    editor.setDisplaySize(QSize(400, 200));
    editor.setBackgroundColor(Qt::black);

    DisplayElement text;
    text.id = "speed";
    text.type = ElementType::TEXT;
    text.source = DataSource::SPEED;
    text.position = QPoint(20, 20);
    text.size = QSize(160, 60);
    text.color = Qt::red;
    text.fontSize = 48;
    editor.addElement(text);

    DisplayElement bar;
    bar.id = "rpm_bar";
    bar.type = ElementType::BAR;
    bar.source = DataSource::RPM;
    bar.position = QPoint(20, 100);
    bar.size = QSize(300, 20);
    bar.color = QColor("#22c55e");
    bar.minValue = 0;
    bar.maxValue = 8000;
    editor.addElement(bar);

    DisplayElement digits;
    digits.id = "gear";
    digits.type = ElementType::DIGIT_GROUP;
    digits.source = DataSource::GEAR;
    digits.position = QPoint(320, 20);
    digits.size = QSize(60, 60);
    digits.color = Qt::white;
    digits.fontSize = 48;
    editor.addElement(digits);

    editor.updatePhysicsValue(DataSource::SPEED, 180.0);
    editor.updatePhysicsValue(DataSource::RPM, 4500.0);
    editor.updatePhysicsValue(DataSource::GEAR, 4.0);

    QString path = QDir::tempPath() + "/ks_display_elements.png";
    QVERIFY(editor.exportAsImage(path));
    QVERIFY(QFile::exists(path));
    QFile::remove(path);
}

void TestCockpitInstruments::test_saveLoadJson() {
    ksCockpitInstruments editor;
    editor.setDisplayName("JSON Test");
    editor.setDisplaySize(QSize(800, 480));

    DisplayElement elem;
    elem.id = "speed";
    elem.type = ElementType::TEXT;
    elem.source = DataSource::SPEED;
    elem.position = QPoint(10, 20);
    elem.size = QSize(200, 60);
    elem.color = Qt::white;
    elem.fontSize = 48;
    elem.visible = true;
    editor.addElement(elem);

    QString path = QDir::tempPath() + "/ks_display_test.json";
    QVERIFY(editor.saveToFile(path));

    ksCockpitInstruments loaded;
    QVERIFY(loaded.loadFromFile(path));
    QCOMPARE(loaded.getDisplayName(), "JSON Test");
    QCOMPARE(loaded.getDisplaySize(), QSize(800, 480));
    QCOMPARE(loaded.getAllElements().size(), 1);

    DisplayElement* loaded_elem = loaded.getElement("speed");
    QVERIFY(loaded_elem != nullptr);
    QCOMPARE(loaded_elem->type, ElementType::TEXT);
    QCOMPARE(loaded_elem->source, DataSource::SPEED);

    QFile::remove(path);
}

void TestCockpitInstruments::test_saveLoadIni() {
    ksCockpitInstruments editor;
    editor.setDisplayName("INI Test");

    DisplayElement elem;
    elem.id = "rpm";
    elem.type = ElementType::BAR;
    elem.source = DataSource::RPM;
    elem.position = QPoint(40, 100);
    elem.size = QSize(300, 20);
    elem.color = QColor("#22c55e");
    elem.minValue = 0;
    elem.maxValue = 8000;
    editor.addElement(elem);

    QString path = QDir::tempPath() + "/ks_display_test.ini";
    QVERIFY(editor.saveToFile(path));

    ksCockpitInstruments loaded;
    QVERIFY(loaded.loadFromFile(path));
    QCOMPARE(loaded.getDisplayName(), "INI Test");

    QFile::remove(path);
}

void TestCockpitInstruments::test_saveLoadLua() {
    ksCockpitInstruments editor;
    editor.setDisplayName("Lua Test");

    DisplayElement elem;
    elem.id = "fuel";
    elem.type = ElementType::TEXT;
    elem.source = DataSource::FUEL;
    elem.position = QPoint(40, 140);
    elem.size = QSize(160, 30);
    elem.color = Qt::cyan;
    editor.addElement(elem);

    QString path = QDir::tempPath() + "/ks_display_test.lua";
    QVERIFY(editor.saveToFile(path));

    ksCockpitInstruments loaded;
    QVERIFY(loaded.loadFromFile(path));

    QFile::remove(path);
}

void TestCockpitInstruments::test_animationConfig() {
    ksCockpitInstruments editor;

    DisplayElement elem;
    elem.id = "animated";
    elem.type = ElementType::TEXT;
    elem.source = DataSource::SPEED;
    editor.addElement(elem);

    AnimationConfig anim;
    anim.type = AnimationType::PULSE;
    anim.durationMs = 1000;
    anim.loop = true;
    anim.pulseMinScale = 0.9;
    anim.pulseMaxScale = 1.1;

    editor.setAnimation("animated", anim);

    AnimationState state = editor.getAnimationState("animated");
    QCOMPARE(state.config.type, AnimationType::PULSE);
    QCOMPARE(state.config.durationMs, 1000);
    QVERIFY(state.config.loop);
}

void TestCockpitInstruments::test_physicsValues() {
    ksCockpitInstruments editor;

    editor.updatePhysicsValue(DataSource::SPEED, 123.45);
    editor.updatePhysicsValue(DataSource::RPM, 6500.0);
    editor.updatePhysicsValue(DataSource::FUEL, 45.0);

    QCOMPARE(editor.getPhysicsValue(DataSource::SPEED), 123.45);
    QCOMPARE(editor.getPhysicsValue(DataSource::RPM), 6500.0);
    QCOMPARE(editor.getPhysicsValue(DataSource::FUEL), 45.0);
}

void TestCockpitInstruments::test_exportImageProgressRing() {
    ksCockpitInstruments editor;
    editor.setDisplaySize(QSize(200, 200));
    editor.setBackgroundColor(Qt::black);

    DisplayElement ring;
    ring.id = "progress";
    ring.type = ElementType::PROGRESS_RING;
    ring.source = DataSource::RPM;
    ring.position = QPoint(50, 50);
    ring.size = QSize(100, 100);
    ring.color = QColor("#E10600");
    ring.minValue = 0;
    ring.maxValue = 8000;
    ring.fontSize = 14;
    editor.addElement(ring);

    editor.updatePhysicsValue(DataSource::RPM, 5000.0);

    QString path = QDir::tempPath() + "/ks_display_ring.png";
    QVERIFY(editor.exportAsImage(path));
    QVERIFY(QFile::exists(path));

    QImage img(path);
    QVERIFY(!img.isNull());

    QFile::remove(path);
}

void TestCockpitInstruments::test_exportImageAnimatedText() {
    ksCockpitInstruments editor;
    editor.setDisplaySize(QSize(300, 100));
    editor.setBackgroundColor(Qt::black);

    DisplayElement animText;
    animText.id = "anim_speed";
    animText.type = ElementType::ANIMATED_TEXT;
    animText.source = DataSource::SPEED;
    animText.position = QPoint(20, 20);
    animText.size = QSize(260, 60);
    animText.color = Qt::green;
    animText.fontSize = 48;
    animText.decimalPlaces = 1;
    editor.addElement(animText);

    editor.updatePhysicsValue(DataSource::SPEED, 185.5);

    QString path = QDir::tempPath() + "/ks_display_animtext.png";
    QVERIFY(editor.exportAsImage(path));
    QVERIFY(QFile::exists(path));
    QFile::remove(path);
}

void TestCockpitInstruments::test_multipleElements() {
    ksCockpitInstruments editor;

    for (int i = 0; i < 10; ++i) {
        DisplayElement elem;
        elem.id = QString("elem_%1").arg(i);
        elem.type = ElementType::TEXT;
        elem.source = DataSource::SPEED;
        elem.position = QPoint(i * 10, 0);
        editor.addElement(elem);
    }

    QCOMPARE(editor.getAllElements().size(), 10);

    editor.removeElement("elem_0");
    editor.removeElement("elem_9");
    QCOMPARE(editor.getAllElements().size(), 8);
}

void TestCockpitInstruments::test_getElementById() {
    ksCockpitInstruments editor;

    DisplayElement elem;
    elem.id = "unique_id";
    elem.type = ElementType::CIRCLE;
    elem.source = DataSource::STEERING_ANGLE;
    elem.position = QPoint(100, 100);
    elem.size = QSize(50, 50);
    elem.color = Qt::blue;
    editor.addElement(elem);

    DisplayElement* found = editor.getElement("unique_id");
    QVERIFY(found != nullptr);
    QCOMPARE(found->type, ElementType::CIRCLE);
    QCOMPARE(found->position, QPoint(100, 100));

    DisplayElement* notFound = editor.getElement("nonexistent");
    QVERIFY(notFound == nullptr);
}

QTEST_MAIN(TestCockpitInstruments)
#include "test_CockpitInstruments.moc"
