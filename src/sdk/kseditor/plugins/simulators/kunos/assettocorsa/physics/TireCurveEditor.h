#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QCheckBox>
#include <QGroupBox>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QScatterSeries>
#include <QValueAxis>
#include <QTabWidget>

#include "PacejkaTireModel.h"

namespace ks {

struct TireCurvePreset {
    QString name;
    PacejkaTireModel::TireCoefficients coeffs;
    double normalForce;
    double frictionCoeff;
};

class TireCurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit TireCurveEditor(QWidget* parent = nullptr);

    struct CurveResult {
        QVector<QPair<float, float>> lateralCurve;
        QVector<QPair<float, float>> longitudinalCurve;
        double peakLateralForce;
        double peakSlipAngle;
        double peakLongitudinalForce;
        double peakSlipRatio;
        double stiffnessLateral;
        double stiffnessLongitudinal;
    };

    CurveResult currentCurve() const { return m_currentResult; }
    CurveResult comparisonCurve() const { return m_compareResult; }

public slots:
    void loadFromIni(const QString& carFolder);
    void refreshCurves();
    void resetToDefaults();

signals:
    void curveChanged();
    void compoundChanged(const QString& name);
    void comparisonChanged(const QString& name);

private slots:
    void onCompoundChanged(int index);
    void onCompareCompoundChanged(int index);
    void onParameterChanged();
    void onLoadChanged(double value);
    void onFrictionChanged(double value);
    void onComparisonToggled(bool enabled);
    void onExportCurve();
    void onImportCurve();
    void onCopyToCompare();

private:
    void buildUI();
    void updateCharts();
    void computeCurves();
    static CurveResult buildResult(
        const QVector<QPair<float, float>>& lat,
        const QVector<QPair<float, float>>& lon);
    void buildCurvePage(QWidget* parent);
    QWidget* buildGripCirclePage(QWidget* parent);
    void computeGripCircle();
    void updateGripCircle();
    void setupAxes(QChart* chart, double xMin, double xMax, double yMin, double yMax,
                   const QString& xTitle, const QString& yTitle);
    void clearOldAxes(QChart* chart);

    // Presets
    void populateCompoundPresets();
    void applyCompoundToSliders(int idx);

    // Full Pacejka coefficient editing
    void buildCoefficientPanel(QWidget* parent);
    void onCoefficientChanged();

    // Charts
    QTabWidget* m_chartTabs = nullptr;

    // Lateral chart
    QChart*     m_latChart = nullptr;
    QChartView* m_latChartView = nullptr;
    // Longitudinal chart
    QChart*     m_lonChart = nullptr;
    QChartView* m_lonChartView = nullptr;
    // Grip circle chart
    QChart*     m_gripChart = nullptr;
    QChartView* m_gripChartView = nullptr;

    // Primary curve series
    QLineSeries* m_latPrimarySeries = nullptr;
    QLineSeries* m_lonPrimarySeries = nullptr;
    // Comparison curve series
    QLineSeries* m_latCompareSeries = nullptr;
    QLineSeries* m_lonCompareSeries = nullptr;
    // Grip circle series
    QScatterSeries* m_gripCloudSeries = nullptr;
    QLineSeries*    m_gripEnvelopeSeries = nullptr;
    QLineSeries*    m_gripLatAxis = nullptr;
    QLineSeries*    m_gripLonAxis = nullptr;

    // Compound selectors
    QComboBox*  m_compoundCombo = nullptr;
    QComboBox*  m_compareCombo = nullptr;
    QCheckBox*  m_comparisonToggle = nullptr;

    // B/C/D/E sliders (lateral)
    QSlider*     m_latBSlider = nullptr;
    QSlider*     m_latCSlider = nullptr;
    QSlider*     m_latDSlider = nullptr;
    QSlider*     m_latESlider = nullptr;
    QLabel*      m_latBLabel = nullptr;
    QLabel*      m_latCLabel = nullptr;
    QLabel*      m_latDLabel = nullptr;
    QLabel*      m_latELabel = nullptr;
    QDoubleSpinBox* m_latBSpin = nullptr;
    QDoubleSpinBox* m_latCSpin = nullptr;
    QDoubleSpinBox* m_latDSpin = nullptr;
    QDoubleSpinBox* m_latESpin = nullptr;

    // B/C/D/E sliders (longitudinal)
    QSlider*     m_lonBSlider = nullptr;
    QSlider*     m_lonCSlider = nullptr;
    QSlider*     m_lonDSlider = nullptr;
    QSlider*     m_lonESlider = nullptr;
    QLabel*      m_lonBLabel = nullptr;
    QLabel*      m_lonCLabel = nullptr;
    QLabel*      m_lonDLabel = nullptr;
    QLabel*      m_lonELabel = nullptr;
    QDoubleSpinBox* m_lonBSpin = nullptr;
    QDoubleSpinBox* m_lonCSpin = nullptr;
    QDoubleSpinBox* m_lonDSpin = nullptr;
    QDoubleSpinBox* m_lonESpin = nullptr;

    // Load and friction
    QDoubleSpinBox* m_normalLoadSpin = nullptr;
    QDoubleSpinBox* m_frictionSpin = nullptr;

    // Stats labels
    QLabel* m_peakLatForceLabel = nullptr;
    QLabel* m_peakSlipAngleLabel = nullptr;
    QLabel* m_peakLonForceLabel = nullptr;
    QLabel* m_peakSlipRatioLabel = nullptr;
    QLabel* m_latStiffnessLabel = nullptr;
    QLabel* m_lonStiffnessLabel = nullptr;

    // Buttons
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_copyToCompareBtn = nullptr;

    // Data
    QVector<TireCurvePreset> m_presets;
    int m_currentCompoundIdx = 0;
    int m_compareCompoundIdx = 1;
    bool m_comparisonEnabled = false;

    CurveResult m_currentResult;
    CurveResult m_compareResult;

    // Pacejka model instances for direct computation
    PacejkaTireModel m_primaryModel;
    PacejkaTireModel m_compareModel;

    // Full coefficient editing widgets
    QGroupBox* m_advancedCoeffGroup = nullptr;
    QDoubleSpinBox* m_latCoeffSpin[13] = {};
    QDoubleSpinBox* m_lonCoeffSpin[8] = {};
    QDoubleSpinBox* m_alignCoeffSpin[13] = {};

};

} // namespace ks
