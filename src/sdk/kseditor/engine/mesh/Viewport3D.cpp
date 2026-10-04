#include "Viewport3DSystem.h"
#include <QImage>
#include <QFile>
#include <QDir>
#include <cmath>

namespace ks {

Viewport3D* Viewport3D::s_instance = nullptr;

Viewport3D* Viewport3D::instance() {
    if (!s_instance) s_instance = new Viewport3D();
    return s_instance;
}

Viewport3D::Viewport3D(QObject* parent) : QObject(parent) {}

Viewport3D::~Viewport3D() = default;

bool Viewport3D::initialize() {
    if (m_initialized) return true;
    m_initialized = true;
    emit initialized(true);
    emit initializedChanged(true);
    return true;
}

void Viewport3D::setBackgroundColor(const QColor& color) {
    m_backgroundColor = color;
    emit renderSettingsChanged();
}

QColor Viewport3D::getBackgroundColor() const {
    return m_backgroundColor;
}

void Viewport3D::setDisplayMode(const QString& mode) {
    m_displayMode = mode;
    emit renderSettingsChanged();
}

QString Viewport3D::getDisplayMode() const {
    return m_displayMode;
}

void Viewport3D::setShadingMode(const QString& mode) {
    m_shadingMode = mode;
    emit renderSettingsChanged();
}

QString Viewport3D::getShadingMode() const {
    return m_shadingMode;
}

void Viewport3D::setShowGrid(bool show) {
    m_showGrid = show;
    emit renderSettingsChanged();
}

bool Viewport3D::getShowGrid() const {
    return m_showGrid;
}

void Viewport3D::setShowAxes(bool show) {
    m_showAxes = show;
    emit renderSettingsChanged();
}

bool Viewport3D::getShowAxes() const {
    return m_showAxes;
}

void Viewport3D::setShowNormals(bool show) {
    m_showNormals = show;
    emit renderSettingsChanged();
}

bool Viewport3D::getShowNormals() const {
    return m_showNormals;
}

void Viewport3D::setShowWireframe(bool show) {
    m_showWireframe = show;
    emit renderSettingsChanged();
}

bool Viewport3D::getShowWireframe() const {
    return m_showWireframe;
}

void Viewport3D::setShowLighting(bool show) {
    m_showLighting = show;
    emit renderSettingsChanged();
}

bool Viewport3D::getShowLighting() const {
    return m_showLighting;
}

void Viewport3D::setShowShadows(bool show) {
    m_showShadows = show;
    emit renderSettingsChanged();
}

bool Viewport3D::getShowShadows() const {
    return m_showShadows;
}

void Viewport3D::setCameraSpeed(float speed) {
    m_cameraSpeed = qMax(0.1f, speed);
}

float Viewport3D::getCameraSpeed() const {
    return m_cameraSpeed;
}

void Viewport3D::setCameraSensitivity(float sensitivity) {
    m_cameraSensitivity = qBound(0.01f, sensitivity, 10.0f);
}

float Viewport3D::getCameraSensitivity() const {
    return m_cameraSensitivity;
}

void Viewport3D::orbit(float dx, float dy) {
    m_cameraYaw += dx * m_cameraSensitivity;
    m_cameraPitch = qBound(-89.0f, m_cameraPitch + dy * m_cameraSensitivity, 89.0f);
    emit cameraChanged();
}

void Viewport3D::pan(float dx, float dy) {
    m_cameraPanX += dx * m_cameraSpeed * 0.01f;
    m_cameraPanY += dy * m_cameraSpeed * 0.01f;
    emit cameraChanged();
}

void Viewport3D::zoom(float delta) {
    m_cameraDistance = qMax(0.5f, m_cameraDistance * (1.0f - delta * 0.001f));
    emit cameraChanged();
}

void Viewport3D::focusOnSelection() {
    if (m_selectedObjects.isEmpty()) return;
    // Focus on first selected object (placeholder logic)
    emit cameraChanged();
}

void Viewport3D::focusOnAll() {
    m_cameraTarget = QVector3D(0, 0, 0);
    m_cameraDistance = 50.0f;
    emit cameraChanged();
}

void Viewport3D::resetCamera() {
    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.0f;
    m_cameraPanX = 0.0f;
    m_cameraPanY = 0.0f;
    m_cameraDistance = 50.0f;
    m_cameraTarget = QVector3D(0, 0, 0);
    emit cameraChanged();
}

void Viewport3D::setGizmoMode(const QString& mode) {
    m_gizmoMode = mode;
    emit renderSettingsChanged();
}

QString Viewport3D::getGizmoMode() const {
    return m_gizmoMode;
}

void Viewport3D::setMeshData(const QVariant& meshData) {
    m_meshData = meshData.toMap();
    emit renderSettingsChanged();
}

QVariant Viewport3D::getMeshData() const {
    return QVariant(m_meshData);
}

void Viewport3D::createMesh(const QString& name, const QVariant& vertices, const QVariant& indices) {
    MeshData md;
    md.vertices = vertices.toList();
    md.indices = indices.toList();
    m_meshes[name] = md;
    emit meshCreated(name);
}

void Viewport3D::updateMesh(const QString& name, const QVariant& vertices, const QVariant& indices) {
    if (!m_meshes.contains(name)) {
        createMesh(name, vertices, indices);
        return;
    }
    m_meshes[name].vertices = vertices.toList();
    m_meshes[name].indices = indices.toList();
    emit renderSettingsChanged();
}

void Viewport3D::destroyMesh(const QString& name) {
    if (m_meshes.remove(name) > 0) {
        emit meshDestroyed(name);
    }
}

void Viewport3D::setMeshTransform(const QString& name, const QVariant& transform) {
    m_meshTransforms[name] = transform;
    emit meshTransformChanged(name);
}

QVariant Viewport3D::getMeshTransform(const QString& name) const {
    return m_meshTransforms.value(name);
}

void Viewport3D::clearMeshes() {
    for (auto it = m_meshes.constBegin(); it != m_meshes.constEnd(); ++it) {
        emit meshDestroyed(it.key());
    }
    m_meshes.clear();
    m_meshTransforms.clear();
}

QVector3D Viewport3D::screenToWorld(int x, int y, float depth) {
    float ndcX = (2.0f * x) / m_viewportWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * y) / m_viewportHeight;

