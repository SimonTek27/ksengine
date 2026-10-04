#include "Viewport3DSystem.h"
#include "Graphics/SceneObject.h"
#include "Graphics/SceneMesh.h"
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLContext>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ks {

using pfn_glGenVertexArrays = void(*)(int, unsigned int*);
using pfn_glBindVertexArray = void(*)(unsigned int);
using pfn_glDeleteVertexArrays = void(*)(int, const unsigned int*);

static pfn_glGenVertexArrays _glGenVertexArrays = nullptr;
static pfn_glBindVertexArray _glBindVertexArray = nullptr;
static pfn_glDeleteVertexArrays _glDeleteVertexArrays = nullptr;

static const char* gridVertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    uniform mat4 uView;
    uniform mat4 uProj;
    void main() {
        gl_Position = uProj * uView * vec4(aPos, 1.0);
    }
)";

static const char* gridFragmentShader = R"(
    #version 330 core
    out vec4 fragColor;
    uniform vec3 uColor;
    void main() {
        fragColor = vec4(uColor, 1.0);
    }
)";

static const char* meshVertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aColor;
    layout(location = 2) in vec3 aNormal;
    uniform mat4 uView;
    uniform mat4 uProj;
    uniform mat4 uModel;
    uniform vec3 uLightDir;
    uniform vec3 uAmbient;
    uniform float uSunIntensity;
    uniform float uAmbientIntensity;
    out vec3 vColor;
    void main() {
        vec3 worldPos = vec3(uModel * vec4(aPos, 1.0));
        vec3 worldNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
        float ndotl = max(0.0, dot(worldNormal, normalize(uLightDir)));
        vec3 lit = uAmbient * uAmbientIntensity + aColor * (uSunIntensity * ndotl + uAmbientIntensity * 0.3);
        gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
        vColor = lit;
    }
)";

static const char* meshFragmentShader = R"(
    #version 330 core
    in vec3 vColor;
    out vec4 fragColor;
    uniform bool uWireframe;
    void main() {
        if (uWireframe)
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        else
            fragColor = vec4(vColor, 1.0);
    }
)";

static const char* gizmoVertexShader = R"(
    #version 330 core
    layout(location = 0) in vec3 aPos;
    layout(location = 1) in vec3 aColor;
    uniform mat4 uView;
    uniform mat4 uProj;
    uniform mat4 uModel;
    out vec3 vColor;
    void main() {
        gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
        vColor = aColor;
    }
)";

static const char* gizmoFragmentShader = R"(
    #version 330 core
    in vec3 vColor;
    out vec4 fragColor;
    void main() {
        fragColor = vec4(vColor, 1.0);
    }
)";

Viewport3DWidget::Viewport3DWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_selectedObject(nullptr)
    , m_gizmoMode(GizmoMode::None)
    , m_activeAxis(GizmoAxis::None)
    , m_gizmoDragging(false)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_fpsTimer.start();
}

Viewport3DWidget::~Viewport3DWidget()
{
    makeCurrent();
    if (m_gridVAO) _glDeleteVertexArrays(1, &m_gridVAO);
    if (m_gridVBO) glDeleteBuffers(1, &m_gridVBO);
    if (m_axesVAO) _glDeleteVertexArrays(1, &m_axesVAO);
    if (m_axesVBO) glDeleteBuffers(1, &m_axesVBO);
    if (m_gizmoVAO) _glDeleteVertexArrays(1, &m_gizmoVAO);
    if (m_gizmoVBO) glDeleteBuffers(1, &m_gizmoVBO);
    for (auto it = m_meshGLData.begin(); it != m_meshGLData.end(); ++it) {
        if (it.value().vao) _glDeleteVertexArrays(1, &it.value().vao);
        if (it.value().vbo) glDeleteBuffers(1, &it.value().vbo);
        if (it.value().ebo) glDeleteBuffers(1, &it.value().ebo);
    }
    m_meshGLData.clear();
    doneCurrent();
}

void Viewport3DWidget::setScene(SceneObject* root)
{
    m_sceneRoot = root;
    makeCurrent();
    rebuildSceneGLData();
    doneCurrent();
    update();
}

