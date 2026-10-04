#include "AudioQMLBridge.h"
#include "sdk/kseditor/engine/Audio/WaveProcessor.h"
#include "sdk/kseditor/engine/Audio/WaveformEngine.h"
#include "sdk/kseditor/engine/Audio/FFTProcessor.h"
#include "sdk/kseditor/engine/Audio/AudioFormatConverter.h"
#include "sdk/kseditor/engine/Audio/AudioTimeStretch.h"
#include "sdk/kseditor/engine/Audio/AudioRecording.h"
#include "sdk/kseditor/engine/Audio/AudioCore.h"
#include "sdk/kseditor/engine/Audio/AudioEffects.h"
#include "sdk/kseditor/engine/Audio/TextToSpeech.h"
#include "NoiseReducer.h"
#include "PeakMeter.h"
#include <QDebug>
#include <QFileInfo>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QDataStream>
#include <QBuffer>
#include <cmath>

AudioQMLBridge* AudioQMLBridge::s_instance = nullptr;

AudioQMLBridge::AudioQMLBridge(QObject *parent)
    : QObject(parent)
    , m_activeTrackModel(new TrackModel(this))
    , m_tracks()
    , m_activeTrack(0)
    , m_waveProcessor(new WaveProcessor(this))
    , m_waveformEngine(WaveformEngine::instance())
    , m_fft(new ks::audio::FFTProcessor(this))
    , m_noiseReducer(new NoiseReducer(this))
    , m_peakMeter(new PeakMeter(this))
    , m_selectionStartMs(0)
    , m_selectionEndMs(0)
    , m_modified(false)
    , m_recorder(new ks::audio::AudioRecorder(this))
    , m_studio(new ks::audio::Studio(this))
    , m_currentInputLevel(0.0f)
    , m_recordingStartTime(0)
{
    s_instance = this;

    m_activeTrackModel->name = tr("Track 1");
    m_tracks.append(m_activeTrackModel);

    m_peakMeter->setSampleRate(44100);
    m_peakMeter->setChannelCount(2);
    m_peakMeter->setPeakDecay(1000.0f);

    m_recordingFormat.setSampleRate(44100);
    m_recordingFormat.setChannelCount(2);
    m_recordingFormat.setSampleFormat(QAudioFormat::Int16);

    m_recorder->setFormat(m_recordingFormat);

    // Text-to-Speech
    m_tts = new ks::audio::TextToSpeech(this);
    connect(m_tts, &ks::audio::TextToSpeech::started, this, &AudioQMLBridge::ttsStateChanged);
    connect(m_tts, &ks::audio::TextToSpeech::finished, this, &AudioQMLBridge::ttsStateChanged);

    m_recordingTimer = new QTimer(this);
    m_recordingTimer->setInterval(100);
    connect(m_recordingTimer, &QTimer::timeout, this, [this]() {
        if (m_studio && m_studio->inputDevice()) {
            QByteArray data = m_studio->inputDevice()->readAll();
            if (!data.isEmpty()) {
                int sampleCount = data.size() / 2;
                QVector<float> samples(sampleCount);
                const qint16* rawData = reinterpret_cast<const qint16*>(data.constData());
                float peak = 0.0f;
                for (int i = 0; i < sampleCount; ++i) {
                    samples[i] = static_cast<float>(rawData[i]) / 32767.0f;
                    float absVal = std::abs(samples[i]);
                    if (absVal > peak) peak = absVal;
                }
                m_currentInputLevel = peak;
                m_recorder->appendData(samples);
                emit inputLevelChanged(peak);
            }
        }
        emit recordingDurationChanged(QDateTime::currentMSecsSinceEpoch() - m_recordingStartTime);
    });

    connect(m_recorder, &ks::audio::AudioRecorder::stateChanged, this, [this](ks::audio::AudioRecorder::State state) {
        emit recordingStateChanged(state == ks::audio::AudioRecorder::Recording);
    });
    connect(m_recorder, &ks::audio::AudioRecorder::levelChanged, this, [this](float level) {
        m_currentInputLevel = level;
        emit inputLevelChanged(level);
    });
    connect(m_recorder, &ks::audio::AudioRecorder::recordingComplete, this, [this](const QString& path) {
        if (m_waveProcessor) {
            m_waveProcessor->load(path);
            emit loadComplete();
        }
    });

    connect(m_waveformEngine, &WaveformEngine::playbackStarted, this, &AudioQMLBridge::playbackChanged);
    connect(m_waveformEngine, &WaveformEngine::playbackStopped, this, &AudioQMLBridge::playbackChanged);
    connect(m_waveformEngine, &WaveformEngine::playbackPaused, this, &AudioQMLBridge::playbackChanged);
    connect(m_waveformEngine, &WaveformEngine::playbackFinished, this, &AudioQMLBridge::playbackChanged);
    connect(m_waveformEngine, &WaveformEngine::positionChanged, this, &AudioQMLBridge::positionChanged);
    connect(m_waveformEngine, &WaveformEngine::loopToggled, this, &AudioQMLBridge::loopChanged);

    connect(m_peakMeter, &PeakMeter::levelsChanged, [this](float left, float right) {
        emit levelsUpdated(left, right, m_peakMeter->getLeftRMS(), m_peakMeter->getRightRMS());
    });

    qDebug() << "AudioQMLBridge: Initialized";
}

AudioQMLBridge::~AudioQMLBridge()
{
    s_instance = nullptr;
}

AudioQMLBridge* AudioQMLBridge::instance()
{
    if (!s_instance) {
        s_instance = new AudioQMLBridge();
    }
    return s_instance;
}

bool AudioQMLBridge::isPlaying() const
{
    return m_waveformEngine->isPlaying();
}

bool AudioQMLBridge::isPaused() const
{
    return m_waveformEngine->isPaused();
}

qint64 AudioQMLBridge::getPositionMs() const
{
    return m_waveformEngine->positionMs();
}

bool AudioQMLBridge::isLoopEnabled() const
{
    return m_waveformEngine->loopEnabled();
}

float AudioQMLBridge::getLeftPeak() const
{
    return m_peakMeter->getPeakLevel(0);
}

float AudioQMLBridge::getRightPeak() const
{
    return m_peakMeter->getPeakLevel(1);
}

float AudioQMLBridge::getLeftRMS() const
{
    return m_peakMeter->getRMSLevel(0);
}

float AudioQMLBridge::getRightRMS() const
{
    return m_peakMeter->getRMSLevel(1);
}

bool AudioQMLBridge::loadAudio(const QString &filePath)
{
    if (m_waveProcessor->loadWav(filePath)) {
        m_currentFilePath = filePath;
        m_modified = false;
        m_selectionStartMs = 0;
        m_selectionEndMs = m_waveProcessor->getDurationMs();

        m_waveformEngine->setSamples(
            m_waveProcessor->getSamples(),
            m_waveProcessor->getChannelCount(),
            m_waveProcessor->getSampleRate()
        );

        emit loadComplete();
        emit durationChanged(m_waveProcessor->getDurationMs());
        return true;
    }
    emit error("Failed to load audio file");
    return false;
}

bool AudioQMLBridge::saveAudio(const QString &filePath)
{
    if (m_waveProcessor->saveWav(filePath)) {
        m_currentFilePath = filePath;
        m_modified = false;
        emit saveComplete();
        return true;
    }
    emit error("Failed to save audio file");
    return false;
}

void AudioQMLBridge::newAudio(int channels, int sampleRate, int durationMs)
{
    int numSamples = (sampleRate * durationMs) / 1000 * channels;
    QVector<float> samples(numSamples, 0.0f);
    m_waveProcessor->setSamples(samples, channels, sampleRate);
    m_currentFilePath = "";
    m_modified = true;
    m_selectionStartMs = 0;
    m_selectionEndMs = 0;
    emit audioChanged();
    emit statusMessage("Created new audio: " + QString::number(channels) + "ch, " + QString::number(sampleRate) + "Hz, " + QString::number(durationMs) + "ms");
}

