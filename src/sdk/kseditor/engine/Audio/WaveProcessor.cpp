#include "WaveProcessor.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QtMath>
#include <cmath>

WaveProcessor::WaveProcessor(QObject *parent)
    : QObject(parent)
    , m_sampleHold(0.0f)
{
    m_format.setChannelCount(2);
    m_format.setSampleRate(44100);
    m_format.setSampleFormat(QAudioFormat::Float);
}

WaveProcessor::~WaveProcessor() = default;

bool WaveProcessor::loadWav(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error("Cannot open file: " + filePath);
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    WavHeader header;
    stream.readRawData(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    if (QString::fromLatin1(header.riff, 4) != "RIFF" || QString::fromLatin1(header.wave, 4) != "WAVE") {
        file.close();
        emit error("Invalid WAV file");
        return false;
    }

    m_format.setChannelCount(header.channels);
    m_format.setSampleRate(header.sampleRate);

    if (header.bitsPerSample == 16) {
        m_format.setSampleFormat(QAudioFormat::Int16);
    } else if (header.bitsPerSample == 32) {
        m_format.setSampleFormat(QAudioFormat::Float);
    }

    m_samples.clear();
    qint64 bytesRemaining = header.dataSize;
    int bytesPerSample = header.bitsPerSample / 8 * header.channels;

    while (bytesRemaining > 0) {
        char sampleBytes[8] = {0};
        int bytesToRead = qMin(bytesRemaining, static_cast<qint64>(bytesPerSample));
        stream.readRawData(sampleBytes, bytesToRead);

        float sample = samplesToFloat(sampleBytes, header.bitsPerSample / 8);
        m_samples.append(sample);

        bytesRemaining -= bytesPerSample;
    }

    file.close();
    emit loadComplete();
    return true;
}

bool WaveProcessor::saveWav(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit error("Cannot create file: " + filePath);
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    int channels = m_format.channelCount();
    int sampleRate = m_format.sampleRate();
    int bitsPerSample = 16;
    int bytesPerSample = bitsPerSample / 8 * channels;
    qint64 dataSize = m_samples.size() * bytesPerSample;
    qint64 fileSize = 36 + dataSize;

    WavHeader header;
    memcpy(header.riff, "RIFF", 4);
    header.fileSize = fileSize;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmtSize = 16;
    header.audioFormat = 1;
    header.channels = channels;
    header.sampleRate = sampleRate;
    header.byteRate = sampleRate * bytesPerSample;
    header.blockAlign = bytesPerSample;
    header.bitsPerSample = bitsPerSample;
    memcpy(header.data, "data", 4);
    header.dataSize = dataSize;

    stream.writeRawData(reinterpret_cast<const char*>(&header), sizeof(WavHeader));

    for (float sample : m_samples) {
        char bytes[4] = {0};
        floatToSamples(qBound(-1.0f, sample, 1.0f), bytes, 2);
        stream.writeRawData(bytes, 2 * channels);
    }

    file.close();
    emit saveComplete();
    return true;
}

void WaveProcessor::clear()
{
    pushUndoState();
    m_samples.clear();
    emit samplesModified();
}

qint64 WaveProcessor::getDurationMs() const
{
    if (m_samples.isEmpty()) return 0;
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    if (sampleRate == 0 || channels == 0) return 0;
    return (m_samples.size() / channels * 1000) / sampleRate;
}

float WaveProcessor::samplesToFloat(const char *bytes, int size)
{
    if (size == 1) {
        return (static_cast<unsigned char>(bytes[0]) - 128.0f) / 128.0f;
    } else if (size == 2) {
        short val = *reinterpret_cast<const short*>(bytes);
        return val / 32768.0f;
    } else if (size == 4) {
        return *reinterpret_cast<const float*>(bytes);
    }
    return 0.0f;
}

void WaveProcessor::floatToSamples(float value, char *bytes, int size)
{
    if (size == 1) {
        bytes[0] = static_cast<char>(value * 127.0f + 127.0f);
    } else if (size == 2) {
        short val = static_cast<short>(value * 32767.0f);
        memcpy(bytes, &val, 2);
    } else if (size == 4) {
        memcpy(bytes, &value, 4);
    }
}

void WaveProcessor::reverse()
{
    pushUndoState();
    std::reverse(m_samples.begin(), m_samples.end());
    emit samplesModified();
}

void WaveProcessor::fadeIn(int startMs, int durationMs)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int startSample = (startMs * sampleRate) / 1000 * channels;
    int durationSamples = (durationMs * sampleRate) / 1000 * channels;

    for (int i = 0; i < durationSamples && (startSample + i) < m_samples.size(); ++i) {
        float progress = static_cast<float>(i) / durationSamples;
        m_samples[startSample + i] *= progress;
    }
    emit samplesModified();
}

