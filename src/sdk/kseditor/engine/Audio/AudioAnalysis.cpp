#include "AudioAnalysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ks { namespace audio {

// ============================================================================
// AudioFFT
// ============================================================================

void AudioFFT::generateHannWindow()
{
    for (int i = 0; i < m_size; ++i) {
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (m_size - 1)));
    }
}

QVector<std::complex<float>> AudioFFT::compute(const QVector<float>& input)
{
    int n = m_size;
    QVector<std::complex<float>> result(n);

    for (int i = 0; i < n && i < input.size(); ++i) {
        result[i] = std::complex<float>(input[i] * m_window[i], 0.0f);
    }
    for (int i = input.size(); i < n; ++i) {
        result[i] = std::complex<float>(0.0f, 0.0f);
    }

    int levels = 0;
    while ((1 << levels) < n) levels++;

    for (int i = 0; i < n; ++i) {
        int j = 0;
        for (int b = 0; b < levels; ++b) {
            j = (j << 1) | ((i >> b) & 1);
        }
        if (j > i) {
            std::swap(result[i], result[j]);
        }
    }

    for (int len = 2; len <= n; len *= 2) {
        float angle = -2.0f * M_PI / len;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = result[i + j];
                std::complex<float> v = result[i + j + len / 2] * w;
                result[i + j] = u + v;
                result[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    return result;
}

QVector<float> AudioFFT::magnitude(const QVector<std::complex<float>>& fft)
{
    QVector<float> mag(fft.size());
    for (int i = 0; i < fft.size(); ++i) {
        mag[i] = std::abs(fft[i]);
    }
    return mag;
}

QVector<float> AudioFFT::phase(const QVector<std::complex<float>>& fft)
{
    QVector<float> ph(fft.size());
    for (int i = 0; i < fft.size(); ++i) {
        ph[i] = std::arg(fft[i]);
    }
    return ph;
}

// ============================================================================
// LoudnessMeter
// ============================================================================

float LoudnessMeter::processBlock(const QVector<float>& block)
{
    if (block.isEmpty()) return -70.0f;

    float sum = 0.0f;
    for (float s : block) {
        sum += s * s;
    }
    float rms = std::sqrt(sum / block.size());
    if (rms < 1e-10f) return -70.0f;
    return 20.0f * std::log10(rms) + 0.691f;
}

LoudnessMeter::LoudnessResult LoudnessMeter::process(const QVector<float>& input)
{
    LoudnessResult result;

    if (input.isEmpty()) {
        result.momentary = -70.0f;
        result.shortTerm = -70.0f;
        result.integrated = -70.0f;
        result.truePeak = -70.0f;
        result.lufs = -70.0f;
        return result;
    }

    int momentarySize = static_cast<int>(m_sampleRate * 0.4);
    int shortTermSize = static_cast<int>(m_sampleRate * 3.0);

    QVector<float> momentaryBlock = input.mid(qMax(0, input.size() - momentarySize));
    result.momentary = processBlock(momentaryBlock);

    QVector<float> shortTermBlock = input.mid(qMax(0, input.size() - shortTermSize));
    result.shortTerm = processBlock(shortTermBlock);

    m_shortTermBuffer.append(result.shortTerm);
    if (m_shortTermBuffer.size() > 100) m_shortTermBuffer.removeFirst();

    float sum = 0.0f;
    for (float v : m_shortTermBuffer) {
        if (v > -70.0f) sum += std::pow(10.0f, v / 10.0f);
    }
    if (!m_shortTermBuffer.isEmpty() && sum > 0.0f) {
        result.integrated = 10.0f * std::log10(sum / m_shortTermBuffer.size());
    } else {
        result.integrated = -70.0f;
    }

    m_integratedLoudness = result.integrated;

    float peak = 0.0f;
    for (float s : input) {
        float absS = std::abs(s);
        if (absS > peak) peak = absS;
    }
    result.truePeak = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -70.0f;

    result.lufs = result.integrated;

    return result;
}

void LoudnessMeter::reset()
{
    m_momentary = -70.0f;
    m_shortTerm = -70.0f;
    m_integratedLoudness = -70.0f;
    m_truePeak = -70.0f;
    m_shortTermBuffer.clear();
    m_gatingBuffer.clear();
}

// ============================================================================
// SpectrumAnalyzer
// ============================================================================

QVector<float> SpectrumAnalyzer::compute(const QVector<float>& input)
{
    if (input.isEmpty()) {
        emit spectrumReady(QVector<float>());
        return QVector<float>();
    }

    AudioFFT fft;
    fft.setSize(m_fftSize);

    QVector<float> windowed = input.mid(0, m_fftSize);
    while (windowed.size() < m_fftSize) windowed.append(0.0f);

    if (m_windowType == "Hann") {
        for (int i = 0; i < m_fftSize; ++i) {
            windowed[i] *= 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (m_fftSize - 1)));
        }
    } else if (m_windowType == "Hamming") {
        for (int i = 0; i < m_fftSize; ++i) {
            windowed[i] *= 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (m_fftSize - 1));
        }
    } else if (m_windowType == "Blackman") {
        for (int i = 0; i < m_fftSize; ++i) {
            windowed[i] *= 0.42f - 0.5f * std::cos(2.0f * M_PI * i / (m_fftSize - 1)) + 0.08f * std::cos(4.0f * M_PI * i / (m_fftSize - 1));
        }
    }

    auto spectrum = fft.compute(windowed);
    QVector<float> magnitude = fft.magnitude(spectrum);

    int halfSize = m_fftSize / 2;
    QVector<float> result(halfSize);
    for (int i = 0; i < halfSize; ++i) {
        result[i] = (magnitude[i] > 1e-10f) ? 20.0f * std::log10(magnitude[i] / m_fftSize) : -120.0f;
    }

    emit spectrumReady(result);
    return result;
}

