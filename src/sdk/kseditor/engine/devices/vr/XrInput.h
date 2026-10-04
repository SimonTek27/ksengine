#pragma once

#include <QObject>
#include <QVector2D>
#include <QVector3D>
#include <functional>

#include "XrManager.h"

namespace ks {
namespace vr {

class XrInput : public QObject {
    Q_OBJECT
public:
    explicit XrInput(XrManager* xr, QObject* parent = nullptr);
    ~XrInput() override;

    enum Hand { Left = 0, Right = 1 };
    enum Button { Trigger = 0, Grip = 1, Menu = 2, System = 3,
                  A = 4, B = 5, Thumbstick = 6, Trackpad = 7 };
    enum Axis { ThumbstickAxis = 0, TrackpadAxis = 1 };

    bool isButtonPressed(Hand hand, Button button) const;
    float getTriggerValue(Hand hand) const;
    float getSqueezeValue(Hand hand) const;
    QVector2D getThumbstickValue(Hand hand) const;
    QVector2D getTrackpadValue(Hand hand) const;
    QMatrix4x4 getAimPose(Hand hand) const;
    QMatrix4x4 getGripPose(Hand hand) const;
    bool isPoseValid(Hand hand) const;
    bool isControllerConnected(Hand hand) const;

    // Ray casting helper
    void getAimRay(Hand hand, QVector3D& origin, QVector3D& direction) const;

signals:
    void buttonPressed(int hand, int button, bool pressed);
    void axisMoved(int hand, int axis, float x, float y);
    void poseUpdated(int hand);

private:
    void onControllerStateChanged();

    XrManager* m_xr;

    struct ButtonState {
        bool current = false;
        bool previous = false;
        bool justPressed() const { return current && !previous; }
        bool justReleased() const { return !current && previous; }
    };

    ButtonState m_leftButtons[8];
    ButtonState m_rightButtons[8];
    float m_leftTriggerPrev = 0.0f;
    float m_rightTriggerPrev = 0.0f;

    void updateButtonState(ButtonState& state, bool newValue);
    void checkButtonEdge(Hand hand, Button button, bool newValue);
};

}} // namespace ks::vr
