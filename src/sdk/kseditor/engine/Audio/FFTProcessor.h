#ifndef FFT_PROCESSOR_H
#define FFT_PROCESSOR_H

#include <QObject>
#include <QVector>
#include <complex>
#include <QtMath>

namespace ks { namespace audio {

class FFTProcessor : public QObject
{
    Q_OBJECT

public:
    explicit FFTProcessor(QObject *parent = nullptr);
    ~FFTProcessor();

    void setFFTSize(int size);
    int getFFTSize() const { return m_fftSize; }

    void setWindowType(int type);
    int getWindowType() const { return m_windowType; }

    QVector<float> computeSpectrum(const QVector<float> &samples);
    QVector<float> computeMagnitudes(const QVector<float> &samples);
    QVector<float> computeLogMagnitudes(const QVector<float> &samples);
    QVector<float> computePhase(const QVector<float> &samples);

    QVector<float> applyWindow(const QVector<float> &samples);

    QVector<float> getFrequencyBands(const QVector<float> &samples, int bandCount);
    QVector<float> getMelSpectrum(const QVector<float> &samples, int melBands);

    QVector<float> generateNoiseProfile(const QVector<float> &noiseSamples);
    QVector<float> spectralSubtraction(const QVector<float> &samples, const QVector<float> &noiseProfile, float reductionDb = -15.0f, float smoothing = 0.3f);
    QVector<float> spectralEdit(const QVector<float> &samples, int sampleRate, int startMs, int endMs, float lowHz, float highHz, float gainDb);
    QVector<float> deHum(const QVector<float> &samples, int sampleRate, float humFreq = 50.0f, float bw = 5.0f, int harmonics = 5);
    QVector<float> deClick(const QVector<float> &samples, float threshold = 0.5f);

    static float hzToMel(float hz);
    static float melToHz(float mel);

    void fft(QVector<std::complex<float>> &data);
    void ifft(QVector<std::complex<float>> &data);
    void fftReal(const QVector<float> &input, QVector<std::complex<float>> &output);

signals:
    void spectrumComputed(const QVector<float> &magnitudes);
    void analysisComplete();

private:
    void initFFT();
    void destroyFFT();

    QVector<float> m_hannWindow;
    QVector<float> m_hammingWindow;
    QVector<float> m_blackmanWindow;

    int m_fftSize;
    int m_windowType;

    QVector<std::complex<float>> m_fftBuffer;

    float m_noiseFloor;
    float m_spectralFloor;
    int m_sampleRate = 44100;
};

} // namespace audio
} // namespace ks

#endif // FFT_PROCESSOR_H
