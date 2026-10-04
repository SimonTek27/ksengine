#include "sdk/kseditor/engine/animation/AnimationTimeline.h"
#include "TimelineEditor.h"
#include "animation/AnimationSystem.h"

#include <QTimer>
#include <QUuid>
#include <QDebug>
#include <algorithm>

namespace ks {

AnimationTimeline* AnimationTimeline::s_instance = nullptr;

AnimationTimeline* AnimationTimeline::instance()
{
    if (!s_instance) s_instance = new AnimationTimeline();
    return s_instance;
}

AnimationTimeline::AnimationTimeline(QObject* parent)
    : QObject(parent)
    , m_frameRate(30)
    , m_currentFrame(0)
    , m_rangeStart(0)
    , m_rangeEnd(250)
{
    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &AnimationTimeline::advanceFrame);
}

AnimationTimeline::~AnimationTimeline() { s_instance = nullptr; }

// ─── Animation CRUD ───────────────────────────────────────────────────────────

void AnimationTimeline::createAnimation(const QString& name)
{
    Animation anim;
    anim.id         = QUuid::createUuid().toString(QUuid::WithoutBraces);
    anim.name       = name;
    anim.startFrame = 0;
    anim.endFrame   = 250;
    anim.frameRate  = m_frameRate;
    anim.loop       = false;
    m_animations.insert(anim.id, anim);
    emit animationCreated(anim.id);
}

void AnimationTimeline::deleteAnimation(const QString& id)
{
    m_animations.remove(id);
    m_tracks.remove(id);
    if (m_currentAnimationId == id) m_currentAnimationId.clear();
    emit animationDeleted(id);
}

void AnimationTimeline::setCurrentAnimation(const QString& id)
{
    if (m_currentAnimationId == id) return;
    stop();
    m_currentAnimationId = id;
    if (m_animations.contains(id)) {
        m_rangeStart   = m_animations[id].startFrame;
        m_rangeEnd     = m_animations[id].endFrame;
        m_currentFrame = m_rangeStart;
        m_frameRate    = m_animations[id].frameRate;
        m_playTimer->setInterval(1000 / qMax(1, m_frameRate));
    }
    emit currentAnimationChanged(id);
    emit currentFrameChanged(m_currentFrame);
}

void AnimationTimeline::renameAnimation(const QString& id, const QString& name)
{
    if (m_animations.contains(id)) m_animations[id].name = name;
}

void AnimationTimeline::duplicateAnimation(const QString& id)
{
    if (!m_animations.contains(id)) return;
    Animation copy   = m_animations[id];
    copy.id          = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name        = copy.name + " Copy";
    m_animations.insert(copy.id, copy);
    // Deep-copy tracks
    if (m_tracks.contains(id)) m_tracks.insert(copy.id, m_tracks[id]);
    emit animationCreated(copy.id);
}

QVector<AnimationTimeline::Animation> AnimationTimeline::getAnimations() const
{
    return m_animations.values().toVector();
}

AnimationTimeline::Animation AnimationTimeline::getAnimation(const QString& id) const
{
    return m_animations.value(id);
}

// ─── Track management ─────────────────────────────────────────────────────────

void AnimationTimeline::addTrack(const QString& animId, const Track& track)
{
    m_tracks[animId].insert(track.id, track);
    emit trackAdded(animId, track.id);
}

void AnimationTimeline::removeTrack(const QString& animId, const QString& trackId)
{
    if (m_tracks.contains(animId)) {
        m_tracks[animId].remove(trackId);
        emit trackRemoved(animId, trackId);
    }
}

void AnimationTimeline::setTrackLocked(const QString& animId, const QString& trackId, bool locked)
{
    if (m_tracks.contains(animId) && m_tracks[animId].contains(trackId))
        m_tracks[animId][trackId].locked = locked;
}

void AnimationTimeline::setTrackMuted(const QString& animId, const QString& trackId, bool muted)
{
    if (m_tracks.contains(animId) && m_tracks[animId].contains(trackId))
        m_tracks[animId][trackId].muted = muted;
}

void AnimationTimeline::setTrackSolo(const QString& animId, const QString& trackId, bool solo)
{
    if (m_tracks.contains(animId) && m_tracks[animId].contains(trackId))
        m_tracks[animId][trackId].solo = solo;
}

