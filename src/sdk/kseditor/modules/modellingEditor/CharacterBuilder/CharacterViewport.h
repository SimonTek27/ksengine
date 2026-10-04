#pragma once

#include <QWidget>
#include <QVulkanWindow>
#include <QMatrix4x4>
#include <QVector3D>
#include <QMouseEvent>
#include <QWheelEvent>

#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "CharacterEditor.h"

namespace ks {

class CharacterViewport : public QWidget {
    Q_OBJECT
public:
    explicit CharacterViewport(QWidget* parent = nullptr);
    ~CharacterViewport() override;

    void setCharacterEditor(CharacterEditor* editor);

    void resetCamera();
    void focusOnCharacter();
    void setViewMode(const QString& mode);

    void setDisplayMode(const QString& mode);
    QString displayMode() const { return m_displayMode; }

signals:
    void boneSelected(int index);
    void viewportChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void updateCamera();

    CharacterEditor* m_characterEditor = nullptr;
    VulkanViewportWidget* m_vulkanViewport = nullptr;

    float m_camYaw = 45.0f;
    float m_camPitch = 30.0f;
    float m_camDist = 10.0f;
    QVector3D m_camTarget = QVector3D(0, 0.9f, 0);

    QPoint m_lastMouse;
    bool m_lmbDown = false;
    bool m_rmbDown = false;
    bool m_mmbDown = false;

    int m_selectedBone = -1;
    QString m_displayMode = "skeleton";
};

}