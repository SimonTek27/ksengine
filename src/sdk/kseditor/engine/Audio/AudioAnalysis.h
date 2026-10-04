#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <complex>
#include <cmath>
#include <QVector3D>
#include <QMap>

namespace ks {

namespace audio {

class AudioFFT : public QObject
{
    Q_OBJECT
public:
    explicit AudioFFT(QObject* parent = nullptr) : QObject(parent) { m_window.resize(1024); }
    ~AudioFFT() {}

    void setSize(int size) { m_size = size; m_window.resize(size); generateHannWindow(); }
    int size() const { return m_size; }

    QVector<std::complex<float>> compute(const QVector<float>& input);
    QVector<float> magnitude(const QVector<std::complex<float>>& fft);
    QVector<float> phase(const QVector<std::complex<float>>& fft);

private:
    void generateHannWindow();

    int m_size = 1024;
    QVector<float> m_window;
};

class LoudnessMeter : public QObject
{
    Q_OBJECT
public:
    explicit LoudnessMeter(QObject* parent = nullptr) : QObject(parent) {}
    ~LoudnessMeter() {}

    struct LoudnessResult {
        float momentary;
        float shortTerm;
        float integrated;
        float truePeak;
        float lufs;
    };

    void setSampleRate(int rate) { m_sampleRate = rate; }
    void setBlockSize(int size) { m_blockSize = size; }

    LoudnessResult process(const QVector<float>& input);
    void reset();
    float getIntegratedLoudness() const { return m_integratedLoudness; }

private:
    float processBlock(const QVector<float>& block);

    int m_sampleRate = 48000;
    int m_blockSize = 4000;
    int m_hopSize = 1000;

    float m_momentary = -70.0f;
    float m_shortTerm = -70.0f;
    float m_integratedLoudness = -70.0f;
    float m_truePeak = -70.0f;

    QVector<float> m_shortTermBuffer;
    QVector<float> m_gatingBuffer;
};

class SpectrumAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit SpectrumAnalyzer(QObject* parent = nullptr) : QObject(parent) {}
    ~SpectrumAnalyzer() {}

    void setFFTSize(int size) { m_fftSize = size; }
    int fftSize() const { return m_fftSize; }

    void setWindowType(const QString& type) { m_windowType = type; }
    QString windowType() const { return m_windowType; }

    QVector<float> compute(const QVector<float>& input);

signals:
    void spectrumReady(const QVector<float>& spectrum);

private:
    int m_fftSize = 2048;
    QString m_windowType = "Hann";
};

class Spectrogram : public QObject
{
    Q_OBJECT
public:
    explicit Spectrogram(QObject* parent = nullptr) : QObject(parent) {}
    ~Spectrogram() {}

    void setFFTSize(int size) { m_fftSize = size; }
    void setHopSize(int size) { m_hopSize = size; }
    void setWindowSize(int size) { m_windowSize = size; }

    void compute(const QVector<float>& input, int sampleRate);
    QVector<QVector<float>> getSpectrogram() const { return m_spectrogram; }

    void setMinFreq(float freq) { m_minFreq = freq; }
    void setMaxFreq(float freq) { m_maxFreq = freq; }

signals:
    void computed();

private:
    int m_fftSize = 2048;
    int m_hopSize = 512;
    int m_windowSize = 2048;
    float m_minFreq = 20.0f;
    float m_maxFreq = 20000.0f;
    QVector<QVector<float>> m_spectrogram;
};

class AudioProfiler : public QObject
{
    Q_OBJECT
public:
    explicit AudioProfiler(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioProfiler() {}

    struct ProfilerEvent {
        QString id;
        QString name;
        quint64 startTime;
        quint64 duration;
        float cpuUsage;
        float memoryUsage;
    };

    struct ProfilerTrack {
        QString id;
        QString name;
        QVector<ProfilerEvent> events;
    };

    void startSession(const QString& sessionName);
    void endSession();

    void recordEvent(const QString& name, quint64 duration);
    QVector<ProfilerEvent> getEvents() const { return m_events; }
    QVector<ProfilerTrack> getTracks() const { return m_tracks; }

    QVector<ProfilerEvent> getBottlenecks(int count = 10) const;

signals:
    void sessionStarted(const QString& name);
    void sessionEnded();

private:
    QString m_currentSession;
    QVector<ProfilerEvent> m_events;
    QVector<ProfilerTrack> m_tracks;
    quint64 m_sessionStart = 0;
};

class SpatialAudioMapper : public QObject
{
    Q_OBJECT
public:
    explicit SpatialAudioMapper(QObject* parent = nullptr) : QObject(parent) {}
    ~SpatialAudioMapper() {}

    struct AudioZone {
        QString id;
        QString name;
        QVector3D position;
        QVector3D dimensions;
        float reverbMix;
        float occlusion;
    };

    struct Listener {
        QVector3D position;
        QVector3D forward;
        QVector3D up;
    };

    void addZone(const AudioZone& zone) { m_zones[zone.id] = zone; }
    AudioZone getZone(const QString& zoneId) const { return m_zones.value(zoneId); }
    void setListener(const Listener& l) { m_listener = l; }
    Listener listener() const { return m_listener; }

    float getZoneGain(const QString& zoneId) const;
    float getZoneReverb(const QString& zoneId) const;

signals:
    void listenerMoved();

private:
    QMap<QString, AudioZone> m_zones;
    Listener m_listener;
};

class WaveformProcessor : public QObject
{
    Q_OBJECT
public:
    explicit WaveformProcessor(QObject* parent = nullptr) : QObject(parent) {}
    ~WaveformProcessor() {}

    void setData(const QVector<float>& data) { m_data = data; }
    QVector<float> data() const { return m_data; }

    QVector<float> getPeaks(int numPeaks) const;
    QVector<float> getRMS(int windowSize) const;
    QVector<float> normalize(float targetPeak = 1.0f);
    QVector<float> fadeIn(int samples);
    QVector<float> fadeOut(int samples);
    QVector<float> reverse();
    QVector<float> resample(int newSize);

    void setMarkers(const QMap<QString, int>& markers) { m_markers = markers; }
    QMap<QString, int> markers() const { return m_markers; }

signals:
    void processed();

private:
    QVector<float> m_data;
    QMap<QString, int> m_markers;
};

} // namespace audio
} // namespace ks