QVector<AnimationTimeline::Track> AnimationTimeline::getTracks(const QString& animId) const
{
    return m_tracks.value(animId).values().toVector();
}

// ─── Keyframe management ──────────────────────────────────────────────────────

void AnimationTimeline::addKeyframe(const QString& animId,
                                     const QString& trackId,
                                     const Keyframe& kf)
{
    m_tracks[animId][trackId].keyframes.insert(kf.frame, kf);
    emit keyframeAdded(animId, trackId, kf.frame);
}

void AnimationTimeline::removeKeyframe(const QString& animId,
                                        const QString& trackId,
                                        int frame)
{
    if (m_tracks.contains(animId) && m_tracks[animId].contains(trackId)) {
        m_tracks[animId][trackId].keyframes.remove(frame);
        emit keyframeRemoved(animId, trackId, frame);
    }
}

void AnimationTimeline::setKeyframeValue(const QString& animId,
                                          const QString& trackId,
                                          int frame, float value)
{
    if (m_tracks.contains(animId) && m_tracks[animId].contains(trackId) &&
        m_tracks[animId][trackId].keyframes.contains(frame)) {
        m_tracks[animId][trackId].keyframes[frame].value = value;
        emit keyframeChanged(animId, trackId, frame);
    }
}

void AnimationTimeline::setKeyframeInterpolation(const QString& animId,
                                                   const QString& trackId,
                                                   int frame,
                                                   const QString& interp)
{
    if (m_tracks.contains(animId) && m_tracks[animId].contains(trackId) &&
        m_tracks[animId][trackId].keyframes.contains(frame)) {
        m_tracks[animId][trackId].keyframes[frame].interpolation = interp;
    }
}

float AnimationTimeline::evaluateTrack(const QString& animId,
                                        const QString& trackId,
                                        int frame) const
{
    const auto animIt = m_tracks.constFind(animId);
    if (animIt == m_tracks.constEnd()) return 0.f;
    const auto trackIt = animIt->constFind(trackId);
    if (trackIt == animIt->constEnd()) return 0.f;

    const auto& kfs = trackIt->keyframes;
    if (kfs.isEmpty()) return 0.f;

    // Exact match
    if (kfs.contains(frame)) return kfs[frame].value;

    // Find surrounding keyframes for interpolation
    auto it = kfs.upperBound(frame);
    if (it == kfs.begin()) return it.value().value;
    if (it == kfs.end())   return (--it).value().value;

    auto next = it;
    --it; // previous keyframe

    int   f0 = it.key(),   f1 = next.key();
    float v0 = it.value().value, v1 = next.value().value;
    float t  = float(frame - f0) / float(f1 - f0);

    const QString& interp = it.value().interpolation;
    if (interp == "constant") return v0;
    if (interp == "cubic") {
        // Smooth-step
        t = t * t * (3.f - 2.f * t);
    }
    return v0 + t * (v1 - v0); // linear (default)
}

// ─── Playback ─────────────────────────────────────────────────────────────────

void AnimationTimeline::setCurrentFrame(int frame)
{
    m_currentFrame = qBound(m_rangeStart, frame, m_rangeEnd);
    emit currentFrameChanged(m_currentFrame);
}

void AnimationTimeline::setPlayRange(int start, int end)
{
    m_rangeStart = start;
    m_rangeEnd   = qMax(start + 1, end);
}

void AnimationTimeline::getPlayRange(int& start, int& end) const
{
    start = m_rangeStart;
    end   = m_rangeEnd;
}

void AnimationTimeline::setFrameRate(int fps)
{
    m_frameRate = qBound(1, fps, 120);
    m_playTimer->setInterval(1000 / m_frameRate);
    if (m_currentAnimationId.isEmpty()) return;
    if (m_animations.contains(m_currentAnimationId))
        m_animations[m_currentAnimationId].frameRate = m_frameRate;
    emit frameRateChanged(m_frameRate);
}

void AnimationTimeline::setLoop(bool loop)
{
    m_loop = loop;
    if (!m_currentAnimationId.isEmpty() && m_animations.contains(m_currentAnimationId))
        m_animations[m_currentAnimationId].loop = loop;
}

void AnimationTimeline::play()
{
    if (m_playing) return;
    m_playing = true;
    m_playTimer->setInterval(1000 / qMax(1, m_frameRate));
    m_playTimer->start();
    emit playbackStateChanged(true);
}

