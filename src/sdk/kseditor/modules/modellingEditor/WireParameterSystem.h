#pragma once

#include <QString>
#include <QVector>
#include <QVariant>

#include "sdk/kseditor/engine/Graphics/SceneObject.h"

namespace ks {

// Wire Parameters (3ds Max / Softimage): binds a scalar parameter of a driver
// object to a scalar parameter of a driven object, with an affine map
// `value = driver * scale + offset`, or — when `expression` is non-empty — a
// full math expression over the driver value `x` (see ExpressionEvaluator.h).
struct WireBinding {
    int driverId = -1;
    QString driverName;
    QString driverProp = QStringLiteral("position.x");
    int drivenId = -1;
    QString drivenName;
    QString drivenProp = QStringLiteral("position.y");
    float scale = 1.0f;
    float offset = 0.0f;
    bool enabled = true;
    QString expression;   // empty → legacy affine map

    QVariant toVariant() const;
    void fromVariant(const QVariant& v);
};

class WireParameterSystem
{
public:
    WireParameterSystem() = default;

    bool add(int driverId, const QString& driverName, const QString& driverProp,
             int drivenId, const QString& drivenName, const QString& drivenProp,
             float scale, float offset);
    // Bindings are keyed by driven objectId (a panel lists them per object);
    // `driverObjectId` lets the UI remove bindings that drive the selected object.
    bool remove(int drivenId, int index);
    bool setEnabled(int drivenId, int index, bool on);
    bool setParams(int drivenId, int index, float scale, float offset);
    bool setProperty(int drivenId, int index, const QString& drivenProp);
    // Sets a math expression over the driver value (`x`) for a binding.
    // Passing an empty string restores the legacy affine map (`scale`/`offset`).
    bool setExpression(int drivenId, int index, const QString& expression);
    void clearDriven(int drivenId);
    void clearDriver(int driverId);
    void clearAll();

    QVector<WireBinding> forObject(int objectId) const;        // bindings that drive `objectId`
    QVector<WireBinding> drivenBy(int objectId) const;         // bindings that read `objectId`
    bool hasAny() const { return !m_bindings.isEmpty(); }
    int count(int objectId) const { return m_bindings.value(objectId).size(); }
    QVector<int> controlledObjectIds() const { return m_bindings.keys(); }

    // Applies every enabled binding; returns the number of bindings applied.
    int evaluate(SceneGraph* graph);

private:
    void applyOne(const WireBinding& b, SceneGraph* graph, bool& changed);

    QMap<int, QVector<WireBinding>> m_bindings;    // keyed by drivenId
};

} // namespace ks