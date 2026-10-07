#pragma once

#include <functional>
#include <string>

#include "XrManager.h"

namespace ks {
namespace device {

class XrInput {
public:
    explicit XrInput(XrManager* xr);
    ~XrInput();

    enum Hand { Left = 0, Right = 1 };
    enum Button { Trigger = 0, Grip = 1, Menu = 2, System = 3,
                  A = 4, B = 5, Thumbstick = 6, Trackpad = 7 };
    enum Axis { ThumbstickAxis = 0, TrackpadAxis = 1 };

    bool isButtonPressed(Hand hand, Button button) const;
    float getTriggerValue(Hand hand) const;
    float getSqueezeValue(Hand hand) const;
    Vec2 getThumbstickValue(Hand hand) const;
    Vec2 getTrackpadValue(Hand hand) const;
    XrMat4 getAimPose(Hand hand) const;
    XrMat4 getGripPose(Hand hand) const;
    bool isPoseValid(Hand hand) const;
    bool isControllerConnected(Hand hand) const;

    /** Ray origin/direction from aim pose (origin = translation, dir = -Z axis). */
    void getAimRay(Hand hand, float origin[3], float direction[3]) const;

    // Callbacks
    std::function<void(int hand, int button, bool pressed)> onButtonPressed;
    std::function<void(int hand, int axis, float x, float y)> onAxisMoved;
    std::function<void(int hand)> onPoseUpdated;

    void update(); // poll and edge-detect buttons

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

} // namespace device
} // namespace ks