void Viewport3DWidget::initializeGL()
{
    initializeOpenGLFunctions();

    auto ctx = QOpenGLContext::currentContext();
    _glGenVertexArrays = reinterpret_cast<pfn_glGenVertexArrays>(ctx->getProcAddress("glGenVertexArrays"));
    _glBindVertexArray = reinterpret_cast<pfn_glBindVertexArray>(ctx->getProcAddress("glBindVertexArray"));
    _glDeleteVertexArrays = reinterpret_cast<pfn_glDeleteVertexArrays>(ctx->getProcAddress("glDeleteVertexArrays"));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    setupShaders();
    setupGrid();
    setupAxes();
    setupGizmo();
    rebuildSceneGLData();
}

void Viewport3DWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void Viewport3DWidget::paintGL()
{
    glClearColor(0.1f, 0.1f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_showGrid) drawGrid();
    if (m_showAxes) drawAxes();
    drawSceneMeshes();
    if (m_selectedObject && m_gizmoMode != GizmoMode::None) drawGizmo();

    m_frameCount++;
    if (m_fpsTimer.elapsed() >= 1000) {
        m_fps = static_cast<float>(m_frameCount * 1000.0 / m_fpsTimer.elapsed());
        m_frameCount = 0;
        m_fpsTimer.restart();
        emit renderStatsUpdated(m_triCount, m_vertCount, m_fps);
    }
}

void Viewport3DWidget::setupShaders()
{
    if (!m_gridShader.addShaderFromSourceCode(QOpenGLShader::Vertex, gridVertexShader)) {
        qWarning() << "Grid vertex shader error:" << m_gridShader.log();
    }
    if (!m_gridShader.addShaderFromSourceCode(QOpenGLShader::Fragment, gridFragmentShader)) {
        qWarning() << "Grid fragment shader error:" << m_gridShader.log();
    }
    m_gridShader.link();

    if (!m_meshShader.addShaderFromSourceCode(QOpenGLShader::Vertex, meshVertexShader)) {
        qWarning() << "Mesh vertex shader error:" << m_meshShader.log();
    }
    if (!m_meshShader.addShaderFromSourceCode(QOpenGLShader::Fragment, meshFragmentShader)) {
        qWarning() << "Mesh fragment shader error:" << m_meshShader.log();
    }
    m_meshShader.link();

    if (!m_gizmoShader.addShaderFromSourceCode(QOpenGLShader::Vertex, gizmoVertexShader)) {
        qWarning() << "Gizmo vertex shader error:" << m_gizmoShader.log();
    }
    if (!m_gizmoShader.addShaderFromSourceCode(QOpenGLShader::Fragment, gizmoFragmentShader)) {
        qWarning() << "Gizmo fragment shader error:" << m_gizmoShader.log();
    }
    m_gizmoShader.link();
}

