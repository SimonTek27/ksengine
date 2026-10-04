#include <QtTest>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "ShowroomEditorModule.h"
#include "ShowroomSystem.h"
#include "ShowroomViewport3D.h"

using namespace ks;

class TestShowroomEditor : public QObject {
    Q_OBJECT

private slots:
    void test_defaultConfig();
    void test_validateConfig();
    void test_saveLoadConfig();
    void test_cameraCRUD();
    void test_lightCRUD();
    void test_defaultCamera();
    void test_defaultLights();
    void test_validatePreviewConfig();
};

void TestShowroomEditor::test_defaultConfig() {
    ShowroomSystem::ShowroomConfig cfg = ShowroomSystem::getDefaultConfig();
    QCOMPARE(cfg.name, QString("Default Showroom"));
    QCOMPARE(cfg.cameraDistance, 5.0f);
    QCOMPARE(cfg.cameraHeight, 2.0f);
    QCOMPARE(cfg.cameraAngle, 30.0f);
    QCOMPARE(cfg.cameraFov, 60.0f);
    QCOMPARE(cfg.rotateSpeed, 0.5f);
    QVERIFY(cfg.autoRotate);
    QCOMPARE(cfg.sunIntensity, 1.0f);
    QCOMPARE(cfg.ambientIntensity, 0.3f);
    QCOMPARE(cfg.sunColor, QColor(255, 250, 240));
    QCOMPARE(cfg.ambientColor, QColor(200, 200, 200));
}

void TestShowroomEditor::test_validateConfig() {
    ShowroomSystem::ShowroomConfig cfg = ShowroomSystem::getDefaultConfig();
    QString error;
    QVERIFY(ShowroomSystem::validateConfig(cfg, &error));

    cfg.cameraDistance = -1.0f;
    QVERIFY(!ShowroomSystem::validateConfig(cfg, &error));
    QVERIFY(!error.isEmpty());

    cfg = ShowroomSystem::getDefaultConfig();
    cfg.cameraFov = 200.0f;
    QVERIFY(!ShowroomSystem::validateConfig(cfg, &error));

    cfg = ShowroomSystem::getDefaultConfig();
    cfg.sunIntensity = -1.0f;
    QVERIFY(!ShowroomSystem::validateConfig(cfg, &error));
}

void TestShowroomEditor::test_saveLoadConfig() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString configPath = tempDir.path() + "/showroom.ini";

    ShowroomSystem::ShowroomConfig original = ShowroomSystem::getDefaultConfig();
    original.name = "Test Showroom";
    original.cameraDistance = 4.5f;
    original.cameraHeight = 1.8f;
    original.cameraAngle = 25.0f;
    original.cameraFov = 70.0f;
    original.rotateSpeed = 0.3f;
    original.autoRotate = false;
    original.sunIntensity = 1.5f;
    original.ambientIntensity = 0.5f;
    original.sunColor = QColor(255, 200, 100);
    original.ambientColor = QColor(150, 150, 180);

    QVERIFY(ShowroomSystem::saveConfig(original, configPath));

    ShowroomSystem::ShowroomConfig loaded = ShowroomSystem::loadConfig(configPath);
    QCOMPARE(loaded.name, original.name);
    QCOMPARE(loaded.cameraDistance, original.cameraDistance);
    QCOMPARE(loaded.cameraHeight, original.cameraHeight);
    QCOMPARE(loaded.cameraAngle, original.cameraAngle);
    QCOMPARE(loaded.cameraFov, original.cameraFov);
    QCOMPARE(loaded.rotateSpeed, original.rotateSpeed);
    QCOMPARE(loaded.autoRotate, original.autoRotate);
    QCOMPARE(loaded.sunIntensity, original.sunIntensity);
    QCOMPARE(loaded.ambientIntensity, original.ambientIntensity);
    QCOMPARE(loaded.sunColor, original.sunColor);
    QCOMPARE(loaded.ambientColor, original.ambientColor);

    QFile::remove(configPath);
}