void AnimationTimeline::pause()
{
    if (!m_playing) return;
    m_playing = false;
    m_playTimer->stop();
    emit playbackStateChanged(false);
}

void AnimationTimeline::stop()
{
    pause();
    setCurrentFrame(m_rangeStart);
}

void AnimationTimeline::togglePlayPause()
{
    m_playing ? pause() : play();
}

void AnimationTimeline::stepForward()
{
    setCurrentFrame(qMin(m_currentFrame + 1, m_rangeEnd));
}

void AnimationTimeline::stepBackward()
{
    setCurrentFrame(qMax(m_currentFrame - 1, m_rangeStart));
}

void AnimationTimeline::goToStart() { setCurrentFrame(m_rangeStart); }
void AnimationTimeline::goToEnd()   { setCurrentFrame(m_rangeEnd); }

void AnimationTimeline::advanceFrame()
{
    if (m_currentFrame >= m_rangeEnd) {
        if (m_loop) {
            setCurrentFrame(m_rangeStart);
        } else {
            stop();
        }
    } else {
        setCurrentFrame(m_currentFrame + 1);
    }
}

// ─── Baking ───────────────────────────────────────────────────────────────────

QMap<int, float> AnimationTimeline::bakeTrack(const QString& animId,
                                               const QString& trackId,
                                               int step) const
{
    QMap<int, float> baked;
    const auto animIt = m_animations.constFind(animId);
    if (animIt == m_animations.constEnd()) return baked;
    const auto& anim = animIt.value();
    for (int f = anim.startFrame; f <= anim.endFrame; f += qMax(1, step))
        baked[f] = evaluateTrack(animId, trackId, f);
    return baked;
}

// ─── TimelineEditor ──────────────────────────────────────────────────────────

static TimelineEditor* s_timelineInstance = nullptr;

TimelineEditor* TimelineEditor::instance()
{
    if (!s_timelineInstance) s_timelineInstance = new TimelineEditor();
    return s_timelineInstance;
}

TimelineEditor::TimelineEditor(QObject* parent) : QObject(parent) {}
TimelineEditor::~TimelineEditor() { s_timelineInstance = nullptr; }

void TimelineEditor::setAnimationTimeline(AnimationTimeline* timeline) { m_timeline = timeline; }
void TimelineEditor::setFrameView(int start, int end) { m_frameViewStart = start; m_frameViewEnd = end; emit viewChanged(start, end); }
void TimelineEditor::getFrameView(int& start, int& end) const { start = m_frameViewStart; end = m_frameViewEnd; }
void TimelineEditor::setZoom(float zoom) { m_zoom = zoom; emit zoomChanged(zoom); }
void TimelineEditor::setHeight(int height) { m_height = height; }
void TimelineEditor::showFrames(bool show) { m_showFrames = show; }
void TimelineEditor::showSeconds(bool show) { m_showSeconds = show; }
void TimelineEditor::setSnapToFrames(bool snap) { m_snapToFrames = snap; }
void TimelineEditor::setSnapToKeyframes(bool snap) { m_snapToKeyframes = snap; }
void TimelineEditor::selectAll()
{
    m_selectedFrames.clear();
    for (int f = m_frameViewStart; f <= m_frameViewEnd; ++f)
        m_selectedFrames.append(f);
}

void TimelineEditor::deselectAll()
{
    m_selectedFrames.clear();
}

void TimelineEditor::invertSelection()
{
    QVector<int> inverted;
    for (int f = m_frameViewStart; f <= m_frameViewEnd; ++f) {
        if (!m_selectedFrames.contains(f))
            inverted.append(f);
    }
    m_selectedFrames = inverted;
}

void TimelineEditor::copySelection()
{
    m_clipboard.clear();
    if (m_selectedFrames.isEmpty() || !m_timeline) return;

    std::sort(m_selectedFrames.begin(), m_selectedFrames.end());
    int start = m_selectedFrames.first();
    int end = m_selectedFrames.last();
    m_clipboard.append({start, end - start + 1});
}