void Viewport3DWidget::setupGrid()
{
    std::vector<float> vertices;
    int gridSize = 20;
    int step = 1;

    for (int i = -gridSize; i <= gridSize; i += step) {
        bool isAxis = (i == 0);
        float alpha = isAxis ? 0.5f : 0.15f;
        vertices.push_back(static_cast<float>(i));
        vertices.push_back(0.0f);
        vertices.push_back(static_cast<float>(-gridSize));
        vertices.push_back(alpha);

        vertices.push_back(static_cast<float>(i));
        vertices.push_back(0.0f);
        vertices.push_back(static_cast<float>(gridSize));
        vertices.push_back(alpha);

        vertices.push_back(static_cast<float>(-gridSize));
        vertices.push_back(0.0f);
        vertices.push_back(static_cast<float>(i));
        vertices.push_back(alpha);

        vertices.push_back(static_cast<float>(gridSize));
        vertices.push_back(0.0f);
        vertices.push_back(static_cast<float>(i));
        vertices.push_back(alpha);
    }

    m_gridVertexCount = static_cast<int>(vertices.size() / 4);

    _glGenVertexArrays(1, &m_gridVAO);
    glGenBuffers(1, &m_gridVBO);
    _glBindVertexArray(m_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    _glBindVertexArray(0);
}

void Viewport3DWidget::setupAxes()
{
    float len = 2.0f;
    std::vector<float> vertices = {
        0, 0, 0,  1.0f, 0.2f, 0.2f,
        len, 0, 0,  1.0f, 0.2f, 0.2f,

        0, 0, 0,  0.2f, 1.0f, 0.2f,
        0, len, 0,  0.2f, 1.0f, 0.2f,

        0, 0, 0,  0.2f, 0.2f, 1.0f,
        0, 0, len,  0.2f, 0.2f, 1.0f,
    };

    m_axesVertexCount = 6;

    _glGenVertexArrays(1, &m_axesVAO);
    glGenBuffers(1, &m_axesVBO);
    _glBindVertexArray(m_axesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_axesVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    _glBindVertexArray(0);
}

void Viewport3DWidget::rebuildSceneGLData()
{
    for (auto it = m_meshGLData.begin(); it != m_meshGLData.end(); ++it) {
        if (it.value().vao) _glDeleteVertexArrays(1, &it.value().vao);
        if (it.value().vbo) glDeleteBuffers(1, &it.value().vbo);
        if (it.value().ebo) glDeleteBuffers(1, &it.value().ebo);
    }
    m_meshGLData.clear();
    m_triCount = 0;
    m_vertCount = 0;

    if (!m_sceneRoot) return;

    // Traverse scene hierarchy (breadth-first)
    QVector<SceneObject*> stack = { m_sceneRoot };
    while (!stack.isEmpty()) {
        SceneObject* obj = stack.takeFirst();
        if (obj->type() == SceneObject::Type::Mesh && obj->hasMesh() && obj->isVisible()) {
            // Build world transform from hierarchy
            QMatrix4x4 worldMat;
            SceneObject* p = obj;
            QVector<SceneObject*> chain;
            while (p) { chain.prepend(p); p = p->parent(); }
            for (SceneObject* c : chain) {
                QMatrix4x4 qm = c->transform();
                worldMat = worldMat * qm;
            }

            MeshGLData glData;
            glData.transform = worldMat;
            uploadMeshToGL(glData, obj->mesh(), worldMat);
            m_meshGLData.insert(obj, glData);
            m_triCount += glData.indexCount / 3;
            m_vertCount += glData.vertexCount;
        }
        for (SceneObject* child : obj->children())
            stack.append(child);
    }
}

void Viewport3DWidget::uploadMeshToGL(MeshGLData& data, const SceneMesh* mesh, const QMatrix4x4& transform)
{
    const auto& verts = mesh->geometry().vertices;
    const auto& idx = mesh->geometry().indices;
    if (verts.isEmpty() || idx.isEmpty()) return;

    // Pack position (3) + color (3) + normal (3) per vertex
    QVector<float> packed;
    packed.reserve(verts.size() * 9);
    for (const auto& v : verts) {
        packed.append(v.position.x());
        packed.append(v.position.y());
        packed.append(v.position.z());
        packed.append(v.color.x());
        packed.append(v.color.y());
        packed.append(v.color.z());
        packed.append(v.normal.x());
        packed.append(v.normal.y());
        packed.append(v.normal.z());
    }

    data.indexCount = idx.size();
    data.vertexCount = verts.size();

    _glGenVertexArrays(1, &data.vao);
    glGenBuffers(1, &data.vbo);
    glGenBuffers(1, &data.ebo);

    _glBindVertexArray(data.vao);
    glBindBuffer(GL_ARRAY_BUFFER, data.vbo);
    glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(float), packed.constData(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint32_t), idx.constData(), GL_STATIC_DRAW);
    _glBindVertexArray(0);
}

void Viewport3DWidget::drawGrid()
{
    m_gridShader.bind();
    m_gridShader.setUniformValue("uView", viewMatrix());
    m_gridShader.setUniformValue("uProj", projectionMatrix());

    _glBindVertexArray(m_gridVAO);

    glDepthMask(GL_FALSE);
    glLineWidth(1.0f);

    for (int i = 0; i < m_gridVertexCount; i += 2) {
        m_gridShader.setUniformValue("uColor", QVector3D(0.35f, 0.35f, 0.38f));
        glDrawArrays(GL_LINES, i, 2);
    }

    glLineWidth(2.0f);
    m_gridShader.setUniformValue("uColor", QVector3D(0.5f, 0.5f, 0.55f));
    glDrawArrays(GL_LINES, 0, 2);
    glDrawArrays(GL_LINES, m_gridVertexCount / 2, 2);

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    _glBindVertexArray(0);
    m_gridShader.release();
}

void Viewport3DWidget::drawAxes()
{
    m_gridShader.bind();
    m_gridShader.setUniformValue("uView", viewMatrix());
    m_gridShader.setUniformValue("uProj", projectionMatrix());

    _glBindVertexArray(m_axesVAO);
    glLineWidth(2.0f);

    for (int i = 0; i < m_axesVertexCount; ++i) {
        float alpha = 1.0f;
        m_gridShader.setUniformValue("uColor", QVector3D(1.0f, 1.0f, 1.0f));
        glDrawArrays(GL_LINES, i, 1);
    }

    glLineWidth(1.0f);
    _glBindVertexArray(0);
    m_gridShader.release();
}

void Viewport3DWidget::drawSceneMeshes()
{
    m_meshShader.bind();
    m_meshShader.setUniformValue("uView", viewMatrix());
    m_meshShader.setUniformValue("uProj", projectionMatrix());
    m_meshShader.setUniformValue("uWireframe", m_renderMode == Wireframe);
    m_meshShader.setUniformValue("uLightDir", QVector3D(0.5f, 1.0f, 0.5f).normalized());
    m_meshShader.setUniformValue("uAmbient", QVector3D(0.25f, 0.25f, 0.30f));
    m_meshShader.setUniformValue("uSunIntensity", 1.0f);
    m_meshShader.setUniformValue("uAmbientIntensity", 0.3f);

    bool wireframe = (m_renderMode == Wireframe);
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    for (auto it = m_meshGLData.cbegin(); it != m_meshGLData.cend(); ++it) {
        m_meshShader.setUniformValue("uModel", it.value().transform);
        _glBindVertexArray(it.value().vao);
        glDrawElements(GL_TRIANGLES, it.value().indexCount, GL_UNSIGNED_INT, nullptr);
    }

    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    _glBindVertexArray(0);
    m_meshShader.release();
}

QMatrix4x4 Viewport3DWidget::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(m_camPos, m_camTarget, QVector3D(0, 1, 0));
    return view;
}

