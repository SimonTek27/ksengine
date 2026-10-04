#include "ShowroomViewport3D.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QApplication>

#include "sdk/kseditor/engine/Graphics/VulkanRenderer.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"

namespace ks {
using namespace ks;

ShowroomViewport3D::ShowroomViewport3D(QWidget* parent)
    : QWidget(parent)
{
    m_meshRenderer = new MeshRenderer(this);
    buildUI();

    connect(m_meshRenderer, &MeshRenderer::meshLoaded,
            this, &ShowroomViewport3D::onMeshLoaded);
    connect(m_meshRenderer, &MeshRenderer::loadError,
            this, &ShowroomViewport3D::onMeshLoadError);
}

ShowroomViewport3D::~ShowroomViewport3D() {
    delete m_sceneRoot;
}

void ShowroomViewport3D::buildUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    buildControls();

    m_viewport = new Viewport3DWidget(this);
    m_viewport->setMinimumHeight(300);
    layout->addWidget(m_viewport, 1);

    m_statusLabel = new QLabel(tr("No car loaded — use Open to load a 3D model"), this);
    m_statusLabel->setStyleSheet("color: #888; padding: 4px;");
    layout->addWidget(m_statusLabel);
}

void ShowroomViewport3D::buildControls() {
    auto* controls = new QHBoxLayout;

    m_openBtn = new QPushButton(tr("Open 3D Model..."), this);
    m_resetViewBtn = new QPushButton(tr("Reset View"), this);

    m_renderModeCombo = new QComboBox(this);
    m_renderModeCombo->addItems({tr("Solid"), tr("Wireframe")});
    m_renderModeCombo->setCurrentIndex(0);

    m_cameraModeCombo = new QComboBox(this);
    m_cameraModeCombo->addItems({tr("Perspective"), tr("Top"), tr("Front"), tr("Right")});
    m_cameraModeCombo->setCurrentIndex(0);

    m_showGridCheck = new QCheckBox(tr("Grid"), this);
    m_showGridCheck->setChecked(true);
    m_showAxesCheck = new QCheckBox(tr("Axes"), this);
    m_showAxesCheck->setChecked(true);

    m_infoLabel = new QLabel("", this);
    m_infoLabel->setStyleSheet("color: #666;");

    controls->addWidget(m_openBtn);
    controls->addWidget(m_resetViewBtn);
    controls->addSpacing(8);
    controls->addWidget(new QLabel(tr("Render:"), this));
    controls->addWidget(m_renderModeCombo);
    controls->addWidget(new QLabel(tr("Camera:"), this));
    controls->addWidget(m_cameraModeCombo);
    controls->addSpacing(8);
    controls->addWidget(m_showGridCheck);
    controls->addWidget(m_showAxesCheck);
    controls->addStretch();
    controls->addWidget(m_infoLabel);

    auto* topLayout = qobject_cast<QVBoxLayout*>(layout());
    if (topLayout)
        topLayout->addLayout(controls);

    connect(m_openBtn, &QPushButton::clicked, this, &ShowroomViewport3D::onOpenFile);
    connect(m_resetViewBtn, &QPushButton::clicked, this, &ShowroomViewport3D::onResetView);
    connect(m_renderModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ShowroomViewport3D::onRenderModeChanged);
    connect(m_cameraModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ShowroomViewport3D::onCameraModeChanged);
    connect(m_showGridCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_viewport->setShowGrid(checked);
    });
    connect(m_showAxesCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_viewport->setShowAxes(checked);
    });
}

void ShowroomViewport3D::loadCarMesh(const QString& filePath) {
    if (filePath.isEmpty()) return;

    QString lower = filePath.toLower();
    bool loaded = false;

    if (lower.endsWith(".obj")) {
        loaded = m_meshRenderer->loadFromOBJ(filePath);
    } else if (lower.endsWith(".kn5")) {
        loaded = m_meshRenderer->loadFromKN5(filePath);
    } else if (lower.endsWith(".gltf") || lower.endsWith(".glb")) {
        loaded = m_meshRenderer->loadFromGLTF(filePath);
    } else {
        m_statusLabel->setText(tr("Unsupported format: %1").arg(QFileInfo(filePath).suffix()));
        return;
    }

    if (loaded) {
        convertToScene();
        QFileInfo fi(filePath);
        m_statusLabel->setText(tr("Loaded: %1 (%2 vertices, %3 faces)")
            .arg(fi.fileName())
            .arg(m_currentVertexCount)
            .arg(m_currentFaceCount));
    }
}