    float yawRad = m_cameraYaw * 3.14159265f / 180.0f;
    float pitchRad = m_cameraPitch * 3.14159265f / 180.0f;

    QVector3D eye(
        m_cameraTarget.x() + m_cameraDistance * cos(pitchRad) * cos(yawRad),
        m_cameraTarget.y() + m_cameraDistance * sin(pitchRad),
        m_cameraTarget.z() + m_cameraDistance * cos(pitchRad) * sin(yawRad)
    );

    QVector3D forward = (m_cameraTarget - eye).normalized();
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
    QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    float fov = 50.0f * 3.14159265f / 180.0f;
    float aspect = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
    float tanHalfFov = tan(fov * 0.5f);

    QVector3D worldPos = eye + forward * depth +
                         right * (ndcX * tanHalfFov * aspect * depth) +
                         up * (ndcY * tanHalfFov * depth);
    return worldPos;
}

QPoint Viewport3D::worldToScreen(const QVector3D& pos) {
    float yawRad = m_cameraYaw * 3.14159265f / 180.0f;
    float pitchRad = m_cameraPitch * 3.14159265f / 180.0f;

    QVector3D eye(
        m_cameraTarget.x() + m_cameraDistance * cos(pitchRad) * cos(yawRad),
        m_cameraTarget.y() + m_cameraDistance * sin(pitchRad),
        m_cameraTarget.z() + m_cameraDistance * cos(pitchRad) * sin(yawRad)
    );

    QVector3D forward = (m_cameraTarget - eye).normalized();
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
    QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    QVector3D rel = pos - eye;
    float z = QVector3D::dotProduct(rel, forward);
    if (z <= 0.01f) return QPoint(-1, -1);

    float fov = 50.0f * 3.14159265f / 180.0f;
    float aspect = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
    float tanHalfFov = tan(fov * 0.5f);

    float ndcX = QVector3D::dotProduct(rel, right) / (z * tanHalfFov * aspect);
    float ndcY = QVector3D::dotProduct(rel, up) / (z * tanHalfFov);

    int sx = static_cast<int>((ndcX + 1.0f) * 0.5f * m_viewportWidth);
    int sy = static_cast<int>((1.0f - ndcY) * 0.5f * m_viewportHeight);
    return QPoint(sx, sy);
}

float Viewport3D::getDepthAt(int x, int y) {
    Q_UNUSED(x); Q_UNUSED(y);
    return 1.0f;
}

QVariantMap Viewport3D::rayPick(int x, int y) {
    QVariantMap result;
    QVector3D worldPos = screenToWorld(x, y, m_cameraDistance);
    result["valid"] = false;
    result["distance"] = -1.0f;
    return result;
}

QStringList Viewport3D::rayPickAll(int x, int y) {
    Q_UNUSED(x); Q_UNUSED(y);
    return {};
}

void Viewport3D::setSelection(const QStringList& objectIds) {
    m_selectedObjects = objectIds;
    emit selectionChanged(objectIds);
}

QStringList Viewport3D::getSelection() const {
    return m_selectedObjects;
}

void Viewport3D::clearSelection() {
    m_selectedObjects.clear();
    emit selectionChanged(m_selectedObjects);
}

void Viewport3D::setRenderSettings(const QVariant& settings) {
    QVariantMap map = settings.toMap();
    if (map.contains("backgroundColor")) m_backgroundColor = QColor(map["backgroundColor"].toString());
    if (map.contains("displayMode")) m_displayMode = map["displayMode"].toString();
    if (map.contains("shadingMode")) m_shadingMode = map["shadingMode"].toString();
    if (map.contains("showGrid")) m_showGrid = map["showGrid"].toBool();
    if (map.contains("showAxes")) m_showAxes = map["showAxes"].toBool();
    if (map.contains("showWireframe")) m_showWireframe = map["showWireframe"].toBool();
    emit renderSettingsChanged();
}

QVariant Viewport3D::getRenderSettings() const {
    QVariantMap map;
    map["backgroundColor"] = m_backgroundColor.name();
    map["displayMode"] = m_displayMode;
    map["shadingMode"] = m_shadingMode;
    map["showGrid"] = m_showGrid;
    map["showAxes"] = m_showAxes;
    map["showWireframe"] = m_showWireframe;
    map["showLighting"] = m_showLighting;
    map["showShadows"] = m_showShadows;
    return map;
}

void Viewport3D::takeScreenshot(const QString& path) {
    QImage img(m_viewportWidth, m_viewportHeight, QImage::Format_ARGB32);
    img.fill(m_backgroundColor);
    if (img.save(path)) {
        emit screenshotTaken(path);
    } else {
        emit error("Failed to save screenshot to " + path);
    }
}

QImage Viewport3D::getViewportImage() {
    QImage img(m_viewportWidth, m_viewportHeight, QImage::Format_ARGB32);
    img.fill(m_backgroundColor);
    return img;
}

} // namespace ks
