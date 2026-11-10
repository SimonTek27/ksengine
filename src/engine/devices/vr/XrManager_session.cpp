#include "XrManager.h"
#include "XrDispatch.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <string>
#include <utility>
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include <vulkan/vulkan.h>

namespace ks {
namespace device {

bool XrManager::createSwapchains()
{
    if (m_eyeCount == 0) return false;
    if (!s_dispatch.EnumerateSwapchainFormats) return false;

    uint32_t formatCount = 0;
    s_dispatch.EnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);
    m_swapchainFormats.resize(formatCount);
    s_dispatch.EnumerateSwapchainFormats(m_session, formatCount, &formatCount, m_swapchainFormats.data());

    m_colorFormat = 0;
    for (auto fmt : m_swapchainFormats) {
        if (fmt == VK_FORMAT_B8G8R8A8_SRGB || fmt == VK_FORMAT_R8G8B8A8_SRGB) {
            m_colorFormat = fmt;
            break;
        }
    }
    if (m_colorFormat == 0 && !m_swapchainFormats.empty())
        m_colorFormat = m_swapchainFormats[0];

    for (int i = 0; i < m_eyeCount && i < 2; i++) {
        auto& eye = m_eyes[i];
        XrSwapchainCreateInfo swapInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        swapInfo.format = m_colorFormat;
        swapInfo.sampleCount = 1;
        swapInfo.width = eye.swapchainImageWidth;
        swapInfo.height = eye.swapchainImageHeight;
        swapInfo.faceCount = 1;
        swapInfo.arraySize = 1;
        swapInfo.mipCount = 1;

        XrResult result = s_dispatch.CreateSwapchain(m_session, &swapInfo, &eye.swapchain);
        if (XR_FAILED(result)) return false;

        uint32_t imageCount = 0;
        s_dispatch.EnumerateSwapchainImages(eye.swapchain, 0, &imageCount, nullptr);
        std::vector<XrSwapchainImageVulkanKHR> colorImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        s_dispatch.EnumerateSwapchainImages(eye.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(colorImages.data()));

        eye.colorImages.clear();
        for (const auto& img : colorImages)
            eye.colorImages.push_back(img.image);
    }
    std::vector<XrActionSuggestedBinding> bindings;
    const XrPath profile = stringToPath("/interaction_profiles/oculus/touch_controller");
    if (profile == XR_NULL_PATH) return false;
    for (const XrControllerState* controller : {&m_leftController, &m_rightController}) {
        const char* hand = controller == &m_leftController ? "/user/hand/left" : "/user/hand/right";
        const std::string prefix(hand);
        const std::pair<XrAction, std::string> paths[] = {
            {controller->triggerAction, prefix + "/input/trigger/value"},
            {controller->squeezeAction, prefix + "/input/squeeze/value"},
            {controller->menuAction, prefix + "/input/menu/click"},
            {controller->thumbstickAction, prefix + "/input/thumbstick"},
            {controller->aimAction, prefix + "/input/aim/pose"},
            {controller->gripAction, prefix + "/input/grip/pose"}
        };
        for (const auto& actionPath : paths) {
            const XrPath path = stringToPath(actionPath.second.c_str());
            if (path == XR_NULL_PATH) return false;
            bindings.push_back({actionPath.first, path});
        }
    }
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    if (!s_dispatch.SuggestInteractionProfileBindings ||
        XR_FAILED(s_dispatch.SuggestInteractionProfileBindings(m_instance, &suggested))) return false;
    return true;
}

void XrManager::destroySwapchains()
{
    for (int i = 0; i < 2; i++) {
        auto& eye = m_eyes[i];
        if (eye.swapchain && s_dispatch.DestroySwapchain) {
            s_dispatch.DestroySwapchain(eye.swapchain);
            eye.swapchain = XR_NULL_HANDLE;
        }
        if (eye.depthSwapchain && s_dispatch.DestroySwapchain) {
            s_dispatch.DestroySwapchain(eye.depthSwapchain);
            eye.depthSwapchain = XR_NULL_HANDLE;
        }
        eye.colorImages.clear();
        eye.depthImages.clear();
    }
}

bool XrManager::createActions()
{
    if (!s_dispatch.CreateActionSet || !s_dispatch.CreateAction ||
        !s_dispatch.StringToPath || !s_dispatch.CreateActionSpace) return false;
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(setInfo.actionSetName, "game", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(setInfo.localizedActionSetName, "Game", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    setInfo.priority = 0;
    if (XR_FAILED(s_dispatch.CreateActionSet(m_instance, &setInfo, &m_gameActionSet))) return false;

    m_leftHandPath = stringToPath("/user/hand/left");
    m_rightHandPath = stringToPath("/user/hand/right");
    if (m_leftHandPath == XR_NULL_PATH || m_rightHandPath == XR_NULL_PATH) return false;
    const std::vector<XrPath> hands{m_leftHandPath, m_rightHandPath};
    const auto makeHandActions = [this, &hands](XrControllerState& controller,
                                                 XrPath hand, const char* suffix) {
        controller.handPath = hand;
        const std::string side = suffix;
        controller.triggerAction = createAction(m_gameActionSet,
            ("trigger_" + side).c_str(), "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, hands);
        controller.squeezeAction = createAction(m_gameActionSet,
            ("squeeze_" + side).c_str(), "Squeeze", XR_ACTION_TYPE_FLOAT_INPUT, hands);
        controller.menuAction = createAction(m_gameActionSet,
            ("menu_" + side).c_str(), "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, hands);
        controller.thumbstickAction = createAction(m_gameActionSet,
            ("thumbstick_" + side).c_str(), "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, hands);
        controller.aimAction = createAction(m_gameActionSet,
            ("aim_pose_" + side).c_str(), "Aim pose", XR_ACTION_TYPE_POSE_INPUT, hands);
        controller.gripAction = createAction(m_gameActionSet,
            ("grip_pose_" + side).c_str(), "Grip pose", XR_ACTION_TYPE_POSE_INPUT, hands);
        if (!controller.triggerAction || !controller.squeezeAction || !controller.menuAction ||
            !controller.thumbstickAction || !controller.aimAction || !controller.gripAction)
            return false;
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceInfo.subactionPath = hand;
        spaceInfo.action = controller.aimAction;
        if (XR_FAILED(s_dispatch.CreateActionSpace(m_session, &spaceInfo, &controller.aimSpace)))
            return false;
        spaceInfo.action = controller.gripAction;
        return XR_SUCCEEDED(s_dispatch.CreateActionSpace(m_session, &spaceInfo,
                                                          &controller.gripSpace));
    };
    if (!makeHandActions(m_leftController, m_leftHandPath, "left") ||
        !makeHandActions(m_rightController, m_rightHandPath, "right")) return false;
    std::vector<XrActionSuggestedBinding> bindings;
    const XrPath profile = stringToPath("/interaction_profiles/oculus/touch_controller");
    if (profile == XR_NULL_PATH) return false;
    for (const XrControllerState* controller : {&m_leftController, &m_rightController}) {
        const std::string prefix = controller == &m_leftController
            ? "/user/hand/left" : "/user/hand/right";
        const std::pair<XrAction, std::string> paths[] = {
            {controller->triggerAction, prefix + "/input/trigger/value"},
            {controller->squeezeAction, prefix + "/input/squeeze/value"},
            {controller->menuAction, prefix + "/input/menu/click"},
            {controller->thumbstickAction, prefix + "/input/thumbstick"},
            {controller->aimAction, prefix + "/input/aim/pose"},
            {controller->gripAction, prefix + "/input/grip/pose"}
        };
        for (const auto& actionPath : paths) {
            const XrPath path = stringToPath(actionPath.second.c_str());
            if (path == XR_NULL_PATH) return false;
            bindings.push_back({actionPath.first, path});
        }
    }
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = profile;
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    if (!s_dispatch.SuggestInteractionProfileBindings ||
        XR_FAILED(s_dispatch.SuggestInteractionProfileBindings(m_instance, &suggested))) return false;
    return true;
}

void XrManager::destroyActions()
{
    for (XrControllerState* controller : {&m_leftController, &m_rightController}) {
        if (controller->aimSpace && s_dispatch.DestroySpace)
            s_dispatch.DestroySpace(controller->aimSpace);
        if (controller->gripSpace && s_dispatch.DestroySpace)
            s_dispatch.DestroySpace(controller->gripSpace);
        controller->aimSpace = controller->gripSpace = XR_NULL_HANDLE;
        const XrAction actions[] = {controller->aimAction, controller->gripAction,
            controller->triggerAction, controller->squeezeAction,
            controller->thumbstickAction, controller->menuAction};
        for (XrAction action : actions)
            if (action && s_dispatch.DestroyAction) s_dispatch.DestroyAction(action);
        controller->aimAction = controller->gripAction = controller->triggerAction =
            controller->squeezeAction = controller->thumbstickAction = controller->menuAction =
                XR_NULL_HANDLE;
        controller->connected = false;
        controller->aimValid = controller->gripValid = false;
        controller->triggerValue = controller->squeezeValueFloat = 0.0f;
        controller->triggerClicked = controller->squeezeValue = controller->menuClicked = false;
        controller->thumbstickValue = Vec2{};
    }
    if (m_gameActionSet && s_dispatch.DestroyActionSet)
        s_dispatch.DestroyActionSet(m_gameActionSet);
    m_gameActionSet = XR_NULL_HANDLE;
}

bool XrManager::suggestBindings()
{
    return true;
}

bool XrManager::attachActions()
{
    if (!s_dispatch.AttachSessionActionSets) return false;
    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_gameActionSet;
    return XR_SUCCEEDED(s_dispatch.AttachSessionActionSets(m_session, &attachInfo));
}

bool XrManager::pollEvents()
{
    if (!m_instance || !s_dispatch.PollEvent) return false;
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result = s_dispatch.PollEvent(m_instance, &event);
    while (result == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto& sessionEvent = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            handleSessionStateChanged(sessionEvent);
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            if (onInstanceLost) onInstanceLost();
            return false;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
        result = s_dispatch.PollEvent(m_instance, &event);
    }
    return result == XR_EVENT_UNAVAILABLE;
}

void XrManager::handleSessionStateChanged(const XrEventDataSessionStateChanged& event)
{
    XrSessionState oldState = m_sessionState;
    m_sessionState = event.state;
    if (onSessionStateChanged) onSessionStateChanged(oldState, event.state);

    switch (event.state) {
    case XR_SESSION_STATE_READY:
        if (s_dispatch.BeginSession && !m_sessionRunning) {
            XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (XR_SUCCEEDED(s_dispatch.BeginSession(m_session, &beginInfo))) {
                m_sessionRunning = true;
                if (onSessionRunningChanged) onSessionRunningChanged(true);
            }
        }
        break;
    case XR_SESSION_STATE_STOPPING:
        if (m_frameBegun) endXRFrame();
        if (m_sessionRunning && s_dispatch.EndSession) s_dispatch.EndSession(m_session);
        m_sessionRunning = false;
        if (onSessionRunningChanged) onSessionRunningChanged(false);
        break;
    case XR_SESSION_STATE_LOSS_PENDING:
    case XR_SESSION_STATE_EXITING:
        if (m_frameBegun) endXRFrame();
        if (m_sessionRunning && s_dispatch.EndSession) s_dispatch.EndSession(m_session);
        m_sessionRunning = false;
        m_sessionFocused = false;
        if (onSessionRunningChanged) onSessionRunningChanged(false);
        if (onSessionFocusChanged) onSessionFocusChanged(false);
        break;
    case XR_SESSION_STATE_FOCUSED:
        m_sessionFocused = true;
        if (onSessionFocusChanged) onSessionFocusChanged(true);
        break;
    case XR_SESSION_STATE_VISIBLE:
        m_sessionFocused = false;
        if (onSessionFocusChanged) onSessionFocusChanged(false);
        break;
    default:
        break;
    }
}

bool XrManager::pollActions()
{
    if (!m_sessionRunning || !s_dispatch.SyncActions || !s_dispatch.GetActionStateFloat ||
        !s_dispatch.GetActionStateBoolean || !s_dispatch.GetActionStateVector2f ||
        !s_dispatch.GetActionStatePose || !s_dispatch.LocateSpace)
        return false;
    XrActiveActionSet activeSet{m_gameActionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    if (XR_FAILED(s_dispatch.SyncActions(m_session, &syncInfo))) return false;

    const XrSpaceLocationFlags poseFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                           XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    const XrSpaceLocationFlags trackedFlags = XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                                              XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    for (XrControllerState* controller : {&m_leftController, &m_rightController}) {
        controller->connected = false;
        controller->aimValid = controller->gripValid = false;
        controller->triggerValue = controller->squeezeValueFloat = 0.0f;
        controller->triggerClicked = controller->squeezeValue = controller->menuClicked = false;
        controller->thumbstickValue = Vec2{};
        XrSpaceLocation aimLocation{XR_TYPE_SPACE_LOCATION};
        XrSpaceLocation gripLocation{XR_TYPE_SPACE_LOCATION};
        const XrPath hand = controller->handPath;
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.subactionPath = hand;
        getInfo.action = controller->triggerAction;
        XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_FAILED(s_dispatch.GetActionStateFloat(m_session, &getInfo, &trigger))) return false;
        controller->triggerValue = trigger.isActive ? trigger.currentState : 0.0f;
        controller->triggerClicked = trigger.isActive && trigger.currentState > 0.5f;

        getInfo.action = controller->squeezeAction;
        XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_FAILED(s_dispatch.GetActionStateFloat(m_session, &getInfo, &squeeze))) return false;
        controller->squeezeValueFloat = squeeze.isActive ? squeeze.currentState : 0.0f;
        controller->squeezeValue = controller->squeezeValueFloat > 0.5f;

        getInfo.action = controller->menuAction;
        XrActionStateBoolean menu{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (XR_FAILED(s_dispatch.GetActionStateBoolean(m_session, &getInfo, &menu))) return false;
        controller->menuClicked = menu.isActive && menu.currentState;

        getInfo.action = controller->thumbstickAction;
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_FAILED(s_dispatch.GetActionStateVector2f(m_session, &getInfo, &stick))) return false;
        controller->thumbstickValue = stick.isActive
            ? Vec2{stick.currentState.x, stick.currentState.y} : Vec2{};

        XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
        getInfo.action = controller->aimAction;
        if (XR_FAILED(s_dispatch.GetActionStatePose(m_session, &getInfo, &poseState))) return false;
        if (poseState.isActive) {
            aimLocation = {XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(s_dispatch.LocateSpace(controller->aimSpace, m_stageSpace
                    ? m_stageSpace : m_localSpace, m_predictedDisplayTime, &aimLocation))) {
                controller->aimPoseRaw = aimLocation.pose;
                controller->aimValid = (aimLocation.locationFlags & poseFlags) == poseFlags;
            } else controller->aimValid = false;
        }

        getInfo.action = controller->gripAction;
        if (XR_FAILED(s_dispatch.GetActionStatePose(m_session, &getInfo, &poseState))) return false;
        if (poseState.isActive) {
            gripLocation = {XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(s_dispatch.LocateSpace(controller->gripSpace, m_stageSpace
                    ? m_stageSpace : m_localSpace, m_predictedDisplayTime, &gripLocation))) {
                controller->gripPoseRaw = gripLocation.pose;
                controller->gripValid = (gripLocation.locationFlags & poseFlags) == poseFlags;
            } else controller->gripValid = false;
        }
        controller->connected =
            (controller->aimValid && (aimLocation.locationFlags & trackedFlags) != 0) ||
            (controller->gripValid && (gripLocation.locationFlags & trackedFlags) != 0);
    }
    return true;
}

bool XrManager::beginXRFrame()
{
    if (!m_sessionRunning || !s_dispatch.WaitFrame || !s_dispatch.BeginFrame) return false;
    m_shouldRenderFrame = false;
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(s_dispatch.WaitFrame(m_session, &waitInfo, &frameState))) return false;
    m_predictedDisplayTime = frameState.predictedDisplayTime;
    m_shouldRenderFrame = frameState.shouldRender == XR_TRUE;
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(s_dispatch.BeginFrame(m_session, &beginInfo))) return false;
    m_frameBegun = true;
    if (!m_shouldRenderFrame) {
        endXRFrame();
        return false;
    }
    if (s_dispatch.LocateViews) {
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = m_predictedDisplayTime;
        locateInfo.space = m_stageSpace ? m_stageSpace : m_localSpace;
        uint32_t locatedCount = 0;
        XrView locatedViews[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        m_viewState = {XR_TYPE_VIEW_STATE};
        XrResult result = s_dispatch.LocateViews(m_session, &locateInfo, &m_viewState,
                                                  static_cast<uint32_t>(m_eyeCount),
                                                  &locatedCount, locatedViews);
        if (XR_FAILED(result) || locatedCount != static_cast<uint32_t>(m_eyeCount)) {
            endXRFrame();
            return false;
        }
        for (int eyeIndex = 0; eyeIndex < m_eyeCount; ++eyeIndex)
            m_eyes[eyeIndex].view = locatedViews[eyeIndex];
    }
    if (s_dispatch.LocateSpace && m_viewSpace) {
        XrSpaceLocation headLocation{XR_TYPE_SPACE_LOCATION};
        const XrSpace baseSpace = m_stageSpace ? m_stageSpace : m_localSpace;
        if (XR_SUCCEEDED(s_dispatch.LocateSpace(m_viewSpace, baseSpace,
                m_predictedDisplayTime, &headLocation))) {
            m_headPoseRaw = headLocation.pose;
            const XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            m_headPoseValid = (headLocation.locationFlags & required) == required;
        } else {
            m_headPoseValid = false;
        }
    }
    pollActions();
    return true;
}

bool XrManager::endXRFrame()
{
    if (!s_dispatch.EndFrame || !m_frameBegun) return false;
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;
    const XrResult result = s_dispatch.EndFrame(m_session, &endInfo);
    m_frameBegun = false;
    return XR_SUCCEEDED(result);
}

bool XrManager::beginEyeRender(int eyeIndex)
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) return false;
    auto& eye = m_eyes[eyeIndex];
    if (!eye.swapchain || !s_dispatch.AcquireSwapchainImage) return false;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t imageIndex = 0;
    if (XR_FAILED(s_dispatch.AcquireSwapchainImage(eye.swapchain, &acquireInfo, &imageIndex))) return false;
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(s_dispatch.WaitSwapchainImage(eye.swapchain, &waitInfo))) return false;
    eye.currentImageIndex = static_cast<int32_t>(imageIndex);
    return true;
}

void XrManager::endEyeRender(int eyeIndex)
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) return;
    auto& eye = m_eyes[eyeIndex];
    if (!eye.swapchain || !s_dispatch.ReleaseSwapchainImage) return;
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    s_dispatch.ReleaseSwapchainImage(eye.swapchain, &releaseInfo);
    eye.currentImageIndex = -1;
}

