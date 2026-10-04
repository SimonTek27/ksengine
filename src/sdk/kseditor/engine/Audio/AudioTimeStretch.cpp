#include "AudioTimeStretch.h"
#include <QDebug>
#include <QtMath>
#include <cmath>

AudioTimeStretch::AudioTimeStretch(QObject *parent)
    : QObject(parent)
    , m_mode(MODE_PHASE_VOCODER)
    , m_quality(QUALITY_NORMAL)
    , m_pitchShiftSemitones(0.0f)
{
}

AudioTimeStretch::~AudioTimeStretch() = default;

QVector<float> AudioTimeStretch::stretch(const QVector<float> &samples, int channels,
                                          int sampleRate, float stretchRatio)
{
    if (stretchRatio <= 0.0f) return samples;
    if (qFuzzyCompare(stretchRatio, 1.0f)) return samples;

    switch (m_mode) {
        case MODE_PRESET:
            return simpleStretch(samples, channels, stretchRatio);
        case MODE_PHASE_VOCODER:
        case MODE_OLAE:
        case MODE_HRTF:
        default: {
            int fftSize = (m_quality == QUALITY_HIGH) ? 4096 : (m_quality == QUALITY_FAST) ? 512 : 2048;
            int hopSize = fftSize / 4;

QVector<AudioTimeStretch::AnalysisFrame> frames = analyzeFrames(samples, channels, fftSize, hopSize);
    phaseVocoderProcess(frames, stretchRatio, fftSize, hopSize);

    int outputLength = samples.size() * stretchRatio;
    QVector<float> output = synthesizeFrames(frames, channels, fftSize, hopSize, outputLength);

            return output;
        }
    }
}

QVector<float> AudioTimeStretch::compress(const QVector<float> &samples, int channels,
                                           int sampleRate, float compressRatio)
{
    return stretch(samples, channels, sampleRate, 1.0f / compressRatio);
}

QVector<float> AudioTimeStretch::simpleStretch(const QVector<float> &samples, int channels,
                                                 float ratio, int fftSize)
{
    if (ratio == 1.0f) return samples;

    int inputSamples = samples.size() / channels;
    int outputSamples = static_cast<int>(inputSamples / ratio);

    QVector<float> output(outputSamples * channels);

    float position = 0.0f;
    for (int i = 0; i < outputSamples; ++i) {
        float srcPos = position;
        int idx0 = static_cast<int>(srcPos) * channels;
        float frac = srcPos - static_cast<int>(srcPos);

        for (int c = 0; c < channels; ++c) {
            int idx1 = idx0 + channels + c;
            if (idx1 < samples.size()) {
                float s0 = samples[idx0 + c];
                float s1 = samples[idx1];
                output[i * channels + c] = s0 + frac * (s1 - s0);
            } else if (idx0 + c < samples.size()) {
                output[i * channels + c] = samples[idx0 + c];
            }
        }

        position += ratio;
    }

    return output;
}

QVector<AudioTimeStretch::AnalysisFrame> AudioTimeStretch::analyzeFrames(
    const QVector<float> &samples, int channels, int fftSize, int hopSize)
{
    QVector<AnalysisFrame> frames;

    int numFrames = (samples.size() / channels - fftSize) / hopSize + 1;

    for (int f = 0; f < numFrames; ++f) {
        AnalysisFrame frame;
        frame.magnitudes.resize(fftSize / 2);
        frame.phases.resize(fftSize / 2);
        frame.prevPhases.resize(fftSize / 2);
        frame.peak = 0.0f;

        for (int c = 0; c < channels; ++c) {
            QVector<float> channelSamples(fftSize, 0.0f);
            for (int i = 0; i < fftSize && (f * hopSize + i) * channels + c < samples.size(); ++i) {
                channelSamples[i] = samples[(f * hopSize + i) * channels + c];
            }

            for (int i = 0; i < fftSize; ++i) {
                float window = getWindowValue(i, fftSize);
                channelSamples[i] *= window;
            }

            for (int i = 0; i < fftSize / 2; ++i) {
                float mag = qAbs(channelSamples[i]);
                frame.magnitudes[i] = qMax(frame.magnitudes[i], mag);
                frame.peak = qMax(frame.peak, mag);
            }
        }

        frames.append(frame);
    }

    return frames;
}