QMatrix4x4 Viewport3DWidget::projectionMatrix() const
{
    QMatrix4x4 proj;
    float aspect = static_cast<float>(width()) / static_cast<float>(height());
    proj.perspective(50.0f, aspect, 0.1f, 1000.0f);
    return proj;
}

void Viewport3DWidget::mousePressEvent(QMouseEvent* event)
{
    // Check gizmo first
    if (m_selectedObject && m_gizmoMode != GizmoMode::None) {
        handleGizmoInteraction(event);
        if (m_gizmoDragging) {
            event->accept();
            return;
        }
    }

    m_mousePressed = true;
    m_mouseButton = event->button();
    m_lastMousePos = event->pos();

    // Object picking on left click
    if (event->button() == Qt::LeftButton && !m_gizmoDragging) {
        SceneObject* picked = pickObject(event->pos());
        if (picked) {
            m_selectedObject = picked;
            emit objectSelected(picked);
        }
    }

    event->accept();
}

void Viewport3DWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_gizmoDragging) {
        handleGizmoInteraction(event);
        event->accept();
        return;
    }

    if (!m_mousePressed) return;

    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (m_mouseButton == Qt::LeftButton) {
        m_azimuth -= delta.x() * 0.5f;
        m_elevation += delta.y() * 0.5f;
        m_elevation = qBound(-89.0f, m_elevation, 89.0f);
        updateCameraFromMode();
    } else if (m_mouseButton == Qt::MiddleButton || (m_mouseButton == Qt::LeftButton && event->modifiers() & Qt::ShiftModifier)) {
        float panSpeed = m_camDistance * 0.002f;
        QMatrix4x4 view = viewMatrix();
        QVector3D right = view.inverted().map(QVector3D(1, 0, 0)).normalized();
        QVector3D up = view.inverted().map(QVector3D(0, 1, 0)).normalized();
        m_camTarget += right * (-delta.x() * panSpeed);
        m_camTarget += up * (delta.y() * panSpeed);
        updateCameraFromMode();
    }

    emit cameraMoved();
    update();
}