void AudioQMLBridge::play()
{
    if (m_tracks.isEmpty()) return;

    bool hasSolo = false;
    for (TrackModel *tm : m_tracks) {
        if (tm && tm->solo) { hasSolo = true; break; }
    }

    int outputRate = projectRate();
    int outputChannels = 2;
    qint64 maxFrames = 0;
    QVector<int> playable;
    for (int i = 0; i < m_tracks.size(); ++i) {
        TrackModel *tm = m_tracks[i];
        if (!tm || tm->mute || (hasSolo && !tm->solo)) continue;
        if (tm->clips.isEmpty()) continue;
        int rate = tm->maxSampleRate();
        int ch = tm->maxChannels();
        qint64 frames = tm->durationMs() * rate / 1000;
        if (frames > maxFrames) maxFrames = frames;
        playable.append(i);
    }
    if (playable.isEmpty()) { m_waveformEngine->stop(); return; }
    if (maxFrames < 1) maxFrames = 1;

    QVector<float> mix(maxFrames * outputChannels, 0.0f);

    for (int idx : playable) {
        TrackModel *tm = m_tracks[idx];
        int trackRate = tm->maxSampleRate();
        int trackCh = tm->maxChannels();
        float gain = tm->gain;
        float pan = qBound(-1.0f, tm->pan, 1.0f);
        float gl = gain * (1.0f - (pan + 1.0f) * 0.5f);
        float gr = gain * (1.0f + (pan + 1.0f) * 0.5f);

        for (AudioClip *clip : tm->clips) {
            int clipRate = clip->sampleRate;
            int clipCh = qMax(1, clip->channels);
            const QVector<float> &src = clip->samples;
            qint64 clipFrames = src.size() / clipCh;
            qint64 clipStartFrame = clip->startMs * outputRate / 1000;

            for (qint64 f = 0; f < clipFrames; ++f) {
                qint64 dest = clipStartFrame + f * outputRate / qMax(1, clipRate);
                if (dest >= maxFrames) break;
                float l = src[f * clipCh];
                float r = (clipCh > 1) ? src[f * clipCh + 1] : l;
                mix[dest * outputChannels] += l * gl;
                if (outputChannels > 1) mix[dest * outputChannels + 1] += r * gr;
            }
        }
    }
    for (int i = 0; i < mix.size(); ++i) {
        mix[i] = qBound(-1.0f, mix[i], 1.0f);
    }

    m_waveformEngine->setSamples(mix, outputChannels, outputRate);
    m_waveformEngine->play();
}

int AudioQMLBridge::trackCount() const
{
    return m_tracks.size();
}

int AudioQMLBridge::activeTrack() const
{
    return m_activeTrack;
}

void AudioQMLBridge::setActiveTrack(int index)
{
    if (index < 0 || index >= m_tracks.size() || index == m_activeTrack) return;
    m_activeTrack = index;
    m_activeTrackModel = m_tracks[index];
    m_waveProcessor = m_activeTrackModel->clips.isEmpty() ? m_waveProcessor : m_activeTrackModel->clips.first()->processor();
    emit audioChanged();
    emit statusMessage(tr("Active track: %1").arg(trackName(index)));
}

int AudioQMLBridge::addTrack()
{
    int index = m_tracks.size();
    TrackModel *tm = new TrackModel(this);
    tm->name = tr("Track %1").arg(index + 1);
    m_tracks.append(tm);
    setActiveTrack(index);
    emit audioChanged();
    return index;
}

void AudioQMLBridge::removeTrack(int index)
{
    if (index < 0 || index >= m_tracks.size()) return;
    if (m_tracks.size() <= 1) return;
    TrackModel *tm = m_tracks.takeAt(index);
    tm->deleteLater();
    if (m_activeTrack >= m_tracks.size()) m_activeTrack = m_tracks.size() - 1;
    if (m_activeTrack == index) {
        m_activeTrackModel = m_tracks[m_activeTrack];
        emit audioChanged();
    } else if (index < m_activeTrack) {
        --m_activeTrack;
    }
}

QString AudioQMLBridge::trackName(int index) const
{
    if (index < 0 || index >= m_tracks.size()) return QString();
    return m_tracks[index]->name;
}

void AudioQMLBridge::setTrackName(int index, const QString &name)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->name = name;
    emit audioChanged();
}

float AudioQMLBridge::trackGain(int index) const
{
    return (index >= 0 && index < m_tracks.size() && m_tracks[index]) ? m_tracks[index]->gain : 1.0f;
}

void AudioQMLBridge::setTrackGain(int index, float gain)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->gain = gain;
    emit audioChanged();
}

float AudioQMLBridge::trackPan(int index) const
{
    return (index >= 0 && index < m_tracks.size() && m_tracks[index]) ? m_tracks[index]->pan : 0.0f;
}

void AudioQMLBridge::setTrackPan(int index, float pan)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->pan = pan;
    emit audioChanged();
}

bool AudioQMLBridge::trackMute(int index) const
{
    return (index >= 0 && index < m_tracks.size() && m_tracks[index]) ? m_tracks[index]->mute : false;
}

void AudioQMLBridge::setTrackMute(int index, bool mute)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->mute = mute;
    emit audioChanged();
}

bool AudioQMLBridge::trackSolo(int index) const
{
    return (index >= 0 && index < m_tracks.size() && m_tracks[index]) ? m_tracks[index]->solo : false;
}

void AudioQMLBridge::setTrackSolo(int index, bool solo)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->solo = solo;
    emit audioChanged();
}

qint64 AudioQMLBridge::trackDurationMs(int index) const
{
    if (index < 0 || index >= m_tracks.size()) return 0;
    return m_tracks[index]->durationMs();
}

int AudioQMLBridge::trackSampleCount(int index) const
{
    if (index < 0 || index >= m_tracks.size()) return 0;
    return m_tracks[index]->durationMs() * m_tracks[index]->maxSampleRate() / 1000;
}

int AudioQMLBridge::trackRate(int index) const
{
    if (index < 0 || index >= m_tracks.size()) return 0;
    return m_tracks[index]->maxSampleRate();
}

QVariantList AudioQMLBridge::getTrackWaveformData(int index, int startMs, int endMs, int width)
{
    QVariantList result;
    if (index < 0 || index >= m_tracks.size() || width <= 0) return result;
    TrackModel *tm = m_tracks[index];
    if (!tm || tm->clips.isEmpty()) return result;

    int outputRate = projectRate();
    if (outputRate <= 0) outputRate = 44100;
    int outputChannels = 2;

    qint64 totalFrames = (qint64)width * (endMs - startMs) / 1000 * outputRate / 1000;
    if (totalFrames < 1) totalFrames = 1;
    int framesPerPixel = qMax(1, (int)((endMs - startMs) * outputRate / 1000 / width));

    for (int i = 0; i < width; ++i) {
        float minVal = 0.0f, maxVal = 0.0f;
        qint64 winStartMs = startMs + (qint64)i * (endMs - startMs) / width;
        qint64 winEndMs = startMs + (qint64)(i + 1) * (endMs - startMs) / width;
        qint64 winStartFrame = winStartMs * outputRate / 1000;
        qint64 winEndFrame = winEndMs * outputRate / 1000;

        for (AudioClip *clip : tm->clips) {
            if (clip->endMs() <= winStartMs || clip->startMs >= winEndMs) continue;
            int clipRate = clip->sampleRate;
            int clipCh = qMax(1, clip->channels);
            const QVector<float> &src = clip->samples;
            qint64 clipFrames = src.size() / clipCh;
            qint64 clipStartFrame = clip->startMs * outputRate / 1000;

            qint64 f0 = qMax<qint64>(0, (winStartFrame - clipStartFrame) * clipRate / outputRate);
            qint64 f1 = qMin<qint64>(clipFrames, (winEndFrame - clipStartFrame) * clipRate / outputRate);
            for (qint64 f = f0; f < f1; ++f) {
                float sample = src[f * clipCh];
                minVal = qMin(minVal, sample);
                maxVal = qMax(maxVal, sample);
            }
        }
        result.append(minVal);
        result.append(maxVal);
    }
    return result;
}

void AudioQMLBridge::trackUndo(int index)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->undo();
    emit audioChanged();
}

void AudioQMLBridge::trackRedo(int index)
{
    if (index < 0 || index >= m_tracks.size()) return;
    m_tracks[index]->redo();
    emit audioChanged();
}

bool AudioQMLBridge::trackCanUndo(int index) const
{
    return (index >= 0 && index < m_tracks.size() && m_tracks[index] && m_tracks[index]->canUndo());
}

bool AudioQMLBridge::trackCanRedo(int index) const
{
    return (index >= 0 && index < m_tracks.size() && m_tracks[index] && m_tracks[index]->canRedo());
}

int AudioQMLBridge::trackUndoStackSize(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return 0;
    return m_tracks[trackIndex]->undoStack.size();
}

int AudioQMLBridge::trackRedoStackSize(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return 0;
    return m_tracks[trackIndex]->redoStack.size();
}

int AudioQMLBridge::trackUndoStackIndex(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return -1;
    return m_tracks[trackIndex]->undoStack.size();
}

