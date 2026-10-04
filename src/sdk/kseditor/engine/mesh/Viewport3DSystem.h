#pragma once

// Unified Viewport3D System
// Merges: Viewport3D.h, Viewport3DWidget.h, VulkanViewportWidget (from Viewport.h)

#include <QObject>
#include <QVariant>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QImage>
#include <QColor>
#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QQuaternion>
#include <QElapsedTimer>
#include <QHash>
#include <QPoint>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QBasicTimer>

#if HAS_VULKAN
#include "../Graphics/VulkanRenderer.h"
#endif

namespace ks {
class SceneObject;
class SceneMesh;

// ── Vulkan Fallback Widget (OpenGL-based) ──────────────────────────────────
// Provides basic OpenGL rendering with camera orbit/pan/zoom and grid overlay.

class VulkanViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit VulkanViewportWidget(QWidget* parent = nullptr)
        : QOpenGLWidget(parent) {
        m_fpsTimer.start(1000, this);
    }
    ~VulkanViewportWidget() override { makeCurrent(); doneCurrent(); }

    void resizeViewport(int w, int h) { resize(w, h); }
    void renderFrame() { update(); }

    void setCameraTarget(const QVector3D& target) { m_camTarget = target; update(); }
    QVector3D cameraTarget() const { return m_camTarget; }
    void setOrbit(float yaw, float pitch, float dist) {
        m_camYaw = yaw; m_camPitch = pitch; m_camDist = dist;
        updateMatrices(); update();
    }
    void orbit(float deltaYaw, float deltaPitch) {
        m_camYaw += deltaYaw;
        m_camPitch = qBound(-89.0f, m_camPitch + deltaPitch, 89.0f);
        updateMatrices(); update();
        emit cameraChanged();
    }
    void pan(float dx, float dy) {
        QMatrix4x4 inv = m_view.inverted();
        QVector3D right = inv.mapVector(QVector3D(1, 0, 0)) * dx * m_camDist * 0.002f;
        QVector3D up = inv.mapVector(QVector3D(0, 1, 0)) * dy * m_camDist * 0.002f;
        m_camTarget += right + up;
        updateMatrices(); update();
        emit cameraChanged();
    }
    void zoom(float delta) {
        m_camDist *= (1.0f - delta * 0.001f);
        m_camDist = qMax(0.5f, m_camDist);
        updateMatrices(); update();
        emit cameraChanged();
    }

    void setGridVisible(bool v) { m_gridVisible = v; update(); }
    bool isGridVisible() const { return m_gridVisible; }
    void setGridSize(float size) { m_gridSize = size; update(); }
    void setBackgroundColor(const QColor& c) { m_bgColor = c; update(); }
    void setViewMatrix(const QMatrix4x4& view) { m_view = view; update(); }
    int triangleCount() const { return m_triangleCount; }
    int fps() const { return m_fps; }

signals:
    void cameraChanged();
    void frameRendered(int triangles, int fps);

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), 1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        updateMatrices();
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
        m_projection.setToIdentity();
        m_projection.perspective(m_fov, float(w) / float(h ? h : 1), 0.1f, 10000.0f);
    }

    void paintGL() override {
        glClearColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadMatrixf((m_projection * m_view).constData());

        m_triangleCount = 0;
        if (m_gridVisible) renderGrid();
        renderAxes();

        m_frameCount++;
        emit frameRendered(m_triangleCount, m_fps);
    }

    void mousePressEvent(QMouseEvent* e) override {
        m_lastMousePos = e->pos();
        if (e->button() == Qt::LeftButton) m_lmbDown = true;
        if (e->button() == Qt::RightButton) m_rmbDown = true;
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        int dx = e->pos().x() - m_lastMousePos.x();
        int dy = e->pos().y() - m_lastMousePos.y();
        m_lastMousePos = e->pos();
        if (m_lmbDown) orbit(dx * 0.5f, dy * 0.5f);
        if (m_rmbDown) pan(dx, dy);
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) m_lmbDown = false;
        if (e->button() == Qt::RightButton) m_rmbDown = false;
    }

    void wheelEvent(QWheelEvent* e) override { zoom(e->angleDelta().y()); }

    void timerEvent(QTimerEvent* e) override {
        if (e->timerId() == m_fpsTimer.timerId()) {
            m_fps = m_frameCount;
            m_frameCount = 0;
        }
    }

