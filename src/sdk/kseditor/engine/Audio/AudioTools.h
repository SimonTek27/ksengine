#ifndef KSAUDIOTOOLS_H
#define KSAUDIOTOOLS_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QAudioFormat>
#include <QAudioSource>
#include <QFile>
#include <QTimer>

namespace ks {
namespace audio {

// ============================================================================
// KSAudioRecorder - Audio recording
// ============================================================================

class KSAudioRecorder : public QObject {
    Q_OBJECT
public:
    explicit KSAudioRecorder(QObject* parent = nullptr);
    ~KSAudioRecorder();

    bool startRecording(const QString& outputPath, int sampleRate = 44100, int channels = 2);
    void stopRecording();
    bool isRecording() const { return m_recording; }

    void setInputDevice(const QString& deviceName);
    QStringList availableInputDevices() const;

    qint64 durationMs() const;

signals:
    void recordingStarted(const QString& path);
    void recordingStopped(const QString& path);
    void error(const QString& message);

private:
    bool m_recording = false;
    QAudioSource* m_audioSource = nullptr;
    QFile m_outputFile;
    QTimer* m_timer = nullptr;
    qint64 m_startTime = 0;

    void writeWavHeader();
    void finalizeWavFile();
};

// ============================================================================
// KSAudioMultiTrack - Multi-track mixing
// ============================================================================

struct Track {
    QString name;
    QVector<float> samples;
    int sampleRate = 44100;
    int channels = 2;
    float volume = 1.0f;
    bool muted = false;
    bool solo = false;
    bool armed = false; // Record arm
    QAudioFormat format;
    bool isRecording = false;
};

class KSAudioMultiTrack : public QObject {
    Q_OBJECT
public:
    explicit KSAudioMultiTrack(QObject* parent = nullptr);
    ~KSAudioMultiTrack();

    int addTrack(const QString& name);
    bool removeTrack(int trackIndex);
    void clearTracks();

    int trackCount() const { return m_tracks.size(); }
    Track* getTrack(int index);
    const Track* getTrack(int index) const;

    bool loadAudioToTrack(int trackIndex, const QString& filePath);
    bool recordToTrack(int trackIndex);

    void setTrackVolume(int trackIndex, float volume);
    float trackVolume(int trackIndex) const;
    void setTrackMute(int trackIndex, bool mute);
    bool trackMuted(int trackIndex) const;
    void setTrackSolo(int trackIndex, bool solo);
    bool trackSolo(int trackIndex) const;

    void mixDown(QVector<float>& output, int outputSampleRate, int outputChannels);
    bool exportMix(const QString& outputPath, int sampleRate = 44100, int channels = 2);

    void setMasterVolume(float volume) { m_masterVolume = volume; }
    float masterVolume() const { return m_masterVolume; }

signals:
    void trackAdded(int index, const QString& name);
    void trackRemoved(int index);
    void trackVolumeChanged(int index, float volume);
    void mixCompleted(const QString& path);

private:
    QVector<Track> m_tracks;
    float m_masterVolume = 1.0f;
    int m_nextTrackId = 0;
    KSAudioRecorder* m_recorder = nullptr;
};

// ============================================================================
// KSAudioBatchProcessor - Batch processing
// ============================================================================

class KSAudioBatchProcessor : public QObject {
    Q_OBJECT
public:
    explicit KSAudioBatchProcessor(QObject* parent = nullptr);

    void addFile(const QString& inputPath, const QString& outputPath);
    void clearFiles();
    int fileCount() const { return m_files.size(); }

    void addEffect(int effectType, const QVector<float>& parameters);
    void clearEffects();
    int effectCount() const { return m_effects.size(); }

    void setOutputFormat(const QString& format) { m_outputFormat = format; }
    void setOutputSampleRate(int sr) { m_outputSampleRate = sr; }
    void setOutputChannels(int ch) { m_outputChannels = ch; }

    void startProcessing();
    void stopProcessing();
    bool isProcessing() const { return m_processing; }

    float progress() const { return m_progress; }

signals:
    void processingStarted();
    void processingFinished();
    void progressChanged(float progress);
    void fileProcessed(int index, const QString& outputPath);
    void error(const QString& message);

private:
    struct BatchFile { QString input, output; };
    struct BatchEffect { int type; QVector<float> params; };

    QVector<BatchFile> m_files;
    QVector<BatchEffect> m_effects;
    QString m_outputFormat = "wav";
    int m_outputSampleRate = 44100, m_outputChannels = 2;
    bool m_processing = false;
    float m_progress = 0.0f;

    void processFile(const BatchFile& file);
};

// ============================================================================
// KSAudioFileMerger - Join multiple files
// ============================================================================

class KSAudioFileMerger : public QObject {
    Q_OBJECT
public:
    explicit KSAudioFileMerger(QObject* parent = nullptr);

    void addFile(const QString& filePath);
    void removeFile(int index);
    void clearFiles();
    int fileCount() const { return m_files.size(); }

    void setCrossfadeDuration(int ms) { m_crossfadeMs = ms; }
    int crossfadeDuration() const { return m_crossfadeMs; }

    bool merge(const QString& outputPath, int sampleRate = 44100, int channels = 2);

signals:
    void fileAdded(int index, const QString& path);
    void fileRemoved(int index);
    void mergeCompleted(const QString& outputPath);
    void error(const QString& message);

private:
    QVector<QString> m_files;
    int m_crossfadeMs = 500;
};

// ============================================================================
// KSAudioExpression - Tone/expression generator
// ============================================================================

class KSAudioExpression : public QObject {
    Q_OBJECT
public:
    explicit KSAudioExpression(QObject* parent = nullptr);

    enum WaveformType { Sine, Square, Sawtooth, Triangle, WhiteNoise };

    QVector<float> generateTone(float frequency, float durationSec, int sampleRate = 44100,
                                WaveformType type = Sine, float amplitude = 1.0f);
    QVector<float> generateSweep(float startFreq, float endFreq, float durationSec,
                                int sampleRate = 44100, WaveformType type = Sine);
    QVector<float> generateClickTrack(float bpm, float durationSec, int sampleRate = 44100);
    QVector<float> generateDTMF(const QString& digits, int sampleRate = 44100);

    bool saveToWav(const QVector<float>& samples, const QString& outputPath,
                     int sampleRate = 44100, int channels = 1);
};

} // namespace audio
} // namespace ks

#endif // KSAUDIOTOOLS_H
