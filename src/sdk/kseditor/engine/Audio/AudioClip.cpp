#include "AudioClip.h"
#include <QColor>

AudioClip::AudioClip(QObject *parent)
    : QObject(parent), channels(1), sampleRate(44100), startMs(0), color(QColor("#569cd6"))
{}

AudioClip::AudioClip(const QVector<float> &s, int ch, int sr, qint64 ms, QObject *parent)
    : QObject(parent), samples(s), channels(ch), sampleRate(sr), startMs(ms), color(QColor("#569cd6"))
{}

AudioClip::~AudioClip() {
    delete m_processor;
}

WaveProcessor* AudioClip::processor() const {
    if (!m_processor && !samples.isEmpty()) {
        m_processor = new WaveProcessor(const_cast<AudioClip*>(this));
        m_processor->setSamples(samples, channels, sampleRate);
    }
    return m_processor;
}

QVector<float> AudioClip::applyEnvelope() const {
    if (envelope.isEmpty()) return samples;

    QVector<float> result = samples;
    int ch = qMax(1, channels);
    int frameCount = samples.size() / ch;
    if (frameCount == 0) return result;

    // Sort envelope points by time
    QVector<EnvelopePoint> env = envelope;
    std::sort(env.begin(), env.end(), [](const EnvelopePoint& a, const EnvelopePoint& b) {
        return a.timeMs < b.timeMs;
    });

    float sampleRateHz = sampleRate;
    for (int f = 0; f < frameCount; ++f) {
        qint64 timeMs = (qint64)f * 1000 / sampleRate;
        float gain = 1.0f;

        // Find envelope segment
        for (int i = 0; i < env.size() - 1; ++i) {
            if (timeMs >= env[i].timeMs && timeMs < env[i + 1].timeMs) {
                float t = (float)(timeMs - env[i].timeMs) / (env[i + 1].timeMs - env[i].timeMs);
                gain = env[i].gain + t * (env[i + 1].gain - env[i].gain);
                break;
            }
        }
        // Before first point
        if (timeMs < env.first().timeMs) gain = env.first().gain;
        // After last point
        if (timeMs >= env.last().timeMs) gain = env.last().gain;

        for (int c = 0; c < ch; ++c) {
            result[f * ch + c] *= gain;
        }
    }
    return result;
}

void AudioClip::addEnvelopePoint(qint64 timeMs, float gain) {
    envelope.append(EnvelopePoint(timeMs, gain));
    std::sort(envelope.begin(), envelope.end(), [](const EnvelopePoint& a, const EnvelopePoint& b) {
        return a.timeMs < b.timeMs;
    });
}

void AudioClip::removeEnvelopePoint(int index) {
    if (index >= 0 && index < envelope.size()) {
        envelope.removeAt(index);
    }
}

void AudioClip::setEnvelopePoint(int index, qint64 timeMs, float gain) {
    if (index >= 0 && index < envelope.size()) {
        envelope[index].timeMs = timeMs;
        envelope[index].gain = gain;
        std::sort(envelope.begin(), envelope.end(), [](const EnvelopePoint& a, const EnvelopePoint& b) {
            return a.timeMs < b.timeMs;
        });
    }
}

TrackModel::TrackModel(QObject *parent)
    : QObject(parent)
{}

TrackModel::~TrackModel() {
    qDeleteAll(clips);
    for (auto& snapshot : undoStack)
        qDeleteAll(snapshot);
    for (auto& snapshot : redoStack)
        qDeleteAll(snapshot);
}

qint64 TrackModel::durationMs() const {
    qint64 maxEnd = 0;
    for (AudioClip *c : clips) {
        maxEnd = qMax(maxEnd, c->endMs());
    }
    return maxEnd;
}

int TrackModel::maxChannels() const {
    int maxCh = 1;
    for (AudioClip *c : clips) {
        maxCh = qMax(maxCh, c->channels);
    }
    return maxCh;
}

int TrackModel::maxSampleRate() const {
    int maxSr = 44100;
    for (AudioClip *c : clips) {
        maxSr = qMax(maxSr, c->sampleRate);
    }
    return maxSr;
}

