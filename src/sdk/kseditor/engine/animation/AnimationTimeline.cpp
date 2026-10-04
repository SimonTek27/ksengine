#include "AnimationTimeline.h"
#include <cmath>

namespace ks {

AnimationTimeline* AnimationTimeline::s_instance = nullptr;

AnimationTimeline* AnimationTimeline::instance() {
    if (!s_instance) s_instance = new AnimationTimeline();
    return s_instance;
}

AnimationTimeline::AnimationTimeline(QObject* parent) : QObject(parent) {
    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &AnimationTimeline::advanceFrame);
}

AnimationTimeline::~AnimationTimeline() {
    m_playTimer->stop();
}

void AnimationTimeline::createAnimation(const QString& name) {
    Animation anim;
    anim.id = QUuid::createUuid().toString();
    anim.name = name.isEmpty() ? "Animation " + QString::number(m_animations.size() + 1) : name;
    m_animations[anim.id] = anim;
    m_tracks[anim.id] = {};
    if (m_currentAnimationId.isEmpty()) m_currentAnimationId = anim.id;
    emit animationCreated(anim.id);
}

void AnimationTimeline::deleteAnimation(const QString& id) {
    if (!m_animations.contains(id)) return;
    m_animations.remove(id);
    m_tracks.remove(id);
    if (m_currentAnimationId == id) {
        m_currentAnimationId = m_animations.isEmpty() ? QString() : m_animations.constBegin().key();
        emit currentAnimationChanged(m_currentAnimationId);
    }
    emit animationDeleted(id);
}

void AnimationTimeline::setCurrentAnimation(const QString& id) {
    if (m_animations.contains(id) && m_currentAnimationId != id) {
        m_currentAnimationId = id;
        auto anim = m_animations[id];
        m_rangeStart = anim.startFrame;
        m_rangeEnd = anim.endFrame;
        m_frameRate = anim.frameRate;
        m_loop = anim.loop;
        emit currentAnimationChanged(id);
    }
}

void AnimationTimeline::renameAnimation(const QString& id, const QString& name) {
    if (m_animations.contains(id)) {
        m_animations[id].name = name;
        emit currentAnimationChanged(id);
    }
}

void AnimationTimeline::duplicateAnimation(const QString& id) {
    if (!m_animations.contains(id)) return;
    Animation newAnim = m_animations[id];
    newAnim.id = QUuid::createUuid().toString();
    newAnim.name += " (Copy)";
    m_animations[newAnim.id] = newAnim;
    m_tracks[newAnim.id] = m_tracks.value(id);
    emit animationCreated(newAnim.id);
}

QVector<AnimationTimeline::Animation> AnimationTimeline::getAnimations() const {
    return m_animations.values().toVector();
}

AnimationTimeline::Animation AnimationTimeline::getAnimation(const QString& id) const {
    return m_animations.value(id);
}

void AnimationTimeline::addTrack(const QString& animId, const Track& track) {
    if (!m_animations.contains(animId)) return;
    Track t = track;
    if (t.id.isEmpty()) t.id = QUuid::createUuid().toString();
    m_tracks[animId][t.id] = t;
    emit trackAdded(animId, t.id);
}

void AnimationTimeline::removeTrack(const QString& animId, const QString& trackId) {
    if (!m_animations.contains(animId)) return;
    m_tracks[animId].remove(trackId);
    emit trackRemoved(animId, trackId);
}

void AnimationTimeline::setTrackLocked(const QString& animId, const QString& trackId, bool locked) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    m_tracks[animId][trackId].locked = locked;
}

void AnimationTimeline::setTrackMuted(const QString& animId, const QString& trackId, bool muted) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    m_tracks[animId][trackId].muted = muted;
}

void AnimationTimeline::setTrackSolo(const QString& animId, const QString& trackId, bool solo) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    m_tracks[animId][trackId].solo = solo;
}

QVector<AnimationTimeline::Track> AnimationTimeline::getTracks(const QString& animId) const {
    if (!m_animations.contains(animId)) return {};
    return m_tracks.value(animId).values().toVector();
}

void AnimationTimeline::addKeyframe(const QString& animId, const QString& trackId, const Keyframe& kf) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    m_tracks[animId][trackId].keyframes[kf.frame] = kf;
    emit keyframeAdded(animId, trackId, kf.frame);
}

void AnimationTimeline::removeKeyframe(const QString& animId, const QString& trackId, int frame) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    m_tracks[animId][trackId].keyframes.remove(frame);
    emit keyframeRemoved(animId, trackId, frame);
}

void AnimationTimeline::setKeyframeValue(const QString& animId, const QString& trackId, int frame, float value) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    auto& kfs = m_tracks[animId][trackId].keyframes;
    if (!kfs.contains(frame)) {
        Keyframe kf;
        kf.frame = frame;
        kf.value = value;
        kfs[frame] = kf;
    } else {
        kfs[frame].value = value;
    }
    emit keyframeChanged(animId, trackId, frame);
}

