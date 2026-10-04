#include "AIAudioEngine.h"
#include <cmath>
#include <algorithm>
#include <QRandomGenerator>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ks { namespace audio {

// ============================================================================
// AIAudioAnalyzer
// ============================================================================

AIAudioAnalyzer::AnalysisResult AIAudioAnalyzer::analyze(const QVector<float>& audio, int sampleRate)
{
    AnalysisResult result;

    if (audio.isEmpty()) {
        result.classification = Unknown;
        result.confidence = 0.0f;
        result.description = "No audio data";
        return result;
    }

    float rms = 0.0f;
    float peak = 0.0f;
    float zeroCrossings = 0.0f;
    float spectralCentroid = 0.0f;

    for (int i = 0; i < audio.size(); ++i) {
        float absS = std::abs(audio[i]);
        if (absS > peak) peak = absS;
        rms += audio[i] * audio[i];

        if (i > 0 && ((audio[i] >= 0 && audio[i - 1] < 0) || (audio[i] < 0 && audio[i - 1] >= 0))) {
            zeroCrossings++;
        }
    }

    rms = std::sqrt(rms / audio.size());
    zeroCrossings /= audio.size();

    float duration = static_cast<float>(audio.size()) / sampleRate;
    float fundamentalFreq = zeroCrossings * sampleRate / 2.0f;

    result.features.append(rms);
    result.features.append(peak);
    result.features.append(zeroCrossings);
    result.features.append(fundamentalFreq);
    result.features.append(duration);

    if (fundamentalFreq > 50 && fundamentalFreq < 300 && rms > 0.1f) {
        result.classification = Engine;
        result.confidence = 0.85f;
        result.description = QString("Engine sound at ~%1 Hz").arg(static_cast<int>(fundamentalFreq));
    } else if (fundamentalFreq > 1000 && zeroCrossings > 0.3f) {
        result.classification = Wind;
        result.confidence = 0.7f;
        result.description = "Wind noise detected";
    } else if (peak > 0.8f && duration < 0.1f) {
        result.classification = Impact;
        result.confidence = 0.8f;
        result.description = "Impact/transient detected";
    } else if (fundamentalFreq > 80 && fundamentalFreq < 400 && rms > 0.05f) {
        result.classification = Voice;
        result.confidence = 0.6f;
        result.description = "Voice-like characteristics";
    } else if (rms < 0.01f) {
        result.classification = Ambient;
        result.confidence = 0.9f;
        result.description = "Ambient/quiet audio";
    } else {
        result.classification = Unknown;
        result.confidence = 0.3f;
        result.description = "Unclassified audio";
    }

    emit analysisComplete(result);
    return result;
}

QVector<AIAudioAnalyzer::AudioFingerprint> AIAudioAnalyzer::findSimilar(const QVector<float>& audio, int limit) const
{
    QVector<AudioFingerprint> results;

    for (const auto& fp : m_fingerprints) {
        float similarity = 0.0f;
        int minSize = qMin(audio.size(), fp.hash.size());
        if (minSize > 0) {
            float diff = 0.0f;
            for (int i = 0; i < minSize; ++i) {
                diff += std::abs(audio[i] - fp.hash[i]);
            }
            similarity = 1.0f - (diff / minSize);
        }
        if (similarity > 0.5f) {
            results.append(fp);
        }
    }

    std::sort(results.begin(), results.end(), [](const AudioFingerprint& a, const AudioFingerprint& b) {
        return a.id > b.id;
    });

    if (results.size() > limit) results.resize(limit);
    return results;
}

void AIAudioAnalyzer::trainModel(const QVector<AnalysisResult>& samples)
{
    for (const auto& sample : samples) {
        m_trainedFeatures[sample.classification].append(sample.features);
    }
}

// ============================================================================
// EngineSoundSynth
// ============================================================================

float EngineSoundSynth::calculateFiringAngle(float rpm)
{
    return 720.0f / m_params.cylinders;
}

float EngineSoundSynth::calculatePrimaryFreq(float rpm)
{
    return rpm / 60.0f * m_params.cylinders / 2.0f;
}

float EngineSoundSynth::calculateSecondaryFreq(float rpm)
{
    return rpm / 60.0f * m_params.cylinders;
}

QVector<float> EngineSoundSynth::generate(int sampleRate, int durationMs, float rpm)
{
    int numSamples = static_cast<int>(sampleRate * durationMs / 1000.0f);
    QVector<float> output(numSamples);

    float primaryFreq = calculatePrimaryFreq(rpm);
    float secondaryFreq = calculateSecondaryFreq(rpm);
    float firingAngle = calculateFiringAngle(rpm);

    float rpmNorm = rpm / m_params.maxRPM;
    float loadFactor = rpmNorm * 0.5f + 0.5f;

    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float sample = 0.0f;

        float primaryPhase = 2.0f * M_PI * primaryFreq * t;
        float secondaryPhase = 2.0f * M_PI * secondaryFreq * t;

        float fundamental = std::sin(primaryPhase);
        float secondHarmonic = std::sin(secondaryPhase) * 0.5f;
        float thirdHarmonic = std::sin(3.0f * primaryPhase) * 0.3f;

        float exhaustTone = 0.0f;
        if (m_exhaustType == "dual") {
            exhaustTone = std::sin(primaryPhase + M_PI) * 0.3f;
        } else if (m_exhaustType == "quad") {
            exhaustTone = (std::sin(primaryPhase + M_PI / 2) + std::sin(primaryPhase + M_PI * 1.5f)) * 0.2f;
        }

        float intakeNoise = 0.0f;
        if (m_intakeType == "turbo") {
            intakeNoise = (static_cast<float>(QRandomGenerator::global()->generateDouble()) - 0.5f) * 0.1f * m_turbo.boost;
        }

        float turboWhistle = 0.0f;
        if (m_params.turbocharged && rpm > m_params.idleRPM * 2) {
            float turboFreq = primaryFreq * 3.0f * m_turbo.size;
            turboWhistle = std::sin(2.0f * M_PI * turboFreq * t) * m_turbo.boost * 0.15f;
        }

        sample = (fundamental + secondHarmonic + thirdHarmonic + exhaustTone + intakeNoise + turboWhistle) * loadFactor;

        float envelope = 1.0f;
        int attackSamples = qMin(500, numSamples / 20);
        int releaseSamples = qMin(500, numSamples / 20);
        if (i < attackSamples) {
            envelope = static_cast<float>(i) / attackSamples;
        } else if (i > numSamples - releaseSamples) {
            envelope = static_cast<float>(numSamples - i) / releaseSamples;
        }

        output[i] = sample * envelope * 0.3f;
    }

    emit generated();
    return output;
}

QVector<float> EngineSoundSynth::generateAtRPM(float rpm, int sampleRate)
{
    return generate(sampleRate, 1000, rpm);
}

void EngineSoundSynth::setTurboParams(float boost, float size, float response)
{
    m_turbo.boost = boost;
    m_turbo.size = size;
    m_turbo.response = response;
}

void EngineSoundSynth::setIntakeParams(const QString& type)
{
    m_intakeType = type;
}

QVector<float> EngineSoundSynth::generateLoad(float loadFactor, int sampleRate)
{
    float rpm = m_params.idleRPM + (m_params.maxRPM - m_params.idleRPM) * loadFactor;
    return generate(sampleRate, 1000, rpm);
}

}} // ks::audio