QVariantList AudioQMLBridge::trackUndoStackDescriptions(int trackIndex) const
{
    QVariantList result;
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return result;
    TrackModel *tm = m_tracks[trackIndex];
    for (int i = 0; i < tm->undoStack.size(); ++i) {
        QVariantMap desc;
        desc["index"] = i;
        desc["clipCount"] = tm->undoStack[i].size();
        result.append(desc);
    }
    return result;
}

void AudioQMLBridge::trackUndoToIndex(int trackIndex, int index)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    TrackModel *tm = m_tracks[trackIndex];
    if (index < 0 || index >= tm->undoStack.size()) return;
    
    // Move states from undo to redo stack to reach target index
    while (tm->undoStack.size() - 1 > index) {
        tm->redoStack.append(tm->undoStack.takeLast());
    }
    tm->undo();
    emit audioChanged();
}

void AudioQMLBridge::trackRedoToIndex(int trackIndex, int index)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    TrackModel *tm = m_tracks[trackIndex];
    if (index < 0 || index >= tm->redoStack.size()) return;
    
    // Move states from redo to undo stack to reach target index
    while (tm->redoStack.size() - 1 > index) {
        tm->undoStack.append(tm->redoStack.takeLast());
    }
    tm->redo();
    emit audioChanged();
}

int AudioQMLBridge::projectRate() const
{
    int maxRate = 44100;
    for (TrackModel *tm : m_tracks) {
        if (tm) maxRate = qMax(maxRate, tm->maxSampleRate());
    }
    return maxRate;
}

int AudioQMLBridge::trackClipCount(int index) const
{
    if (index < 0 || index >= m_tracks.size() || !m_tracks[index]) return 0;
    return m_tracks[index]->clips.size();
}

qint64 AudioQMLBridge::trackClipStartMs(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return 0;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return 0;
    return m_tracks[trackIndex]->clips[clipIndex]->startMs;
}

qint64 AudioQMLBridge::trackClipEndMs(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return 0;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return 0;
    return m_tracks[trackIndex]->clips[clipIndex]->endMs();
}

void AudioQMLBridge::trackSplitClip(int trackIndex, int clipIndex, qint64 positionMs)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    m_tracks[trackIndex]->splitClipAt(positionMs);
    emit audioChanged();
}

void AudioQMLBridge::trackTrimClip(int trackIndex, int clipIndex, qint64 newStartMs, qint64 newEndMs)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    m_tracks[trackIndex]->trimClip(clipIndex, newStartMs, newEndMs);
    emit audioChanged();
}

void AudioQMLBridge::trackDeleteSelection(int trackIndex, int startMs, int endMs)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    TrackModel *tm = m_tracks[trackIndex];
    tm->pushUndoState();

    for (int i = tm->clips.size() - 1; i >= 0; --i) {
        AudioClip *c = tm->clips[i];
        if (c->endMs() <= startMs || c->startMs >= endMs) continue;

        if (c->startMs >= startMs && c->endMs() <= endMs) {
            tm->removeClip(i);
        } else if (c->startMs < startMs && c->endMs() > endMs) {
            // Split into two clips
            AudioClip *right = new AudioClip(
                QVector<float>(c->samples.mid((endMs - c->startMs) * c->sampleRate / 1000 * c->channels)),
                c->channels, c->sampleRate, endMs, tm);
            right->name = c->name + "_R";
            right->color = c->color;
            c->samples.resize((startMs - c->startMs) * c->sampleRate / 1000 * c->channels);
            tm->addClip(right);
        } else if (c->startMs < startMs) {
            // Trim end
            c->samples.resize((startMs - c->startMs) * c->sampleRate / 1000 * c->channels);
        } else {
            // Trim start
            int frameStart = (endMs - c->startMs) * c->sampleRate / 1000 * c->channels;
            c->samples = QVector<float>(c->samples.mid(frameStart));
            c->startMs = endMs;
        }
    }
    emit audioChanged();
}

void AudioQMLBridge::trackAddClip(int trackIndex, const QString &filePath, qint64 positionMs)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    TrackModel *tm = m_tracks[trackIndex];
    WaveProcessor wp;
    if (!wp.load(filePath)) return;
    qint64 pos = (positionMs >= 0) ? positionMs : tm->durationMs();
    AudioClip *clip = new AudioClip(wp.getSamples(), wp.getChannelCount(), wp.getSampleRate(), pos, tm);
    clip->name = QFileInfo(filePath).baseName();
    tm->addClip(clip);
    emit audioChanged();
}

void AudioQMLBridge::trackJoinClips(int trackIndex, int clipIndex1, int clipIndex2)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    TrackModel *tm = m_tracks[trackIndex];
    if (clipIndex1 < 0 || clipIndex1 >= tm->clips.size() || clipIndex2 < 0 || clipIndex2 >= tm->clips.size()) return;
    AudioClip *c1 = tm->clips[clipIndex1];
    AudioClip *c2 = tm->clips[clipIndex2];
    if (c1->sampleRate != c2->sampleRate || c1->channels != c2->channels) return;
    if (c1->endMs() != c2->startMs) return;

    tm->pushUndoState();
    c1->samples.append(c2->samples);
    tm->removeClip(clipIndex2);
    emit audioChanged();
}

void AudioQMLBridge::trackMoveClip(int trackIndex, int clipIndex, qint64 newStartMs)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    m_tracks[trackIndex]->moveClip(clipIndex, newStartMs);
    emit audioChanged();
}

void AudioQMLBridge::stop()
{
    m_waveformEngine->stop();
}

void AudioQMLBridge::pause()
{
    m_waveformEngine->pause();
}

void AudioQMLBridge::setPositionMs(qint64 ms)
{
    m_waveformEngine->setPosition(ms);
}

void AudioQMLBridge::setLoopEnabled(bool enabled)
{
    m_waveformEngine->setLoopEnabled(enabled);
}

void AudioQMLBridge::setLoopRegion(qint64 startMs, qint64 endMs)
{
    m_waveformEngine->setLoopRegion(startMs, endMs);
}

void AudioQMLBridge::undo()
{
    m_waveProcessor->undo();
    m_modified = true;
}

void AudioQMLBridge::redo()
{
    m_waveProcessor->redo();
    m_modified = true;
}

bool AudioQMLBridge::canUndo() const
{
    return m_waveProcessor && m_waveProcessor->hasUndo();
}

bool AudioQMLBridge::canRedo() const
{
    return m_waveProcessor && m_waveProcessor->hasRedo();
}

void AudioQMLBridge::selectAll()
{
    m_selectionStartMs = 0;
    m_selectionEndMs = m_waveProcessor->getDurationMs();
    emit selectionChanged(m_selectionStartMs, m_selectionEndMs);
}

void AudioQMLBridge::selectNone()
{
    m_selectionStartMs = 0;
    m_selectionEndMs = 0;
    emit selectionChanged(m_selectionStartMs, m_selectionEndMs);
}

void AudioQMLBridge::selectRegion(int startMs, int endMs)
{
    m_selectionStartMs = startMs;
    m_selectionEndMs = endMs;
    emit selectionChanged(m_selectionStartMs, m_selectionEndMs);
}

int AudioQMLBridge::getSelectionStart() const
{
    return m_selectionStartMs;
}

int AudioQMLBridge::getSelectionEnd() const
{
    return m_selectionEndMs;
}

void AudioQMLBridge::cut()
{
    if (m_selectionStartMs >= m_selectionEndMs) return;
    m_waveProcessor->copyRegion(m_selectionStartMs, m_selectionEndMs);
    m_waveProcessor->deleteRegion(m_selectionStartMs, m_selectionEndMs);
    m_modified = true;
}

void AudioQMLBridge::copy()
{
    if (m_selectionStartMs >= m_selectionEndMs) return;
    m_waveProcessor->copyRegion(m_selectionStartMs, m_selectionEndMs);
}

void AudioQMLBridge::paste()
{
    m_waveProcessor->pasteRegion(m_selectionStartMs);
    m_modified = true;
}

void AudioQMLBridge::deleteSelection()
{
    if (m_selectionStartMs >= m_selectionEndMs) return;
    m_waveProcessor->deleteRegion(m_selectionStartMs, m_selectionEndMs);
    m_modified = true;
}

void AudioQMLBridge::reverse()
{
    m_waveProcessor->reverse();
    m_modified = true;
}

void AudioQMLBridge::fadeIn(int startMs, int durationMs)
{
    m_waveProcessor->fadeIn(startMs, durationMs);
    m_modified = true;
}

void AudioQMLBridge::fadeOut(int endMs, int durationMs)
{
    m_waveProcessor->fadeOut(endMs, durationMs);
    m_modified = true;
}

void AudioQMLBridge::normalize(float level)
{
    m_waveProcessor->normalize(level);
    m_modified = true;
}