private:
    void renderGrid() {
        glBegin(GL_LINES);
        float half = m_gridSize * 0.5f;
        float step = m_gridSize / 20.0f;
        glColor4f(0.35f, 0.35f, 0.35f, 0.5f);
        for (float x = -half; x <= half; x += step) {
            glVertex3f(x, 0, -half);
            glVertex3f(x, 0, half);
        }
        for (float z = -half; z <= half; z += step) {
            glVertex3f(-half, 0, z);
            glVertex3f(half, 0, z);
        }
        glEnd();
        m_triangleCount += 2 * int(m_gridSize / step);
    }

    void renderAxes() {
        glBegin(GL_LINES);
        float len = m_gridSize * 0.15f;
        glColor3f(1, 0, 0); glVertex3f(0, 0, 0); glVertex3f(len, 0, 0);
        glColor3f(0, 1, 0); glVertex3f(0, 0, 0); glVertex3f(0, len, 0);
        glColor3f(0, 0, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, len);
        glEnd();
        m_triangleCount += 3;
    }

    void updateMatrices() {
        float yawRad = qDegreesToRadians(m_camYaw);
        float pitchRad = qDegreesToRadians(m_camPitch);
        QVector3D eye(
            m_camTarget.x() + m_camDist * qCos(pitchRad) * qSin(yawRad),
            m_camTarget.y() + m_camDist * qSin(pitchRad),
            m_camTarget.z() + m_camDist * qCos(pitchRad) * qCos(yawRad)
        );
        m_view.setToIdentity();
        m_view.lookAt(eye, m_camTarget, QVector3D(0, 1, 0));
    }

    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QVector3D m_camTarget;
    float m_camYaw = 30.0f;
    float m_camPitch = 45.0f;
    float m_camDist = 10.0f;
    float m_fov = 50.0f;

    QPoint m_lastMousePos;
    bool m_lmbDown = false;
    bool m_rmbDown = false;

    bool m_gridVisible = true;
    float m_gridSize = 10.0f;
    QColor m_bgColor = QColor(30, 30, 30);

    int m_triangleCount = 0;
    int m_fps = 0;
    int m_frameCount = 0;
    QBasicTimer m_fpsTimer;
};

// ── OpenGL Viewport Widget (Modern Shaders) ────────────────────────────────

struct MeshGLData {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;
    int vertexCount = 0;
    QMatrix4x4 transform;
    QVector3D color = QVector3D(0.35f, 0.55f, 0.85f);
};

enum class GizmoMode { None, Translate, Rotate, Scale };
enum class GizmoAxis { None, X, Y, Z, XY, YZ, ZX };

class Viewport3DWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum CameraMode { Perspective, Top, Front, Right };
    enum RenderMode { Solid, Wireframe, Textured, Lit };

    explicit Viewport3DWidget(QWidget* parent = nullptr);
    ~Viewport3DWidget() override;

    void setScene(SceneObject* root);
    void setCameraMode(CameraMode mode);
    void setRenderMode(RenderMode mode);
    void setShowGrid(bool show);
    void setShowAxes(bool show);
    void resetCamera();
    void focusOnPoint(const QVector3D& point, float distance = 5.0f);
    void setCameraOrbit(float azimuthDeg, float elevationDeg, float distance);

    void setGizmoMode(GizmoMode mode);
    GizmoMode gizmoMode() const { return m_gizmoMode; }
    void setSelectedObject(SceneObject* obj);
    SceneObject* selectedObject() const { return m_selectedObject; }
    QVector3D screenToWorldRay(const QPoint& screenPos, QVector3D& rayOrigin, QVector3D& rayDir) const;
    SceneObject* pickObject(const QPoint& screenPos);

    QVector3D cameraPosition() const { return m_camPos; }
    QVector3D cameraTarget() const { return m_camTarget; }
    float fps() const { return m_fps; }
    int triangleCount() const { return m_triCount; }
    int vertexCount() const { return m_vertCount; }

