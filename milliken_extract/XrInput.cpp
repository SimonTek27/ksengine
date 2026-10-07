#include "XrInput.h"
#include <cmath>
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)

namespace ks {
namespace device {

XrInput::XrInput(XrManager* xr)
    : m_xr(xr)
{
}

XrInput::~XrInput() = default;

bool XrInput::isButtonPressed(Hand hand, Button button) const
{
    if (button < 0 || button >= 8) return false;
    return hand == Left ? m_leftButtons[button].current
                        : m_rightButtons[button].current;
}

float XrInput::getTriggerValue(Hand hand) const
{
    return hand == Left ? m_xr->leftController().triggerValue
                        : m_xr->rightController().triggerValue;
}

float XrInput::getSqueezeValue(Hand hand) const
{
    return hand == Left ? m_xr->leftController().squeezeValueFloat
                        : m_xr->rightController().squeezeValueFloat;
}

Vec2 XrInput::getThumbstickValue(Hand hand) const
{
    return hand == Left ? m_xr->leftController().thumbstickValue
                        : m_xr->rightController().thumbstickValue;
}

Vec2 XrInput::getTrackpadValue(Hand hand) const
{
    return hand == Left ? m_xr->leftController().trackpadValue
                        : m_xr->rightController().trackpadValue;
}

XrMat4 XrInput::getAimPose(Hand hand) const
{
    return hand == Left ? m_xr->leftController().aimPose
                        : m_xr->rightController().aimPose;
}

XrMat4 XrInput::getGripPose(Hand hand) const
{
    return hand == Left ? m_xr->leftController().gripPose
                        : m_xr->rightController().gripPose;
}

bool XrInput::isPoseValid(Hand hand) const
{
    return hand == Left ? m_xr->leftController().aimValid
                        : m_xr->rightController().aimValid;
}

bool XrInput::isControllerConnected(Hand hand) const
{
    return hand == Left ? m_xr->leftController().connected
                        : m_xr->rightController().connected;
}

void XrInput::getAimRay(Hand hand, float origin[3], float direction[3]) const
{
    origin[0] = origin[1] = origin[2] = 0.0f;
    direction[0] = 0.0f;
    direction[1] = 0.0f;
    direction[2] = -1.0f;

    if (!m_xr) return;
    auto* ctrl = hand == Left ? &m_xr->leftController() : &m_xr->rightController();
    if (!ctrl->aimValid) return;

    // Column-major: translation in m[12], m[13], m[14]
    const auto& mat = ctrl->aimPose;
    origin[0] = mat.m[12];
    origin[1] = mat.m[13];
    origin[2] = mat.m[14];

    // -Z axis of the rotation part
    float dx = -mat.m[8];
    float dy = -mat.m[9];
    float dz = -mat.m[10];
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len > 1e-6f) {
        dx /= len; dy /= len; dz /= len;
    }
    direction[0] = dx;
    direction[1] = dy;
    direction[2] = dz;
}

void XrInput::updateButtonState(ButtonState& state, bool newValue)
{
    state.previous = state.current;
    state.current = newValue;
}

void XrInput::onControllerStateChanged()
{
    auto updateFromCtrl = [this](Hand hand, const XrControllerState& ctrl) {
        checkButtonEdge(hand, Trigger, ctrl.triggerClicked);
        checkButtonEdge(hand, Grip, ctrl.squeezeValue);
        checkButtonEdge(hand, Menu, ctrl.menuClicked);
        if (ctrl.aimValid) if (onPoseUpdated) onPoseUpdated((int)hand);
    };
    updateFromCtrl(Left, m_xr->leftController());
    updateFromCtrl(Right, m_xr->rightController());
}

void XrInput::checkButtonEdge(Hand hand, Button button, bool newValue)
{
    auto& state = hand == Left ? m_leftButtons[button] : m_rightButtons[button];
    updateButtonState(state, newValue);

    if (state.justPressed()) {
        if (onButtonPressed) onButtonPressed((int)hand, (int)button, true);
    } else if (state.justReleased()) {
        if (onButtonPressed) onButtonPressed((int)hand, (int)button, false);
    }
}

} // namespace device
} // namespace ks

#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
