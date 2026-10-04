#include "AudioProcessing.h"
#include <cmath>
#include <algorithm>
#include <QRandomGenerator>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ks { namespace audio {

// ============================================================================
// BatchAudioProcessor
// ============================================================================

void BatchAudioProcessor::addJob(const ProcessingJob& job)
{
    m_jobs.append(job);
}

void BatchAudioProcessor::clearJobs()
{
    m_jobs.clear();
    m_currentJob = 0;
}

void BatchAudioProcessor::process()
{
    if (m_jobs.isEmpty()) {
        emit finished();
        return;
    }

    emit status(QString("Processing %1 jobs...").arg(m_jobs.size()));
    emit progress(0);

    for (int i = 0; i < m_jobs.size(); ++i) {
        const auto& job = m_jobs[i];
        emit status(QString("Processing: %1").arg(job.inputPath));

        bool success = processFile(job.inputPath, job.outputPath);

        if (!success) {
            emit status(QString("Failed: %1").arg(job.inputPath));
        }

        emit progress(static_cast<int>((i + 1) * 100.0 / m_jobs.size()));
    }

    emit finished();
}

bool BatchAudioProcessor::processFile(const QString& input, const QString& output)
{
    QFile inFile(input);
    if (!inFile.open(QIODevice::ReadOnly)) return false;

    QByteArray data = inFile.readAll();
    inFile.close();

    // Find the matching job to get effects/params
    ProcessingJob job;
    for (const auto& j : m_jobs) {
        if (j.inputPath == input) { job = j; break; }
    }

    // Apply effects based on job parameters
    if (!job.effects.isEmpty() || !job.params.isEmpty()) {
        // Interpret raw bytes as 16-bit PCM samples for processing
        int sampleCount = data.size() / 2;
        if (sampleCount > 0) {
            qint16* samples = reinterpret_cast<qint16*>(data.data());

            // Normalize if requested
            if (job.effects.contains("normalize") || job.params.contains("normalize")) {
                float gain = job.params.value("normalize", 1.0f);
                qint16 maxVal = 0;
                for (int i = 0; i < sampleCount; ++i)
                    maxVal = qMax(maxVal, qAbs(samples[i]));
                if (maxVal > 0) {
                    float scale = (32767.0f * gain) / maxVal;
                    for (int i = 0; i < sampleCount; ++i)
                        samples[i] = static_cast<qint16>(qBound(static_cast<qint16>(-32768), static_cast<qint16>(samples[i] * scale), static_cast<qint16>(32767)));
                }
            }

            // Fade in
            if (job.params.contains("fadeIn")) {
                int fadeSamples = static_cast<int>(job.params["fadeIn"] * sampleCount);
                for (int i = 0; i < qMin(fadeSamples, sampleCount); ++i)
                    samples[i] = static_cast<qint16>(samples[i] * static_cast<float>(i) / fadeSamples);
            }

            // Fade out
            if (job.params.contains("fadeOut")) {
                int fadeSamples = static_cast<int>(job.params["fadeOut"] * sampleCount);
                int start = qMax(0, sampleCount - fadeSamples);
                for (int i = start; i < sampleCount; ++i)
                    samples[i] = static_cast<qint16>(samples[i] * static_cast<float>(sampleCount - i) / fadeSamples);
            }

            // Gain
            if (job.params.contains("gain")) {
                float gain = job.params["gain"];
                for (int i = 0; i < sampleCount; ++i)
                    samples[i] = static_cast<qint16>(qBound(static_cast<qint16>(-32768), static_cast<qint16>(samples[i] * gain), static_cast<qint16>(32767)));
            }

            // Trim (reduce sample count)
            if (job.params.contains("trimStart") || job.params.contains("trimEnd")) {
                int start = static_cast<int>(job.params.value("trimStart", 0.0f) * sampleCount);
                int end = static_cast<int>(job.params.value("trimEnd", 1.0f) * sampleCount);
                start = qBound(0, start, sampleCount);
                end = qBound(start, end, sampleCount);
                data = QByteArray(reinterpret_cast<const char*>(samples + start), (end - start) * 2);
            }
        }
    }

    QFile outFile(output);
    if (!outFile.open(QIODevice::WriteOnly)) return false;
    outFile.write(data);
    outFile.close();
    return true;
}

// ============================================================================
// WaveformMouseEditor
// ============================================================================

void WaveformMouseEditor::drawAt(int sampleIndex, float value)
{
    if (m_samples.isEmpty()) return;
    pushUndo();

    int start = qMax(0, sampleIndex - m_brushSize / 2);
    int end = qMin(m_samples.size(), sampleIndex + m_brushSize / 2);

    for (int i = start; i < end; ++i) {
        float dist = std::abs(i - sampleIndex) / (m_brushSize / 2.0f);
        float influence = (1.0f - dist * dist) * m_brushStrength;
        m_samples[i] = m_samples[i] * (1.0f - influence) + value * influence;
    }

    emit dataChanged();
}

void WaveformMouseEditor::eraseAt(int sampleIndex)
{
    if (m_samples.isEmpty()) return;
    pushUndo();

    int start = qMax(0, sampleIndex - m_brushSize / 2);
    int end = qMin(m_samples.size(), sampleIndex + m_brushSize / 2);

    for (int i = start; i < end; ++i) {
        float dist = std::abs(i - sampleIndex) / (m_brushSize / 2.0f);
        float influence = (1.0f - dist * dist) * m_brushStrength;
        m_samples[i] *= (1.0f - influence);
    }

    emit dataChanged();
}

