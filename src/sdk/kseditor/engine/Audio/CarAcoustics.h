#ifndef KSCARACOUSTICS_H
#define KSCARACOUSTICS_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>

namespace ks {
namespace audio {

class KSCarAcoustics : public QObject {
    Q_OBJECT

public:
    enum class AcousticMode {
        Exterior,
        Interior
    };

    enum class CarType {
        Sedan,
        Coupe,
        SUV,
        Convertible,
        RaceCar,
        Truck
    };

    enum class ExteriorPreset {
        Raw,
        Sport,
        Race,
        Classic,
        Modern
    };

    enum class InteriorPreset {
        Stock,
        Sport,
        Racing,
        Open,
        Luxury
    };

    struct CabinModel {
        float volume;
        float absorption;
        float windowArea;
        float seatMaterial;
        float glassThickness;
        float carpetAbsorption;
        float doorSealRating;

        CabinModel() = default;

        CabinModel(float v, float a, float wa, float sm, float gt, float ca, float ds)
            : volume(v), absorption(a), windowArea(wa), seatMaterial(sm)
            , glassThickness(gt), carpetAbsorption(ca), doorSealRating(ds) {}
    };

    struct FilterConfig {
        float lowPassFreq;
        float highPassFreq;
        float resonance;
        float gain;

        FilterConfig(float lpf = 20000.0f, float hpf = 20.0f, float res = 0.7f, float g = 0.0f)
            : lowPassFreq(lpf), highPassFreq(hpf), resonance(res), gain(g) {}
    };

    explicit KSCarAcoustics(QObject* parent = nullptr);
    ~KSCarAcoustics() = default;

    void setMode(AcousticMode mode) { m_mode = mode; }
    AcousticMode mode() const { return m_mode; }

    void setCarType(CarType type);
    CarType carType() const { return m_carType; }

    void setExteriorPreset(ExteriorPreset preset);
    ExteriorPreset exteriorPreset() const { return m_exteriorPreset; }

    void setInteriorPreset(InteriorPreset preset);
    InteriorPreset interiorPreset() const { return m_interiorPreset; }

    void setCabinModel(const CabinModel& model) { m_cabin = model; }
    CabinModel cabinModel() const { return m_cabin; }

    QVector<float> process(const QVector<float>& input, int sampleRate, int channels);

    void applyCabinAcoustics(float* samples, int sampleCount, int sampleRate, int channels);
    void applyExteriorAcoustics(float* samples, int sampleCount, int sampleRate, int channels);
    void applyLowPassFilter(float* samples, int sampleCount, float cutoffFreq, float resonance, int sampleRate, int channels);
    void applyHighPassFilter(float* samples, int sampleCount, float cutoffFreq, float resonance, int sampleRate, int channels);
    void applyFilter(float* samples, int sampleCount, const FilterConfig& config, bool isLowPass, int sampleRate, int channels);
    void applyCabinEQ(float* samples, int sampleCount, int sampleRate, int channels);
    void applyDopplerEffect(float* samples, int sampleCount, int sampleRate, float speed, float direction, int channels);
    void applyDistanceAttenuation(float* samples, int sampleCount, float distance, float rolloff, int channels);

    QVector<float> getImpulseResponse(int sampleRate, int durationMs);

    static CarType carTypeFromString(const QString& str);
    static QString carTypeToString(CarType type);
    static ExteriorPreset exteriorPresetFromString(const QString& str);
    static QString exteriorPresetToString(ExteriorPreset preset);
    static InteriorPreset interiorPresetFromString(const QString& str);
    static QString interiorPresetToString(InteriorPreset preset);

signals:
    void modeChanged(AcousticMode mode);
    void carTypeChanged(CarType type);
    void presetChanged();

private:
    void applyFilter(float* samples, int sampleCount, const FilterConfig& config, bool isLowPass, int channels);
    void calculateCabinResponse(float* response, int size, int sampleRate);

    AcousticMode m_mode = AcousticMode::Exterior;
    CarType m_carType = CarType::Sedan;
    ExteriorPreset m_exteriorPreset = ExteriorPreset::Raw;
    InteriorPreset m_interiorPreset = InteriorPreset::Stock;
    CabinModel m_cabin;

    QMap<CarType, CabinModel> m_cabinModels;
    QMap<ExteriorPreset, FilterConfig> m_exteriorFilters;
    QMap<InteriorPreset, FilterConfig> m_interiorFilters;

    QVector<float> m_cabinIR;
    float m_lastSpeed = 0.0f;
};

}
}

#endif