XrPosef XrManager::headPose() const
{
    return m_headPoseRaw;
}

bool XrManager::headPoseValid() const
{
    return m_headPoseValid;
}

XrMat4 XrManager::projectionMatrix(int eyeIndex, float nearZ, float farZ) const
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) return XrMat4::identity();
    const XrFovf& fov = m_eyes[eyeIndex].view.fov;
    XrMat4 result{};
    const float tanLeft = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanDown = std::tan(fov.angleDown);
    const float tanUp = std::tan(fov.angleUp);
    const float width = tanRight - tanLeft;
    const float height = tanUp - tanDown;
    if (width <= 0.0f || height <= 0.0f || farZ <= nearZ)
        return XrMat4::identity();
    result.m[0] = 2.0f / width;
    result.m[5] = 2.0f / height;
    result.m[8] = (tanRight + tanLeft) / width;
    result.m[9] = (tanUp + tanDown) / height;
    result.m[10] = -farZ / (farZ - nearZ);
    result.m[11] = -1.0f;
    result.m[14] = -(farZ * nearZ) / (farZ - nearZ);
    result.m[15] = 0.0f;
    return result;
}

XrMat4 XrManager::viewMatrix(int eyeIndex) const
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) return XrMat4::identity();
    const XrPosef& pose = m_eyes[eyeIndex].view.pose;
    XrMat4 result{};
    result.rotate(pose.orientation.w, -pose.orientation.x, -pose.orientation.y,
                  -pose.orientation.z);
    result.translate(-pose.position.x, -pose.position.y, -pose.position.z);
    return result;
}

XrPath XrManager::stringToPath(const char* str)
{
    if (!s_dispatch.StringToPath || !m_instance) return XR_NULL_PATH;
    XrPath path = XR_NULL_PATH;
    s_dispatch.StringToPath(m_instance, str, &path);
    return path;
}

XrAction XrManager::createAction(XrActionSet actionSet, const char* name, const char* localizedName,
                                  XrActionType type, const std::vector<XrPath>& subactionPaths)
{
    if (!s_dispatch.CreateAction) return XR_NULL_HANDLE;
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(info.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.actionType = type;
    info.countSubactionPaths = static_cast<uint32_t>(subactionPaths.size());
    info.subactionPaths = subactionPaths.empty() ? nullptr : subactionPaths.data();
    XrAction action = XR_NULL_HANDLE;
    s_dispatch.CreateAction(actionSet, &info, &action);
    return action;
}

void XrManager::suggestInteractionProfileBindings(const char* profile,
                                                   const std::vector<XrActionSuggestedBinding>& bindings)
{
    if (!s_dispatch.SuggestInteractionProfileBindings || !m_instance) return;
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = stringToPath(profile);
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    s_dispatch.SuggestInteractionProfileBindings(m_instance, &suggested);
}

} // namespace device
} // namespace ks

#endif