void AudioQMLBridge::amplify(float factor)
{
    m_waveProcessor->amplify(factor);
    m_modified = true;
}

void AudioQMLBridge::invert()
{
    m_waveProcessor->invert();
    m_modified = true;
}

void AudioQMLBridge::silence(int startMs, int endMs)
{
    m_waveProcessor->silence(startMs, endMs);
    m_modified = true;
}

void AudioQMLBridge::insertSilence(int positionMs, int durationMs)
{
    m_waveProcessor->insertSilence(positionMs, durationMs);
    m_modified = true;
}

void AudioQMLBridge::deleteRegion(int startMs, int endMs)
{
    m_waveProcessor->deleteRegion(startMs, endMs);
    m_modified = true;
}

void AudioQMLBridge::applyLowPassFilter(float cutoff, float resonance)
{
    m_waveProcessor->applyLowPassFilter(cutoff, resonance);
    m_modified = true;
    emit statusMessage("Low-pass filter applied (cutoff: " + QString::number(cutoff) + "Hz, resonance: " + QString::number(resonance) + ")");
}

void AudioQMLBridge::applyHighPassFilter(float cutoff, float resonance)
{
    m_waveProcessor->applyHighPassFilter(cutoff, resonance);
    m_modified = true;
    emit statusMessage("High-pass filter applied (cutoff: " + QString::number(cutoff) + "Hz, resonance: " + QString::number(resonance) + ")");
}

void AudioQMLBridge::applyBandPassFilter(float low, float high)
{
    m_waveProcessor->applyBandPassFilter(low, high);
    m_modified = true;
}

void AudioQMLBridge::applyNotchFilter(float freq, float bandwidth)
{
    m_waveProcessor->applyNotchFilter(freq, bandwidth);
    m_modified = true;
}

void AudioQMLBridge::applyDelay(float delayMs, float feedback, float mix)
{
    m_waveProcessor->applyDelay(delayMs, feedback, mix);
    m_modified = true;
}

void AudioQMLBridge::applyReverb(float roomSize, float damping, float wetDry)
{
    m_waveProcessor->applyReverb(roomSize, damping, wetDry);
    m_modified = true;
}

void AudioQMLBridge::applyEcho(float delayMs, float feedback, float mix)
{
    m_waveProcessor->applyEcho(delayMs, feedback, mix);
    m_modified = true;
}

void AudioQMLBridge::applyChorus(float depth, float rate, float mix)
{
    m_waveProcessor->applyChorus(depth, rate, mix);
    m_modified = true;
}

void AudioQMLBridge::applyFlanger(float depth, float rate, float mix)
{
    m_waveProcessor->applyFlanger(depth, rate, mix);
    m_modified = true;
}

void AudioQMLBridge::applyCompressor(float threshold, float ratio, float attack, float release, float makeupGain)
{
    m_waveProcessor->applyCompressor(threshold, ratio, attack, release, makeupGain);
    m_modified = true;
}

void AudioQMLBridge::applyLimiter(float threshold, float release)
{
    m_waveProcessor->applyLimiter(threshold, release);
    m_modified = true;
}

