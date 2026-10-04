#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include "../mesh/MeshOperations.h"
#include "../mesh/ShapeKeyData.h"

namespace ks {

class AnimationTimeline;

struct Keyframe {
    enum Interpolation {
        InterpolationNone = 0,
        InterpolationLinear,
        InterpolationSmooth,
        InterpolationStep
    };
    int frame = 0;
    float value = 0.0f;
    Interpolation interpolation = InterpolationLinear;
};

struct GraphCurve {
    QVector<Keyframe> keyframes;

    void addKeyframe(const Keyframe& kf) {
        keyframes.append(kf);
    }

    void removeKeyframe(int frame) {
        for (int i = 0; i < keyframes.size(); ++i) {
            if (keyframes[i].frame == frame) {
                keyframes.removeAt(i);
                return;
            }
        }
    }

    float evaluate(float frame) const {
        if (keyframes.isEmpty()) return 0.0f;
        if (keyframes.size() == 1) return keyframes[0].value;

        // Extrapolation: hold first/last
        if (frame <= keyframes.first().frame) return keyframes.first().value;
        if (frame >= keyframes.last().frame) return keyframes.last().value;

        for (int i = 0; i < keyframes.size() - 1; ++i) {
            if (frame >= keyframes[i].frame && frame <= keyframes[i+1].frame) {
                float span = (float)(keyframes[i+1].frame - keyframes[i].frame);
                if (span <= 0.0f) return keyframes[i].value;
                float t = (frame - keyframes[i].frame) / span;
                t = qBound(0.0f, t, 1.0f);

                switch (keyframes[i].interpolation) {
                case Keyframe::InterpolationStep:
                    return keyframes[i].value;
                case Keyframe::InterpolationSmooth: {
                    // Hermite cubic interpolation
                    float t2 = t * t;
                    float t3 = t2 * t;
                    float h00 = 2*t3 - 3*t2 + 1;
                    float h10 = t3 - 2*t2 + t;
                    float h01 = -2*t3 + 3*t2;
                    float h11 = t3 - t2;
                    return h00*keyframes[i].value + h10*span*0.5f
                         + h01*keyframes[i+1].value + h11*span*0.5f;
                }
                case Keyframe::InterpolationLinear:
                default:
                    return keyframes[i].value + (keyframes[i+1].value - keyframes[i].value) * t;
                }
            }
        }
        return keyframes.last().value;
    }
};

struct ShapeKeyAnimChannel {
    QString shapeKeyName;
    GraphCurve curve;
    int targetIndex = -1;
};

class ShapeKeyAnimDriver : public QObject {
    Q_OBJECT
    public:
    explicit ShapeKeyAnimDriver(QObject* parent = nullptr);

    void setTargetMesh(MeshData* mesh);
    MeshData* targetMesh() const { return m_mesh; }

    void addChannel(const QString& shapeKeyName);
    void removeChannel(const QString& shapeKeyName);
    void clearChannels();

    int channelCount() const { return m_channels.size(); }
    QStringList channelNames() const;

    void setKeyframe(const QString& shapeKeyName, int frame, float value, Keyframe::Interpolation interp = Keyframe::InterpolationLinear);
    void removeKeyframe(const QString& shapeKeyName, int frame);
    void clearKeyframes(const QString& shapeKeyName);

    void evaluateAtFrame(int frame);
    void evaluateAtTime(float timeSeconds, int fps = 30);

    QVector<ShapeKeyAnimChannel> channels() const { return m_channels; }

    void connectToTimeline(AnimationTimeline* timeline);
    void disconnectFromTimeline();

signals:
    void channelAdded(const QString& name);
    void channelRemoved(const QString& name);
    void evaluationApplied();
    void weightChanged(const QString& name, float weight);

private:
    void applyShapeKeyWeight(const QString& name, float weight);

    MeshData* m_mesh = nullptr;
    QVector<ShapeKeyAnimChannel> m_channels;
    AnimationTimeline* m_timeline = nullptr;

private slots:
    void onFrameChanged(int frame);
};

} // namespace ks