void TimelineEditor::pasteSelection()
{
    if (m_clipboard.isEmpty() || !m_timeline) return;

    const QString animId = m_timeline->currentAnimationId();
    if (animId.isEmpty()) return;

    for (const auto& range : m_clipboard) {
        int srcStart = range.first;
        int length = range.second;
        int destStart = m_timeline->currentFrame();

        for (int i = 0; i < length; ++i) {
            int srcFrame = srcStart + i;
            int destFrame = destStart + i;
            auto tracks = m_timeline->getTracks(animId);
            for (const auto& track : tracks) {
                auto kf = track.keyframes.value(srcFrame);
                if (kf.frame == srcFrame)
                    m_timeline->setKeyframeValue(animId, track.id, destFrame, kf.value);
            }
        }
    }
}

void TimelineEditor::deleteSelection()
{
    if (m_selectedFrames.isEmpty() || !m_timeline) return;

    const QString animId = m_timeline->currentAnimationId();
    if (animId.isEmpty()) return;

    for (int frame : m_selectedFrames) {
        auto tracks = m_timeline->getTracks(animId);
        for (const auto& track : tracks)
            m_timeline->removeKeyframe(animId, track.id, frame);
    }
    m_selectedFrames.clear();
}
void TimelineEditor::setPlayheadStyle(const QString& style) { m_playheadStyle = style; }
void TimelineEditor::setTrackHeight(int height) { m_trackHeight = height; }

// ─── TimelineKeyframeEditor ──────────────────────────────────────────────────

static TimelineKeyframeEditor* s_kfInstance = nullptr;

TimelineKeyframeEditor* TimelineKeyframeEditor::instance()
{
    if (!s_kfInstance) s_kfInstance = new TimelineKeyframeEditor();
    return s_kfInstance;
}

TimelineKeyframeEditor::TimelineKeyframeEditor(QObject* parent) : QObject(parent) {}
TimelineKeyframeEditor::~TimelineKeyframeEditor() { s_kfInstance = nullptr; }

void TimelineKeyframeEditor::setSelectedKeyframes(const QVector<int>& frames) { m_selectedFrames = frames; emit keyframesSelected(frames); }
void TimelineKeyframeEditor::setInterpolation(const QString& interpolation) { m_interpolation = interpolation; }
void TimelineKeyframeEditor::setEasing(const QString& easing) { m_easing = easing; }
void TimelineKeyframeEditor::setTangentLocked(bool locked) { m_tangentLocked = locked; }
void TimelineKeyframeEditor::moveSelectedKeyframes(int delta)
{
    if (m_selectedFrames.isEmpty()) return;
    for (int& f : m_selectedFrames) f += delta;
    // Keep sorted
    std::sort(m_selectedFrames.begin(), m_selectedFrames.end());
    emit keyframesSelected(m_selectedFrames);
    emit keyframesModified();
}

void TimelineKeyframeEditor::scaleSelectedKeyframes(float scale, int origin)
{
    if (m_selectedFrames.isEmpty()) return;
    for (int& f : m_selectedFrames) {
        float offset = static_cast<float>(f) - origin;
        f = static_cast<int>(qRound(origin + offset * scale));
    }
    std::sort(m_selectedFrames.begin(), m_selectedFrames.end());
    emit keyframesSelected(m_selectedFrames);
    emit keyframesModified();
}

void TimelineKeyframeEditor::flipSelectedKeyframes()
{
    if (m_selectedFrames.isEmpty()) return;
    int minF = m_selectedFrames.first();
    int maxF = m_selectedFrames.last();
    for (int& f : m_selectedFrames) f = minF + maxF - f;
    std::sort(m_selectedFrames.begin(), m_selectedFrames.end());
    emit keyframesSelected(m_selectedFrames);
    emit keyframesModified();
}

void TimelineKeyframeEditor::setValues(float value)
{
    if (m_selectedFrames.isEmpty()) return;
    AnimationTimeline* timeline = AnimationTimeline::instance();
    const QString animId = timeline->currentAnimationId();
    if (animId.isEmpty()) return;
    auto tracks = timeline->getTracks(animId);
    for (const auto& track : tracks) {
        for (int frame : m_selectedFrames) {
            if (track.keyframes.contains(frame))
                timeline->setKeyframeValue(animId, track.id, frame, value);
        }
    }
    emit keyframesModified();
}

