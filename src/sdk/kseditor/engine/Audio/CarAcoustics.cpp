#include "CarAcoustics.h"
#include <cmath>
#include <QtMath>

namespace ks {
namespace audio {

KSCarAcoustics::KSCarAcoustics(QObject* parent)
    : QObject(parent)
{
    m_cabinModels[CarType::Sedan] = CabinModel{ 2.5f, 0.3f, 1.2f, 0.5f, 0.005f, 0.6f, 0.7f };
    m_cabinModels[CarType::Coupe] = CabinModel{ 1.8f, 0.35f, 0.9f, 0.6f, 0.005f, 0.5f, 0.8f };
    m_cabinModels[CarType::SUV] = CabinModel{ 3.5f, 0.25f, 1.8f, 0.4f, 0.006f, 0.7f, 0.6f };
    m_cabinModels[CarType::Convertible] = CabinModel{ 1.2f, 0.5f, 0.6f, 0.7f, 0.003f, 0.3f, 0.4f };
    m_cabinModels[CarType::RaceCar] = CabinModel{ 0.8f, 0.6f, 0.3f, 0.8f, 0.002f, 0.2f, 0.2f };
    m_cabinModels[CarType::Truck] = CabinModel{ 4.0f, 0.2f, 2.0f, 0.3f, 0.008f, 0.8f, 0.5f };

    m_exteriorFilters[ExteriorPreset::Raw] = FilterConfig(20000.0f, 20.0f, 0.7f, 0.0f);
    m_exteriorFilters[ExteriorPreset::Sport] = FilterConfig(12000.0f, 80.0f, 0.8f, -2.0f);
    m_exteriorFilters[ExteriorPreset::Race] = FilterConfig(15000.0f, 100.0f, 0.9f, -1.0f);
    m_exteriorFilters[ExteriorPreset::Classic] = FilterConfig(8000.0f, 60.0f, 0.6f, -3.0f);
    m_exteriorFilters[ExteriorPreset::Modern] = FilterConfig(10000.0f, 50.0f, 0.7f, -1.5f);

    m_interiorFilters[InteriorPreset::Stock] = FilterConfig(3500.0f, 150.0f, 0.8f, -4.0f);
    m_interiorFilters[InteriorPreset::Sport] = FilterConfig(4000.0f, 120.0f, 1.0f, -3.0f);
    m_interiorFilters[InteriorPreset::Racing] = FilterConfig(5000.0f, 100.0f, 1.2f, -2.0f);
    m_interiorFilters[InteriorPreset::Open] = FilterConfig(6000.0f, 100.0f, 0.9f, -2.0f);
    m_interiorFilters[InteriorPreset::Luxury] = FilterConfig(3000.0f, 200.0f, 0.6f, -5.0f);

    setCarType(CarType::Sedan);
    setExteriorPreset(ExteriorPreset::Raw);
    setInteriorPreset(InteriorPreset::Stock);
}

void KSCarAcoustics::setCarType(CarType type) {
    if (m_carType == type) return;
    m_carType = type;
    auto it = m_cabinModels.find(type);
    if (it != m_cabinModels.end()) {
        m_cabin = it.value();
    }
    emit carTypeChanged(type);
}

void KSCarAcoustics::setExteriorPreset(ExteriorPreset preset) {
    if (m_exteriorPreset == preset) return;
    m_exteriorPreset = preset;
    emit presetChanged();
}

void KSCarAcoustics::setInteriorPreset(InteriorPreset preset) {
    if (m_interiorPreset == preset) return;
    m_interiorPreset = preset;
    emit presetChanged();
}

QVector<float> KSCarAcoustics::getImpulseResponse(int sampleRate, int durationMs) {
    int length = (sampleRate * durationMs) / 1000;
    QVector<float> ir(length, 0.0f);

    float rt60 = m_cabin.volume * (1.0f - m_cabin.absorption) * 0.5f;
    rt60 = qBound(0.1f, rt60, 1.5f);

    float decay = -6.0f / (rt60 * sampleRate);
    for (int i = 0; i < length; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        ir[i] = expf(decay * t) * (0.5f - 0.5f * cosf(2.0f * M_PI * t * 50.0f));
        if (i < 128) {
            float gaussian = expf(-powf(static_cast<float>(i - 64) / 20.0f, 2.0f));
            ir[i] += gaussian * 0.3f;
        }
    }

    return ir;
}

QVector<float> KSCarAcoustics::process(const QVector<float>& input, int sampleRate, int channels) {
    QVector<float> output = input;

    if (m_mode == AcousticMode::Interior) {
        applyCabinAcoustics(output.data(), output.size() / channels, sampleRate, channels);
        applyCabinEQ(output.data(), output.size() / channels, sampleRate, channels);
    } else {
        applyExteriorAcoustics(output.data(), output.size() / channels, sampleRate, channels);
    }

    return output;
}

void KSCarAcoustics::applyCabinAcoustics(float* samples, int sampleCount, int sampleRate, int channels) {
    auto presetIt = m_interiorFilters.find(m_interiorPreset);
    if (presetIt != m_interiorFilters.end()) {
        applyFilter(samples, sampleCount, presetIt.value(), false, sampleRate, channels);
        applyFilter(samples, sampleCount, presetIt.value(), true, sampleRate, channels);
    }

    float windowFactor = 1.0f - (m_cabin.windowArea / 3.0f);
    windowFactor = qBound(0.5f, windowFactor, 1.0f);

    float baseFreq = 800.0f - (m_cabin.volume * 100.0f);
    baseFreq = qBound(200.0f, baseFreq, 1200.0f);

    QVector<float> resonance(sampleCount, 1.0f);
    float resonanceQ = 2.0f / m_cabin.doorSealRating;
    for (int i = 0; i < sampleCount; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float resonanceFactor = 1.0f + 0.3f * expf(-powf(t - 0.015f, 2.0f) * 5000.0f);
        resonance[i] = resonanceFactor;
    }

    for (int ch = 0; ch < channels && ch < 2; ++ch) {
        for (int i = ch; i < sampleCount * channels; i += channels) {
            samples[i] *= resonance[i / channels] * windowFactor;
        }
    }
}

void KSCarAcoustics::applyExteriorAcoustics(float* samples, int sampleCount, int sampleRate, int channels) {
    auto presetIt = m_exteriorFilters.find(m_exteriorPreset);
    if (presetIt != m_exteriorFilters.end()) {
        applyFilter(samples, sampleCount, presetIt.value(), true, sampleRate, channels);
        applyFilter(samples, sampleCount, presetIt.value(), false, sampleRate, channels);
    }
}

void KSCarAcoustics::applyFilter(float* samples, int sampleCount, const FilterConfig& config, bool isLowPass, int sampleRate, int channels) {
    float fc = isLowPass ? config.lowPassFreq : config.highPassFreq;
    if (fc <= 0.0f || fc >= sampleRate / 2.0f) return;

    float w0 = 2.0f * M_PI * fc / sampleRate;
    float alpha = sinf(w0) / (2.0f * config.resonance);

    float b0, b1, b2, a0, a1, a2;

    if (isLowPass) {
        b0 = (1.0f - cosf(w0)) / 2.0f;
        b1 = 1.0f - cosf(w0);
        b2 = (1.0f - cosf(w0)) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cosf(w0);
        a2 = 1.0f - alpha;
    } else {
        b0 = (1.0f + cosf(w0)) / 2.0f;
        b1 = -(1.0f + cosf(w0));
        b2 = (1.0f + cosf(w0)) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cosf(w0);
        a2 = 1.0f - alpha;
    }

    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;

    for (int ch = 0; ch < channels && ch < 2; ++ch) {
        float w1 = 0.0f, w2 = 0.0f;
        for (int i = ch; i < sampleCount * channels; i += channels) {
            float input = samples[i];
            float output = b0 * input + w1;
            w1 = b1 * input - a1 * output + w2;
            w2 = b2 * input - a2 * output;
            samples[i] = output * config.gain;
        }
    }
}

void KSCarAcoustics::applyCabinEQ(float* samples, int sampleCount, int sampleRate, int channels) {
    QVector<float> gains = {
        1.0f,    
        0.9f,    
        0.8f,    
        0.7f,    
        0.6f,    
        0.5f,    
        0.4f,    
        0.3f     
    };

    int fftSize = 2048;
    for (int ch = 0; ch < channels && ch < 2; ++ch) {
        QVector<float> segment(fftSize, 0.0f);
        for (int i = 0; i < sampleCount; i += fftSize) {
            int blockEnd = qMin(i + fftSize, sampleCount);
            int blockSize = blockEnd - i;

            for (int j = 0; j < blockSize; ++j) {
                segment[j] = samples[(i + j) * channels + ch];
            }
            for (int j = blockSize; j < fftSize; ++j) {
                segment[j] = 0.0f;
            }

            float windowFactor = 1.0f - (0.3f * m_cabin.absorption);
            for (int j = 0; j < blockSize; ++j) {
                segment[j] *= windowFactor;
            }

            for (int j = 0; j < blockSize; ++j) {
                samples[(i + j) * channels + ch] = segment[j];
            }
        }
    }
}

void KSCarAcoustics::applyLowPassFilter(float* samples, int sampleCount, float cutoffFreq, float resonance, int sampleRate, int channels) {
    applyFilter(samples, sampleCount, FilterConfig(cutoffFreq, 20.0f, resonance, 1.0f), true, sampleRate, channels);
}

void KSCarAcoustics::applyHighPassFilter(float* samples, int sampleCount, float cutoffFreq, float resonance, int sampleRate, int channels) {
    applyFilter(samples, sampleCount, FilterConfig(20000.0f, cutoffFreq, resonance, 1.0f), false, sampleRate, channels);
}

void KSCarAcoustics::applyDopplerEffect(float* samples, int sampleCount, int sampleRate, float speed, float direction, int channels) {
    if (qAbs(speed) < 0.01f) return;

    float dopplerFactor = 0.05f * speed / 50.0f;
    dopplerFactor = qBound(-0.2f, dopplerFactor, 0.2f);

    int segmentSize = sampleRate / 10;
    for (int i = 0; i < sampleCount; i += segmentSize) {
        float segmentDoppler = dopplerFactor * (1.0f - static_cast<float>(i) / sampleCount);
        float pitchMod = 1.0f + segmentDoppler;

        int end = qMin(i + segmentSize, sampleCount);
        for (int j = i; j < end; ++j) {
            for (int ch = 0; ch < channels && ch < 2; ++ch) {
                samples[j * channels + ch] *= pitchMod;
            }
        }
    }
}

void KSCarAcoustics::applyDistanceAttenuation(float* samples, int sampleCount, float distance, float rolloff, int channels) {
    if (distance <= 0.0f) distance = 1.0f;

    float minDist = 1.0f;
    float maxDist = 50.0f;

    float attenuation;
    if (distance <= minDist) {
        attenuation = 1.0f;
    } else if (distance >= maxDist) {
        attenuation = 0.1f / powf(distance / minDist, rolloff);
    } else {
        attenuation = 1.0f / (1.0f + (distance - minDist) / minDist * rolloff);
    }

    for (int i = 0; i < sampleCount * channels; ++i) {
        samples[i] *= attenuation;
    }
}

KSCarAcoustics::CarType KSCarAcoustics::carTypeFromString(const QString& str) {
    QMap<QString, CarType> map = {
        {"sedan", CarType::Sedan},
        {"coupe", CarType::Coupe},
        {"suv", CarType::SUV},
        {"convertible", CarType::Convertible},
        {"racecar", CarType::RaceCar},
        {"truck", CarType::Truck}
    };
    return map.value(str.toLower(), CarType::Sedan);
}

QString KSCarAcoustics::carTypeToString(CarType type) {
    switch (type) {
    case CarType::Sedan: return "Sedan";
    case CarType::Coupe: return "Coupe";
    case CarType::SUV: return "SUV";
    case CarType::Convertible: return "Convertible";
    case CarType::RaceCar: return "Race Car";
    case CarType::Truck: return "Truck";
    default: return "Unknown";
    }
}

KSCarAcoustics::ExteriorPreset KSCarAcoustics::exteriorPresetFromString(const QString& str) {
    QMap<QString, ExteriorPreset> map = {
        {"raw", ExteriorPreset::Raw},
        {"sport", ExteriorPreset::Sport},
        {"race", ExteriorPreset::Race},
        {"classic", ExteriorPreset::Classic},
        {"modern", ExteriorPreset::Modern}
    };
    return map.value(str.toLower(), ExteriorPreset::Raw);
}

QString KSCarAcoustics::exteriorPresetToString(ExteriorPreset preset) {
    switch (preset) {
    case ExteriorPreset::Raw: return "Raw";
    case ExteriorPreset::Sport: return "Sport";
    case ExteriorPreset::Race: return "Race";
    case ExteriorPreset::Classic: return "Classic";
    case ExteriorPreset::Modern: return "Modern";
    default: return "Unknown";
    }
}

KSCarAcoustics::InteriorPreset KSCarAcoustics::interiorPresetFromString(const QString& str) {
    QMap<QString, InteriorPreset> map = {
        {"stock", InteriorPreset::Stock},
        {"sport", InteriorPreset::Sport},
        {"racing", InteriorPreset::Racing},
        {"open", InteriorPreset::Open},
        {"luxury", InteriorPreset::Luxury}
    };
    return map.value(str.toLower(), InteriorPreset::Stock);
}

QString KSCarAcoustics::interiorPresetToString(InteriorPreset preset) {
    switch (preset) {
    case InteriorPreset::Stock: return "Stock";
    case InteriorPreset::Sport: return "Sport";
    case InteriorPreset::Racing: return "Racing";
    case InteriorPreset::Open: return "Open";
    case InteriorPreset::Luxury: return "Luxury";
    default: return "Unknown";
    }
}

}
}
