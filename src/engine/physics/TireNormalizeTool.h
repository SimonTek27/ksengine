#pragma once
#include "KsExport.h"

/**
 * @file TireNormalizeTool.h
 * @brief Offline tire data normalization / validation (Milliken Ch.14).
 *
 * Not in sim hot path. Reports peak grip, cornering stiffness, load sensitivity,
 * optional D-scale normalization to a target μ at Fz0, and CSV pure-slip curves.
 */

#include "KsTireModel.h"

#include <string>
#include <vector>

namespace ks {
namespace physics {

class KSENGINE_API TireNormalizeTool {
public:
    struct Metrics {
        float fz0 = 4000.f;
        float peakFy = 0.f;       ///< |Fy| max at Fz0, pure lateral
        float peakFx = 0.f;       ///< |Fx| max at Fz0, pure long
        float muY = 0.f;          ///< peakFy / Fz0
        float muX = 0.f;          ///< peakFx / Fz0
        float alphaPeakDeg = 0.f; ///< slip angle at peak Fy
        float kappaPeak = 0.f;    ///< slip ratio at peak Fx
        float corneringStiff = 0.f; ///< N/rad near α=0
        float longStiff = 0.f;     ///< N per unit κ near 0
        float loadSensRatio = 1.f; ///< peakFy(1.5 Fz0) / (1.5 * peakFy(Fz0))
        bool valid = true;
        std::string message;
    };

    struct CurvePoint {
        float x = 0.f; // α rad or κ
        float force = 0.f;
    };

    TireNormalizeTool();
    explicit TireNormalizeTool(const KsTireModel::TireCoefficients& c);

    void setCoefficients(const KsTireModel::TireCoefficients& c);
    KsTireModel::TireCoefficients coefficients() const { return m_tires.getCoefficients(); }
    KsTireModel& model() { return m_tires; }

    bool loadIni(const std::string& path);
    void usePreset(const std::string& name); // slick|street|wet|rally

    Metrics evaluate(float fz0 = -1.f, float mu = 1.f) const;

    /** Scale longitudinal/lateral peak toward target μ at Fz0 (adjusts a2/b2 proxies via D floor path). */
    Metrics normalizeToMu(float targetMu, float fz0 = -1.f);

    std::vector<CurvePoint> lateralCurve(float fz, float maxAlphaDeg = 15.f, int points = 61) const;
    std::vector<CurvePoint> longitudinalCurve(float fz, float maxKappa = 0.3f, int points = 61) const;

    static bool writeCurveCsv(const std::string& path, const std::vector<CurvePoint>& lat,
                              const std::vector<CurvePoint>& lng);
    static bool writeReport(const std::string& path, const Metrics& m,
                            const KsTireModel::TireCoefficients& c);

private:
    KsTireModel m_tires;
};

} // namespace physics
} // namespace ks