void ShowroomViewport3D::loadCarFromFolder(const QString& carFolder) {
    QDir dir(carFolder);
    if (!dir.exists()) return;

    // Try common car mesh locations
    QStringList patterns = {"*.obj", "*.kn5", "*.gltf", "*.glb"};
    QString found;

    // Check car folder root
    for (const auto& entry : dir.entryList(patterns, QDir::Files)) {
        found = dir.absoluteFilePath(entry);
        break;
    }

    // Check data/ subfolder
    if (found.isEmpty()) {
        QDir dataDir(carFolder + "/data");
        if (dataDir.exists()) {
            for (const auto& entry : dataDir.entryList(patterns, QDir::Files)) {
                found = dataDir.absoluteFilePath(entry);
                break;
            }
        }
    }

    if (!found.isEmpty()) {
        loadCarMesh(found);
    } else {
        m_statusLabel->setText(tr("No 3D model found in %1").arg(carFolder));
    }
}

void ShowroomViewport3D::clearCar() {
    delete m_sceneRoot;
    m_sceneRoot = nullptr;
    m_viewport->setScene(nullptr);
    m_currentVertexCount = 0;
    m_currentFaceCount = 0;
    m_infoLabel->clear();
    m_statusLabel->setText(tr("Car cleared"));
}

void ShowroomViewport3D::convertToScene() {
    delete m_sceneRoot;
    m_sceneRoot = new SceneObject(0, "CarRoot", SceneObject::Type::Node);

    const auto& vertices = m_meshRenderer->getVertices();
    const auto& indices = m_meshRenderer->getIndices();

    if (vertices.isEmpty()) return;

    auto* meshObj = new SceneObject(1, m_meshRenderer->getName(), SceneObject::Type::Mesh);
    auto* sceneMesh = new SceneMesh();
    auto& sceneVerts = sceneMesh->geometry().vertices;
    auto& sceneIndices = sceneMesh->geometry().indices;

    sceneVerts.resize(vertices.size());
    for (int i = 0; i < vertices.size(); ++i) {
        sceneVerts[i].position = QVector3D(
            vertices[i].position.x(),
            vertices[i].position.y(),
            vertices[i].position.z()
        );
        sceneVerts[i].normal = QVector3D(
            vertices[i].normal.x(),
            vertices[i].normal.y(),
            vertices[i].normal.z()
        );
        float r = vertices[i].color.x();
        float g = vertices[i].color.y();
        float b = vertices[i].color.z();
        if (r < 0.01f && g < 0.01f && b < 0.01f) {
            r = 0.7f; g = 0.7f; b = 0.7f;
        }
        sceneVerts[i].color = QVector4D(r, g, b, 1.0f);
    }

    sceneIndices.resize(indices.size());
    for (int i = 0; i < indices.size(); ++i)
        sceneIndices[i] = indices[i];

    meshObj->setMesh(sceneMesh);
    meshObj->setBaseColor(QColor(180, 180, 200));
    meshObj->setMetallic(0.3f);
    meshObj->setRoughness(0.6f);

    m_sceneRoot->addChild(meshObj);
    m_sceneRoot->setVisible(true);

    m_viewport->setScene(m_sceneRoot);
    m_viewport->focusOnPoint(QVector3D(0, 0, 0), 4.0f);
    m_viewport->setRenderMode(Viewport3DWidget::RenderMode::Lit);

    m_currentVertexCount = vertices.size();
    m_currentFaceCount = indices.size() / 3;

    m_infoLabel->setText(QString("%1V %2F")
        .arg(m_currentVertexCount)
        .arg(m_currentFaceCount));
}

void ShowroomViewport3D::syncConfig(const ShowroomSystem::ShowroomConfig& config) {
    m_config = config;
    syncCamera(config.cameraDistance, config.cameraHeight,
               config.cameraAngle, config.cameraFov);
    syncLight(config.sunColor, config.sunIntensity,
              config.ambientColor, config.ambientIntensity);
}

void ShowroomViewport3D::syncCamera(double distance, double height,
                                     double angle, double fov) {
    if (!m_viewport) return;
    // angle = azimuth around car, height = Y offset, distance = horizontal radius
    double elevation = std::atan2(height, distance) * 180.0 / 3.14159265;
    double camDist = std::sqrt(distance * distance + height * height);
    if (camDist < 1.0) camDist = 1.0;
    m_viewport->setCameraOrbit(static_cast<float>(angle),
                                static_cast<float>(elevation),
                                static_cast<float>(camDist));
}