void Viewport3DWidget::mouseReleaseEvent(QMouseEvent* event)
{
    m_mousePressed = false;
    m_mouseButton = Qt::NoButton;
    event->accept();
}

void Viewport3DWidget::wheelEvent(QWheelEvent* event)
{
    float delta = -event->angleDelta().y() * 0.01f;
    m_camDistance += delta;
    m_camDistance = qMax(1.0f, m_camDistance);
    updateCameraFromMode();
    emit cameraMoved();
    update();
}

void Viewport3DWidget::updateCameraFromMode()
{
    float azRad = m_azimuth * M_PI / 180.0f;
    float elRad = m_elevation * M_PI / 180.0f;

    m_camPos.setX(m_camTarget.x() + m_camDistance * cos(elRad) * cos(azRad));
    m_camPos.setY(m_camTarget.y() + m_camDistance * sin(elRad));
    m_camPos.setZ(m_camTarget.z() + m_camDistance * cos(elRad) * sin(azRad));
}

void Viewport3DWidget::setCameraMode(CameraMode mode)
{
    m_cameraMode = mode;
    switch (mode) {
        case Top:
            m_azimuth = 0; m_elevation = 90; break;
        case Front:
            m_azimuth = 0; m_elevation = 0; break;
        case Right:
            m_azimuth = 90; m_elevation = 0; break;
        case Perspective:
        default:
            m_azimuth = -45; m_elevation = 25; break;
    }
    m_camDistance = 10.0f;
    updateCameraFromMode();
    update();
}

void Viewport3DWidget::setRenderMode(RenderMode mode)
{
    m_renderMode = mode;
    update();
}

void Viewport3DWidget::setShowGrid(bool show)
{
    m_showGrid = show;
    update();
}

void Viewport3DWidget::setShowAxes(bool show)
{
    m_showAxes = show;
    update();
}

void Viewport3DWidget::resetCamera()
{
    m_camTarget = QVector3D(0, 0, 0);
    m_camDistance = 10.0f;
    m_azimuth = -45.0f;
    m_elevation = 25.0f;
    updateCameraFromMode();
    update();
}

void Viewport3DWidget::focusOnPoint(const QVector3D& point, float distance)
{
    m_camTarget = point;
    m_camDistance = distance;
    updateCameraFromMode();
    update();
}

void Viewport3DWidget::setCameraOrbit(float azimuthDeg, float elevationDeg, float distance)
{
    m_azimuth = azimuthDeg;
    m_elevation = qBound(-89.0f, elevationDeg, 89.0f);
    m_camDistance = qMax(1.0f, distance);
    updateCameraFromMode();
    update();
}

// ── Gizmo Rendering ──────────────────────────────────────────────────────

void Viewport3DWidget::setupGizmo()
{
    _glGenVertexArrays(1, &m_gizmoVAO);
    glGenBuffers(1, &m_gizmoVBO);
}

