#pragma once
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QPushButton>
#include <QVector3D>
#include <QList>
#include <QMap>
#include "sdk/kseditor/engine/Graphics/SceneData.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "engine/sys/UndoStack.h"

namespace Ks {

enum class ColliderType { Box, Sphere, Capsule, Mesh, ConvexHull };

struct PhysicsCollider {
    ColliderType type = ColliderType::Box;
    QVector3D center;
    QVector3D halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float height = 1.0f;
    float friction = 0.7f;
    float restitution = 0.3f;
    bool isTrigger = false;
    QString surfaceType;
};

struct PhysicsBody {
    bool isStatic = true;
    float mass = 1.0f;
    float linearDamp = 0.0f;
    float angularDamp = 0.0f;
    QList<PhysicsCollider> colliders;
};
using ks::UndoStack;
using ks::SceneGraph;

class PhysicsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PhysicsPanel(UndoStack* undoStack, QWidget* parent = nullptr);

    void setScene(SceneGraph* scene);
    void setSelectedNode(const QString& nodeName);
    void refresh();

signals:
    void physicsChanged();

private slots:
    void onAddCollider();
    void onRemoveCollider();
    void onColliderSelected(int idx);
    void onColliderTypeChanged(int idx);
    void onBodyTypeChanged(int idx);
    void onParamChanged();
    void onSurfaceTypeChanged(const QString&);

private:
    void buildUI();
    void populateColliderList();
    void loadCollider(int idx);
    void saveCurrentCollider();

    SceneGraph*       m_scene     = nullptr;
    UndoStack*   m_undo;
    QString      m_nodeName;
    int          m_currentCollider = -1;
    QMap<QString, PhysicsBody> m_physicsBodies;
    bool         m_isDirty = false;

    QComboBox*       m_bodyType;
    QDoubleSpinBox*  m_mass;
    QDoubleSpinBox*  m_linDamp;
    QDoubleSpinBox*  m_angDamp;

    QListWidget*     m_colliderList;
    QPushButton*     m_addBtn;
    QPushButton*     m_removeBtn;

    QGroupBox*       m_colliderGroup;
    QComboBox*       m_colType;
    QDoubleSpinBox*  m_hx, *m_hy, *m_hz;
    QDoubleSpinBox*  m_radius;
    QDoubleSpinBox*  m_height;
    QDoubleSpinBox*  m_cx, *m_cy, *m_cz;
    QDoubleSpinBox*  m_friction;
    QDoubleSpinBox*  m_restitution;
    QCheckBox*       m_isTrigger;
    QComboBox*       m_surfaceType;

    static const QStringList kSurfaceTypes;
};

} // namespace Ks