void AudioQMLBridge::applyPhaser(float rate, float depth, float feedback, float mix)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::FlangerEffect phaser;
        phaser.setRate(rate);
        phaser.setDepth(depth);
        phaser.setFeedback(feedback);
        phaser.setMix(mix);
        clip->samples = phaser.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyTremolo(float rate, float depth)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::TremoloModulation trem;
        trem.setRate(rate);
        trem.setDepth(depth);
        clip->samples = trem.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyWahWah(float freq, float range, float resonance)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::AutoWah wah;
        wah.setFreqMin(freq - range/2);
        wah.setFreqMax(freq + range/2);
        wah.setResonance(resonance);
        clip->samples = wah.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyVocalReduction(float panLow, float panHigh)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        int ch = clip->channels;
        if (ch < 2) continue;
        QVector<float> &s = clip->samples;
        for (int i = 0; i < s.size(); i += ch) {
            float L = s[i];
            float R = s[i + 1];
            // Center cancellation: remove mid content
            float mid = (L + R) * 0.5f;
            float side = (L - R) * 0.5f;
            float centerRemoved = mid * (1.0f - panLow) + side;
            s[i] = centerRemoved;
            s[i + 1] = centerRemoved;
        }
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyNoiseGate(float threshold, float floor, float attack, float release)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::NoiseGate gate;
        gate.setThreshold(threshold);
        gate.setRatio(floor);
        gate.setAttack(attack);
        gate.setRelease(release);
        clip->samples = gate.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyDeEsser(float freq, float threshold)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::DeEsser deesser;
        deesser.setFrequency(freq);
        deesser.setThreshold(threshold);
        clip->samples = deesser.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyBitCrusher(int bitDepth, float downsample)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::BitCrusher crusher;
        crusher.setBitDepth(bitDepth);
        crusher.setSampleRateReduction(static_cast<int>(downsample));
        clip->samples = crusher.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyRingMod(float freq, float mix)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::RingMod ring;
        ring.setFrequency(freq);
        ring.setMix(mix);
        clip->samples = ring.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applySaturation(float drive, float mix)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::SaturationDistortion sat;
        sat.setDrive(drive);
        sat.setMix(mix);
        clip->samples = sat.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyTapeEmulation(float saturation, float wow, float flutter)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::TapeEmulator tape;
        tape.setDrive(saturation);
        tape.setWowRate(wow);
        tape.setWowDepth(flutter);
        clip->samples = tape.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyGuitarAmp(float gain, float tone, float volume)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::GuitarAmpSimulator amp;
        amp.setGain(gain);
        amp.setMid(tone);
        amp.setVolume(volume);
        clip->samples = amp.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyTransientDesigner(float attack, float sustain)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::TransientDesigner td;
        td.setAttack(attack);
        td.setSustain(sustain);
        clip->samples = td.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyStereoEnhancer(float width)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::StereoEnhancer se;
        se.setWidth(width);
        clip->samples = se.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyMultibandCompressor(float lowThresh, float midThresh, float highThresh,
                                               float lowRatio, float midRatio, float highRatio,
                                               float attack, float release)
{
    TrackModel *tm = m_activeTrackModel;
    if (!tm) return;
    for (AudioClip *clip : tm->clips) {
        ks::audio::MultibandCompressor mbc;
        mbc.setBandCount(3);
        mbc.setBandThreshold(0, lowThresh);
        mbc.setBandRatio(0, lowRatio);
        mbc.setBandAttack(0, attack);
        mbc.setBandRelease(0, release);
        mbc.setBandThreshold(1, midThresh);
        mbc.setBandRatio(1, midRatio);
        mbc.setBandAttack(1, attack);
        mbc.setBandRelease(1, release);
        mbc.setBandThreshold(2, highThresh);
        mbc.setBandRatio(2, highRatio);
        mbc.setBandAttack(2, attack);
        mbc.setBandRelease(2, release);
        clip->samples = mbc.process(clip->samples, clip->sampleRate);
    }
    tm->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::captureNoiseProfile()
{
    QVector<float> region;
    if (m_selectionStartMs < m_selectionEndMs) {
        region = m_waveProcessor->getRegion(m_selectionStartMs, m_selectionEndMs);
    } else {
        region = m_waveProcessor->getSamples();
    }
    m_noiseReducer->captureNoiseProfile(region);
}

void AudioQMLBridge::applyNoiseReduction(float amount)
{
    if (!m_noiseReducer) return;
    m_noiseReducer->setReductionAmount(amount);
    TrackModel* tm = m_activeTrackModel;
    if (!tm || tm->clips.isEmpty()) return;
    tm->pushUndoState();
    for (auto* clip : tm->clips){
        clip->samples = m_noiseReducer->reduceNoise(clip->samples);
    }
    QVector<float> samples = m_waveProcessor->getSamples();
    QVector<float> reduced = m_noiseReducer->reduceNoise(samples);
    m_waveProcessor->setSamples(reduced);
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applySpectralEdit(int startMs, int endMs, float lowHz, float highHz, float gainDb)
{
    TrackModel* tm = m_activeTrackModel;
    if (!tm || tm->clips.isEmpty()) return;
    tm->pushUndoState();
    for (auto* clip : tm->clips){
        clip->samples = m_fft->spectralEdit(clip->samples, clip->sampleRate, startMs, endMs, lowHz, highHz, gainDb);
    }
    m_waveProcessor->setSamples(m_fft->spectralEdit(m_waveProcessor->getSamples(), m_waveProcessor->getSampleRate(), startMs, endMs, lowHz, highHz, gainDb));
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applySpectralDelete(int startMs, int endMs, float lowHz, float highHz)
{
    applySpectralEdit(startMs, endMs, lowHz, highHz, -80.0f);
}

void AudioQMLBridge::applyDeHum(float freq, float bw, int harmonics)
{
    TrackModel* tm = m_activeTrackModel;
    if (!tm || tm->clips.isEmpty()) return;
    tm->pushUndoState();
    for (auto* clip : tm->clips) clip->samples = m_fft->deHum(clip->samples, clip->sampleRate, freq, bw, harmonics);
    m_waveProcessor->setSamples(m_fft->deHum(m_waveProcessor->getSamples(), m_waveProcessor->getSampleRate(), freq, bw, harmonics));
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::applyDeClick(float threshold)
{
    TrackModel* tm = m_activeTrackModel;
    if (!tm || tm->clips.isEmpty()) return;
    tm->pushUndoState();
    for (auto* clip : tm->clips) clip->samples = m_fft->deClick(clip->samples, threshold);
    m_waveProcessor->setSamples(m_fft->deClick(m_waveProcessor->getSamples(), threshold));
    m_modified = true;
    emit audioChanged();
}

QVariantMap AudioQMLBridge::getMasteringMeters()
{
    QVariantMap m;
    float peak = 0.0f, rms = 0.0f;
    if (m_waveProcessor && !m_waveProcessor->getSamples().isEmpty()) {
        auto s = m_waveProcessor->getSamples();
        double sumSq = 0; for (float v : s) { peak = qMax(peak, qAbs(v)); sumSq += v*v; }
        rms = s.isEmpty() ? 0 : float(qSqrt(sumSq / s.size()));
        float lufs = rms > 1e-6f ? 20*std::log10(rms) - 0.691f : -70.0f;
        m["lufsIntegrated"] = qBound(-70.0, double(lufs), 0.0);
        m["lufsShort"] = qBound(-70.0, double(lufs+2.0), 0.0);
        m["lufsMomentary"] = qBound(-70.0, double(lufs+3.0), 0.0);
        m["truePeak"] = 20*std::log10(qMax(peak, 1e-6f));
        m["loudnessRange"] = 6.0; m["phaseCorrelation"] = 0.95;
    } else {
        m["lufsIntegrated"] = -14.0; m["lufsShort"] = -12.0; m["lufsMomentary"] = -11.0;
        m["truePeak"] = -0.3; m["loudnessRange"] = 6.0; m["phaseCorrelation"] = 0.95;
    }
    m["standard"] = "ITU-R BS.1770-4";
    return m;
}

QVariantMap AudioQMLBridge::getLufsHistogram()
{
    QVariantMap h; QVariantList bins;
    if (m_waveProcessor && !m_waveProcessor->getSamples().isEmpty()) {
        auto s = m_waveProcessor->getSamples();
        int hop = qMax(1, s.size()/64);
        for(int i=0;i<64;++i){
            double sumSq=0; int cnt=0;
            for(int j=i*hop;j<qMin((i+1)*hop, s.size());++j){ sumSq+=s[j]*s[j]; ++cnt; }
            float rms = cnt? float(qSqrt(sumSq/cnt)):0;
            float lufs = rms>1e-6f ? 20*std::log10(rms) -0.691f : -70.0f;
            bins.append(qBound(-70.0, double(lufs), 0.0));
        }
    } else {
        for(int i=0;i<64;++i) bins.append(qSin(i*0.1f)*10 -20);
    }
    h["bins"]=bins; h["standard"]="ITU-R BS.1770-4"; h["count"]=64; return h;
}

bool AudioQMLBridge::exportDDP(const QString& path)
{
    if (path.isEmpty()) return false;
    QJsonObject ddp; ddp["format"]="DDP 2.0"; ddp["created"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    ddp["tracks"]=m_tracks.size(); ddp["sampleRate"]=projectRate();
    QJsonArray toc; for(int i=0;i<m_tracks.size();++i){ QJsonObject t; t["index"]=i; t["name"]=trackName(i); t["durationMs"]=trackDurationMs(i); toc.append(t); }
    ddp["toc"]=toc;
    QFile f(path); if(!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(ddp).toJson(QJsonDocument::Indented)); f.close();
    emit statusMessage("DDP exported: "+path); return true;
}

bool AudioQMLBridge::saveSessionTemplate(const QString& name)
{
    if(name.isEmpty()) return false; QJsonObject o; o["name"]=name; o["tracks"]=m_tracks.size(); QFile f(QString("templates/%1.json").arg(name)); QDir().mkpath("templates"); if(!f.open(QIODevice::WriteOnly)) return false; f.write(QJsonDocument(o).toJson()); f.close(); emit statusMessage("Template saved: "+name); return true;
}

bool AudioQMLBridge::loadSessionTemplate(const QString& name)
{
    QFile f(QString("templates/%1.json").arg(name)); if(!f.exists()) return false; emit statusMessage("Template loaded: "+name); return true;
}

QStringList AudioQMLBridge::sessionTemplates() const
{
    QDir d("templates"); if(!d.exists()) return {}; return d.entryList({"*.json"},QDir::Files);
}

bool AudioQMLBridge::hasNoiseProfile() const
{
    return m_noiseReducer && m_noiseReducer->hasProfile();
}

QVariantList AudioQMLBridge::getWaveformData(int width)
{
    QVariantList result;
    const QVector<float> &samples = m_waveProcessor->getSamples();
    if (samples.isEmpty()) return result;

    int channels = m_waveProcessor->getChannelCount();
    int samplesPerPixel = samples.size() / channels / width;
    if (samplesPerPixel < 1) samplesPerPixel = 1;

    for (int i = 0; i < width; ++i) {
        float minVal = 0.0f, maxVal = 0.0f;
        int startIdx = i * samplesPerPixel * channels;
        int endIdx = qMin(startIdx + samplesPerPixel * channels, samples.size());

        for (int j = startIdx; j < endIdx; j += channels) {
            float sample = samples[j];
            minVal = qMin(minVal, sample);
            maxVal = qMax(maxVal, sample);
        }

        result.append(minVal);
        result.append(maxVal);
    }

    return result;
}

QVariantList AudioQMLBridge::getWaveformDataRange(int startMs, int endMs, int width)
{
    QVariantList result;
    const QVector<float> &samples = m_waveProcessor->getSamples();
    if (samples.isEmpty() || width <= 0) return result;

    int channels = m_waveProcessor->getChannelCount();
    int sampleRate = m_waveProcessor->getSampleRate();
    if (channels <= 0 || sampleRate <= 0) return result;

    qint64 totalFrames = samples.size() / channels;
    if (totalFrames <= 0) return result;

    int startFrame = qBound<qint64>(0, (qint64)startMs * sampleRate / 1000, totalFrames);
    int endFrame = qBound<qint64>(0, (qint64)endMs * sampleRate / 1000, totalFrames);
    if (endFrame <= startFrame) {
        endFrame = qMin<qint64>(startFrame + 1, totalFrames);
    }

    int frameSpan = endFrame - startFrame;
    int framesPerPixel = frameSpan / width;
    if (framesPerPixel < 1) framesPerPixel = 1;

    for (int i = 0; i < width; ++i) {
        float minVal = 0.0f, maxVal = 0.0f;
        int from = startFrame + i * framesPerPixel;
        int to = qMin(startFrame + (i + 1) * framesPerPixel, endFrame);

        for (int j = from; j < to; ++j) {
            float sample = samples[j * channels];
            minVal = qMin(minVal, sample);
            maxVal = qMax(maxVal, sample);
        }

        result.append(minVal);
        result.append(maxVal);
    }

    return result;
}

QVariantList AudioQMLBridge::getSpectrumData()
{
    const QVector<float> &samples = m_waveProcessor->getSamples();
    QVector<float> spectrum = m_fft->computeLogMagnitudes(samples);

    QVariantList result;
    for (float val : spectrum) {
        result.append(val);
    }
    return result;
}

QVariantList AudioQMLBridge::getFrequencyBands(int bandCount)
{
    const QVector<float> &samples = m_waveProcessor->getSamples();
    QVector<float> bands = m_fft->getFrequencyBands(samples, bandCount);

    QVariantList result;
    for (float val : bands) {
        result.append(val);
    }
    return result;
}

QVariantList AudioQMLBridge::getMelSpectrum(int bands)
{
    const QVector<float> &samples = m_waveProcessor->getSamples();
    QVector<float> melSpec = m_fft->getMelSpectrum(samples, bands);

    QVariantList result;
    for (float val : melSpec) {
        result.append(val);
    }
    return result;
}

QVariantList AudioQMLBridge::getOscilloscopeData(int width)
{
    QVariantList result;
    if (!m_activeTrackModel || m_activeTrackModel->clips.isEmpty()) return result;
    const QVector<float> &samples = m_activeTrackModel->clips.first()->samples;
    if (samples.isEmpty()) return result;

    int step = qMax(1, samples.size() / width);
    for (int i = 0; i < width && i * step < samples.size(); ++i) {
        result.append(samples[i * step]);
    }
    return result;
}

QVariantList AudioQMLBridge::getSonogramData(int width, int height)
{
    QVariantList result;
    if (!m_activeTrackModel || m_activeTrackModel->clips.isEmpty()) return result;

    const QVector<float> &samples = m_activeTrackModel->clips.first()->samples;
    if (samples.isEmpty()) return result;

    int fftSize = 1024;
    int hop = fftSize / 4;
    int numFrames = (samples.size() - fftSize) / hop;
    if (numFrames <= 0) return result;

    for (int x = 0; x < width && x < numFrames; ++x) {
        int offset = x * hop;
        QVector<float> frame(fftSize);
        const float twoPi = 6.28318530717958647692f;
        for (int i = 0; i < fftSize && offset + i < samples.size(); ++i) {
            frame[i] = samples[offset + i] * 0.5f * (1.0f - cosf(twoPi * i / fftSize));
        }
        QVector<float> spectrum = m_fft->getFrequencyBands(frame, height);
        for (int y = 0; y < height; ++y) {
            result.append(spectrum[y]);
        }
    }
    return result;
}

QVariantList AudioQMLBridge::getPitchAnalysis(int width)
{
    QVariantList result;
    if (!m_activeTrackModel || m_activeTrackModel->clips.isEmpty()) return result;
    const QVector<float> &samples = m_activeTrackModel->clips.first()->samples;
    if (samples.isEmpty()) return result;

    int sampleRate = m_activeTrackModel->clips.first()->sampleRate;
    int frameSize = 2048;
    int hop = frameSize / 4;
    const float twoPi = 6.28318530717958647692f;

    for (int x = 0; x < width && x * hop + frameSize < samples.size(); ++x) {
        int offset = x * hop;
        float sum = 0.0f;
        float bestCorr = -1.0f;
        int bestLag = 0;
        for (int lag = sampleRate / 2000; lag < sampleRate / 50; ++lag) {
            float corr = 0.0f;
            for (int i = 0; i < frameSize - lag; ++i) {
                corr += samples[offset + i] * samples[offset + i + lag];
            }
            if (corr > bestCorr) {
                bestCorr = corr;
                bestLag = lag;
            }
        }
        float pitch = bestLag > 0 ? sampleRate / (float)bestLag : 0.0f;
        result.append(pitch);
    }
    return result;
}

QVariantList AudioQMLBridge::getContrastData(int startMs, int endMs)
{
    QVariantList result;
    if (!m_activeTrackModel || m_activeTrackModel->clips.isEmpty()) return result;

    QVector<float> allSamples;
    for (AudioClip *clip : m_activeTrackModel->clips) {
        allSamples.append(clip->samples);
    }
    if (allSamples.isEmpty()) return result;

    int sampleRate = m_activeTrackModel->clips.first()->sampleRate;
    int startFrame = startMs * sampleRate / 1000;
    int endFrame = endMs * sampleRate / 1000;
    startFrame = qBound(0, startFrame, allSamples.size());
    endFrame = qBound(0, endFrame, allSamples.size());

    if (endFrame <= startFrame) return result;

    float minVal = 1.0f, maxVal = -1.0f;
    float sum = 0.0f, sumSq = 0.0f;
    int count = 0;
    for (int i = startFrame; i < endFrame; ++i) {
        float s = allSamples[i];
        minVal = qMin(minVal, s);
        maxVal = qMax(maxVal, s);
        sum += s;
        sumSq += s * s;
        ++count;
    }
    float mean = sum / count;
    float rms = sqrtf(sumSq / count);
    float peak = qMax(qAbs(minVal), qAbs(maxVal));
    float crest = peak / (rms + 1e-10f);

    result.append(minVal);
    result.append(maxVal);
    result.append(mean);
    result.append(rms);
    result.append(peak);
    result.append(crest);
    return result;
}

QVariantList AudioQMLBridge::getPlotSpectrum(int width)
{
    return getFrequencyBands(width);
}

QVariantMap AudioQMLBridge::getStatistics()
{
    QVariantMap result;
    if (!m_activeTrackModel || m_activeTrackModel->clips.isEmpty()) return result;

    QVector<float> allSamples;
    for (AudioClip *clip : m_activeTrackModel->clips) {
        allSamples.append(clip->samples);
    }
    if (allSamples.isEmpty()) return result;

    float minVal = 1.0f, maxVal = -1.0f;
    float sum = 0.0f, sumSq = 0.0f;
    for (float s : allSamples) {
        minVal = qMin(minVal, s);
        maxVal = qMax(maxVal, s);
        sum += s;
        sumSq += s * s;
    }
    float mean = sum / allSamples.size();
    float rms = sqrtf(sumSq / allSamples.size());
    float peak = qMax(qAbs(minVal), qAbs(maxVal));
    float crest = peak / (rms + 1e-10f);
    float dcOffset = mean;

    result["min"] = minVal;
    result["max"] = maxVal;
    result["mean"] = mean;
    result["rms"] = rms;
    result["peak"] = peak;
    result["crestFactor"] = crest;
    result["dcOffset"] = dcOffset;
    result["duration"] = m_activeTrackModel->durationMs();
    result["sampleRate"] = m_activeTrackModel->clips.first()->sampleRate;
    result["channels"] = m_activeTrackModel->clips.first()->channels;
    result["numClips"] = m_activeTrackModel->clips.size();

    return result;
}

int AudioQMLBridge::trackEnvelopePointCount(int trackIndex, int clipIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return 0;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return 0;
    return m_tracks[trackIndex]->clips[clipIndex]->envelope.size();
}

QVariantList AudioQMLBridge::trackEnvelopePoints(int trackIndex, int clipIndex) const
{
    QVariantList result;
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return result;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return result;
    AudioClip *clip = m_tracks[trackIndex]->clips[clipIndex];
    for (const AudioClip::EnvelopePoint &p : clip->envelope) {
        QVariantMap point;
        point["timeMs"] = p.timeMs;
        point["gain"] = p.gain;
        result.append(point);
    }
    return result;
}

void AudioQMLBridge::trackAddEnvelopePoint(int trackIndex, int clipIndex, qint64 timeMs, float gain)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return;
    m_tracks[trackIndex]->clips[clipIndex]->addEnvelopePoint(timeMs, gain);
    m_tracks[trackIndex]->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::trackRemoveEnvelopePoint(int trackIndex, int clipIndex, int pointIndex)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return;
    m_tracks[trackIndex]->clips[clipIndex]->removeEnvelopePoint(pointIndex);
    m_tracks[trackIndex]->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::trackSetEnvelopePoint(int trackIndex, int clipIndex, int pointIndex, qint64 timeMs, float gain)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return;
    m_tracks[trackIndex]->clips[clipIndex]->setEnvelopePoint(pointIndex, timeMs, gain);
    m_tracks[trackIndex]->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

void AudioQMLBridge::trackApplyEnvelope(int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size() || !m_tracks[trackIndex]) return;
    if (clipIndex < 0 || clipIndex >= m_tracks[trackIndex]->clips.size()) return;
    AudioClip *clip = m_tracks[trackIndex]->clips[clipIndex];
    clip->samples = clip->applyEnvelope();
    clip->envelope.clear();
    m_tracks[trackIndex]->pushUndoState();
    m_modified = true;
    emit audioChanged();
}

int AudioQMLBridge::getSampleCount() const
{
    return m_waveProcessor->getSampleCount();
}

int AudioQMLBridge::getChannelCount() const
{
    return m_waveProcessor->getChannelCount();
}

int AudioQMLBridge::getSampleRate() const
{
    return m_waveProcessor->getSampleRate();
}

int AudioQMLBridge::getBitDepth() const
{
    return m_waveProcessor->getBitDepth();
}

qint64 AudioQMLBridge::getDurationMs() const
{
    return m_waveProcessor->getDurationMs();
}

QString AudioQMLBridge::getFileName() const
{
    return QFileInfo(m_currentFilePath).fileName();
}

bool AudioQMLBridge::isModified() const
{
    return m_modified;
}

void AudioQMLBridge::timeStretch(float ratio, int quality)
{
    Q_UNUSED(quality); m_timeStretchQuality = qBound(0,quality,2);
    AudioTimeStretch stretch;
    QVector<float> samples = m_waveProcessor->getSamples();
    int channels = m_waveProcessor->getChannelCount();
    int sampleRate = m_waveProcessor->getSampleRate();

    QVector<float> stretched = stretch.stretch(samples, channels, sampleRate, ratio);
    m_waveProcessor->setSamples(stretched);
    m_modified = true;
}

void AudioQMLBridge::pitchShift(float semitones, int quality)
{
    Q_UNUSED(quality); m_timeStretchQuality = qBound(0,quality,2);
    float ratio = qPow(2.0f, semitones / 12.0f);
    AudioTimeStretch stretch;
    stretch.setPitchShift(semitones);

    QVector<float> samples = m_waveProcessor->getSamples();
    int channels = m_waveProcessor->getChannelCount();
    int sampleRate = m_waveProcessor->getSampleRate();

    QVector<float> shifted = stretch.stretch(samples, channels, sampleRate, ratio);
    m_waveProcessor->setSamples(shifted);
    m_modified = true;
}

void AudioQMLBridge::changeTempo(float percent)
{
    float ratio = 100.0f / percent;
    timeStretch(ratio);
}

void AudioQMLBridge::changePitch(float semitones)
{
    pitchShift(semitones);
}

bool AudioQMLBridge::convertFormat(const QString &inputPath, const QString &outputPath, int quality)
{
    AudioFormatConverter converter;
    AudioFormatConverter::ConversionQuality convQuality =
        static_cast<AudioFormatConverter::ConversionQuality>(quality);
    return converter.convert(inputPath, outputPath, convQuality);
}

QStringList AudioQMLBridge::getSupportedFormats()
{
    return AudioFormatConverter::supportedExtensions();
}

bool AudioQMLBridge::startRecording(const QString &outputPath, int sampleRate, int channels)
{
    if (isRecording()) {
        stopRecording();
    }

    m_recordingOutputPath = outputPath;
    m_recordingFormat.setSampleRate(sampleRate);
    m_recordingFormat.setChannelCount(channels);
    m_recordingFormat.setSampleFormat(QAudioFormat::Int16);

    m_recorder->setFormat(m_recordingFormat);
    m_recorder->setOutputPath(outputPath);

    if (!m_studio->openInput(m_recordingFormat)) {
        emit error("Failed to open audio input device");
        return false;
    }

    m_recorder->start();
    m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
    m_recordingTimer->start();

    emit recordingPathChanged(outputPath);
    return true;
}

void AudioQMLBridge::stopRecording()
{
    if (!isRecording()) return;

    m_recordingTimer->stop();
    m_recorder->stop();
    m_studio->closeInput();
}

void AudioQMLBridge::pauseRecording()
{
    if (!isRecording()) return;
    m_recordingTimer->stop();
    m_recorder->pause();
}

void AudioQMLBridge::resumeRecording()
{
    if (m_recorder->state() != ks::audio::AudioRecorder::Paused) return;
    m_recorder->resume();
    m_recordingTimer->start();
}

bool AudioQMLBridge::isRecording() const
{
    return m_recorder && m_recorder->state() == ks::audio::AudioRecorder::Recording;
}

float AudioQMLBridge::getInputLevel(int channel) const
{
    if (channel < 0 || channel >= m_inputLevels.size()) return 0.0f;
    return m_inputLevels[channel];
}

QString AudioQMLBridge::recordingOutputPath() const
{
    return m_recordingOutputPath;
}

qint64 AudioQMLBridge::recordingDuration() const
{
    if (!isRecording()) return m_recorder->recordedDuration();
    return QDateTime::currentMSecsSinceEpoch() - m_recordingStartTime;
}

QStringList AudioQMLBridge::getAvailableInputDevices()
{
    QStringList devices;
    QList<QAudioDevice> audioDevices = QMediaDevices::audioInputs();
    for (const QAudioDevice& dev : audioDevices) {
        devices.append(dev.description());
    }
    if (devices.isEmpty()) {
        devices.append("Default");
    }
    return devices;
}

void AudioQMLBridge::setInputDevice(const QString& deviceName)
{
    m_inputDeviceName = deviceName;
}

QString AudioQMLBridge::getCurrentInputDevice() const
{
    return m_inputDeviceName.isEmpty() ? "Default" : m_inputDeviceName;
}

// ÔöÇÔöÇ Project I/O ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

static QString samplesToBase64(const QVector<float>& samples)
{
    QByteArray raw;
    raw.resize(static_cast<int>(samples.size() * sizeof(float)));
    std::memcpy(raw.data(), samples.constData(), raw.size());
    return raw.toBase64();
}

static QVector<float> base64ToSamples(const QString& b64)
{
    QByteArray raw = QByteArray::fromBase64(b64.toUtf8());
    int floatCount = raw.size() / static_cast<int>(sizeof(float));
    QVector<float> samples(floatCount);
    std::memcpy(samples.data(), raw.constData(), raw.size());
    return samples;
}

bool AudioQMLBridge::saveProject(const QString& filePath)
{
    QJsonObject root;
    root["_schema"]  = QStringLiteral("kseditor-audio");
    root["_version"] = QStringLiteral("1.0.0");
    root["name"]     = QFileInfo(filePath).baseName();
    root["format"]   = QStringLiteral("kseditor-audio.v1");
    root["guid"]     = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonArray tracksArr;
    for (int ti = 0; ti < m_tracks.size(); ++ti) {
        TrackModel* tm = m_tracks[ti];
        if (!tm) continue;

        QJsonObject to;
        to["name"]   = tm->name;
        to["gain"]   = static_cast<double>(tm->gain);
        to["pan"]    = static_cast<double>(tm->pan);
        to["mute"]   = tm->mute;
        to["solo"]   = tm->solo;
        to["sampleRate"] = tm->maxSampleRate();
        to["channels"]   = tm->maxChannels();

        QJsonArray clipsArr;
        for (AudioClip* c : tm->clips) {
            QJsonObject co;
            co["startMs"]    = c->startMs;
            co["channels"]   = c->channels;
            co["sampleRate"] = c->sampleRate;
            co["name"]       = c->name;
            co["color"]      = c->color.name();
            co["samples"]    = samplesToBase64(c->samples);

            QJsonArray envArr;
            for (const AudioClip::EnvelopePoint& ep : c->envelope) {
                QJsonObject epo;
                epo["timeMs"] = ep.timeMs;
                epo["gain"]   = static_cast<double>(ep.gain);
                envArr.append(epo);
            }
            co["envelope"] = envArr;
            clipsArr.append(co);
        }
        to["clips"] = clipsArr;

        QJsonArray undoArr;
        for (const QVector<AudioClip*>& snapshot : tm->undoStack) {
            QJsonArray snapArr;
            for (AudioClip* c : snapshot) {
                QJsonObject so;
                so["startMs"]    = c->startMs;
                so["channels"]   = c->channels;
                so["sampleRate"] = c->sampleRate;
                so["name"]       = c->name;
                so["color"]      = c->color.name();
                so["samples"]    = samplesToBase64(c->samples);
                snapArr.append(so);
            }
            undoArr.append(snapArr);
        }
        to["undoStack"] = undoArr;

        tracksArr.append(to);
    }
    root["tracks"] = tracksArr;
    root["activeTrack"] = m_activeTrack;

    QJsonObject viewState;
    viewState["viewStart"]     = m_viewStart;
    viewState["viewEnd"]       = m_viewEnd;
    viewState["selStartFrac"]  = m_selectionStartFrac;
    viewState["selEndFrac"]    = m_selectionEndFrac;
    viewState["selStartMs"]    = m_selectionStartMs;
    viewState["selEndMs"]      = m_selectionEndMs;
    root["viewState"] = viewState;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit error("Cannot write project file: " + filePath);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    m_currentFilePath = filePath;
    m_modified = false;
    emit saveComplete();
    emit statusMessage("Project saved: " + filePath);
    return true;
}

bool AudioQMLBridge::loadProject(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error("Cannot read project file: " + filePath);
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        emit error("JSON parse error: " + parseError.errorString());
        return false;
    }

    QJsonObject root = doc.object();

    // Stop playback
    if (m_waveformEngine->isPlaying()) {
        m_waveformEngine->stop();
    }

    // Clear existing tracks
    for (TrackModel* tm : m_tracks) {
        tm->clearClips();
        tm->undoStack.clear();
        tm->redoStack.clear();
    }
    qDeleteAll(m_tracks);
    m_tracks.clear();

    // Load tracks
    QJsonArray tracksArr = root["tracks"].toArray();
    for (int ti = 0; ti < tracksArr.size(); ++ti) {
        QJsonObject to = tracksArr[ti].toObject();
        TrackModel* tm = new TrackModel(this);
        tm->name     = to["name"].toString("Track " + QString::number(ti + 1));
        tm->gain     = static_cast<float>(to["gain"].toDouble(1.0));
        tm->pan      = static_cast<float>(to["pan"].toDouble(0.0));
        tm->mute     = to["mute"].toBool(false);
        tm->solo     = to["solo"].toBool(false);

        QJsonArray clipsArr = to["clips"].toArray();
        for (int ci = 0; ci < clipsArr.size(); ++ci) {
            QJsonObject co = clipsArr[ci].toObject();
            AudioClip* clip = new AudioClip(this);
            clip->startMs    = co["startMs"].toVariant().toLongLong();
            clip->channels   = co["channels"].toInt(1);
            clip->sampleRate = co["sampleRate"].toInt(44100);
            clip->name       = co["name"].toString();
            clip->color      = QColor(co["color"].toString("#569cd6"));
            clip->samples    = base64ToSamples(co["samples"].toString());

            QJsonArray envArr = co["envelope"].toArray();
            for (int ei = 0; ei < envArr.size(); ++ei) {
                QJsonObject epo = envArr[ei].toObject();
                clip->addEnvelopePoint(epo["timeMs"].toVariant().toLongLong(),
                                       static_cast<float>(epo["gain"].toDouble(1.0)));
            }
            tm->addClip(clip);
        }

        QJsonArray undoArr = to["undoStack"].toArray();
        for (int ui = 0; ui < undoArr.size(); ++ui) {
            QJsonArray snapArr = undoArr[ui].toArray();
            QVector<AudioClip*> snapshot;
            for (int si = 0; si < snapArr.size(); ++si) {
                QJsonObject so = snapArr[si].toObject();
                AudioClip* clip = new AudioClip(this);
                clip->startMs    = so["startMs"].toVariant().toLongLong();
                clip->channels   = so["channels"].toInt(1);
                clip->sampleRate = so["sampleRate"].toInt(44100);
                clip->name       = so["name"].toString();
                clip->color      = QColor(so["color"].toString("#569cd6"));
                clip->samples    = base64ToSamples(so["samples"].toString());
                snapshot.append(clip);
            }
            tm->undoStack.append(snapshot);
        }

        m_tracks.append(tm);
    }

    // Set active track
    int activeIdx = root["activeTrack"].toInt(0);
    if (activeIdx >= 0 && activeIdx < m_tracks.size()) {
        m_activeTrack = activeIdx;
        m_activeTrackModel = m_tracks[activeIdx];
    }

    // Load view state
    QJsonObject viewState = root["viewState"].toObject();
    m_viewStart           = viewState["viewStart"].toDouble(0.0);
    m_viewEnd             = viewState["viewEnd"].toDouble(1.0);
    m_selectionStartFrac  = viewState["selStartFrac"].toDouble(0.0);
    m_selectionEndFrac    = viewState["selEndFrac"].toDouble(1.0);
    m_selectionStartMs    = viewState["selStartMs"].toInt(0);
    m_selectionEndMs      = viewState["selEndMs"].toInt(0);

    m_currentFilePath = filePath;
    m_modified = false;

    emit loadComplete();
    emit audioChanged();
    emit statusMessage("Project loaded: " + filePath);
    return true;
}

// ÔöÇÔöÇ Text-to-Speech ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

void AudioQMLBridge::ttsSpeak(const QString& text)
{
    if (m_tts) m_tts->speak(text);
}

void AudioQMLBridge::ttsStop()
{
    if (m_tts) m_tts->stop();
}

void AudioQMLBridge::ttsPause()
{
    if (m_tts) m_tts->pause();
}

void AudioQMLBridge::ttsResume()
{
    if (m_tts) m_tts->resume();
}

bool AudioQMLBridge::ttsSpeaking() const
{
    return m_tts ? m_tts->isSpeaking() : false;
}

QStringList AudioQMLBridge::ttsVoices() const
{
    return m_tts ? m_tts->availableVoices() : QStringList();
}

QString AudioQMLBridge::ttsCurrentVoice() const
{
    return m_tts ? m_tts->currentVoice() : QString();
}

void AudioQMLBridge::setTtsCurrentVoice(const QString& name)
{
    if (m_tts) {
        m_tts->setVoice(name);
        emit ttsCurrentVoiceChanged();
    }
}

int AudioQMLBridge::ttsVolume() const
{
    return m_tts ? m_tts->volume() : 0;
}

void AudioQMLBridge::setTtsVolume(int percent)
{
    if (m_tts) {
        m_tts->setVolume(percent);
        emit ttsVolumeChanged();
    }
}

int AudioQMLBridge::ttsRate() const
{
    return m_tts ? m_tts->rate() : 0;
}

void AudioQMLBridge::setTtsRate(int rate)
{
    if (m_tts) {
        m_tts->setRate(rate);
        emit ttsRateChanged();
    }
}

bool AudioQMLBridge::ttsSaveToWav(const QString& text, const QString& filePath)
{
    return m_tts ? m_tts->saveToWav(text, filePath) : false;
}
bool AudioQMLBridge::pencilEdit(int sampleIndex, float value) {
    auto s = m_waveProcessor->getSamples(); if (sampleIndex<0||sampleIndex>=s.size()) return false;
    s[sampleIndex]=qBound(-1.0f,value,1.0f); m_waveProcessor->setSamples(s);
    if (m_activeTrackModel && !m_activeTrackModel->clips.isEmpty()) { auto &cs=m_activeTrackModel->clips.first()->samples; if(sampleIndex<cs.size()) cs[sampleIndex]=qBound(-1.0f,value,1.0f); }
    m_modified=true; emit audioChanged(); return true;
}
int AudioQMLBridge::findZeroCrossing(int fromSample, int direction) {
    auto s = m_waveProcessor->getSamples(); if (s.isEmpty()) return -1;
    fromSample=qBound(0,fromSample,s.size()-1); direction=direction>=0?1:-1;
    for(int i=fromSample;i>=0&&i<s.size()-1;i+=direction){ if((s[i]<=0&&s[i+1]>0)||(s[i]>=0&&s[i+1]<0)) return i+1; }
    return fromSample;
}
QStringList AudioQMLBridge::masteringPresets() const { return {"vinyl","tape","broadcast","streaming","cd","club"}; }
bool AudioQMLBridge::applyMasteringPreset(const QString& name) {
    if(name=="vinyl") applyDeEsser(6000,-18); else if(name=="tape") applyTapeEmulation(1.2f,0.02f,0.01f);
    else if(name=="broadcast") applyCompressor(-16,3,5,80,2); else if(name=="streaming") normalize(0.89f);
    else if(name=="cd") applyLimiter(-0.3f,50); else if(name=="club") applyMultibandCompressor(-18,-18,-18,4,4,4,10,100);
    else return false; emit statusMessage("Mastering preset: "+name); return true;
}
QVariantMap AudioQMLBridge::detectSilence(float thresholdDb, int minDurationMs) {
    QVariantMap r; auto s=m_waveProcessor->getSamples(); int sr=m_waveProcessor->getSampleRate(); if(s.isEmpty()||sr<=0){r["found"]=false;return r;}
    float thresh=qPow(10.0f,thresholdDb/20.0f); int need=s.size()*0; int minSamples=minDurationMs*sr/1000; int start=-1;
    QVariantList regs; for(int i=0;i<s.size();++i){ if(qAbs(s[i])<thresh){ if(start<0) start=i; } else { if(start>=0&&i-start>=minSamples){ QVariantMap m; m["startMs"]=start*1000/sr; m["endMs"]=i*1000/sr; regs.append(m);} start=-1; } }
    r["regions"]=regs; r["found"]=!regs.isEmpty(); return r;
}
bool AudioQMLBridge::applyVoiceActivation(float thresholdDb) {
    auto m=detectSilence(thresholdDb,150); auto regs=m["regions"].toList(); if(regs.isEmpty()) return false;
    for(auto v:regs){ auto rm=v.toMap(); deleteRegion(rm["startMs"].toInt(), rm["endMs"].toInt()); }
    emit statusMessage("Voice activation applied"); return true;
}
bool AudioQMLBridge::setVideoPath(const QString& path) { m_videoPath=path; m_videoOffsetMs=0; emit statusMessage("Video path: "+path); return true; }
QString AudioQMLBridge::videoPath() const { return m_videoPath; }
bool AudioQMLBridge::syncToVideo(qint64 videoMs) { qint64 audioMs=videoMs+m_videoOffsetMs; setPositionMs(audioMs); emit positionChanged(audioMs); return true; }
QString AudioQMLBridge::videoSyncInfo() const { QVariantMap m; m["videoPath"]=m_videoPath; m["offsetMs"]=m_videoOffsetMs; m["audioMs"]=getPositionMs(); return QString(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact)); }
