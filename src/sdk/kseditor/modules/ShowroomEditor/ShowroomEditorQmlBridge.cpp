#include "ShowroomEditorQmlBridge.h"
#include "sdk/kseditor/engine/sys/LogManager.h"

namespace ks {

ShowroomEditorQmlBridge* ShowroomEditorQmlBridge::s_instance = nullptr;

ShowroomEditorQmlBridge* ShowroomEditorQmlBridge::instance() {
    if (!s_instance) {
        s_instance = new ShowroomEditorQmlBridge();
    }
    return s_instance;
}

void ShowroomEditorQmlBridge::setCameraDistance(float v) {
    if (qFuzzyCompare(m_config.cameraDistance, v)) return;
    m_config.cameraDistance = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setCameraHeight(float v) {
    if (qFuzzyCompare(m_config.cameraHeight, v)) return;
    m_config.cameraHeight = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setCameraAngle(float v) {
    if (qFuzzyCompare(m_config.cameraAngle, v)) return;
    m_config.cameraAngle = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setCameraFov(float v) {
    if (qFuzzyCompare(m_config.cameraFov, v)) return;
    m_config.cameraFov = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setRotateSpeed(float v) {
    if (qFuzzyCompare(m_config.rotateSpeed, v)) return;
    m_config.rotateSpeed = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setAutoRotate(bool v) {
    if (m_config.autoRotate == v) return;
    m_config.autoRotate = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setBackgroundPath(const QString& v) {
    if (m_config.backgroundPath == v) return;
    m_config.backgroundPath = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setAmbientColor(const QString& v) {
    QColor c(v);
    if (m_config.ambientColor == c) return;
    m_config.ambientColor = c;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setSunColor(const QString& v) {
    QColor c(v);
    if (m_config.sunColor == c) return;
    m_config.sunColor = c;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setSunIntensity(float v) {
    if (qFuzzyCompare(m_config.sunIntensity, v)) return;
    m_config.sunIntensity = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setAmbientIntensity(float v) {
    if (qFuzzyCompare(m_config.ambientIntensity, v)) return;
    m_config.ambientIntensity = v;
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::setSelectedCameraIndex(int v) {
    if (m_selectedCameraIndex == v) return;
    m_selectedCameraIndex = v;
    emit configChanged();
}

void ShowroomEditorQmlBridge::setSelectedLightIndex(int v) {
    if (m_selectedLightIndex == v) return;
    m_selectedLightIndex = v;
    emit configChanged();
}

void ShowroomEditorQmlBridge::setBackgroundColor(const QString& v) {
    QColor c(v);
    if (m_config.backgroundColor == c) return;
    m_config.backgroundColor = c;
    markUnsaved();
    emit configChanged();
}



bool ShowroomEditorQmlBridge::loadShowroom(const QString& path) {
    m_config = ShowroomSystem::loadConfig(path);
    m_cameras = ShowroomSystem::loadCameras(path);
    m_lights = ShowroomSystem::loadLights(path);
    m_hasUnsavedChanges = false;
    emit showroomChanged();
    emit unsavedChangesChanged();
    emit configChanged();
    emit showroomLoaded(m_config.name);
    return true;
}

bool ShowroomEditorQmlBridge::saveShowroom(const QString& path) {
    bool ok = ShowroomSystem::saveConfig(m_config, path);
    ok &= ShowroomSystem::saveCameras(m_cameras, path);
    ok &= ShowroomSystem::saveLights(m_lights, path);
    if (ok) {
        m_hasUnsavedChanges = false;
        emit unsavedChangesChanged();
        emit showroomSaved(m_config.name);
    }
    return ok;
}

bool ShowroomEditorQmlBridge::loadFromAc(const QString& acPath, const QString& showroomName) {
    QString configPath = acPath + "/showroom/" + showroomName + "/showroom.ini";
    return loadShowroom(configPath);
}

bool ShowroomEditorQmlBridge::saveToAc(const QString& acPath, const QString& showroomName) {
    QString configPath = acPath + "/showroom/" + showroomName + "/showroom.ini";
    QDir().mkpath(QFileInfo(configPath).absolutePath());
    return saveShowroom(configPath);
}

QVariantList ShowroomEditorQmlBridge::getCameras() {
    QVariantList result;
    for (const auto& cam : m_cameras) {
        QVariantMap m;
        m["name"] = cam.name;
        m["posX"] = static_cast<double>(cam.position[0]);
        m["posY"] = static_cast<double>(cam.position[1]);
        m["posZ"] = static_cast<double>(cam.position[2]);
        m["targetX"] = static_cast<double>(cam.target[0]);
        m["targetY"] = static_cast<double>(cam.target[1]);
        m["targetZ"] = static_cast<double>(cam.target[2]);
        m["upX"] = static_cast<double>(cam.up[0]);
        m["upY"] = static_cast<double>(cam.up[1]);
        m["upZ"] = static_cast<double>(cam.up[2]);
        m["fov"] = static_cast<double>(cam.fov);
        m["isActive"] = cam.isActive;
        result.append(m);
    }
    return result;
}

QVariantMap ShowroomEditorQmlBridge::getCamera(int index) {
    QVariantMap m;
    if (index < 0 || index >= m_cameras.size()) return m;
    const auto& cam = m_cameras[index];
    m["name"] = cam.name;
    m["posX"] = static_cast<double>(cam.position[0]);
    m["posY"] = static_cast<double>(cam.position[1]);
    m["posZ"] = static_cast<double>(cam.position[2]);
    m["targetX"] = static_cast<double>(cam.target[0]);
    m["targetY"] = static_cast<double>(cam.target[1]);
    m["targetZ"] = static_cast<double>(cam.target[2]);
    m["upX"] = static_cast<double>(cam.up[0]);
    m["upY"] = static_cast<double>(cam.up[1]);
    m["upZ"] = static_cast<double>(cam.up[2]);
    m["fov"] = static_cast<double>(cam.fov);
    m["isActive"] = cam.isActive;
    return m;
}

void ShowroomEditorQmlBridge::addCamera(const QVariantMap& camera) {
    ShowroomSystem::ShowroomCamera cam;
    cam.name = camera.value("name", "New Camera").toString();
    cam.position[0] = static_cast<float>(camera.value("posX", 0.0).toDouble());
    cam.position[1] = static_cast<float>(camera.value("posY", 2.0).toDouble());
    cam.position[2] = static_cast<float>(camera.value("posZ", 5.0).toDouble());
    cam.target[0] = static_cast<float>(camera.value("targetX", 0.0).toDouble());
    cam.target[1] = static_cast<float>(camera.value("targetY", 0.0).toDouble());
    cam.target[2] = static_cast<float>(camera.value("targetZ", 0.0).toDouble());
    cam.up[0] = static_cast<float>(camera.value("upX", 0.0).toDouble());
    cam.up[1] = static_cast<float>(camera.value("upY", 1.0).toDouble());
    cam.up[2] = static_cast<float>(camera.value("upZ", 0.0).toDouble());
    cam.fov = static_cast<float>(camera.value("fov", 60.0).toDouble());
    cam.isActive = camera.value("isActive", true).toBool();
    m_cameras.append(cam);
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::removeCamera(int index) {
    if (index < 0 || index >= m_cameras.size()) return;
    m_cameras.removeAt(index);
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::updateCamera(int index, const QVariantMap& data) {
    if (index < 0 || index >= m_cameras.size()) return;
    auto& cam = m_cameras[index];
    if (data.contains("name")) cam.name = data["name"].toString();
    if (data.contains("posX")) cam.position[0] = static_cast<float>(data["posX"].toDouble());
    if (data.contains("posY")) cam.position[1] = static_cast<float>(data["posY"].toDouble());
    if (data.contains("posZ")) cam.position[2] = static_cast<float>(data["posZ"].toDouble());
    if (data.contains("targetX")) cam.target[0] = static_cast<float>(data["targetX"].toDouble());
    if (data.contains("targetY")) cam.target[1] = static_cast<float>(data["targetY"].toDouble());
    if (data.contains("targetZ")) cam.target[2] = static_cast<float>(data["targetZ"].toDouble());
    if (data.contains("upX")) cam.up[0] = static_cast<float>(data["upX"].toDouble());
    if (data.contains("upY")) cam.up[1] = static_cast<float>(data["upY"].toDouble());
    if (data.contains("upZ")) cam.up[2] = static_cast<float>(data["upZ"].toDouble());
    if (data.contains("fov")) cam.fov = static_cast<float>(data["fov"].toDouble());
    if (data.contains("isActive")) cam.isActive = data["isActive"].toBool();
    markUnsaved();
    emit configChanged();
}

QVariantList ShowroomEditorQmlBridge::getLights() {
    QVariantList result;
    for (const auto& light : m_lights) {
        QVariantMap m;
        m["name"] = light.name;
        m["type"] = light.type;
        m["posX"] = static_cast<double>(light.position[0]);
        m["posY"] = static_cast<double>(light.position[1]);
        m["posZ"] = static_cast<double>(light.position[2]);
        m["dirX"] = static_cast<double>(light.direction[0]);
        m["dirY"] = static_cast<double>(light.direction[1]);
        m["dirZ"] = static_cast<double>(light.direction[2]);
        m["color"] = light.color.name();
        m["intensity"] = static_cast<double>(light.intensity);
        m["range"] = static_cast<double>(light.range);
        m["isActive"] = light.isActive;
        result.append(m);
    }
    return result;
}

QVariantMap ShowroomEditorQmlBridge::getLight(int index) {
    QVariantMap m;
    if (index < 0 || index >= m_lights.size()) return m;
    const auto& light = m_lights[index];
    m["name"] = light.name;
    m["type"] = light.type;
    m["posX"] = static_cast<double>(light.position[0]);
    m["posY"] = static_cast<double>(light.position[1]);
    m["posZ"] = static_cast<double>(light.position[2]);
    m["dirX"] = static_cast<double>(light.direction[0]);
    m["dirY"] = static_cast<double>(light.direction[1]);
    m["dirZ"] = static_cast<double>(light.direction[2]);
    m["color"] = light.color.name();
    m["intensity"] = static_cast<double>(light.intensity);
    m["range"] = static_cast<double>(light.range);
    m["isActive"] = light.isActive;
    return m;
}

void ShowroomEditorQmlBridge::addLight(const QVariantMap& light) {
    ShowroomSystem::ShowroomLight l;
    l.name = light.value("name", "New Light").toString();
    l.type = light.value("type", "directional").toString();
    l.position[0] = static_cast<float>(light.value("posX", 0.0).toDouble());
    l.position[1] = static_cast<float>(light.value("posY", 5.0).toDouble());
    l.position[2] = static_cast<float>(light.value("posZ", 0.0).toDouble());
    l.direction[0] = static_cast<float>(light.value("dirX", 0.0).toDouble());
    l.direction[1] = static_cast<float>(light.value("dirY", -1.0).toDouble());
    l.direction[2] = static_cast<float>(light.value("dirZ", 0.0).toDouble());
    l.color = QColor(light.value("color", "#FFFFFF").toString());
    l.intensity = static_cast<float>(light.value("intensity", 1.0).toDouble());
    l.range = static_cast<float>(light.value("range", 10.0).toDouble());
    l.isActive = light.value("isActive", true).toBool();
    m_lights.append(l);
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::removeLight(int index) {
    if (index < 0 || index >= m_lights.size()) return;
    m_lights.removeAt(index);
    markUnsaved();
    emit configChanged();
}

void ShowroomEditorQmlBridge::updateLight(int index, const QVariantMap& data) {
    if (index < 0 || index >= m_lights.size()) return;
    auto& light = m_lights[index];
    if (data.contains("name")) light.name = data["name"].toString();
    if (data.contains("type")) light.type = data["type"].toString();
    if (data.contains("posX")) light.position[0] = static_cast<float>(data["posX"].toDouble());
    if (data.contains("posY")) light.position[1] = static_cast<float>(data["posY"].toDouble());
    if (data.contains("posZ")) light.position[2] = static_cast<float>(data["posZ"].toDouble());
    if (data.contains("dirX")) light.direction[0] = static_cast<float>(data["dirX"].toDouble());
    if (data.contains("dirY")) light.direction[1] = static_cast<float>(data["dirY"].toDouble());
    if (data.contains("dirZ")) light.direction[2] = static_cast<float>(data["dirZ"].toDouble());
    if (data.contains("color")) light.color = QColor(data["color"].toString());
    if (data.contains("intensity")) light.intensity = static_cast<float>(data["intensity"].toDouble());
    if (data.contains("range")) light.range = static_cast<float>(data["range"].toDouble());
    if (data.contains("isActive")) light.isActive = data["isActive"].toBool();
    markUnsaved();
    emit configChanged();
}

QVariantMap ShowroomEditorQmlBridge::validateConfig() {
    QVariantMap result;
    QString error;
    bool valid = ShowroomSystem::validateConfig(m_config, &error);
    result["valid"] = valid;
    result["error"] = error;
    return result;
}

QVariantMap ShowroomEditorQmlBridge::validatePreviewConfig(int width, int height, int samples, float fov) {
    QVariantMap result;
    QString error;
    ShowroomSystem::PreviewConfig config;
    config.width = width;
    config.height = height;
    config.samples = samples;
    config.fov = fov;
    bool valid = ShowroomSystem::validatePreviewConfig(config, &error);
    result["valid"] = valid;
    result["error"] = error;
    return result;
}

QStringList ShowroomEditorQmlBridge::getAvailableShowrooms(const QString& dir) {
    return ShowroomSystem::getAvailableShowrooms(dir);
}

bool ShowroomEditorQmlBridge::generatePreview(const QString& carPath, int width, int height, const QString& outputPath) {
    ShowroomSystem::PreviewConfig config;
    config.width = width;
    config.height = height;
    config.outputPath = outputPath;
    config.cameraDistance = m_config.cameraDistance;
    config.cameraHeight = m_config.cameraHeight;
    config.cameraAngle = m_config.cameraAngle;
    config.fov = m_config.cameraFov;
    config.backgroundColor = m_config.backgroundColor;

    bool ok = ShowroomSystem::generatePreview(carPath, config);
    if (ok) {
        m_previewGenerated = true;
        emit previewGeneratedChanged();
        emit previewGenerated(outputPath);
    }
    return ok;
}

bool ShowroomEditorQmlBridge::generateThumbnail(const QString& carPath, const QString& outputPath) {
    bool ok = ShowroomSystem::generateThumbnail(carPath, outputPath);
    if (ok) {
        m_previewGenerated = true;
        emit previewGeneratedChanged();
        emit previewGenerated(outputPath);
    }
    return ok;
}

void ShowroomEditorQmlBridge::generatePreviewSimple(const QString& path) {
    ShowroomSystem::PreviewConfig config;
    config.width = 1920;
    config.height = 1080;
    config.outputPath = path;
    config.cameraDistance = m_config.cameraDistance;
    config.cameraHeight = m_config.cameraHeight;
    config.cameraAngle = m_config.cameraAngle;
    config.fov = m_config.cameraFov;
    config.backgroundColor = m_config.backgroundColor;

    bool ok = ShowroomSystem::generatePreview("", config);
    if (ok) {
        m_previewGenerated = true;
        emit previewGeneratedChanged();
        emit previewGenerated(path);
    }
}

void ShowroomEditorQmlBridge::resetToDefaults() {
    m_config = ShowroomSystem::getDefaultConfig();
    m_cameras.clear();
    m_cameras.append(ShowroomSystem::getDefaultCamera());
    m_lights = ShowroomSystem::getDefaultLights();
    m_selectedCameraIndex = -1;
    m_selectedLightIndex = -1;
    markUnsaved();
    emit configChanged();
    emit showroomChanged();
}

void ShowroomEditorQmlBridge::markUnsaved() {
    if (!m_hasUnsavedChanges) {
        m_hasUnsavedChanges = true;
        emit unsavedChangesChanged();
    }
}

} // namespace ks
