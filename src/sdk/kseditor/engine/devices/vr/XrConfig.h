#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QMatrix4x4>
#include <QVector3D>

namespace ks {
namespace vr {

struct XrSettings {
    bool enabled = false;
    bool autoStart = false;
    float renderScale = 1.0f;
    int msaaSampleCount = 1;
    bool enableDepthSubmission = true;
    bool enableControllerVisualization = true;
    float movementSpeed = 2.0f;
    float rotationSpeed = 90.0f;
    float defaultEyeHeight = 1.7f;
    QVector3D defaultCameraPosition{0, 1.7f, -3};
    QString preferredGraphicsApi = "vulkan";
    QStringList enabledExtensions;

    void load(const QString& path) {
        if (path.isEmpty()) return;
        QSettings ini(path, QSettings::IniFormat);

        ini.beginGroup("VR");
        enabled = ini.value("enabled", enabled).toBool();
        autoStart = ini.value("autoStart", autoStart).toBool();
        renderScale = ini.value("renderScale", renderScale).toFloat();
        msaaSampleCount = ini.value("msaaSampleCount", msaaSampleCount).toInt();
        enableDepthSubmission = ini.value("enableDepthSubmission", enableDepthSubmission).toBool();
        enableControllerVisualization = ini.value("enableControllerVisualization", enableControllerVisualization).toBool();
        movementSpeed = ini.value("movementSpeed", movementSpeed).toFloat();
        rotationSpeed = ini.value("rotationSpeed", rotationSpeed).toFloat();
        defaultEyeHeight = ini.value("defaultEyeHeight", defaultEyeHeight).toFloat();
        ini.endGroup();
    }

    void save(const QString& path) const {
        if (path.isEmpty()) return;
        QSettings ini(path, QSettings::IniFormat);

        ini.beginGroup("VR");
        ini.setValue("enabled", enabled);
        ini.setValue("autoStart", autoStart);
        ini.setValue("renderScale", renderScale);
        ini.setValue("msaaSampleCount", msaaSampleCount);
        ini.setValue("enableDepthSubmission", enableDepthSubmission);
        ini.setValue("enableControllerVisualization", enableControllerVisualization);
        ini.setValue("movementSpeed", movementSpeed);
        ini.setValue("rotationSpeed", rotationSpeed);
        ini.setValue("defaultEyeHeight", defaultEyeHeight);
        ini.endGroup();
    }
};

}} // namespace ks::vr