void Viewport3DWidget::drawGizmo()
{
    if (!m_selectedObject || m_gizmoMode == GizmoMode::None) return;

    QMatrix4x4 model = m_selectedObject->worldTransform();
    QVector3D pos(model(0,3), model(1,3), model(2,3));

    m_gizmoShader.bind();
    m_gizmoShader.setUniformValue("uView", viewMatrix());
    m_gizmoShader.setUniformValue("uProj", projectionMatrix());

    float gizmoScale = m_camDistance * 0.15f;

    _glBindVertexArray(m_gizmoVAO);
    glLineWidth(3.0f);
    glDisable(GL_DEPTH_TEST);

    if (m_gizmoMode == GizmoMode::Translate) {
        QVector3D axes[3] = { {gizmoScale,0,0}, {0,gizmoScale,0}, {0,0,gizmoScale} };
        QVector3D colors[3] = { {1,0.2f,0.2f}, {0.2f,1,0.2f}, {0.2f,0.2f,1} };

        for (int i = 0; i < 3; ++i) {
            QVector3D end = pos + axes[i];
            float verts[] = { pos.x(), pos.y(), pos.z(), colors[i].x(), colors[i].y(), colors[i].z(),
                              end.x(), end.y(), end.z(), colors[i].x(), colors[i].y(), colors[i].z() };
            glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3*sizeof(float)));
            glEnableVertexAttribArray(1);
            m_gizmoShader.setUniformValue("uModel", QMatrix4x4());
            glDrawArrays(GL_LINES, 0, 2);
        }
    } else if (m_gizmoMode == GizmoMode::Rotate) {
        const int segments = 48;
        QVector3D colors[3] = { {1,0.2f,0.2f}, {0.2f,1,0.2f}, {0.2f,0.2f,1} };

        for (int axis = 0; axis < 3; ++axis) {
            QVector<float> circleVerts;
            for (int i = 0; i < segments; ++i) {
                float a1 = (float)i / segments * 2.0f * M_PI;
                float a2 = (float)(i + 1) / segments * 2.0f * M_PI;
                float c1 = qCos(a1) * gizmoScale, s1 = qSin(a1) * gizmoScale;
                float c2 = qCos(a2) * gizmoScale, s2 = qSin(a2) * gizmoScale;

                QVector3D p1, p2;
                if (axis == 0) { p1 = pos + QVector3D(0, c1, s1); p2 = pos + QVector3D(0, c2, s2); }
                else if (axis == 1) { p1 = pos + QVector3D(c1, 0, s1); p2 = pos + QVector3D(c2, 0, s2); }
                else { p1 = pos + QVector3D(c1, s1, 0); p2 = pos + QVector3D(c2, s2, 0); }

                circleVerts.append(p1.x()); circleVerts.append(p1.y()); circleVerts.append(p1.z());
                circleVerts.append(colors[axis].x()); circleVerts.append(colors[axis].y()); circleVerts.append(colors[axis].z());
                circleVerts.append(p2.x()); circleVerts.append(p2.y()); circleVerts.append(p2.z());
                circleVerts.append(colors[axis].x()); circleVerts.append(colors[axis].y()); circleVerts.append(colors[axis].z());
            }

            glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVBO);
            glBufferData(GL_ARRAY_BUFFER, circleVerts.size() * sizeof(float), circleVerts.constData(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3*sizeof(float)));
            glEnableVertexAttribArray(1);
            m_gizmoShader.setUniformValue("uModel", QMatrix4x4());
            glDrawArrays(GL_LINES, 0, segments * 2);
        }
    } else if (m_gizmoMode == GizmoMode::Scale) {
        QVector3D axes[3] = { {gizmoScale,0,0}, {0,gizmoScale,0}, {0,0,gizmoScale} };
        QVector3D colors[3] = { {1,0.2f,0.2f}, {0.2f,1,0.2f}, {0.2f,0.2f,1} };

        for (int i = 0; i < 3; ++i) {
            QVector3D end = pos + axes[i];
            float verts[] = { pos.x(), pos.y(), pos.z(), colors[i].x(), colors[i].y(), colors[i].z(),
                              end.x(), end.y(), end.z(), colors[i].x(), colors[i].y(), colors[i].z() };
            glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3*sizeof(float)));
            glEnableVertexAttribArray(1);
            m_gizmoShader.setUniformValue("uModel", QMatrix4x4());
            glDrawArrays(GL_LINES, 0, 2);
        }
    }

    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    _glBindVertexArray(0);
    m_gizmoShader.release();
}