void TestShowroomEditor::test_cameraCRUD() {
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString configPath = tmpDir.path() + "/showroom.ini";

    QVector<ShowroomSystem::ShowroomCamera> cameras;
    ShowroomSystem::ShowroomCamera cam1;
    cam1.name = "Front View";
    cam1.position[0] = 0; cam1.position[1] = 1; cam1.position[2] = 6;
    cam1.target[0] = 0; cam1.target[1] = 0; cam1.target[2] = 0;
    cam1.fov = 50.0f;
    cam1.isActive = true;
    cameras.append(cam1);

    ShowroomSystem::ShowroomCamera cam2;
    cam2.name = "Rear View";
    cam2.position[0] = 0; cam2.position[1] = 1; cam2.position[2] = -6;
    cam2.target[0] = 0; cam2.target[1] = 0; cam2.target[2] = 0;
    cam2.fov = 55.0f;
    cam2.isActive = true;
    cameras.append(cam2);

    QVERIFY(ShowroomSystem::saveCameras(cameras, configPath));

    QVector<ShowroomSystem::ShowroomCamera> loaded = ShowroomSystem::loadCameras(configPath);
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded[0].name, QString("Front View"));
    QCOMPARE(loaded[0].position[2], 6.0f);
    QCOMPARE(loaded[0].fov, 50.0f);
    QCOMPARE(loaded[1].name, QString("Rear View"));
    QCOMPARE(loaded[1].position[2], -6.0f);
    QCOMPARE(loaded[1].fov, 55.0f);

    QFile::remove(configPath);
}

void TestShowroomEditor::test_lightCRUD() {
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    QString configPath = tmpDir.path() + "/showroom.ini";

    QVector<ShowroomSystem::ShowroomLight> lights;
    ShowroomSystem::ShowroomLight sun;
    sun.name = "Test Sun";
    sun.type = "directional";
    sun.position[0] = 3; sun.position[1] = 5; sun.position[2] = 2;
    sun.intensity = 2.0f;
    sun.range = 50.0f;
    sun.isActive = true;
    lights.append(sun);

    QVERIFY(ShowroomSystem::saveLights(lights, configPath));

    QVector<ShowroomSystem::ShowroomLight> loaded = ShowroomSystem::loadLights(configPath);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0].name, QString("Test Sun"));
    QCOMPARE(loaded[0].intensity, 2.0f);
    QCOMPARE(loaded[0].range, 50.0f);
    QVERIFY(loaded[0].isActive);

    QFile::remove(configPath);
}

void TestShowroomEditor::test_defaultCamera() {
    ShowroomSystem::ShowroomCamera cam = ShowroomSystem::getDefaultCamera();
    QCOMPARE(cam.name, QString("Default"));
    QCOMPARE(cam.fov, 60.0f);
    QCOMPARE(cam.position[2], 5.0f);
    QCOMPARE(cam.position[1], 2.0f);
    QVERIFY(cam.isActive);
}

void TestShowroomEditor::test_defaultLights() {
    QVector<ShowroomSystem::ShowroomLight> lights = ShowroomSystem::getDefaultLights();
    QCOMPARE(lights.size(), 3);
    QCOMPARE(lights[0].name, QString("Sun"));
    QCOMPARE(lights[0].type, QString("directional"));
    QCOMPARE(lights[1].name, QString("Ambient"));
    QCOMPARE(lights[1].type, QString("point"));
    QCOMPARE(lights[2].name, QString("Fill"));
    QCOMPARE(lights[2].type, QString("spot"));
}

void TestShowroomEditor::test_validatePreviewConfig() {
    ShowroomSystem::PreviewConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.samples = 4;
    cfg.fov = 60.0f;
    QString error;
    QVERIFY(ShowroomSystem::validatePreviewConfig(cfg, &error));

    cfg.width = 0;
    QVERIFY(!ShowroomSystem::validatePreviewConfig(cfg, &error));

    cfg.width = 1920;
    cfg.samples = 32;
    QVERIFY(!ShowroomSystem::validatePreviewConfig(cfg, &error));

    cfg.samples = 4;
    cfg.fov = 200.0f;
    QVERIFY(!ShowroomSystem::validatePreviewConfig(cfg, &error));
}

QTEST_MAIN(TestShowroomEditor)
#include "test_ShowroomEditor.moc"