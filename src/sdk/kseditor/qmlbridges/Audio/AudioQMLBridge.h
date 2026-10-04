#ifndef AUDIO_QML_BRIDGE_H
#define AUDIO_QML_BRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariantList>
#include <QTimer>
#include "sdk/kseditor/engine/Audio/AudioClip.h"

class WaveProcessor;
class WaveformEngine;
class NoiseReducer;
class PeakMeter;
namespace ks { namespace audio { class FFTProcessor; } }

#include <QAudioFormat>
namespace ks { namespace audio { class AudioRecorder; } }
namespace ks { namespace audio { class Studio; } }
namespace ks { namespace audio { class TextToSpeech; } }

class AudioQMLBridge : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY playbackChanged)
    Q_PROPERTY(qint64 position READ getPositionMs WRITE setPositionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ getDurationMs NOTIFY durationChanged)
    Q_PROPERTY(bool isLoopEnabled READ isLoopEnabled NOTIFY loopChanged)
    Q_PROPERTY(float leftPeak READ getLeftPeak NOTIFY levelsUpdated)
    Q_PROPERTY(float rightPeak READ getRightPeak NOTIFY levelsUpdated)
    Q_PROPERTY(float leftRMS READ getLeftRMS NOTIFY levelsUpdated)
    Q_PROPERTY(float rightRMS READ getRightRMS NOTIFY levelsUpdated)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(float inputLevel READ getInputLevel NOTIFY inputLevelChanged)
    Q_PROPERTY(QString recordingOutputPath READ recordingOutputPath NOTIFY recordingPathChanged)

    // Text-to-Speech properties
    Q_PROPERTY(bool ttsSpeaking READ ttsSpeaking NOTIFY ttsStateChanged)
    Q_PROPERTY(int ttsVolume READ ttsVolume WRITE setTtsVolume NOTIFY ttsVolumeChanged)
    Q_PROPERTY(int ttsRate READ ttsRate WRITE setTtsRate NOTIFY ttsRateChanged)
    Q_PROPERTY(QStringList ttsVoices READ ttsVoices NOTIFY ttsVoicesChanged)
    Q_PROPERTY(QString ttsCurrentVoice READ ttsCurrentVoice WRITE setTtsCurrentVoice NOTIFY ttsCurrentVoiceChanged)