signals:
    void cameraMoved();
    void mousePositionChanged(const QVector3D& worldPos);
    void objectClicked(const QString& id);
    void objectSelected(SceneObject* obj);
    void objectTransformed(SceneObject* obj);
    void renderStatsUpdated(int triangles, int vertices, float fps);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void setupShaders();
    void setupGrid();
    void setupAxes();
    void setupGizmo();
    void rebuildSceneGLData();
    void uploadMeshToGL(MeshGLData& data, const SceneMesh* mesh, const QMatrix4x4& transform);

    void drawGrid();
    void drawAxes();
    void drawSceneMeshes();
    void drawGizmo();

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix() const;

    void updateCameraFromMode();
    void handleGizmoInteraction(QMouseEvent* event);
    QVector3D projectToScreen(const QVector3D& worldPos) const;
    QVector3D unprojectFromScreen(const QPoint& screenPos, float depth) const;
    bool rayIntersectsBounds(const QVector3D& rayOrigin, const QVector3D& rayDir,
                             const QVector3D& min, const QVector3D& max, float& outDist) const;

    QOpenGLShaderProgram m_gridShader;
    QOpenGLShaderProgram m_meshShader;
    QOpenGLShaderProgram m_gizmoShader;

    GLuint m_gridVAO = 0;
    GLuint m_gridVBO = 0;
    int m_gridVertexCount = 0;

    GLuint m_axesVAO = 0;
    GLuint m_axesVBO = 0;
    int m_axesVertexCount = 0;

    GLuint m_gizmoVAO = 0;
    GLuint m_gizmoVBO = 0;
    int m_gizmoVertexCount = 0;

    SceneObject* m_sceneRoot = nullptr;
    SceneObject* m_selectedObject = nullptr;
    QHash<SceneObject*, MeshGLData> m_meshGLData;

    QVector3D m_camPos = QVector3D(5, 4, 8);
    QVector3D m_camTarget = QVector3D(0, 0, 0);
    float m_camDistance = 10.0f;
    float m_azimuth = -45.0f;
    float m_elevation = 25.0f;

    CameraMode m_cameraMode = Perspective;
    RenderMode m_renderMode = Solid;
    bool m_showGrid = true;
    bool m_showAxes = true;

    GizmoMode m_gizmoMode = GizmoMode::None;
    GizmoAxis m_activeAxis = GizmoAxis::None;
    bool m_gizmoDragging = false;
    QPoint m_gizmoStartPos;
    QVector3D m_gizmoStartWorldPos;
    QVector3D m_gizmoStartObjectPos;
    QQuaternion m_gizmoStartObjectRot;
    QVector3D m_gizmoStartObjectScale;

    bool m_mousePressed = false;
    Qt::MouseButton m_mouseButton = Qt::NoButton;
    QPoint m_lastMousePos;

    float m_fps = 0.0f;
    int m_frameCount = 0;
    QElapsedTimer m_fpsTimer;

    int m_triCount = 0;
    int m_vertCount = 0;
};

// ── Viewport3D Singleton (QML API) ─────────────────────────────────────────

class Viewport3D : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isInitialized READ isInitialized NOTIFY initializedChanged)

