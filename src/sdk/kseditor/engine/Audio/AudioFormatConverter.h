#ifndef AUDIO_FORMAT_CONVERTER_H
#define AUDIO_FORMAT_CONVERTER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QAudioFormat>
#include <QFile>
#include <QDataStream>

class AudioFormatConverter : public QObject
{
    Q_OBJECT

public:
    explicit AudioFormatConverter(QObject *parent = nullptr);
    ~AudioFormatConverter();

    enum AudioFormat {
        FORMAT_WAV,
        FORMAT_MP3,
        FORMAT_OGG,
        FORMAT_FLAC,
        FORMAT_AIFF,
        FORMAT_UNKNOWN
    };

    enum ConversionQuality {
        QUALITY_LOW,
        QUALITY_MEDIUM,
        QUALITY_HIGH,
        QUALITY_LOSSLESS
    };

    struct AudioMetadata {
        QString title;
        QString artist;
        QString album;
        QString genre;
        int year;
        int track;
        int durationMs;
        int sampleRate;
        int channels;
        int bitrate;
    };

    static AudioFormat formatFromExtension(const QString &ext);
    static QString extensionFromFormat(AudioFormat format);
    static QString formatName(AudioFormat format);
    static bool isLossless(AudioFormat format);
    static QStringList supportedExtensions();

    bool convert(const QString &inputPath, const QString &outputPath,
                 ConversionQuality quality = QUALITY_HIGH);

    bool decodeMp3(const QString &inputPath, QVector<float> &samples,
                   QAudioFormat &format, AudioMetadata &metadata);
    bool decodeOgg(const QString &inputPath, QVector<float> &samples,
                   QAudioFormat &format, AudioMetadata &metadata);
    bool decodeFlac(const QString &inputPath, QVector<float> &samples,
                    QAudioFormat &format, AudioMetadata &metadata);

    bool encodeMp3(const QString &outputPath, const QVector<float> &samples,
                   const QAudioFormat &format, ConversionQuality quality);
    bool encodeOgg(const QString &outputPath, const QVector<float> &samples,
                   const QAudioFormat &format, ConversionQuality quality);
    bool encodeFlac(const QString &outputPath, const QVector<float> &samples,
                    const QAudioFormat &format);

    QVector<float> resample(const QVector<float> &input, int inputRate,
                            int outputRate, int channels);
    QVector<float> mixToMono(const QVector<float> &stereo, int channels);

signals:
    void progressChanged(int percent);
    void conversionFinished(bool success);
    void error(const QString &message);

private:
    struct Mp3Frame {
        quint32 header;
        quint32 sync;
        int version;
        int layer;
        int bitrate;
        int samplerate;
        int padding;
        int channels;
        int samples;
    };

    struct FlacMetadata {
        quint32 minBlockSize;
        quint32 maxBlockSize;
        quint32 sampleRate;
        quint8 channels;
        quint8 bitsPerSample;
        quint64 totalSamples;
        QVector<QPair<QString, QString>> vorbisComments;
    };

    bool parseMp3Header(QDataStream &stream, Mp3Frame &frame);
    bool parseMp3Frames(const QByteArray &data, QVector<float> &samples,
                        QAudioFormat &format, AudioMetadata &metadata);
    bool parseFlacMetadata(QDataStream &stream, FlacMetadata &metadata);
    bool decodeFlacFrames(const QByteArray &data, const FlacMetadata &metadata,
                          QVector<float> &samples);

    QVector<float> decodeMpegAudio(const QByteArray &data);
    void applyMpegWindow(QVector<float> &samples, int blockSize);

    int getMp3Bitrate(int bitrateIndex, int version, int layer);
    int getMp3SampleRate(int rateIndex, int version);

    QVector<float> m_decodeBuffer;
    int m_progress;
};

class AudioBuffer : public QObject
{
    Q_OBJECT

public:
    explicit AudioBuffer(QObject *parent = nullptr);
    ~AudioBuffer();

    void setSamples(const QVector<float> &samples, int channels, int sampleRate);
    const QVector<float>& getSamples() const { return m_samples; }
    int getChannelCount() const { return m_channels; }
    int getSampleRate() const { return m_sampleRate; }
    int getSampleCount() const { return m_samples.size() / m_channels; }
    qint64 getDurationMs() const;

    void append(const AudioBuffer &other);
    void insert(int positionSamples, const AudioBuffer &other);
    void remove(int startSamples, int countSamples);
    void mix(const AudioBuffer &other, float mixLevel = 0.5f);

    AudioBuffer* getRegion(int startMs, int endMs) const;
    void setRegion(int startMs, const AudioBuffer &region);

    void applyGain(float gainDb);
    void applyFadeIn(int startMs, int durationMs);
    void applyFadeOut(int endMs, int durationMs);

    QByteArray toWavData() const;
    bool fromWavData(const QByteArray &data);

    static QByteArray samplesToWav(const QVector<float> &samples,
                                    int channels, int sampleRate, int bitsPerSample = 16);
    static bool wavToSamples(const QByteArray &data, QVector<float> &samples,
                              int &channels, int &sampleRate, int &bitsPerSample);

private:
    QVector<float> m_samples;
    int m_channels;
    int m_sampleRate;
};

#endif // AUDIO_FORMAT_CONVERTER_H