void TimelineKeyframeEditor::offsetValues(float offset)
{
    if (m_selectedFrames.isEmpty()) return;
    AnimationTimeline* timeline = AnimationTimeline::instance();
    const QString animId = timeline->currentAnimationId();
    if (animId.isEmpty()) return;
    auto tracks = timeline->getTracks(animId);
    for (const auto& track : tracks) {
        for (int frame : m_selectedFrames) {
            if (track.keyframes.contains(frame)) {
                float newVal = track.keyframes[frame].value + offset;
                timeline->setKeyframeValue(animId, track.id, frame, newVal);
            }
        }
    }
    emit keyframesModified();
}

void TimelineKeyframeEditor::scaleValues(float scale)
{
    if (m_selectedFrames.isEmpty()) return;
    AnimationTimeline* timeline = AnimationTimeline::instance();
    const QString animId = timeline->currentAnimationId();
    if (animId.isEmpty()) return;
    auto tracks = timeline->getTracks(animId);
    for (const auto& track : tracks) {
        for (int frame : m_selectedFrames) {
            if (track.keyframes.contains(frame)) {
                float newVal = track.keyframes[frame].value * scale;
                timeline->setKeyframeValue(animId, track.id, frame, newVal);
            }
        }
    }
    emit keyframesModified();
}

void TimelineKeyframeEditor::bakeKeyframes()
{
    // Snap selected keyframes to nearest integer frame and remove duplicates
    if (m_selectedFrames.isEmpty()) return;

    // Build value map for each selected frame from the current track data
    QMap<int, float> snappedValues;
    for (int rawFrame : m_selectedFrames) {
        int snapped = qRound((float)rawFrame);
        snappedValues[snapped] = 0.0f;
    }

    // Replace keyframes at snapped positions
    m_selectedFrames.clear();
    m_selectedFrames.reserve(snappedValues.size());
    for (auto it = snappedValues.begin(); it != snappedValues.end(); ++it) {
        m_selectedFrames.append(it.key());
    }
    std::sort(m_selectedFrames.begin(), m_selectedFrames.end());

    emit keyframesSelected(m_selectedFrames);
    emit keyframesModified();
}

// ─── TimelineTrackControls ───────────────────────────────────────────────────

static TimelineTrackControls* s_tcInstance = nullptr;

TimelineTrackControls* TimelineTrackControls::instance()
{
    if (!s_tcInstance) s_tcInstance = new TimelineTrackControls();
    return s_tcInstance;
}

TimelineTrackControls::TimelineTrackControls(QObject* parent) : QObject(parent) {}
TimelineTrackControls::~TimelineTrackControls() { s_tcInstance = nullptr; }

void TimelineTrackControls::setTrackLocked(const QString& trackId, bool locked) { m_locked[trackId] = locked; emit trackLockChanged(trackId); }
bool TimelineTrackControls::isTrackLocked(const QString& trackId) const { return m_locked.value(trackId, false); }

void TimelineTrackControls::setTrackMuted(const QString& trackId, bool muted) { m_muted[trackId] = muted; emit trackMuteChanged(trackId); }
bool TimelineTrackControls::isTrackMuted(const QString& trackId) const { return m_muted.value(trackId, false); }

void TimelineTrackControls::setTrackSolo(const QString& trackId, bool solo) { m_solo[trackId] = solo; emit trackSoloChanged(trackId); }
bool TimelineTrackControls::isTrackSolo(const QString& trackId) const { return m_solo.value(trackId, false); }

void TimelineTrackControls::setTrackVisible(const QString& trackId, bool visible) { m_visible[trackId] = visible; emit trackVisibilityChanged(trackId); }
bool TimelineTrackControls::isTrackVisible(const QString& trackId) const { return m_visible.value(trackId, true); }

void TimelineTrackControls::muteAllExcept(const QString& trackId)
{
    for (auto it = m_muted.begin(); it != m_muted.end(); ++it)
        it.value() = (it.key() != trackId);
}

void TimelineTrackControls::unmuteAll() { for (auto it = m_muted.begin(); it != m_muted.end(); ++it) it.value() = false; }
void TimelineTrackControls::soloAllExcept(const QString& trackId) { for (auto it = m_solo.begin(); it != m_solo.end(); ++it) it.value() = (it.key() == trackId); }
void TimelineTrackControls::unsoloAll() { for (auto it = m_solo.begin(); it != m_solo.end(); ++it) it.value() = false; }

} // namespace ks