// ============================================================================
// Spectrogram
// ============================================================================

void Spectrogram::compute(const QVector<float>& input, int sampleRate)
{
    m_spectrogram.clear();

    AudioFFT fft;
    fft.setSize(m_fftSize);

    for (int pos = 0; pos + m_windowSize <= input.size(); pos += m_hopSize) {
        QVector<float> windowed = input.mid(pos, m_fftSize);
        while (windowed.size() < m_fftSize) windowed.append(0.0f);

        for (int i = 0; i < m_fftSize; ++i) {
            windowed[i] *= 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (m_fftSize - 1)));
        }

        auto spectrum = fft.compute(windowed);
        auto magnitude = fft.magnitude(spectrum);

        int halfSize = m_fftSize / 2;
        QVector<float> frame(halfSize);
        for (int i = 0; i < halfSize; ++i) {
            float freq = static_cast<float>(i) * sampleRate / m_fftSize;
            if (freq >= m_minFreq && freq <= m_maxFreq) {
                frame[i] = (magnitude[i] > 1e-10f) ? 20.0f * std::log10(magnitude[i] / m_fftSize) : -120.0f;
            } else {
                frame[i] = -120.0f;
            }
        }

        m_spectrogram.append(frame);
    }

    emit computed();
}

// ============================================================================
// AudioProfiler
// ============================================================================

void AudioProfiler::startSession(const QString& sessionName)
{
    m_currentSession = sessionName;
    m_events.clear();
    m_tracks.clear();
    m_sessionStart = 0;
    emit sessionStarted(sessionName);
}

void AudioProfiler::endSession()
{
    m_currentSession.clear();
    emit sessionEnded();
}

void AudioProfiler::recordEvent(const QString& name, quint64 duration)
{
    ProfilerEvent evt;
    evt.id = QString("evt_%1").arg(m_events.size());
    evt.name = name;
    evt.startTime = m_sessionStart + m_events.size();
    evt.duration = duration;
    evt.cpuUsage = static_cast<float>(duration) / 1000000.0f;
    evt.memoryUsage = 0.0f;
    m_events.append(evt);
}

QVector<AudioProfiler::ProfilerEvent> AudioProfiler::getBottlenecks(int count) const
{
    QVector<ProfilerEvent> sorted = m_events;
    std::sort(sorted.begin(), sorted.end(), [](const ProfilerEvent& a, const ProfilerEvent& b) {
        return a.duration > b.duration;
    });
    if (sorted.size() > count) sorted.resize(count);
    return sorted;
}