public:
    static AudioQMLBridge* instance();

    explicit AudioQMLBridge(QObject *parent = nullptr);
    ~AudioQMLBridge();

    bool isPlaying() const;
    bool isPaused() const;
    qint64 getPositionMs() const;
    qint64 getDurationMs() const;
    bool isLoopEnabled() const;
    float getLeftPeak() const;
    float getRightPeak() const;
    float getLeftRMS() const;
    float getRightRMS() const;

    Q_INVOKABLE bool loadAudio(const QString &filePath);
    Q_INVOKABLE bool saveAudio(const QString &filePath);
    Q_INVOKABLE void newAudio(int channels, int sampleRate, int durationMs);

    Q_INVOKABLE int trackCount() const;
    Q_INVOKABLE int activeTrack() const;
    Q_INVOKABLE void setActiveTrack(int index);
    Q_INVOKABLE int addTrack();
    Q_INVOKABLE void removeTrack(int index);
    Q_INVOKABLE QString trackName(int index) const;
    Q_INVOKABLE void setTrackName(int index, const QString &name);
    Q_INVOKABLE float trackGain(int index) const;
    Q_INVOKABLE void setTrackGain(int index, float gain);
    Q_INVOKABLE float trackPan(int index) const;
    Q_INVOKABLE void setTrackPan(int index, float pan);
    Q_INVOKABLE bool trackMute(int index) const;
    Q_INVOKABLE void setTrackMute(int index, bool mute);
    Q_INVOKABLE bool trackSolo(int index) const;
    Q_INVOKABLE void setTrackSolo(int index, bool solo);
    Q_INVOKABLE qint64 trackDurationMs(int index) const;
    Q_INVOKABLE int trackSampleCount(int index) const;
    Q_INVOKABLE int trackRate(int index) const;
    Q_INVOKABLE QVariantList getTrackWaveformData(int index, int startMs, int endMs, int width);
    Q_INVOKABLE void trackUndo(int index);
    Q_INVOKABLE void trackRedo(int index);
    Q_INVOKABLE bool trackCanUndo(int index) const;
    Q_INVOKABLE bool trackCanRedo(int index) const;
    Q_INVOKABLE int projectRate() const;

    // Clip operations
    Q_INVOKABLE int trackClipCount(int index) const;
    Q_INVOKABLE qint64 trackClipStartMs(int trackIndex, int clipIndex) const;
    Q_INVOKABLE qint64 trackClipEndMs(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void trackSplitClip(int trackIndex, int clipIndex, qint64 positionMs);
    Q_INVOKABLE void trackTrimClip(int trackIndex, int clipIndex, qint64 newStartMs, qint64 newEndMs);
    Q_INVOKABLE void trackDeleteSelection(int trackIndex, int startMs, int endMs);
    Q_INVOKABLE void trackAddClip(int trackIndex, const QString &filePath, qint64 positionMs = -1);
    Q_INVOKABLE void trackJoinClips(int trackIndex, int clipIndex1, int clipIndex2);
    Q_INVOKABLE void trackMoveClip(int trackIndex, int clipIndex, qint64 newStartMs);

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void setPositionMs(qint64 ms);
    Q_INVOKABLE void setLoopEnabled(bool enabled);
    Q_INVOKABLE void setLoopRegion(qint64 startMs, qint64 endMs);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool canUndo() const;
    Q_INVOKABLE bool canRedo() const;

    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void selectRegion(int startMs, int endMs);
    Q_INVOKABLE int getSelectionStart() const;
    Q_INVOKABLE int getSelectionEnd() const;

    Q_INVOKABLE void cut();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void deleteSelection();

    Q_INVOKABLE void reverse();
    Q_INVOKABLE void fadeIn(int startMs, int durationMs);
    Q_INVOKABLE void fadeOut(int endMs, int durationMs);
    Q_INVOKABLE void normalize(float level = 1.0f);
    Q_INVOKABLE void amplify(float factor);
    Q_INVOKABLE void invert();
    Q_INVOKABLE void silence(int startMs, int endMs);
    Q_INVOKABLE void insertSilence(int positionMs, int durationMs);
    Q_INVOKABLE void deleteRegion(int startMs, int endMs);

    Q_INVOKABLE void applyLowPassFilter(float cutoff, float resonance = 0.7f);
    Q_INVOKABLE void applyHighPassFilter(float cutoff, float resonance = 0.7f);
    Q_INVOKABLE void applyBandPassFilter(float low, float high);
    Q_INVOKABLE void applyNotchFilter(float freq, float bandwidth);

    Q_INVOKABLE void applyDelay(float delayMs, float feedback = 0.3f, float mix = 0.5f);
    Q_INVOKABLE void applyReverb(float roomSize = 0.5f, float damping = 0.5f, float wetDry = 0.3f);
    Q_INVOKABLE void applyEcho(float delayMs, float feedback = 0.5f, float mix = 0.5f);
    Q_INVOKABLE void applyChorus(float depth = 1.0f, float rate = 0.5f, float mix = 0.3f);
    Q_INVOKABLE void applyFlanger(float depth = 1.0f, float rate = 0.5f, float mix = 0.5f);
    Q_INVOKABLE void applyCompressor(float threshold = -20.0f, float ratio = 4.0f,
                                      float attack = 10.0f, float release = 100.0f, float makeupGain = 1.0f);
    Q_INVOKABLE void applyLimiter(float threshold = -0.1f, float release = 50.0f);

    Q_INVOKABLE void applyPhaser(float rate = 0.5f, float depth = 1.0f, float feedback = 0.0f, float mix = 0.5f);
    Q_INVOKABLE void applyTremolo(float rate = 5.0f, float depth = 1.0f);
    Q_INVOKABLE void applyWahWah(float freq = 1000.0f, float range = 1200.0f, float resonance = 1.0f);
    Q_INVOKABLE void applyVocalReduction(float panLow = 0.1f, float panHigh = 0.9f);
    Q_INVOKABLE void applyNoiseGate(float threshold = -40.0f, float floor = -80.0f, float attack = 5.0f, float release = 100.0f);
    Q_INVOKABLE void applyDeEsser(float freq = 5000.0f, float threshold = -20.0f);
    Q_INVOKABLE void applyBitCrusher(int bitDepth = 8, float downsample = 1.0f);
    Q_INVOKABLE void applyRingMod(float freq = 1000.0f, float mix = 0.5f);
    Q_INVOKABLE void applySaturation(float drive = 2.0f, float mix = 0.5f);
    Q_INVOKABLE void applyTapeEmulation(float saturation = 1.0f, float wow = 0.0f, float flutter = 0.0f);
    Q_INVOKABLE void applyGuitarAmp(float gain = 10.0f, float tone = 0.5f, float volume = 1.0f);
    Q_INVOKABLE void applyTransientDesigner(float attack = 0.0f, float sustain = 0.0f);
    Q_INVOKABLE void applyStereoEnhancer(float width = 1.5f);
    Q_INVOKABLE void applyMultibandCompressor(float lowThresh = -20.0f, float midThresh = -20.0f, float highThresh = -20.0f,
                                               float lowRatio = 4.0f, float midRatio = 4.0f, float highRatio = 4.0f,
                                               float attack = 10.0f, float release = 100.0f);

    Q_INVOKABLE void timeStretch(float ratio, int quality = 2);
    Q_INVOKABLE void pitchShift(float semitones, int quality = 2);
    Q_INVOKABLE int timeStretchQuality() const { return m_timeStretchQuality; }
    Q_INVOKABLE void setTimeStretchQuality(int q) { m_timeStretchQuality = qBound(0,q,2); }
    Q_INVOKABLE void changeTempo(float percent);
    Q_INVOKABLE void changePitch(float semitones);

    Q_INVOKABLE bool convertFormat(const QString &inputPath, const QString &outputPath, int quality = 2);
    Q_INVOKABLE QStringList getSupportedFormats();

    // Recording
    Q_INVOKABLE bool startRecording(const QString &outputPath, int sampleRate = 44100, int channels = 2);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    Q_INVOKABLE bool isRecording() const;
    Q_INVOKABLE float getInputLevel(int channel = 0) const;
    Q_INVOKABLE QString recordingOutputPath() const;
    Q_INVOKABLE qint64 recordingDuration() const;
    Q_INVOKABLE QStringList getAvailableInputDevices();
    Q_INVOKABLE void setInputDevice(const QString& deviceName);
    Q_INVOKABLE QString getCurrentInputDevice() const;

    Q_INVOKABLE void captureNoiseProfile();
    Q_INVOKABLE void applyNoiseReduction(float amount = -15.0f);
    Q_INVOKABLE bool hasNoiseProfile() const;
    Q_INVOKABLE void applySpectralEdit(int startMs, int endMs, float lowHz, float highHz, float gainDb);
    Q_INVOKABLE void applySpectralDelete(int startMs, int endMs, float lowHz, float highHz);
    Q_INVOKABLE void applyDeHum(float freq = 50.0f, float bw = 2.0f, int harmonics = 5);
    Q_INVOKABLE void applyDeClick(float threshold = 0.35f);
    Q_INVOKABLE QVariantMap getMasteringMeters();
    Q_INVOKABLE bool exportDDP(const QString& path);
    Q_INVOKABLE bool saveSessionTemplate(const QString& name);
    Q_INVOKABLE bool loadSessionTemplate(const QString& name);
    Q_INVOKABLE QStringList sessionTemplates() const;
    Q_INVOKABLE QVariantMap getLufsHistogram();
    Q_INVOKABLE bool pencilEdit(int sampleIndex, float value);
    Q_INVOKABLE int findZeroCrossing(int fromSample, int direction = 1);
    Q_INVOKABLE QStringList masteringPresets() const;
    Q_INVOKABLE bool applyMasteringPreset(const QString& name);
    Q_INVOKABLE QVariantMap detectSilence(float thresholdDb = -40.0f, int minDurationMs = 200);
    Q_INVOKABLE bool applyVoiceActivation(float thresholdDb = -35.0f);
    Q_INVOKABLE bool setVideoPath(const QString& path);
    Q_INVOKABLE QString videoPath() const;
    Q_INVOKABLE bool syncToVideo(qint64 videoMs);
    Q_INVOKABLE QString videoSyncInfo() const;

    Q_INVOKABLE QVariantList getWaveformData(int width);
    Q_INVOKABLE QVariantList getWaveformDataRange(int startMs, int endMs, int width);
    Q_INVOKABLE QVariantList getSpectrumData();
    QVariantList getFrequencyBands(int bandCount);
    Q_INVOKABLE QVariantList getMelSpectrum(int bands = 64);

    // Analysis tools
    Q_INVOKABLE QVariantList getOscilloscopeData(int width);
    Q_INVOKABLE QVariantList getSonogramData(int width, int height);
    Q_INVOKABLE QVariantList getPitchAnalysis(int width);
    Q_INVOKABLE QVariantList getContrastData(int startMs, int endMs);
    Q_INVOKABLE QVariantList getPlotSpectrum(int width);
    Q_INVOKABLE QVariantMap getStatistics();

    // Envelope tools
    Q_INVOKABLE int trackEnvelopePointCount(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVariantList trackEnvelopePoints(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void trackAddEnvelopePoint(int trackIndex, int clipIndex, qint64 timeMs, float gain);
    Q_INVOKABLE void trackRemoveEnvelopePoint(int trackIndex, int clipIndex, int pointIndex);
    Q_INVOKABLE void trackSetEnvelopePoint(int trackIndex, int clipIndex, int pointIndex, qint64 timeMs, float gain);
    Q_INVOKABLE void trackApplyEnvelope(int trackIndex, int clipIndex);

    // Project I/O
    Q_INVOKABLE bool saveProject(const QString &filePath);
    Q_INVOKABLE bool loadProject(const QString &filePath);

    // Indexed undo/redo stack
    Q_INVOKABLE int trackUndoStackSize(int trackIndex) const;
    Q_INVOKABLE int trackRedoStackSize(int trackIndex) const;
    Q_INVOKABLE int trackUndoStackIndex(int trackIndex) const;
    Q_INVOKABLE QVariantList trackUndoStackDescriptions(int trackIndex) const;
    Q_INVOKABLE void trackUndoToIndex(int trackIndex, int index);
    Q_INVOKABLE void trackRedoToIndex(int trackIndex, int index);

    Q_INVOKABLE int getSampleCount() const;
    Q_INVOKABLE int getChannelCount() const;
    Q_INVOKABLE int getSampleRate() const;
    Q_INVOKABLE int getBitDepth() const;
    Q_INVOKABLE QString getFileName() const;
    Q_INVOKABLE bool isModified() const;

    // Text-to-Speech
    Q_INVOKABLE void ttsSpeak(const QString& text);
    Q_INVOKABLE void ttsStop();
    Q_INVOKABLE void ttsPause();
    Q_INVOKABLE void ttsResume();
    Q_INVOKABLE bool ttsSpeaking() const;
    Q_INVOKABLE QStringList ttsVoices() const;
    Q_INVOKABLE QString ttsCurrentVoice() const;
    Q_INVOKABLE void setTtsCurrentVoice(const QString& name);
    Q_INVOKABLE int ttsVolume() const;
    Q_INVOKABLE void setTtsVolume(int percent);
    Q_INVOKABLE int ttsRate() const;
    Q_INVOKABLE void setTtsRate(int rate);
    Q_INVOKABLE bool ttsSaveToWav(const QString& text, const QString& filePath);

signals:
    void playbackChanged();
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void loopChanged(bool enabled);
    void levelsUpdated(float left, float right, float leftRMS, float rightRMS);
    void selectionChanged(int startMs, int endMs);
    void loadComplete();
    void saveComplete();
    void error(const QString &message);
    void recordingStateChanged(bool recording);
    void inputLevelChanged(float level);
    void recordingPathChanged(const QString& path);
    void recordingDurationChanged(qint64 duration);
    void audioChanged();
    void statusMessage(const QString& msg);

    // Text-to-Speech signals
    void ttsStateChanged();
    void ttsVolumeChanged();
    void ttsRateChanged();
    void ttsVoicesChanged();
    void ttsCurrentVoiceChanged();

private:
    static AudioQMLBridge* s_instance;

    TrackModel *m_activeTrackModel;
    QVector<TrackModel*> m_tracks;
    int m_activeTrack;
    WaveProcessor *m_waveProcessor;  // legacy active track processor
    WaveformEngine *m_waveformEngine;
    ks::audio::FFTProcessor *m_fft;
    NoiseReducer *m_noiseReducer;
    PeakMeter *m_peakMeter;

    int m_selectionStartMs;
    int m_selectionEndMs;
    QString m_currentFilePath;
    bool m_modified;

    // View state (for project save/load)
    double m_viewStart = 0.0;
    double m_viewEnd = 1.0;
    double m_selectionStartFrac = 0.0;
    double m_selectionEndFrac = 1.0;

    // Recording members
    ks::audio::AudioRecorder* m_recorder;
    ks::audio::Studio* m_studio;
    QAudioFormat m_recordingFormat;
    QString m_recordingOutputPath;
    float m_currentInputLevel;
    QVector<float> m_inputLevels;
    QString m_inputDeviceName;
    QTimer* m_recordingTimer;
    qint64 m_recordingStartTime;

    // Text-to-Speech
    ks::audio::TextToSpeech* m_tts;
    QString m_videoPath;
    qint64 m_videoOffsetMs = 0;
    int m_timeStretchQuality = 2;
};

#endif // AUDIO_QML_BRIDGE_H