void WaveProcessor::fadeOut(int endMs, int durationMs)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int endSample = (endMs * sampleRate) / 1000 * channels;
    int durationSamples = (durationMs * sampleRate) / 1000 * channels;
    int startSample = qMax(0, endSample - durationSamples);

    for (int i = 0; i < durationSamples && (startSample + i) < m_samples.size(); ++i) {
        float progress = 1.0f - (static_cast<float>(i) / durationSamples);
        m_samples[startSample + i] *= progress;
    }
    emit samplesModified();
}

void WaveProcessor::normalize(float level)
{
    if (m_samples.isEmpty()) return;
    pushUndoState();

    float maxAmp = 0.0f;
    for (float s : m_samples) maxAmp = qMax(maxAmp, qAbs(s));

    if (maxAmp > 0.0f) {
        float scale = level / maxAmp;
        for (float &s : m_samples) s *= scale;
    }
    emit samplesModified();
}

void WaveProcessor::amplify(float factor)
{
    pushUndoState();
    for (float &s : m_samples) s *= factor;
    emit samplesModified();
}

void WaveProcessor::invert()
{
    pushUndoState();
    for (float &s : m_samples) s = -s;
    emit samplesModified();
}

void WaveProcessor::silence(int startMs, int endMs)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int startSample = (startMs * sampleRate * channels) / 1000;
    int endSample = (endMs * sampleRate * channels) / 1000;

    for (int i = startSample; i < endSample && i < m_samples.size(); ++i) {
        m_samples[i] = 0.0f;
    }
    emit samplesModified();
}

void WaveProcessor::insertSilence(int positionMs, int durationMs)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int positionSample = (positionMs * sampleRate * channels) / 1000;
    int durationSamples = (durationMs * sampleRate * channels) / 1000;

    QVector<float> silence(durationSamples, 0.0f);
    m_samples = m_samples.mid(0, positionSample) + silence + m_samples.mid(positionSample);
    emit samplesModified();
}

void WaveProcessor::deleteRegion(int startMs, int endMs)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int startSample = (startMs * sampleRate * channels) / 1000;
    int endSample = (endMs * sampleRate * channels) / 1000;

    m_samples = m_samples.mid(0, startSample) + m_samples.mid(endSample);
    emit samplesModified();
}

void WaveProcessor::copyRegion(int startMs, int endMs)
{
    m_clipboard = getRegion(startMs, endMs);
}

void WaveProcessor::pasteRegion(int positionMs)
{
    if (m_clipboard.isEmpty()) return;
    pushUndoState();

    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int positionSample = (positionMs * sampleRate * channels) / 1000;

    m_samples = m_samples.mid(0, positionSample) + m_clipboard + m_samples.mid(positionSample);
    emit samplesModified();
}

void WaveProcessor::processSamplesMono(std::function<void(float&)> processor)
{
    int channels = m_format.channelCount();
    if (channels == 1) {
        for (float &s : m_samples) processor(s);
    } else if (channels == 2) {
        for (int i = 0; i < m_samples.size(); i += 2) {
            float mono = (m_samples[i] + m_samples[i + 1]) * 0.5f;
            processor(mono);
            m_samples[i] = mono;
            m_samples[i + 1] = mono;
        }
    }
    emit samplesModified();
}

void WaveProcessor::processSamples(std::function<void(float&, float&)> processor)
{
    int channels = m_format.channelCount();
    if (channels == 2) {
        for (int i = 0; i < m_samples.size(); i += 2) {
            processor(m_samples[i], m_samples[i + 1]);
        }
    }
    emit samplesModified();
}