public:
    static Viewport3D* instance();

    Q_INVOKABLE bool initialize();
    bool isInitialized() const { return m_initialized; }

    Q_INVOKABLE void setBackgroundColor(const QColor& color);
    Q_INVOKABLE QColor getBackgroundColor() const;

    Q_INVOKABLE void setDisplayMode(const QString& mode);
    Q_INVOKABLE QString getDisplayMode() const;

    Q_INVOKABLE void setShadingMode(const QString& mode);
    Q_INVOKABLE QString getShadingMode() const;

    Q_INVOKABLE void setShowGrid(bool show);
    Q_INVOKABLE bool getShowGrid() const;

    Q_INVOKABLE void setShowAxes(bool show);
    Q_INVOKABLE bool getShowAxes() const;

    Q_INVOKABLE void setShowNormals(bool show);
    Q_INVOKABLE bool getShowNormals() const;

    Q_INVOKABLE void setShowWireframe(bool show);
    Q_INVOKABLE bool getShowWireframe() const;

    Q_INVOKABLE void setShowLighting(bool show);
    Q_INVOKABLE bool getShowLighting() const;

    Q_INVOKABLE void setShowShadows(bool show);
    Q_INVOKABLE bool getShowShadows() const;

    Q_INVOKABLE void setCameraSpeed(float speed);
    Q_INVOKABLE float getCameraSpeed() const;

    Q_INVOKABLE void setCameraSensitivity(float sensitivity);
    Q_INVOKABLE float getCameraSensitivity() const;

    Q_INVOKABLE void orbit(float dx, float dy);
    Q_INVOKABLE void pan(float dx, float dy);
    Q_INVOKABLE void zoom(float delta);

    Q_INVOKABLE void focusOnSelection();
    Q_INVOKABLE void focusOnAll();
    Q_INVOKABLE void resetCamera();

    Q_INVOKABLE void setGizmoMode(const QString& mode);
    Q_INVOKABLE QString getGizmoMode() const;

    Q_INVOKABLE void setMeshData(const QVariant& meshData);
    Q_INVOKABLE QVariant getMeshData() const;

    Q_INVOKABLE void createMesh(const QString& name, const QVariant& vertices, const QVariant& indices);
    Q_INVOKABLE void updateMesh(const QString& name, const QVariant& vertices, const QVariant& indices);
    Q_INVOKABLE void destroyMesh(const QString& name);

    Q_INVOKABLE void setMeshTransform(const QString& name, const QVariant& transform);
    Q_INVOKABLE QVariant getMeshTransform(const QString& name) const;

    Q_INVOKABLE void clearMeshes();

    Q_INVOKABLE QVector3D screenToWorld(int x, int y, float depth);
    Q_INVOKABLE QPoint worldToScreen(const QVector3D& pos);

    Q_INVOKABLE float getDepthAt(int x, int y);

    struct RayPickResult {
        QString objectId;
        QVector3D hitPoint;
        QVector3D hitNormal;
        float distance = -1.0f;
        bool valid() const { return distance >= 0.0f; }
    };
    Q_INVOKABLE QVariantMap rayPick(int x, int y);
    Q_INVOKABLE QStringList rayPickAll(int x, int y);

    Q_INVOKABLE void setSelection(const QStringList& objectIds);
    Q_INVOKABLE QStringList getSelection() const;
    Q_INVOKABLE void clearSelection();

    Q_INVOKABLE void setRenderSettings(const QVariant& settings);
    Q_INVOKABLE QVariant getRenderSettings() const;

    float cameraYaw() const { return m_cameraYaw; }
    float cameraPitch() const { return m_cameraPitch; }
    float cameraPanX() const { return m_cameraPanX; }
    float cameraPanY() const { return m_cameraPanY; }
    float cameraDistance() const { return m_cameraDistance; }
    QVector3D cameraTarget() const { return m_cameraTarget; }

    Q_INVOKABLE void takeScreenshot(const QString& path);
    Q_INVOKABLE QImage getViewportImage();

signals:
    void initialized(bool success);
    void initializedChanged(bool initialized);
    void meshCreated(const QString& name);
    void meshDestroyed(const QString& name);
    void meshTransformChanged(const QString& name);
    void selectionChanged(const QStringList& ids);
    void cameraChanged();
    void renderSettingsChanged();
    void screenshotTaken(const QString& path);
    void objectClicked(const QString& id, int button);
    void objectHovered(const QString& id);
    void gizmoDragged(int axis, const QVector3D& delta);
    void error(const QString& message);

private:
    Viewport3D(QObject* parent = nullptr);
    ~Viewport3D();
    Q_DISABLE_COPY(Viewport3D)

    static Viewport3D* s_instance;

    bool m_initialized = false;
#if HAS_VULKAN
    VulkanViewportWidget* m_viewportWidget = nullptr;
#endif
    QColor m_backgroundColor;
    QString m_displayMode = "perspective";
    QString m_shadingMode = "solid";
    QString m_gizmoMode = "move";
    float m_cameraSpeed = 5.0f;
    float m_cameraSensitivity = 0.5f;
    bool m_showGrid = true;
    bool m_showAxes = true;
    bool m_showNormals = false;
    bool m_showWireframe = true;
    bool m_showLighting = true;
    bool m_showShadows = false;

    QStringList m_selectedObjects;
    QMap<QString, QVariant> m_meshData;
    QMap<QString, QVariant> m_meshTransforms;

    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;
    float m_cameraPanX = 0.0f;
    float m_cameraPanY = 0.0f;
    float m_cameraDistance = 50.0f;
    QVector3D m_cameraTarget;
    int m_viewportWidth = 1920;
    int m_viewportHeight = 1080;

    struct MeshData {
        QVariantList vertices;
        QVariantList indices;
    };
    QMap<QString, MeshData> m_meshes;
};

} // namespace ks