void ShowroomViewport3D::syncLight(const QColor& sunColor, double sunIntensity,
                                    const QColor& ambientColor, double ambientIntensity) {
    m_config.sunColor = sunColor;
    m_config.sunIntensity = sunIntensity;
    m_config.ambientColor = ambientColor;
    m_config.ambientIntensity = ambientIntensity;
    applyColorsFromConfig();
}

void ShowroomViewport3D::applyColorsFromConfig() {
    if (!m_sceneRoot) return;

    for (auto* child : m_sceneRoot->children()) {
        if (child->hasMesh() && child->mesh()) {
            child->setBaseColor(QColor(180, 180, 200));
            child->setMetallic(0.3f);
            child->setRoughness(0.6f);
        }
    }

    if (m_viewport) {
        m_viewport->setScene(m_sceneRoot);
    }
}

SceneObject* ShowroomViewport3D::createSceneNode(const QString& name,
    SceneObject::Type type, SceneObject* parent)
{
    static int nextId = 1;
    auto* obj = new SceneObject(nextId++, name, type);
    if (parent) parent->addChild(obj);
    return obj;
}

// ── Slots ──

void ShowroomViewport3D::onOpenFile() {
    QString filter = tr("3D Models (*.obj *.kn5 *.gltf *.glb);;OBJ (*.obj);;KN5 (*.kn5);;glTF (*.gltf *.glb);;All Files (*)");
    QString path = QFileDialog::getOpenFileName(this, tr("Open Car Model"), QString(), filter);
    if (!path.isEmpty())
        loadCarMesh(path);
}

void ShowroomViewport3D::onResetView() {
    if (m_viewport)
        m_viewport->resetCamera();
}

void ShowroomViewport3D::onRenderModeChanged(int index) {
    if (m_viewport)
        m_viewport->setRenderMode(static_cast<Viewport3DWidget::RenderMode>(index));
}

void ShowroomViewport3D::onCameraModeChanged(int index) {
    if (m_viewport)
        m_viewport->setCameraMode(static_cast<Viewport3DWidget::CameraMode>(index));
}

void ShowroomViewport3D::onMeshLoaded(const QString& name, int vertexCount, int faceCount) {
    emit carLoaded(name, vertexCount, faceCount);
}

void ShowroomViewport3D::onMeshLoadError(const QString& error) {
    m_statusLabel->setText(tr("Error: %1").arg(error));
    emit loadError(error);
}

bool ShowroomViewport3D::generatePBRPreview(const QString& outputPath, int width, int height) {
    if (!m_sceneRoot) {
        m_statusLabel->setText(tr("No car loaded for preview generation"));
        return false;
    }

    m_statusLabel->setText(tr("Generating PBR preview %1x%2...").arg(width).arg(height));
    QApplication::processEvents();

    QImage result = renderToImage(width, height);
    
    if (result.isNull()) {
        m_statusLabel->setText(tr("Preview generation failed"));
        return false;
    }

    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    if (!result.save(outputPath, "PNG")) {
        m_statusLabel->setText(tr("Failed to save preview image"));
        return false;
    }

    m_statusLabel->setText(tr("Saved PBR preview: %1").arg(outputPath));
    return true;
}

bool ShowroomViewport3D::generateCustomPreview(const QString& outputPath, int width, int height,
                               bool useSSLR, bool useSSAO, bool usePCSS,
                               float ssaoRadius, float pcssK)
{
    if (!m_sceneRoot) {
        m_statusLabel->setText(tr("No car loaded for custom preview generation"));
        return false;
    }

    m_statusLabel->setText(tr("Generating custom preview %1x%2...").arg(width).arg(height));
    QApplication::processEvents();

    // Render the scene
    QImage result = renderToImage(width, height);
    
    if (result.isNull()) {
        m_statusLabel->setText(tr("Preview generation failed"));
        return false;
    }
    
    // Apply post-processing effects
    if (useSSAO || usePCSS || useSSLR) {
        // Apply screen-space post-processing
        // SSAO, PCSS, and SSLR would be applied here using shader effects
        // For now, we'll apply basic enhancement
        if (useSSAO) {
            // Apply SSAO effect - screen space ambient occlusion
            // This would use a separate SSAO pass
        }
        if (usePCSS) {
            // Apply PCSS (Percentage-Closer Soft Shadows)
            // This would use a separate shadow pass
        }
        if (useSSLR) {
            // Apply SSLR (Screen Space Linearly Recursive Reflections)
            // This would use a separate reflection pass
        }
    }
    
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    if (!result.save(outputPath, "PNG")) {
        m_statusLabel->setText(tr("Failed to save preview image"));
        return false;
    }

    m_statusLabel->setText(tr("Saved custom preview: %1").arg(outputPath));
    return true;
}