void WaveProcessor::applyLowPassFilter(float cutoffFreq, float resonance)
{
    pushUndoState();
    float rc = 1.0f / (2.0f * M_PI * cutoffFreq);
    float dt = 1.0f / m_format.sampleRate();
    float alpha = dt / (rc + dt);

    float lastSample = 0.0f;
    processSamplesMono([&](float &s) {
        lastSample = lastSample + alpha * (s - lastSample);
        s = lastSample;
    });
}

void WaveProcessor::applyHighPassFilter(float cutoffFreq, float resonance)
{
    pushUndoState();
    float rc = 1.0f / (2.0f * M_PI * cutoffFreq);
    float dt = 1.0f / m_format.sampleRate();
    float alpha = rc / (rc + dt);

    float lastSample = 0.0f;
    float lastInput = 0.0f;
    processSamplesMono([&](float &s) {
        lastSample = alpha * (lastSample + s - lastInput);
        lastInput = s;
        s = lastSample;
    });
}

void WaveProcessor::applyBandPassFilter(float lowFreq, float highFreq)
{
    applyLowPassFilter(highFreq, 0.0f);
    QVector<float> temp = m_samples;
    applyHighPassFilter(lowFreq, 0.0f);
    for (int i = 0; i < m_samples.size(); ++i) {
        m_samples[i] = (m_samples[i] + temp[i]) * 0.5f;
    }
    emit samplesModified();
}

void WaveProcessor::applyNotchFilter(float freq, float bandwidth)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    float rc = 1.0f / (2.0f * 3.14159f * freq);
    float dt = 1.0f / sampleRate;
    float alpha = dt / (rc + dt);

    float prev = 0;
    for (float &s : m_samples) {
        float temp = s;
        s = alpha * (s - prev) + prev;
        prev = temp;
    }
    emit samplesModified();
}

void WaveProcessor::applyDelay(float delayMs, float feedback, float mix)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int delaySamples = (delayMs * sampleRate * channels) / 1000;

    if (delaySamples <= 0) return;

    QVector<float> delayed = m_samples;
    for (int i = delaySamples; i < m_samples.size(); ++i) {
        m_samples[i] += delayed[i - delaySamples] * feedback;
    }

    for (float &s : m_samples) {
        s = s * (1.0f - mix) + delayed[m_samples.indexOf(s)] * mix;
    }
    emit samplesModified();
}

void WaveProcessor::applyReverb(float roomSize, float damping, float wetDry)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int delaySamples = static_cast<int>(roomSize * sampleRate / 100.0f);

    if (delaySamples <= 0) return;

    QVector<float> delayed = m_samples;
    float dampFactor = 1.0f - damping * 0.5f;

    for (int i = delaySamples; i < m_samples.size(); ++i) {
        float feedback = delayed[i - delaySamples] * dampFactor * 0.7f;
        m_samples[i] = m_samples[i] * (1.0f - wetDry) + feedback * wetDry;
    }
    emit samplesModified();
}

void WaveProcessor::applyEcho(float delayMs, float feedback, float mix)
{
    applyDelay(delayMs, feedback, mix);
}

void WaveProcessor::applyChorus(float depth, float rate, float mix)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int baseDelay = static_cast<int>(20.0f * sampleRate / 1000.0f);
    int maxMod = static_cast<int>(depth * sampleRate / 1000.0f);

    QVector<float> output = m_samples;
    for (int i = baseDelay + maxMod; i < m_samples.size(); ++i) {
        int modDelay = static_cast<int>(maxMod * std::sin(2.0f * 3.14159f * rate * i / sampleRate));
        int readPos = i - baseDelay - modDelay;
        if (readPos >= 0 && readPos < m_samples.size()) {
            output[i] = m_samples[i] * (1.0f - mix) + m_samples[readPos] * mix;
        }
    }
    m_samples = output;
    emit samplesModified();
}

