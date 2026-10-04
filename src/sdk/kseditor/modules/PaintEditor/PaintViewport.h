#pragma once

#include <QWidget>
#include <QImage>
#include <QVector3D>

#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "sdk/kseditor/engine/mesh/MeshRenderer.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "PaintEditorModule.h"

namespace ks {
namespace graphics {

class PaintViewport : public QWidget {
    Q_OBJECT
public:
    explicit PaintViewport(QWidget* parent = nullptr);
    ~PaintViewport() override;

    void setCarPath(const QString& path);
    void resetCamera();
    void focusOnModel();
    void setViewMode(const QString& mode);

    void applyPaintTexture(const QImage& texture);

signals:
    void partSelected(const QString& partId);

private:
    void convertToScene();
    void loadMesh(const QString& filePath);

    ks::PaintEditor* m_paintEditor;
    Viewport3DWidget* m_viewport = nullptr;
    ::MeshRenderer* m_meshRenderer = nullptr;
    SceneObject* m_sceneRoot = nullptr;

    QString m_carPath;
    QString m_viewMode = "perspective";
    QImage m_paintTexture;
};

} // namespace graphics
} // namespace ks