#pragma once

#include <QVector>
#include <QPointF>
#include <QString>
#include "engine/physics/PhysicsCoreTypes.h"

namespace ks {

using TireSlipCurve = ks::physics::TireSlipCurve;

// ─────────────────────────────────────────────────────────────────────────────
// phys_TireModel — legacy tire model (kept for backward compat with UI)
// ─────────────────────────────────────────────────────────────────────────────
class phys_TireModel {
public:
    phys_TireModel();

    void setSlipCurve(const TireSlipCurve& curve);
    TireSlipCurve slipCurve() const { return m_slipCurve; }

    double calculateLateralForce(double slipAngle, double normalLoad, double frictionCoeff);
    double calculateLongitudinalForce(double slipRatio, double normalLoad, double frictionCoeff);
    double calculateAligningTorque(double slipAngle, double normalLoad);

    double calculatePeakSlipAngle(double normalLoad) const;
    double calculatePeakSlipRatio(double normalLoad) const;

    void setPacejkaCoefficients(double b, double c, double d, double e);
    QVector<QPointF> generateLateralForceCurve(double normalLoad, double frictionCoeff);
    QVector<QPointF> generateLongitudinalForceCurve(double normalLoad, double frictionCoeff);

private:
    double pacejkaFormula(double x, double b, double c, double d, double e) const;

    double m_b, m_c, m_d, m_e;
    TireSlipCurve m_slipCurve;
};

} // namespace ks