void Viewport3DWidget::handleGizmoInteraction(QMouseEvent* event)
{
    if (!m_selectedObject || m_gizmoMode == GizmoMode::None) return;

    QVector3D rayOrigin, rayDir;
    screenToWorldRay(event->pos(), rayOrigin, rayDir);

    QVector3D objPos = m_selectedObject->translation();
    float gizmoScale = m_camDistance * 0.15f;

    if (!m_gizmoDragging) {
        float bestDist = gizmoScale * gizmoScale * 4.0f;
        GizmoAxis bestAxis = GizmoAxis::None;

        QVector3D axes[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
        GizmoAxis axisEnums[3] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };

        for (int i = 0; i < 3; ++i) {
            QVector3D axisEnd = objPos + axes[i] * gizmoScale;
            float t = QVector3D::dotProduct(objPos - rayOrigin, axes[i]);
            QVector3D closest = rayOrigin + rayDir * t;
            float dist = (closest - objPos).lengthSquared();
            if (dist < bestDist) {
                bestDist = dist;
                bestAxis = axisEnums[i];
            }
        }

        if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton && bestAxis != GizmoAxis::None) {
            m_gizmoDragging = true;
            m_activeAxis = bestAxis;
            m_gizmoStartPos = event->pos();
            m_gizmoStartObjectPos = m_selectedObject->translation();
            m_gizmoStartObjectScale = m_selectedObject->scale();
        }
        m_activeAxis = bestAxis;
    } else if (event->type() == QEvent::MouseMove) {
        QPoint delta = event->pos() - m_gizmoStartPos;
        QVector3D axisDir;
        if (m_activeAxis == GizmoAxis::X) axisDir = QVector3D(1, 0, 0);
        else if (m_activeAxis == GizmoAxis::Y) axisDir = QVector3D(0, 1, 0);
        else if (m_activeAxis == GizmoAxis::Z) axisDir = QVector3D(0, 0, 1);

        QMatrix4x4 view = viewMatrix();
        QVector3D right = view.inverted().map(QVector3D(1, 0, 0)).normalized();
        QVector3D up = view.inverted().map(QVector3D(0, 1, 0)).normalized();
        float screenFactor = QVector3D::dotProduct(axisDir, right) * delta.x() +
                             QVector3D::dotProduct(axisDir, up) * (-delta.y());
        screenFactor *= m_camDistance * 0.002f;

        if (m_gizmoMode == GizmoMode::Translate) {
            m_selectedObject->setTranslation(m_gizmoStartObjectPos + axisDir * screenFactor);
        } else if (m_gizmoMode == GizmoMode::Scale) {
            QVector3D scale = m_gizmoStartObjectScale;
            if (m_activeAxis == GizmoAxis::X) scale.setX(scale.x() + screenFactor * 0.1f);
            else if (m_activeAxis == GizmoAxis::Y) scale.setY(scale.y() + screenFactor * 0.1f);
            else if (m_activeAxis == GizmoAxis::Z) scale.setZ(scale.z() + screenFactor * 0.1f);
            m_selectedObject->setScale(scale);
        }
        update();
    } else if (event->type() == QEvent::MouseButtonRelease && event->button() == Qt::LeftButton) {
        m_gizmoDragging = false;
        m_activeAxis = GizmoAxis::None;
        emit objectTransformed(m_selectedObject);
    }
}

// ── Object Picking ───────────────────────────────────────────────────────

void Viewport3DWidget::setSelectedObject(SceneObject* obj)
{
    m_selectedObject = obj;
    update();
}

QVector3D Viewport3DWidget::screenToWorldRay(const QPoint& screenPos, QVector3D& rayOrigin, QVector3D& rayDir) const
{
    float x = (2.0f * screenPos.x()) / width() - 1.0f;
    float y = 1.0f - (2.0f * screenPos.y()) / height();

    QMatrix4x4 invVP = (projectionMatrix() * viewMatrix()).inverted();

    QVector4D nearPoint = invVP * QVector4D(x, y, -1.0f, 1.0f);
    QVector4D farPoint = invVP * QVector4D(x, y, 1.0f, 1.0f);
    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    rayOrigin = nearPoint.toVector3D();
    rayDir = (farPoint - nearPoint).toVector3D().normalized();
    return rayOrigin;
}