// ============================================================================
// SpatialAudioMapper
// ============================================================================

float SpatialAudioMapper::getZoneGain(const QString& zoneId) const
{
    if (!m_zones.contains(zoneId)) return 0.0f;

    const auto& zone = m_zones[zoneId];
    QVector3D diff = m_listener.position - zone.position;
    float dist = diff.length();

    float maxDist = (zone.dimensions.x() + zone.dimensions.y() + zone.dimensions.z()) / 3.0f;
    if (dist < maxDist) {
        return 1.0f - (dist / maxDist) * (1.0f - zone.occlusion);
    }
    return 0.0f;
}

float SpatialAudioMapper::getZoneReverb(const QString& zoneId) const
{
    if (!m_zones.contains(zoneId)) return 0.0f;
    return m_zones[zoneId].reverbMix;
}

// ============================================================================
// WaveformProcessor
// ============================================================================

QVector<float> WaveformProcessor::getPeaks(int numPeaks) const
{
    if (m_data.isEmpty()) return {};

    int blockSize = qMax(1, m_data.size() / numPeaks);
    QVector<float> peaks;
    peaks.reserve(numPeaks);

    for (int i = 0; i < numPeaks; ++i) {
        int start = i * blockSize;
        int end = qMin(start + blockSize, m_data.size());
        float peak = 0.0f;
        for (int j = start; j < end; ++j) {
            float absVal = std::abs(m_data[j]);
            if (absVal > peak) peak = absVal;
        }
        peaks.append(peak);
    }

    return peaks;
}

QVector<float> WaveformProcessor::getRMS(int windowSize) const
{
    if (m_data.isEmpty()) return {};

    QVector<float> rms;
    for (int i = 0; i < m_data.size(); i += windowSize) {
        int end = qMin(i + windowSize, m_data.size());
        float sum = 0.0f;
        for (int j = i; j < end; ++j) {
            sum += m_data[j] * m_data[j];
        }
        rms.append(std::sqrt(sum / (end - i)));
    }

    return rms;
}

QVector<float> WaveformProcessor::normalize(float targetPeak)
{
    if (m_data.isEmpty()) return m_data;

    float peak = 0.0f;
    for (float s : m_data) {
        float absS = std::abs(s);
        if (absS > peak) peak = absS;
    }

    if (peak > 1e-10f) {
        float gain = targetPeak / peak;
        for (int i = 0; i < m_data.size(); ++i) {
            m_data[i] *= gain;
        }
    }

    emit processed();
    return m_data;
}

QVector<float> WaveformProcessor::fadeIn(int samples)
{
    QVector<float> result = m_data;
    int fadeEnd = qMin(samples, result.size());
    for (int i = 0; i < fadeEnd; ++i) {
        result[i] *= static_cast<float>(i) / fadeEnd;
    }
    emit processed();
    return result;
}

QVector<float> WaveformProcessor::fadeOut(int samples)
{
    QVector<float> result = m_data;
    int start = qMax(0, result.size() - samples);
    for (int i = start; i < result.size(); ++i) {
        result[i] *= static_cast<float>(result.size() - i) / samples;
    }
    emit processed();
    return result;
}

QVector<float> WaveformProcessor::reverse()
{
    QVector<float> result = m_data;
    std::reverse(result.begin(), result.end());
    emit processed();
    return result;
}

QVector<float> WaveformProcessor::resample(int newSize)
{
    if (m_data.isEmpty() || newSize <= 0) return {};

    QVector<float> result(newSize);
    double ratio = static_cast<double>(m_data.size()) / newSize;

    for (int i = 0; i < newSize; ++i) {
        double srcPos = i * ratio;
        int idx = static_cast<int>(srcPos);
        double frac = srcPos - idx;

        if (idx + 1 < m_data.size()) {
            result[i] = m_data[idx] * (1.0 - frac) + m_data[idx + 1] * frac;
        } else if (idx < m_data.size()) {
            result[i] = m_data[idx];
        } else {
            result[i] = 0.0f;
        }
    }

    emit processed();
    return result;
}

}} // ks::audio
