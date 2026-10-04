#ifndef AUDIO_TIME_STRETCH_H
#define AUDIO_TIME_STRETCH_H

#include <QObject>
#include <QVector>
#include <QPair>

class AudioTimeStretch : public QObject
{
    Q_OBJECT

public:
    enum StretchMode {
        MODE_PRESET,
        MODE_PHASE_VOCODER,
        MODE_OLAE,
        MODE_HRTF
    };

    enum Quality {
        QUALITY_FAST,
        QUALITY_NORMAL,
        QUALITY_HIGH
    };

    explicit AudioTimeStretch(QObject *parent = nullptr);
    ~AudioTimeStretch();

    void setMode(StretchMode mode) { m_mode = mode; }
    void setQuality(Quality quality) { m_quality = quality; }
    void setPitchShift(float semitones);

    QVector<float> stretch(const QVector<float> &samples, int channels, int sampleRate,
                           float stretchRatio);
    QVector<float> compress(const QVector<float> &samples, int channels, int sampleRate,
                            float compressRatio);

    static QVector<float> simpleStretch(const QVector<float> &samples, int channels,
                                        float ratio, int fftSize = 1024);

signals:
    void progressChanged(int percent);
    void processingComplete();

private:
    struct AnalysisFrame {
        QVector<float> magnitudes;
        QVector<float> phases;
        QVector<float> prevPhases;
        float peak;
    };

    QVector<AnalysisFrame> analyzeFrames(const QVector<float> &samples, int channels,
                                          int fftSize, int hopSize);
    QVector<float> synthesizeFrames(const QVector<AnalysisFrame> &frames, int channels,
                                     int fftSize, int hopSize, int outputLength);
    void phaseVocoderProcess(QVector<AnalysisFrame> &frames, float stretchRatio,
                             int fftSize, int hopSize);

    QVector<float> olaeProcess(const QVector<float> &input, float ratio);
    float getWindowValue(int i, int size);

    StretchMode m_mode;
    Quality m_quality;
    float m_pitchShiftSemitones;

    static const int DEFAULT_FFT_SIZE = 2048;
    static const int DEFAULT_HOP_SIZE = 512;
};

class PitchCorrector : public QObject
{
    Q_OBJECT

public:
    explicit PitchCorrector(QObject *parent = nullptr);
    ~PitchCorrector();

    void setScale(const QString &scale);
    void setCorrectionStrength(float strength);
    void setFormantPreservation(bool enabled);

    QVector<float> correct(const QVector<float> &samples, int channels, int sampleRate);
    float detectPitch(const QVector<float> &samples, int sampleRate);

    enum Note {
        C, C_SHARP, D, D_SHARP, E, F, F_SHARP, G, G_SHARP, A, A_SHARP, B
    };

    static float noteToFrequency(Note note, int octave);
    static Note frequencyToNote(float frequency, int &octave);
    static float closestScaleNote(float frequency, const QVector<Note> &scale);

signals:
    void pitchDetected(float frequency);
    void pitchCorrected(float fromFreq, float toFreq);
    void correctionComplete();

private:
    QVector<Note> m_scale;
    float m_correctionStrength;
    bool m_formantPreservation;

    float autoCorrelate(const QVector<float> &samples, int sampleRate);
};

#endif // AUDIO_TIME_STRETCH_H