QVector<float> AudioTimeStretch::synthesizeFrames(const QVector<AnalysisFrame> &frames,
                                                   int channels, int fftSize, int hopSize,
                                                   int outputLength)
{
    QVector<float> output(outputLength * channels, 0.0f);
    QVector<float> overlapBuffer(fftSize * channels, 0.0f);

    for (int f = 0; f < frames.size(); ++f) {
        const AnalysisFrame &frame = frames[f];

        QVector<float> fftBuffer(fftSize, 0.0f);
        for (int i = 0; i < fftSize / 2; ++i) {
            float mag = frame.magnitudes[i];
            float phase = frame.phases[i];
            float real = mag * qCos(phase);
            float imag = mag * qSin(phase);

            fftBuffer[i] = real;
            fftBuffer[fftSize - 1 - i] = real;
            fftBuffer[fftSize / 2 + i] = imag;
        }

        for (int c = 0; c < channels; ++c) {
            for (int i = 0; i < fftSize; ++i) {
                float window = getWindowValue(i, fftSize);
                float sample = fftBuffer[i % (fftSize / 2)] * window;
                int outIdx = f * hopSize * channels + i * channels + c;
                if (outIdx < output.size()) {
                    output[outIdx] += sample;
                }
            }
        }
    }

    float maxVal = 0.0f;
    for (float s : output) maxVal = qMax(maxVal, qAbs(s));
    if (maxVal > 1.0f) {
        for (float &s : output) s /= maxVal;
    }

    return output;
}

void AudioTimeStretch::phaseVocoderProcess(QVector<AnalysisFrame> &frames, float stretchRatio,
                                            int fftSize, int hopSize)
{
    int outputHopSize = static_cast<int>(hopSize * stretchRatio);
    QVector<float> framePhases(fftSize / 2, 0.0f);

    for (int f = 1; f < frames.size(); ++f) {
        float phaseAccum = 0.0f;

        for (int i = 0; i < fftSize / 2; ++i) {
            float expectedPhase = framePhases[i] + 2.0f * M_PI * hopSize * i / fftSize;
            float currentPhase = frames[f].phases[i];
            float phaseDiff = currentPhase - expectedPhase;

            while (phaseDiff > M_PI) phaseDiff -= 2.0f * M_PI;
            while (phaseDiff < -M_PI) phaseDiff += 2.0f * M_PI;

            phaseAccum += phaseDiff;
            framePhases[i] = expectedPhase + phaseAccum;

            frames[f].phases[i] = framePhases[i];
        }
    }
}

QVector<float> AudioTimeStretch::olaeProcess(const QVector<float> &input, float ratio)
{
    int olaeSize = 1024;
    int olaeHop = 256;
    int outputSize = static_cast<int>(input.size() * ratio);

    QVector<float> output(outputSize, 0.0f);
    QVector<float> OLAESums(olaeSize, 0.0f);
    QVector<int> OLAECounters(outputSize, 0);

    int position = 0;
    int outPosition = 0;

    while (position < input.size() && outPosition < outputSize) {
        for (int i = 0; i < olaeSize && position + i < input.size(); ++i) {
            OLAESums[i] += input[position + i];
        }

        for (int i = 0; i < olaeHop && outPosition + i < outputSize; ++i) {
            float sum = 0.0f;
            int count = 0;
            for (int j = 0; j < olaeSize; j += olaeHop) {
                int srcIdx = (outPosition + i) - j;
                if (srcIdx >= 0 && srcIdx < outputSize) {
                    sum += OLAESums[j];
                    count++;
                }
            }
            if (count > 0) {
                output[outPosition + i] = sum / count;
            }
        }

        position += olaeHop;
        outPosition += static_cast<int>(olaeHop * ratio);

        for (int i = 0; i < olaeSize - olaeHop; ++i) {
            OLAESums[i] = OLAESums[i + olaeHop];
        }
        for (int i = olaeSize - olaeHop; i < olaeSize; ++i) {
            OLAESums[i] = 0.0f;
        }
    }

    return output;
}

float AudioTimeStretch::getWindowValue(int i, int size)
{
    float t = 2.0f * M_PI * i / (size - 1);
    return 0.5f * (1.0f - qCos(t));
}

void AudioTimeStretch::setPitchShift(float semitones)
{
    m_pitchShiftSemitones = semitones;
}

PitchCorrector::PitchCorrector(QObject *parent)
    : QObject(parent)
    , m_correctionStrength(0.5f)
    , m_formantPreservation(true)
{
    m_scale << C << D << E << F << G << A << B;
}

