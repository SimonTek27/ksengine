#include "FFTProcessor.h"
#include <QDebug>
#include <QtMath>
#include <cmath>
#include <complex>

using QComplex = std::complex<float>;

namespace ks { namespace audio {

FFTProcessor::FFTProcessor(QObject *parent)
    : QObject(parent)
    , m_fftSize(2048)
    , m_windowType(0)
    , m_noiseFloor(-90.0f)
    , m_spectralFloor(-90.0f)
{
    initFFT();
}

FFTProcessor::~FFTProcessor()
{
    destroyFFT();
}

void FFTProcessor::initFFT()
{
    m_hannWindow.resize(m_fftSize);
    m_hammingWindow.resize(m_fftSize);
    m_blackmanWindow.resize(m_fftSize);

    for (int i = 0; i < m_fftSize; ++i) {
        m_hannWindow[i] = 0.5f * (1.0f - qCos(2.0f * M_PI * i / (m_fftSize - 1)));
        m_hammingWindow[i] = 0.54f - 0.46f * qCos(2.0f * M_PI * i / (m_fftSize - 1));
        m_blackmanWindow[i] = 0.42f - 0.5f * qCos(2.0f * M_PI * i / (m_fftSize - 1))
                           + 0.08f * qCos(4.0f * M_PI * i / (m_fftSize - 1));
    }

    m_fftBuffer.resize(m_fftSize);
}

void FFTProcessor::destroyFFT()
{
    m_hannWindow.clear();
    m_hammingWindow.clear();
    m_blackmanWindow.clear();
    m_fftBuffer.clear();
}

void FFTProcessor::setFFTSize(int size)
{
    if (size < 64 || size > 16384) return;

    m_fftSize = size;
    initFFT();
}

void FFTProcessor::setWindowType(int type)
{
    m_windowType = type;
}

QVector<float> FFTProcessor::applyWindow(const QVector<float> &samples)
{
    const QVector<float> &window = (m_windowType == 0) ? m_hannWindow
                                   : (m_windowType == 1) ? m_hammingWindow
                                   : m_blackmanWindow;

    QVector<float> result(m_fftSize);
    int windowSize = qMin(samples.size(), m_fftSize);

    for (int i = 0; i < windowSize; ++i) {
        result[i] = samples[i] * window[i];
    }
    for (int i = windowSize; i < m_fftSize; ++i) {
        result[i] = 0.0f;
    }

    return result;
}

void FFTProcessor::fft(QVector<QComplex> &data)
{
    int n = data.size();
    if (n <= 1) return;

    QVector<QComplex> result(n);
    int bits = static_cast<int>(std::floor(std::log(static_cast<double>(n)) / std::log(2.0) + 0.5));
    int j = 0;

    for (int i = 0; i < n - 1; ++i) {
        if (i < j) {
            qSwap(data[i], data[j]);
        }
        int k = n / 2;
        while (k <= j) {
            j -= k;
            k /= 2;
        }
        j += k;
    }

    for (int len = 2; len <= n; len *= 2) {
        float angle = -2.0f * M_PI / len;
        QComplex wn(cos(angle), sin(angle));

        for (int i = 0; i < n; i += len) {
            QComplex w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                QComplex u = data[i + k];
                QComplex t = w * data[i + k + len / 2];
                data[i + k] = u + t;
                data[i + k + len / 2] = u - t;
                w *= wn;
            }
        }
    }
}

void FFTProcessor::ifft(QVector<QComplex> &data)
{
    int n = data.size();
    for (int i = 0; i < n; ++i) {
        data[i] = QComplex(data[i].real(), -data[i].imag());
    }

    fft(data);

    for (int i = 0; i < n; ++i) {
        data[i] = QComplex(data[i].real() / n, data[i].imag() / n);
    }
}

void FFTProcessor::fftReal(const QVector<float> &input, QVector<QComplex> &output)
{
    output.resize(m_fftSize);

    for (int i = 0; i < m_fftSize; ++i) {
        if (i < input.size()) {
            output[i] = QComplex(input[i], 0.0f);
        } else {
            output[i] = QComplex(0.0f, 0.0f);
        }
    }

    fft(output);
}

QVector<float> FFTProcessor::computeSpectrum(const QVector<float> &samples)
{
    QVector<float> windowed = applyWindow(samples);
    QVector<QComplex> fftData;
    fftReal(windowed, fftData);

    QVector<float> magnitudes(m_fftSize / 2);

    for (int i = 0; i < m_fftSize / 2; ++i) {
        magnitudes[i] = sqrt(fftData[i].real() * fftData[i].real()
                           + fftData[i].imag() * fftData[i].imag()) / m_fftSize;
    }

    return magnitudes;
}

QVector<float> FFTProcessor::computeMagnitudes(const QVector<float> &samples)
{
    return computeSpectrum(samples);
}

QVector<float> FFTProcessor::computeLogMagnitudes(const QVector<float> &samples)
{
    QVector<float> linear = computeSpectrum(samples);
    QVector<float> logMags(m_fftSize / 2);

    for (int i = 0; i < linear.size(); ++i) {
        float val = linear[i];
        if (val < 1e-10f) val = 1e-10f;
        logMags[i] = 20.0f * std::log(val) / qLn(10.0f);

        if (logMags[i] < m_spectralFloor) {
            logMags[i] = m_spectralFloor;
        }
    }

    return logMags;
}

QVector<float> FFTProcessor::computePhase(const QVector<float> &samples)
{
    QVector<float> windowed = applyWindow(samples);
    QVector<QComplex> fftData;
    fftReal(windowed, fftData);

    QVector<float> phases(m_fftSize / 2);

    for (int i = 0; i < m_fftSize / 2; ++i) {
        phases[i] = qAtan2(fftData[i].imag(), fftData[i].real());
    }

    return phases;
}

QVector<float> FFTProcessor::getFrequencyBands(const QVector<float> &samples, int bandCount)
{
    QVector<float> spectrum = computeLogMagnitudes(samples);
    QVector<float> bands(bandCount);

    int binsPerBand = (m_fftSize / 2) / bandCount;
    if (binsPerBand == 0) binsPerBand = 1;

    for (int b = 0; b < bandCount; ++b) {
        float sum = 0.0f;
        int startBin = b * binsPerBand;
        int endBin = qMin(startBin + binsPerBand, spectrum.size());

        for (int i = startBin; i < endBin; ++i) {
            sum += spectrum[i];
        }
        bands[b] = sum / (endBin - startBin);
    }

    return bands;
}

float FFTProcessor::hzToMel(float hz)
{
    return 2595.0f * std::log(1.0f + hz / 700.0f) / qLn(10.0f);
}

float FFTProcessor::melToHz(float mel)
{
    return 700.0f * (qPow(10.0f, mel / 2595.0f) - 1.0f);
}

QVector<float> FFTProcessor::getMelSpectrum(const QVector<float> &samples, int melBands)
{
    QVector<float> spectrum = computeLogMagnitudes(samples);
    QVector<float> melBandsResult(melBands);

    float minMel = hzToMel(0.0f);
    float maxMel = hzToMel(m_sampleRate / 2);
    float binWidth = (m_fftSize / 2) / (m_sampleRate / 2.0f);

    for (int m = 0; m < melBands; ++m) {
        float melLow = minMel + m * (maxMel - minMel) / (melBands + 1);
        float melHigh = minMel + (m + 2) * (maxMel - minMel) / (melBands + 1);

        float fLow = melToHz(melLow);
        float fHigh = melToHz(melHigh);

        int binLow = qMax(0, int(fLow * binWidth));
        int binHigh = qMin(spectrum.size() - 1, int(fHigh * binWidth));

        float sum = 0.0f;
        int count = binHigh - binLow + 1;
        for (int i = binLow; i <= binHigh; ++i) {
            sum += spectrum[i];
        }

        melBandsResult[m] = count > 0 ? sum / count : 0.0f;
    }

    return melBandsResult;
}

QVector<float> FFTProcessor::generateNoiseProfile(const QVector<float> &noiseSamples)
{
    QVector<QComplex> fftData;
    fftReal(noiseSamples, fftData);

    QVector<float> profile(m_fftSize / 2);

    for (int i = 0; i < m_fftSize / 2; ++i) {
        float mag = sqrt(fftData[i].real() * fftData[i].real()
                       + fftData[i].imag() * fftData[i].imag()) / m_fftSize;
        profile[i] = mag * mag;
    }

    return profile;
}

QVector<float> FFTProcessor::spectralSubtraction(const QVector<float> &samples,
                                                  const QVector<float> &noiseProfile, float reductionDb, float smoothing)
{
    float reductionLin = qPow(10.0f, reductionDb / 20.0f);
    float alpha = qBound(0.0f, smoothing, 0.95f);
    QVector<float> windowed = applyWindow(samples);
    QVector<QComplex> fftData;
    fftReal(windowed, fftData);
    QVector<float> result(samples.size());
    static QVector<float> prevGain;
    if (prevGain.size() != m_fftSize/2) prevGain.fill(1.0f, m_fftSize/2);
    for (int i = 0; i < m_fftSize / 2; ++i) {
        float mag = sqrt(fftData[i].real() * fftData[i].real() + fftData[i].imag() * fftData[i].imag());
        float noiseMag = 0.0f;
        if (i < noiseProfile.size()) noiseMag = sqrt(noiseProfile[i]) * 2.0f * reductionLin;
        float targetGain = mag > 1e-10f ? qMax(0.0f, (mag - noiseMag) / mag) : 0.0f;
        float g = alpha * prevGain[i] + (1.0f - alpha) * targetGain;
        prevGain[i] = g;
        float newMag = mag * g;
        float phase = qAtan2(fftData[i].imag(), fftData[i].real());
        fftData[i] = QComplex(newMag * qCos(phase), newMag * qSin(phase));
        if (i > 0) fftData[m_fftSize - i] = QComplex(newMag * qCos(-phase), newMag * qSin(-phase));
    }
    ifft(fftData);
    for (int i = 0; i < result.size() && i < m_fftSize; ++i) result[i] = fftData[i].real();
    return result;
}

QVector<float> FFTProcessor::spectralEdit(const QVector<float> &samples, int sampleRate, int startMs, int endMs, float lowHz, float highHz, float gainDb)
{
    if (samples.isEmpty() || sampleRate <= 0) return samples;
    int hop = m_fftSize / 2;
    QVector<float> out = samples;
    float gainLin = qPow(10.0f, gainDb / 20.0f);
    int startSample = qBound(0, startMs * sampleRate / 1000, samples.size());
    int endSample = qBound(0, endMs * sampleRate / 1000, samples.size());
    if (endSample <= startSample) return samples;
    int binLow = qBound(0, int(lowHz * m_fftSize / sampleRate), m_fftSize/2 -1);
    int binHigh = qBound(0, int(highHz * m_fftSize / sampleRate), m_fftSize/2 -1);
    if (binLow > binHigh) qSwap(binLow, binHigh);
    for (int pos = startSample; pos < endSample; pos += hop) {
        int len = qMin(m_fftSize, samples.size() - pos);
        QVector<float> frame(m_fftSize, 0.0f);
        for (int i=0;i<len;++i) frame[i]=samples[pos+i];
        QVector<float> win = applyWindow(frame);
        QVector<QComplex> fd;
        fftReal(win, fd);
        for (int b=binLow;b<=binHigh && b < m_fftSize/2;++b){
            float mag = sqrt(fd[b].real()*fd[b].real()+fd[b].imag()*fd[b].imag());
            float phase = qAtan2(fd[b].imag(), fd[b].real());
            float newMag = mag * gainLin;
            fd[b]=QComplex(newMag*qCos(phase), newMag*qSin(phase));
            if (b>0) fd[m_fftSize-b]=QComplex(newMag*qCos(-phase), newMag*qSin(-phase));
        }
        ifft(fd);
        for (int i=0;i<hop && pos+i < out.size();++i){
            float w = 0.5f*(1.0f - qCos(2*M_PI*i/hop));
            out[pos+i]= out[pos+i]*(1.0f-w) + fd[i].real()*w;
        }
    }
    return out;
}

QVector<float> FFTProcessor::deHum(const QVector<float> &samples, int sampleRate, float humFreq, float bw, int harmonics)
{
    QVector<float> out = samples;
    for (int h=1; h<=harmonics; ++h){
        float f = humFreq * h;
        if (f > sampleRate/2 - bw) break;
        float omega = 2*M_PI*f/sampleRate;
        float alpha = qSin(omega)*qSin(bw*M_PI/sampleRate/2) / 1.0f;
        float b0=1, b1=-2*qCos(omega), b2=1, a0=1+alpha, a1=-2*qCos(omega), a2=1-alpha;
        float x1=0,x2=0,y1=0,y2=0;
        for (int i=0;i<out.size();++i){
            float x0=out[i];
            float y0 = (b0/a0)*x0 + (b1/a0)*x1 + (b2/a0)*x2 - (a1/a0)*y1 - (a2/a0)*y2;
            x2=x1; x1=x0; y2=y1; y1=y0;
            out[i]=y0;
        }
    }
    return out;
}

QVector<float> FFTProcessor::deClick(const QVector<float> &samples, float threshold)
{
    QVector<float> out = samples;
    float thr = qBound(0.05f, threshold, 0.99f);
    for (int i=1; i<out.size()-1; ++i){
        float d = qAbs(out[i] - 0.5f*(out[i-1]+out[i+1]));
        if (d > thr){
            out[i]=0.5f*(out[i-1]+out[i+1]);
        }
    }
    return out;
}

} // namespace audio
} // namespace ks
