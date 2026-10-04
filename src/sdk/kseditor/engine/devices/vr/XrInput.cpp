#include "XrInput.h"
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include <QDebug>

namespace ks {
namespace vr {

XrInput::XrInput(XrManager* xr, QObject* parent)
    : QObject(parent)
    , m_xr(xr)
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

QVector2D XrInput::getThumbstickValue(Hand hand) const
{
    return hand == Left ? m_xr->leftController().thumbstickValue
                        : m_xr->rightController().thumbstickValue;
}

QVector2D XrInput::getTrackpadValue(Hand hand) const
{
    return hand == Left ? m_xr->leftController().trackpadValue
                        : m_xr->rightController().trackpadValue;
}

QMatrix4x4 XrInput::getAimPose(Hand hand) const
{
    return hand == Left ? m_xr->leftController().aimPose
                        : m_xr->rightController().aimPose;
}

QMatrix4x4 XrInput::getGripPose(Hand hand) const
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

void XrInput::getAimRay(Hand hand, QVector3D& origin, QVector3D& direction) const
{
    origin = QVector3D(0, 0, 0);
    direction = QVector3D(0, 0, -1);

    auto* ctrl = hand == Left ? &m_xr->leftController() : &m_xr->rightController();
    if (!ctrl->aimValid) return;

    origin = ctrl->aimPose * QVector3D(0, 0, 0);
    direction = (ctrl->aimPose * QVector3D(0, 0, -1)) - origin;
    direction.normalize();
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
        if (ctrl.aimValid) emit poseUpdated((int)hand);
    };
    updateFromCtrl(Left, m_xr->leftController());
    updateFromCtrl(Right, m_xr->rightController());
}

void XrInput::checkButtonEdge(Hand hand, Button button, bool newValue)
{
    auto& state = hand == Left ? m_leftButtons[button] : m_rightButtons[button];
    updateButtonState(state, newValue);

    if (state.justPressed()) {
        emit buttonPressed((int)hand, (int)button, true);
    } else if (state.justReleased()) {
        emit buttonPressed((int)hand, (int)button, false);
    }
}

}} // namespace ks::vr

#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