void AnimationTimeline::setKeyframeInterpolation(const QString& animId, const QString& trackId, int frame, const QString& interp) {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return;
    auto& kfs = m_tracks[animId][trackId].keyframes;
    if (kfs.contains(frame)) {
        kfs[frame].interpolation = interp;
        emit keyframeChanged(animId, trackId, frame);
    }
}

float AnimationTimeline::evaluateTrack(const QString& animId, const QString& trackId, int frame) const {
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return 0.0f;
    const Track& track = m_tracks[animId][trackId];
    if (track.keyframes.isEmpty()) return 0.0f;

    const auto& kfs = track.keyframes;

    // Extrapolation: hold first/last
    if (frame <= kfs.constBegin().key()) return kfs.constBegin().value().value;
    auto lastIt = std::prev(kfs.constEnd());
    if (frame >= lastIt.key()) return lastIt.value().value;

    auto it = kfs.lowerBound(frame);
    if (it == kfs.constBegin()) return it.value().value;
    if (it == kfs.constEnd()) return std::prev(it).value().value;

    const Keyframe& right = it.value();
    const Keyframe& left = std::prev(it).value();
    float span = static_cast<float>(right.frame - left.frame);
    if (span <= 0.0f) return left.value;
    float t = static_cast<float>(frame - left.frame) / span;
    t = qBound(0.0f, t, 1.0f);

    const QString& interp = left.interpolation;

    if (interp == "constant" || interp == "step") {
        return left.value;
    }

    if (interp == "cubic" || interp == "bezier") {
        // Hermite interpolation using tangent vectors (slope = value per frame)
        float tIn = left.tangentOut.x() * span;
        float tOut = right.tangentIn.x() * span;
        float t2 = t * t;
        float t3 = t2 * t;
        float h00 = 2*t3 - 3*t2 + 1;
        float h10 = t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 = t3 - t2;
        return h00*left.value + h10*tIn + h01*right.value + h11*tOut;
    }

    if (interp == "easeIn") {
        return left.value + (right.value - left.value) * (t * t);
    }
    if (interp == "easeOut") {
        return left.value + (right.value - left.value) * (1.0f - (1.0f - t) * (1.0f - t));
    }
    if (interp == "easeInOut") {
        return left.value + (right.value - left.value) * (t < 0.5f ? 2*t*t : 1.0f - 2*(1-t)*(1-t));
    }

    // Default: linear
    return left.value + (right.value - left.value) * t;
}

void AnimationTimeline::setCurrentFrame(int frame) {
    if (m_currentFrame != frame) {
        m_currentFrame = frame;
        emit currentFrameChanged(frame);
    }
}

void AnimationTimeline::setPlayRange(int start, int end) {
    m_rangeStart = start;
    m_rangeEnd = end;
}

void AnimationTimeline::getPlayRange(int& start, int& end) const {
    start = m_rangeStart;
    end = m_rangeEnd;
}

void AnimationTimeline::setFrameRate(int fps) {
    if (m_frameRate != fps) {
        m_frameRate = fps;
        if (m_playing) {
            m_playTimer->setInterval(1000 / m_frameRate);
        }
        emit frameRateChanged(fps);
    }
}

void AnimationTimeline::setLoop(bool loop) {
    m_loop = loop;
}

void AnimationTimeline::play() {
    if (m_playing) return;
    m_playing = true;
    m_playTimer->start(1000 / m_frameRate);
    emit playbackStateChanged(true);
}

void AnimationTimeline::pause() {
    if (!m_playing) return;
    m_playing = false;
    m_playTimer->stop();
    emit playbackStateChanged(false);
}

void AnimationTimeline::stop() {
    m_playing = false;
    m_playTimer->stop();
    setCurrentFrame(m_rangeStart);
    emit playbackStateChanged(false);
}

void AnimationTimeline::togglePlayPause() {
    if (m_playing) pause(); else play();
}

void AnimationTimeline::stepForward() {
    setCurrentFrame(qMin(m_currentFrame + 1, m_rangeEnd));
}

void AnimationTimeline::stepBackward() {
    setCurrentFrame(qMax(m_currentFrame - 1, m_rangeStart));
}

void AnimationTimeline::goToStart() {
    setCurrentFrame(m_rangeStart);
}

void AnimationTimeline::goToEnd() {
    setCurrentFrame(m_rangeEnd);
}

QMap<int, float> AnimationTimeline::bakeTrack(const QString& animId, const QString& trackId, int step) const {
    QMap<int, float> baked;
    if (!m_animations.contains(animId) || !m_tracks[animId].contains(trackId)) return baked;
    for (int f = m_rangeStart; f <= m_rangeEnd; f += step) {
        baked[f] = evaluateTrack(animId, trackId, f);
    }
    return baked;
}

void AnimationTimeline::advanceFrame() {
    int nextFrame = m_currentFrame + 1;
    if (nextFrame > m_rangeEnd) {
        if (m_loop) {
            nextFrame = m_rangeStart;
        } else {
            stop();
            return;
        }
    }
    setCurrentFrame(nextFrame);
}

} // namespace ks