AudioClip* TrackModel::clipAt(qint64 ms) const {
    for (AudioClip *c : clips) {
        if (c->containsMs(ms)) return c;
    }
    return nullptr;
}

int TrackModel::clipIndexAt(qint64 ms) const {
    for (int i = 0; i < clips.size(); ++i) {
        if (clips[i]->containsMs(ms)) return i;
    }
    return -1;
}

void TrackModel::addClip(AudioClip *clip) {
    if (!clip) return;
    clip->setParent(this);
    clips.append(clip);
    sortClips();
    emit clipsChanged();
}

void TrackModel::removeClip(int index) {
    if (index < 0 || index >= clips.size()) return;
    AudioClip *c = clips.takeAt(index);
    c->deleteLater();
    emit clipsChanged();
}

void TrackModel::removeClip(AudioClip *clip) {
    int idx = clips.indexOf(clip);
    if (idx >= 0) removeClip(idx);
}

void TrackModel::sortClips() {
    std::sort(clips.begin(), clips.end(), [](AudioClip* a, AudioClip* b) {
        return a->startMs < b->startMs;
    });
}

void TrackModel::splitClipAt(qint64 ms) {
    int idx = clipIndexAt(ms);
    if (idx < 0) return;
    AudioClip *c = clips[idx];
    if (ms <= c->startMs || ms >= c->endMs()) return;

    qint64 relFrame = (ms - c->startMs) * c->sampleRate / 1000;
    int frameIdx = relFrame * c->channels;

    // Create right part
    AudioClip *right = new AudioClip(
        QVector<float>(c->samples.mid(frameIdx)), c->channels, c->sampleRate, ms, this);
    right->name = c->name + "_R";
    right->color = c->color;

    // Trim left part
    c->samples.resize(frameIdx);
    // No need to change c->startMs

    addClip(right);
}

void TrackModel::trimClip(int index, qint64 newStartMs, qint64 newEndMs) {
    if (index < 0 || index >= clips.size()) return;
    AudioClip *c = clips[index];
    if (newStartMs <= c->startMs && newEndMs >= c->endMs()) return;

    int startFrame = qMax(0, (int)((newStartMs - c->startMs) * c->sampleRate / 1000) * c->channels);
    int endFrame = qMin(c->samples.size(), (int)((newEndMs - c->startMs) * c->sampleRate / 1000) * c->channels);
    startFrame = qMax(0, startFrame);
    endFrame = qMin((int)c->samples.size(), endFrame);
    if (endFrame <= startFrame) return;

    c->samples = QVector<float>(c->samples.mid(startFrame, endFrame - startFrame));
    c->startMs = newStartMs;
    sortClips();
    emit clipsChanged();
}

void TrackModel::moveClip(int index, qint64 newStartMs) {
    if (index < 0 || index >= clips.size()) return;
    clips[index]->startMs = newStartMs;
    sortClips();
    emit clipsChanged();
}

void TrackModel::clearClips() {
    qDeleteAll(clips);
    clips.clear();
    emit clipsChanged();
}

void TrackModel::pushUndoState() {
    QVector<AudioClip*> snapshot;
    for (AudioClip *c : clips) {
        AudioClip *copy = new AudioClip(c->samples, c->channels, c->sampleRate, c->startMs, this);
        copy->name = c->name;
        copy->color = c->color;
        snapshot.append(copy);
    }
    undoStack.append(snapshot);
    if (undoStack.size() > 100) {
        qDeleteAll(undoStack.takeFirst());
    }
    for (auto& snapshot : redoStack)
        qDeleteAll(snapshot);
    redoStack.clear();
}

void TrackModel::undo() {
    if (undoStack.isEmpty()) return;
    redoStack.append(clips);
    clips = undoStack.takeLast();
    for (AudioClip *c : clips) c->setParent(this);
    emit clipsChanged();
}

void TrackModel::redo() {
    if (redoStack.isEmpty()) return;
    undoStack.append(clips);
    clips = redoStack.takeLast();
    for (AudioClip *c : clips) c->setParent(this);
    emit clipsChanged();
}

bool TrackModel::canUndo() const { return !undoStack.isEmpty(); }
bool TrackModel::canRedo() const { return !redoStack.isEmpty(); }