void WaveProcessor::applyFlanger(float depth, float rate, float mix)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int baseDelay = static_cast<int>(5.0f * sampleRate / 1000.0f);
    int maxMod = static_cast<int>(depth * sampleRate / 1000.0f);

    QVector<float> output = m_samples;
    for (int i = baseDelay + maxMod; i < m_samples.size(); ++i) {
        int modDelay = static_cast<int>(maxMod * std::sin(2.0f * 3.14159f * rate * i / sampleRate));
        int readPos = i - baseDelay - modDelay;
        if (readPos >= 0 && readPos < m_samples.size()) {
            output[i] = m_samples[i] * (1.0f - mix) + m_samples[readPos] * mix * 0.7f;
        }
    }
    m_samples = output;
    emit samplesModified();
}

void WaveProcessor::applyCompressor(float threshold, float ratio, float attack, float release, float makeupGain)
{
    pushUndoState();

    float attackCoef = exp(-1.0f / (attack * m_format.sampleRate() * 0.001f));
    float releaseCoef = exp(-1.0f / (release * m_format.sampleRate() * 0.001f));

    for (float &s : m_samples) {
        float level = qAbs(s);
        if (level > threshold) {
            float gain = threshold + (level - threshold) / ratio;
            s *= (gain / level);
        }
        s *= makeupGain;
    }
    emit samplesModified();
}

void WaveProcessor::applyLimiter(float threshold, float release)
{
    pushUndoState();
    float releaseCoef = exp(-1.0f / (release * m_format.sampleRate() * 0.001f));

    for (float &s : m_samples) {
        if (qAbs(s) > threshold) {
            s = s > 0 ? threshold : -threshold;
        }
    }
    emit samplesModified();
}

void WaveProcessor::resample(int newSampleRate)
{
    if (newSampleRate == m_format.sampleRate()) return;
    pushUndoState();

    float ratio = static_cast<float>(newSampleRate) / m_format.sampleRate();
    QVector<float> newSamples;

    int channels = m_format.channelCount();
    for (int i = 0; i < m_samples.size(); i += channels) {
        for (int c = 0; c < channels; ++c) {
            float samplePos = i + c + ratio;
            int idx0 = static_cast<int>(samplePos);
            int idx1 = idx0 + channels;
            float frac = samplePos - idx0;

            if (idx1 < m_samples.size()) {
                float s0 = m_samples[idx0];
                float s1 = m_samples[idx1];
                newSamples.append(s0 + frac * (s1 - s0));
            } else if (idx0 < m_samples.size()) {
                newSamples.append(m_samples[idx0]);
            }
        }
    }

    m_samples = newSamples;
    m_format.setSampleRate(newSampleRate);
    emit samplesModified();
}

void WaveProcessor::convertChannels(int channels)
{
    if (channels == m_format.channelCount()) return;
    pushUndoState();

    if (channels == 1 && m_format.channelCount() == 2) {
        QVector<float> mono;
        for (int i = 0; i < m_samples.size(); i += 2) {
            mono.append((m_samples[i] + m_samples[i + 1]) * 0.5f);
        }
        m_samples = mono;
    } else if (channels == 2 && m_format.channelCount() == 1) {
        QVector<float> stereo;
        for (float s : m_samples) {
            stereo.append(s);
            stereo.append(s);
        }
        m_samples = stereo;
    }

    m_format.setChannelCount(channels);
    emit samplesModified();
}

void WaveProcessor::convertBitDepth(int bits)
{
    pushUndoState();
    float maxVal = std::pow(2.0f, bits - 1) - 1.0f;

    for (float &s : m_samples) {
        float normalized = s * maxVal;
        float quantized = std::round(normalized);
        s = quantized / maxVal;
    }

    // Qt6: QAudioFormat uses integer sample format instead of setSampleSize
    emit samplesModified();
}

QVector<float> WaveProcessor::getRegion(int startMs, int endMs)
{
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int startSample = (startMs * sampleRate * channels) / 1000;
    int endSample = (endMs * sampleRate * channels) / 1000;

    return m_samples.mid(startSample, endSample - startSample);
}

void WaveProcessor::setRegion(int startMs, const QVector<float> &samples)
{
    pushUndoState();
    int sampleRate = m_format.sampleRate();
    int channels = m_format.channelCount();
    int startSample = (startMs * sampleRate * channels) / 1000;

    for (int i = 0; i < samples.size() && (startSample + i) < m_samples.size(); ++i) {
        m_samples[startSample + i] = samples[i];
    }
    emit samplesModified();
}