SceneObject* Viewport3DWidget::pickObject(const QPoint& screenPos)
{
    QVector3D rayOrigin, rayDir;
    screenToWorldRay(screenPos, rayOrigin, rayDir);

    SceneObject* closest = nullptr;
    float closestDist = 1e30f;

    if (!m_sceneRoot) return nullptr;

    QVector<SceneObject*> stack = { m_sceneRoot };
    while (!stack.isEmpty()) {
        SceneObject* obj = stack.takeFirst();
        if (obj->type() == SceneObject::Type::Mesh && obj->hasMesh() && obj->isVisible()) {
            auto* mesh = obj->mesh();
            if (mesh) {
                QVector3D bmin = mesh->boundsMin();
                QVector3D bmax = mesh->boundsMax();

                QMatrix4x4 worldMat;
                SceneObject* p = obj;
                QVector<SceneObject*> chain;
                while (p) { chain.prepend(p); p = p->parent(); }
                for (SceneObject* c : chain) {
                    QMatrix4x4 qm = c->transform();
                    worldMat = worldMat * qm;
                }

                QVector3D worldMin = worldMat.map(bmin);
                QVector3D worldMax = worldMat.map(bmax);
                if (worldMin.x() > worldMax.x()) { float t = worldMin.x(); worldMin.setX(worldMax.x()); worldMax.setX(t); }
                if (worldMin.y() > worldMax.y()) { float t = worldMin.y(); worldMin.setY(worldMax.y()); worldMax.setY(t); }
                if (worldMin.z() > worldMax.z()) { float t = worldMin.z(); worldMin.setZ(worldMax.z()); worldMax.setZ(t); }

                float dist;
                if (rayIntersectsBounds(rayOrigin, rayDir, worldMin, worldMax, dist)) {
                    if (dist < closestDist) {
                        closestDist = dist;
                        closest = obj;
                    }
                }
            }
        }
        for (SceneObject* child : obj->children())
            stack.append(child);
    }
    return closest;
}

bool Viewport3DWidget::rayIntersectsBounds(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                            const QVector3D& min, const QVector3D& max, float& outDist) const
{
    QVector3D invDir(1.0f / rayDir.x(), 1.0f / rayDir.y(), 1.0f / rayDir.z());
    QVector3D tbot = QVector3D((min.x() - rayOrigin.x()) * invDir.x(),
                                (min.y() - rayOrigin.y()) * invDir.y(),
                                (min.z() - rayOrigin.z()) * invDir.z());
    QVector3D ttop = QVector3D((max.x() - rayOrigin.x()) * invDir.x(),
                                (max.y() - rayOrigin.y()) * invDir.y(),
                                (max.z() - rayOrigin.z()) * invDir.z());
    QVector3D tmin = QVector3D(qMin(tbot.x(), ttop.x()), qMin(tbot.y(), ttop.y()), qMin(tbot.z(), ttop.z()));
    QVector3D tmax = QVector3D(qMax(tbot.x(), ttop.x()), qMax(tbot.y(), ttop.y()), qMax(tbot.z(), ttop.z()));
    float tEnter = qMax(qMax(tmin.x(), tmin.y()), tmin.z());
    float tExit = qMin(qMin(tmax.x(), tmax.y()), tmax.z());
    if (tEnter > tExit || tExit < 0) return false;
    outDist = tEnter >= 0 ? tEnter : tExit;
    return true;
}

QVector3D Viewport3DWidget::projectToScreen(const QVector3D& worldPos) const
{
    QMatrix4x4 vp = projectionMatrix() * viewMatrix();
    QVector4D clip = vp * QVector4D(worldPos, 1.0f);
    if (clip.w() == 0) return QVector3D();
    clip /= clip.w();
    float x = (clip.x() * 0.5f + 0.5f) * width();
    float y = (1.0f - clip.y() * 0.5f) * height();
    return QVector3D(x, y, clip.z());
}

QVector3D Viewport3DWidget::unprojectFromScreen(const QPoint& screenPos, float depth) const
{
    float x = (2.0f * screenPos.x()) / width() - 1.0f;
    float y = 1.0f - (2.0f * screenPos.y()) / height();
    QMatrix4x4 invVP = (projectionMatrix() * viewMatrix()).inverted();
    QVector4D world = invVP * QVector4D(x, y, depth, 1.0f);
    world /= world.w();
    return world.toVector3D();
}

} // namespace ks