PitchCorrector::~PitchCorrector() = default;

void PitchCorrector::setScale(const QString &scale)
{
    m_scale.clear();
    if (scale.contains("major")) {
        m_scale << C << D << E << F << G << A << B;
    } else if (scale.contains("minor")) {
        m_scale << C << D << D_SHARP << F << G << G_SHARP << A_SHARP;
    } else if (scale.contains("chromatic")) {
        for (int i = 0; i < 12; ++i) m_scale << static_cast<Note>(i);
    } else {
        m_scale << C << D << E << F << G << A << B;
    }
}

void PitchCorrector::setCorrectionStrength(float strength)
{
    m_correctionStrength = qBound(0.0f, strength, 1.0f);
}

void PitchCorrector::setFormantPreservation(bool enabled)
{
    m_formantPreservation = enabled;
}

QVector<float> PitchCorrector::correct(const QVector<float> &samples, int channels, int sampleRate)
{
    QVector<float> corrected = samples;

    float detectedPitch = detectPitch(samples, sampleRate);
    emit pitchDetected(detectedPitch);

    if (detectedPitch <= 0.0f) {
        return corrected;
    }

    Note note;
    int octave;
    float originalFreq = frequencyToNote(detectedPitch, octave);
    float targetFreq = closestScaleNote(detectedPitch, m_scale);

    float semitones = 12.0f * std::log(targetFreq / detectedPitch) / qLn(2.0f);
    semitones *= m_correctionStrength;

    float ratio = qPow(2.0f, semitones / 12.0f);

    AudioTimeStretch stretch;
    stretch.setMode(AudioTimeStretch::MODE_PHASE_VOCODER);
    corrected = stretch.stretch(samples, channels, sampleRate, 1.0f / ratio);

    if (m_formantPreservation) {
        float formantShift = qPow(ratio, 0.5f);
        for (int i = 0; i < corrected.size(); i += channels) {
            for (int ch = 0; ch < channels; ++ch) {
                corrected[i + ch] *= formantShift;
            }
        }
    }

    emit pitchCorrected(detectedPitch, targetFreq);
    return corrected;
}

float PitchCorrector::detectPitch(const QVector<float> &samples, int sampleRate)
{
    if (samples.isEmpty()) return 0.0f;

    float correlation = autoCorrelate(samples, sampleRate);
    if (correlation <= 0.0f) return 0.0f;

    return correlation;
}

float PitchCorrector::autoCorrelate(const QVector<float> &samples, int sampleRate)
{
    int n = samples.size();
    float bestCorrelation = 0.0f;
    int bestLag = 0;

    for (int lag = sampleRate / 1000; lag < sampleRate / 50; ++lag) {
        float sum = 0.0f;
        int count = 0;

        for (int i = 0; i < n - lag; ++i) {
            sum += samples[i] * samples[i + lag];
            count++;
        }

        float correlation = count > 0 ? sum / count : 0.0f;

        if (correlation > bestCorrelation) {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    if (bestLag > 0) {
        return static_cast<float>(sampleRate) / bestLag;
    }

    return 0.0f;
}

float PitchCorrector::noteToFrequency(Note note, int octave)
{
    int semitoneIndex = static_cast<int>(note);
    int midiNote = 12 + semitoneIndex + (octave - 1) * 12;
    return 440.0f * qPow(2.0f, (midiNote - 69) / 12.0f);
}

PitchCorrector::Note PitchCorrector::frequencyToNote(float frequency, int &octave)
{
    if (frequency <= 0.0f) return C;

    float semitones = 12.0f * std::log(frequency / 440.0f) / qLn(2.0f);
    int midiNote = static_cast<int>(qRound(semitones + 69));

    octave = (midiNote / 12) - 1;
    Note note = static_cast<Note>(midiNote % 12);

    return note;
}

float PitchCorrector::closestScaleNote(float frequency, const QVector<Note> &scale)
{
    Note note;
    int octave;
    note = frequencyToNote(frequency, octave);

    int closestIndex = 0;
    int closestDistance = 12;

    for (int i = 0; i < scale.size(); ++i) {
        int distance = qAbs(static_cast<int>(note) - static_cast<int>(scale[i]));
        if (distance < closestDistance) {
            closestDistance = distance;
            closestIndex = i;
        }
    }

    return noteToFrequency(scale[closestIndex], octave);
}