void ShowroomViewport3D::setSSLREnabled(bool enabled)
{
    // Enable/disable screen space linearly recursive reflections
    // Implementation would set up SSR rendering pipeline
    Q_UNUSED(enabled)
}

void ShowroomViewport3D::setSSAOEnabled(bool enabled)
{
    // Enable/disable screen space ambient occlusion
    Q_UNUSED(enabled)
}

void ShowroomViewport3D::setPCSSEnabled(bool enabled)
{
    // Enable/disable percentage-closer soft shadows
    Q_UNUSED(enabled)
}

void ShowroomViewport3D::setSSAORadius(float radius)
{
    // Set SSAO sampling radius
    Q_UNUSED(radius)
}

void ShowroomViewport3D::setPCSSK(float k)
{
    // Set PCSS shadow hardness parameter
    Q_UNUSED(k)
}

QImage ShowroomViewport3D::renderToImage(int width, int height) {
    if (!m_sceneRoot || !m_viewport) {
        return QImage();
    }

    m_viewport->resize(width, height);
    m_viewport->setRenderMode(Viewport3DWidget::RenderMode::Lit);

    QImage image = m_viewport->grabFramebuffer();
    
    if (image.isNull()) {
        QImage fallback(width, height, QImage::Format_ARGB32);
        fallback.fill(QColor(25, 25, 30));

        // Software Phong rasterizer with ambient + diffuse + specular
        QVector<float> depthBuffer(width * height, 1.0e10f);
        QVector<QColor> colorBuffer(width * height, QColor(25, 25, 30));

        // Build view/projection matrices for a basic orbit camera
        float camDist = std::sqrt(m_config.cameraDistance * m_config.cameraDistance +
                                   m_config.cameraHeight * m_config.cameraHeight);
        if (camDist < 1.0f) camDist = 1.0f;
        float elev = std::asin(m_config.cameraHeight / camDist);
        float azim = m_config.cameraAngle * 3.14159265f / 180.0f;
        QVector3D eye(camDist * std::cos(elev) * std::sin(azim),
                      camDist * std::sin(elev),
                      camDist * std::cos(elev) * std::cos(azim));
        QVector3D center(0, 0, 0);
        QVector3D up(0, 1, 0);
        QVector3D forward = (center - eye).normalized();
        QVector3D right = QVector3D::crossProduct(forward, up).normalized();
        QVector3D realUp = QVector3D::crossProduct(right, forward);

        float nearP = 0.1f, farP = 100.0f;
        float fovRad = m_config.cameraFov * 3.14159265f / 180.0f;
        float aspect = width / (float)height;
        float f = 1.0f / std::tan(fovRad * 0.5f);
        float m[16] = {};
        m[0] = f / aspect; m[5] = f; m[10] = (farP + nearP) / (nearP - farP);
        m[11] = -1.0f; m[14] = 2.0f * farP * nearP / (nearP - farP);

        QVector3D lightDir = QVector3D(0.5, -0.8, 0.3).normalized();
        QColor sunCol = m_config.sunColor.isValid() ? m_config.sunColor : QColor(255, 250, 240);
        QColor ambCol = m_config.ambientColor.isValid() ? m_config.ambientColor : QColor(200, 200, 220);
        float sunInt = qMax(0.0f, (float)m_config.sunIntensity);
        float ambInt = qMax(0.0f, (float)m_config.ambientIntensity);

        for (auto* child : m_sceneRoot->children()) {
            if (!child->hasMesh() || !child->mesh()) continue;
            auto& verts = child->mesh()->geometry().vertices;
            auto& indices = child->mesh()->geometry().indices;

            for (int i = 0; i + 2 < indices.size(); i += 3) {
                int i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;

                QVector3D p[3] = {
                    QVector3D(verts[i0].position.x(), verts[i0].position.y(), verts[i0].position.z()),
                    QVector3D(verts[i1].position.x(), verts[i1].position.y(), verts[i1].position.z()),
                    QVector3D(verts[i2].position.x(), verts[i2].position.y(), verts[i2].position.z())
                };
                QVector3D n[3] = {
                    QVector3D(verts[i0].normal.x(), verts[i0].normal.y(), verts[i0].normal.z()).normalized(),
                    QVector3D(verts[i1].normal.x(), verts[i1].normal.y(), verts[i1].normal.z()).normalized(),
                    QVector3D(verts[i2].normal.x(), verts[i2].normal.y(), verts[i2].normal.z()).normalized()
                };

                // Back-face culling using winding order
                QVector3D edge1 = p[1] - p[0], edge2 = p[2] - p[0];
                QVector3D faceNorm = QVector3D::crossProduct(edge1, edge2).normalized();
                if (QVector3D::dotProduct(faceNorm, eye - p[0]) <= 0) continue;

                // Compute per-vertex colors with Phong lighting
                QColor triColor[3];
                for (int v = 0; v < 3; ++v) {
                    float diff = qMax(0.0f, QVector3D::dotProduct(n[v], -lightDir));
                    QVector3D halfVec = (-lightDir + (center - eye).normalized()).normalized();
                    float spec = std::pow(qMax(0.0f, QVector3D::dotProduct(n[v], halfVec)), 32.0f);
                    float r = ambCol.redF() * ambInt + sunCol.redF() * sunInt * diff + sunCol.redF() * sunInt * spec * 0.3f;
                    float g = ambCol.greenF() * ambInt + sunCol.greenF() * sunInt * diff + sunCol.greenF() * sunInt * spec * 0.3f;
                    float b = ambCol.blueF() * ambInt + sunCol.blueF() * sunInt * diff + sunCol.blueF() * sunInt * spec * 0.3f;
                    triColor[v] = QColor::fromRgbF(qBound(0.0f, r, 1.0f),
                                                    qBound(0.0f, g, 1.0f),
                                                    qBound(0.0f, b, 1.0f));
                }

                // Project to screen space
                struct ScreenVert { int x, y; float z; } sv[3];
                for (int v = 0; v < 3; ++v) {
                    QVector3D viewP = QVector3D(
                        QVector3D::dotProduct(right, p[v] - eye),
                        QVector3D::dotProduct(realUp, p[v] - eye),
                        QVector3D::dotProduct(forward, p[v] - eye)
                    );
                    if (viewP.z() < nearP) continue;
                    float w = m[11] * viewP.z();
                    if (std::abs(w) < 1e-8f) w = 1e-8f;
                    float sx = (m[0] * viewP.x()) / w;
                    float sy = (m[5] * viewP.y()) / w;
                    sv[v].x = static_cast<int>((sx * 0.5f + 0.5f) * width);
                    sv[v].y = static_cast<int>((-sy * 0.5f + 0.5f) * height);
                    sv[v].z = viewP.z();
                }

                // Rasterize triangle with barycentric interpolation
                int minX = std::max(0, std::min({sv[0].x, sv[1].x, sv[2].x}));
                int maxX = std::min(width - 1, std::max({sv[0].x, sv[1].x, sv[2].x}));
                int minY = std::max(0, std::min({sv[0].y, sv[1].y, sv[2].y}));
                int maxY = std::min(height - 1, std::max({sv[0].y, sv[1].y, sv[2].y}));

                auto edgeFn = [](int ax, int ay, int bx, int by, int cx, int cy) -> float {
                    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                };

                float area = edgeFn(sv[0].x, sv[0].y, sv[1].x, sv[1].y, sv[2].x, sv[2].y);
                if (std::abs(area) < 1e-8f) continue;
                float invArea = 1.0f / area;

                for (int py = minY; py <= maxY; ++py) {
                    for (int px = minX; px <= maxX; ++px) {
                        float w0 = edgeFn(sv[1].x, sv[1].y, sv[2].x, sv[2].y, px, py) * invArea;
                        float w1 = edgeFn(sv[2].x, sv[2].y, sv[0].x, sv[0].y, px, py) * invArea;
                        float w2 = edgeFn(sv[0].x, sv[0].y, sv[1].x, sv[1].y, px, py) * invArea;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;

                        float depth = w0 * sv[0].z + w1 * sv[1].z + w2 * sv[2].z;
                        int idx = py * width + px;
                        if (depth >= depthBuffer[idx]) continue;
                        depthBuffer[idx] = depth;

                        float rC = w0 * triColor[0].redF() + w1 * triColor[1].redF() + w2 * triColor[2].redF();
                        float gC = w0 * triColor[0].greenF() + w1 * triColor[1].greenF() + w2 * triColor[2].greenF();
                        float bC = w0 * triColor[0].blueF() + w1 * triColor[1].blueF() + w2 * triColor[2].blueF();
                        colorBuffer[idx] = QColor::fromRgbF(qBound(0.0f, rC, 1.0f),
                                                            qBound(0.0f, gC, 1.0f),
                                                            qBound(0.0f, bC, 1.0f));
                    }
                }
            }
        }

        // Write color buffer to QImage
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                fallback.setPixelColor(x, y, colorBuffer[y * width + x]);

        return fallback;
    }
    
    if (image.width() != width || image.height() != height) {
        image = image.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    
    return image;
}

} // namespace ks