void WaveformMouseEditor::smoothAt(int sampleIndex)
{
    if (m_samples.isEmpty()) return;
    pushUndo();

    int start = qMax(1, sampleIndex - m_brushSize / 2);
    int end = qMin(m_samples.size() - 1, sampleIndex + m_brushSize / 2);

    QVector<float> smoothed = m_samples;
    for (int i = start; i < end; ++i) {
        smoothed[i] = (m_samples[i - 1] + m_samples[i] + m_samples[i + 1]) / 3.0f;
    }

    for (int i = start; i < end; ++i) {
        float dist = std::abs(i - sampleIndex) / (m_brushSize / 2.0f);
        float influence = (1.0f - dist * dist) * m_brushStrength;
        m_samples[i] = m_samples[i] * (1.0f - influence) + smoothed[i] * influence;
    }

    emit dataChanged();
}

void WaveformMouseEditor::fadeInAt(int sampleIndex)
{
    if (m_samples.isEmpty()) return;
    pushUndo();

    int start = qMax(0, sampleIndex - m_brushSize);
    int end = qMin(m_samples.size(), sampleIndex);

    for (int i = start; i < end; ++i) {
        float t = static_cast<float>(i - start) / (end - start);
        m_samples[i] *= t * m_brushStrength;
    }

    emit dataChanged();
}

void WaveformMouseEditor::fadeOutAt(int sampleIndex)
{
    if (m_samples.isEmpty()) return;
    pushUndo();

    int start = sampleIndex;
    int end = qMin(m_samples.size(), sampleIndex + m_brushSize);

    for (int i = start; i < end; ++i) {
        float t = 1.0f - static_cast<float>(i - start) / (end - start);
        m_samples[i] *= t * m_brushStrength;
    }

    emit dataChanged();
}

// ============================================================================
// SoundGenerator
// ============================================================================

QVector<float> SoundGenerator::generate(int sampleRate)
{
    int numSamples = static_cast<int>(sampleRate * m_duration / 1000.0f);
    QVector<float> output(numSamples);

    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float phase = 2.0f * M_PI * m_frequency * t;
        float sample = 0.0f;

        switch (m_waveform) {
            case Sine:
                sample = std::sin(phase);
                break;
            case Square:
                sample = (std::sin(phase) >= 0) ? 1.0f : -1.0f;
                break;
            case Sawtooth:
                sample = 2.0f * (m_frequency * t - std::floor(0.5f + m_frequency * t));
                break;
            case Triangle:
                sample = 2.0f * std::abs(2.0f * (m_frequency * t - std::floor(0.5f + m_frequency * t))) - 1.0f;
                break;
            case Noise:
                sample = static_cast<float>(QRandomGenerator::global()->generateDouble()) * 2.0f - 1.0f;
                break;
            case Tone:
                sample = std::sin(phase);
                break;
        }

        float envelope = 1.0f;
        int attackSamples = qMin(100, numSamples / 10);
        int releaseSamples = qMin(100, numSamples / 10);
        if (i < attackSamples) {
            envelope = static_cast<float>(i) / attackSamples;
        } else if (i > numSamples - releaseSamples) {
            envelope = static_cast<float>(numSamples - i) / releaseSamples;
        }

        output[i] = sample * m_volume * envelope;
    }

    emit generated();
    return output;
}

// ============================================================================
// AudioGenerator
// ============================================================================

QVector<float> AudioGenerator::generate(int sampleRate, int channels, int durationMs)
{
    int numSamples = static_cast<int>(sampleRate * durationMs / 1000.0f) * channels;
    QVector<float> output(numSamples);

    switch (m_type) {
        case Tone: {
            float freq = m_params.value("frequency", 440.0f);
            float vol = m_params.value("volume", 0.8f);
            for (int i = 0; i < numSamples; ++i) {
                float t = static_cast<float>(i / channels) / sampleRate;
                output[i] = std::sin(2.0f * M_PI * freq * t) * vol;
            }
            break;
        }
        case Noise: {
            float vol = m_params.value("volume", 0.5f);
            for (int i = 0; i < numSamples; ++i) {
                output[i] = (static_cast<float>(QRandomGenerator::global()->generateDouble()) * 2.0f - 1.0f) * vol;
            }
            break;
        }
        case Sweep: {
            float startFreq = m_params.value("startFreq", 20.0f);
            float endFreq = m_params.value("endFreq", 20000.0f);
            float vol = m_params.value("volume", 0.8f);
            for (int i = 0; i < numSamples; ++i) {
                float t = static_cast<float>(i / channels) / sampleRate;
                float duration = durationMs / 1000.0f;
                float freq = startFreq + (endFreq - startFreq) * (t / duration);
                output[i] = std::sin(2.0f * M_PI * freq * t) * vol;
            }
            break;
        }
        case Click: {
            float vol = m_params.value("volume", 1.0f);
            for (int i = 0; i < numSamples; ++i) {
                if (i < 10) {
                    output[i] = vol * (1.0f - static_cast<float>(i) / 10.0f);
                } else {
                    output[i] = 0.0f;
                }
            }
            break;
        }
        case Silence: {
            break;
        }
        case Custom: {
            if (!m_expression.isEmpty()) {
                for (int i = 0; i < numSamples; ++i) {
                    float t = static_cast<float>(i / channels) / sampleRate;
                    float val = std::sin(2.0f * M_PI * 440.0f * t);
                    if (m_expression.contains("square")) {
                        val = (val >= 0) ? 1.0f : -1.0f;
                    } else if (m_expression.contains("saw")) {
                        val = 2.0f * (440.0f * t - std::floor(0.5f + 440.0f * t));
                    }
                    output[i] = val * m_params.value("volume", 0.8f);
                }
            }
            break;
        }
    }

    emit generated();
    return output;
}

}} // ks::audio
