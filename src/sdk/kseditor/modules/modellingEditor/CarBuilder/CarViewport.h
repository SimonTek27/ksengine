#pragma once

#include <QWidget>
#include <QVulkanWindow>
#include <QMatrix4x4>
#include <QVector3D>
#include <QMouseEvent>
#include <QWheelEvent>

#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "CarEditor.h"

namespace ks {

class CarViewport : public QWidget {
    Q_OBJECT
public:
    explicit CarViewport(QWidget* parent = nullptr);
    ~CarViewport() override;

    void setCarEditor(CarEditor* editor);

    void resetCamera();
    void focusOnCar();
    void setViewMode(const QString& mode);

signals:
    void partSelected(const QString& partId);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void updateCamera();

    CarEditor* m_carEditor = nullptr;
    VulkanViewportWidget* m_vulkanViewport = nullptr;

    float m_camYaw = 45.0f;
    float m_camPitch = 30.0f;
    float m_camDist = 10.0f;
    QVector3D m_camTarget = QVector3D(0, 0, 0);

    QPoint m_lastMouse;
    bool m_lmbDown = false;
    bool m_rmbDown = false;

    int m_selectedPartIndex = -1;
    QString m_viewMode = "perspective";
};

}