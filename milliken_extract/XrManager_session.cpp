#include "XrManager.h"
#include "XrDispatch.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <string>
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include <vulkan/vulkan.h>

namespace ks {
namespace device {

bool XrManager::createSwapchains()
{
    if (m_eyeCount == 0) return false;

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
    if (m_colorFormat == 0 && !m_swapchainFormats.empty()) {
        m_colorFormat = m_swapchainFormats[0];
    }

    std::vector<int64_t> preferredDepthFormats = {
        (int64_t)VK_FORMAT_D32_SFLOAT,
        (int64_t)VK_FORMAT_D24_UNORM_S8_UINT,
        (int64_t)VK_FORMAT_D16_UNORM
    };
    m_depthFormat = 0;
    for (auto pref : preferredDepthFormats) {
        for (auto fmt : m_swapchainFormats) {
            if (fmt == pref) { m_depthFormat = fmt; break; }
        }
        if (m_depthFormat != 0) break;
    }

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
        if (XR_FAILED(result)) {
            emitError((std::string("Failed to create swapchain for eye ") + std::to_string(i)));
            return false;
        }

        uint32_t imageCount = 0;
        s_dispatch.EnumerateSwapchainImages(eye.swapchain, 0, &imageCount, nullptr);
        std::vector<XrSwapchainImageVulkanKHR> colorImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        s_dispatch.EnumerateSwapchainImages(eye.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(colorImages.data()));

        eye.colorImages.clear();
        for (const auto& img : colorImages) {
            eye.colorImages.push_back(img.image);
        }

        if (m_depthFormat != 0) {
            swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            swapInfo.format = m_depthFormat;

            result = s_dispatch.CreateSwapchain(m_session, &swapInfo, &eye.depthSwapchain);
            if (XR_SUCCEEDED(result)) {
                uint32_t depthCount = 0;
                s_dispatch.EnumerateSwapchainImages(eye.depthSwapchain, 0, &depthCount, nullptr);
                std::vector<XrSwapchainImageVulkanKHR> depthImages(depthCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
                s_dispatch.EnumerateSwapchainImages(eye.depthSwapchain, depthCount, &depthCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(depthImages.data()));

                eye.depthImages.clear();
                for (const auto& img : depthImages) {
                    eye.depthImages.push_back(img.image);
                }
            }
        }
    }

    return true;
}

void XrManager::destroySwapchains()
{
    for (int i = 0; i < 2; i++) {
        auto& eye = m_eyes[i];
        if (eye.swapchain) { s_dispatch.DestroySwapchain(eye.swapchain); eye.swapchain = XR_NULL_HANDLE; }
        if (eye.depthSwapchain) { s_dispatch.DestroySwapchain(eye.depthSwapchain); eye.depthSwapchain = XR_NULL_HANDLE; }
        eye.colorImages.clear();
        eye.depthImages.clear();
    }
}

bool XrManager::createActions()
{
    XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(actionSetInfo.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(actionSetInfo.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    actionSetInfo.priority = 0;

    XrResult result = s_dispatch.CreateActionSet(m_instance, &actionSetInfo, &m_gameActionSet);
    if (XR_FAILED(result)) return false;

    m_leftHandPath = stringToPath("/user/hand/left");
    m_rightHandPath = stringToPath("/user/hand/right");

    std::vector<XrPath> handPaths = { m_leftHandPath, m_rightHandPath };

    auto createCtrlActions = [&](XrControllerState& ctrl, const char* handStr, XrPath handPath) {
        ctrl.handPath = handPath;

        std::string prefix = std::string("hand_") + handStr + "_";

        ctrl.aimAction = createAction(m_gameActionSet,
            (prefix + "aim_pose").c_str(),
            /*localized*/ (std::string(handStr) + " Hand Aim Pose").c_str(),
            XR_ACTION_TYPE_POSE_INPUT, handPaths);

        ctrl.gripAction = createAction(m_gameActionSet,
            (prefix + "grip_pose").c_str(),
            /*localized*/ (std::string(handStr) + " Hand Grip Pose").c_str(),
            XR_ACTION_TYPE_POSE_INPUT, handPaths);

        ctrl.squeezeAction = createAction(m_gameActionSet,
            (prefix + "squeeze").c_str(),
            /*localized*/ (std::string(handStr) + " Hand Squeeze").c_str(),
            XR_ACTION_TYPE_FLOAT_INPUT, handPaths);

        ctrl.triggerAction = createAction(m_gameActionSet,
            (prefix + "trigger").c_str(),
            /*localized*/ (std::string(handStr) + " Hand Trigger").c_str(),
            XR_ACTION_TYPE_FLOAT_INPUT, handPaths);

        ctrl.thumbstickAction = createAction(m_gameActionSet,
            (prefix + "thumbstick").c_str(),
            /*localized*/ (std::string(handStr) + " Thumbstick").c_str(),
            XR_ACTION_TYPE_VECTOR2F_INPUT, handPaths);

        ctrl.trackpadAction = createAction(m_gameActionSet,
            (prefix + "trackpad").c_str(),
            /*localized*/ (std::string(handStr) + " Trackpad").c_str(),
            XR_ACTION_TYPE_VECTOR2F_INPUT, handPaths);

        ctrl.menuAction = createAction(m_gameActionSet,
            (prefix + "menu").c_str(),
            /*localized*/ (std::string(handStr) + " Menu Button").c_str(),
            XR_ACTION_TYPE_BOOLEAN_INPUT, handPaths);

        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = ctrl.aimAction;
        spaceInfo.subactionPath = handPath;
        spaceInfo.poseInActionSpace = {{0,0,0,1}, {0,0,0}};
        s_dispatch.CreateActionSpace(m_session, &spaceInfo, &ctrl.aimSpace);

        spaceInfo.action = ctrl.gripAction;
        s_dispatch.CreateActionSpace(m_session, &spaceInfo, &ctrl.gripSpace);
    };

    createCtrlActions(m_leftController, "left", m_leftHandPath);
    createCtrlActions(m_rightController, "right", m_rightHandPath);

    return true;
}

void XrManager::destroyActions()
{
    auto destroyCtrlSpaces = [](XrControllerState& ctrl) {
        if (ctrl.aimSpace) { s_dispatch.DestroySpace(ctrl.aimSpace); ctrl.aimSpace = XR_NULL_HANDLE; }
        if (ctrl.gripSpace) { s_dispatch.DestroySpace(ctrl.gripSpace); ctrl.gripSpace = XR_NULL_HANDLE; }
    };

    destroyCtrlSpaces(m_leftController);
    destroyCtrlSpaces(m_rightController);

    if (m_gameActionSet) {
        s_dispatch.DestroyActionSet(m_gameActionSet);
        m_gameActionSet = XR_NULL_HANDLE;
    }
}

bool XrManager::suggestBindings()
{
    if (!m_gameActionSet) return false;

    auto getBinding = [&](XrAction action, const char* path) -> XrActionSuggestedBinding {
        return { action, stringToPath(path) };
    };

    std::vector<XrActionSuggestedBinding> khrBindings;
    auto addBinding = [&](XrAction action, const char* path) {
        khrBindings.push_back(getBinding(action, path));
    };

    addBinding(m_leftController.aimAction, "/user/hand/left/input/aim/pose");
    addBinding(m_leftController.gripAction, "/user/hand/left/input/grip/pose");
    addBinding(m_leftController.squeezeAction, "/user/hand/left/input/squeeze/value");
    addBinding(m_leftController.triggerAction, "/user/hand/left/input/trigger/value");
    addBinding(m_leftController.menuAction, "/user/hand/left/input/menu/click");

    addBinding(m_rightController.aimAction, "/user/hand/right/input/aim/pose");
    addBinding(m_rightController.gripAction, "/user/hand/right/input/grip/pose");
    addBinding(m_rightController.squeezeAction, "/user/hand/right/input/squeeze/value");
    addBinding(m_rightController.triggerAction, "/user/hand/right/input/trigger/value");
    addBinding(m_rightController.menuAction, "/user/hand/right/input/menu/click");

    suggestInteractionProfileBindings(XR_INTERACTION_PROFILE_KHR_SIMPLE_CONTROLLER, khrBindings);

    return true;
}

bool XrManager::attachActions()
{
    if (!m_gameActionSet) return false;

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_gameActionSet;

    XrResult result = s_dispatch.AttachSessionActionSets(m_session, &attachInfo);
    return XR_SUCCEEDED(result);
}

bool XrManager::pollEvents()
{
    if (!m_instance) return false;

    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    XrResult result = s_dispatch.PollEvent(m_instance, &event);

    while (XR_SUCCEEDED(result)) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto& sessionEvent = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            handleSessionStateChanged(sessionEvent);
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            if (onInstanceLost) onInstanceLost();
            return false;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            std::fprintf(stderr, "XR: %s\n", "info"); // was qInfo stream
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            std::fprintf(stderr, "XR: %s\n", "info"); // was qInfo stream
            break;
        case XR_TYPE_EVENT_DATA_EVENTS_LOST:
            std::fprintf(stderr, "XR: %s\n", "info"); // was qInfo stream
            break;
        default:
            break;
        }

        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        result = s_dispatch.PollEvent(m_instance, &event);
    }

    return true;
}

void XrManager::handleSessionStateChanged(const XrEventDataSessionStateChanged& event)
{
    XrSessionState oldState = m_sessionState;
    m_sessionState = event.state;

    std::fprintf(stderr, "XR Session state changed: %d
", (int)(oldState << "->" << m_sessionState));

    switch (m_sessionState) {
    case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XrResult result = s_dispatch.BeginSession(m_session, &beginInfo);
        if (XR_SUCCEEDED(result)) {
            m_sessionRunning = true;
            if (onSessionRunningChanged) onSessionRunningChanged(true);
        }
        break;
    }
    case XR_SESSION_STATE_SYNCHRONIZED:
    case XR_SESSION_STATE_VISIBLE:
        break;
    case XR_SESSION_STATE_FOCUSED:
        m_sessionFocused = true;
        if (onSessionFocusChanged) onSessionFocusChanged(true);
        break;
    case XR_SESSION_STATE_STOPPING:
        m_sessionRunning = false;
        m_sessionFocused = false;
        if (onSessionFocusChanged) onSessionFocusChanged(false);
        if (onSessionRunningChanged) onSessionRunningChanged(false);
        s_dispatch.EndSession(m_session);
        break;
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
        m_sessionRunning = false;
        m_sessionFocused = false;
        if (onSessionFocusChanged) onSessionFocusChanged(false);
        if (onSessionRunningChanged) onSessionRunningChanged(false);
        break;
    default:
        break;
    }

    if (onSessionStateChanged) onSessionStateChanged(oldState, m_sessionState);
}

bool XrManager::pollActions()
{
    if (!m_session || !m_sessionFocused) return false;

    XrActiveActionSet activeSet{m_gameActionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;

    XrResult result = s_dispatch.SyncActions(m_session, &syncInfo);
    if (XR_FAILED(result)) return false;

    auto pollController = [&](XrControllerState& ctrl, XrPath handPath) {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.subactionPath = handPath;

        getInfo.action = ctrl.squeezeAction;
        XrActionStateFloat squeezeState{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(s_dispatch.GetActionStateFloat(m_session, &getInfo, &squeezeState)) && squeezeState.isActive) {
            ctrl.squeezeValueFloat = squeezeState.currentState;
            ctrl.squeezeValue = squeezeState.currentState > 0.5f;
        }

        getInfo.action = ctrl.triggerAction;
        XrActionStateFloat triggerState{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(s_dispatch.GetActionStateFloat(m_session, &getInfo, &triggerState)) && triggerState.isActive) {
            ctrl.triggerValue = triggerState.currentState;
            ctrl.triggerClicked = triggerState.currentState > 0.95f;
        }

        getInfo.action = ctrl.thumbstickAction;
        XrActionStateVector2f thumbstickState{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(s_dispatch.GetActionStateVector2f(m_session, &getInfo, &thumbstickState)) && thumbstickState.isActive) {
            ctrl.thumbstickValue = Vec2(thumbstickState.currentState.x, thumbstickState.currentState.y);
        }

        getInfo.action = ctrl.trackpadAction;
        XrActionStateVector2f trackpadState{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(s_dispatch.GetActionStateVector2f(m_session, &getInfo, &trackpadState)) && trackpadState.isActive) {
            ctrl.trackpadValue = Vec2(trackpadState.currentState.x, trackpadState.currentState.y);
        }

        getInfo.action = ctrl.menuAction;
        XrActionStateBoolean menuState{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (XR_SUCCEEDED(s_dispatch.GetActionStateBoolean(m_session, &getInfo, &menuState)) && menuState.isActive) {
            ctrl.menuClicked = menuState.currentState;
        }

        getInfo.action = ctrl.aimAction;
        XrActionStatePose aimPoseState{XR_TYPE_ACTION_STATE_POSE};
        if (XR_SUCCEEDED(s_dispatch.GetActionStatePose(m_session, &getInfo, &aimPoseState)) && aimPoseState.isActive) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (ctrl.aimSpace && XR_SUCCEEDED(s_dispatch.LocateSpace(ctrl.aimSpace, m_viewSpace, 0, &location))
                && (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                auto& p = location.pose;
                XrMat4 mat;
                mat.setToIdentity();
                mat.translate(p.position.x, p.position.y, -p.position.z);
                mat.rotate(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
                ctrl.aimPose = mat;
                ctrl.aimValid = true;
            }
        }

        getInfo.action = ctrl.gripAction;
        if (XR_SUCCEEDED(s_dispatch.GetActionStatePose(m_session, &getInfo, &aimPoseState)) && aimPoseState.isActive) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (ctrl.gripSpace && XR_SUCCEEDED(s_dispatch.LocateSpace(ctrl.gripSpace, m_viewSpace, 0, &location))
                && (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                auto& p = location.pose;
                XrMat4 mat;
                mat.setToIdentity();
                mat.translate(p.position.x, p.position.y, -p.position.z);
                mat.rotate(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
                ctrl.gripPose = mat;
                ctrl.gripValid = true;
            }
        }

        ctrl.connected = ctrl.aimValid;
    };

    pollController(m_leftController, m_leftHandPath);
    pollController(m_rightController, m_rightHandPath);

    return true;
}

bool XrManager::beginXRFrame()
{
    if (!m_sessionRunning) return false;

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};

    XrResult result = s_dispatch.WaitFrame(m_session, &waitInfo, &frameState);
    if (XR_FAILED(result)) return false;

    if (!frameState.shouldRender) return false;

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    result = s_dispatch.BeginFrame(m_session, &beginInfo);
    if (XR_FAILED(result)) return false;

    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = frameState.predictedDisplayTime;
    viewLocateInfo.space = m_viewSpace;

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t viewCount = 0;
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};

    result = s_dispatch.LocateViews(m_session, &viewLocateInfo, &viewState,
                           (uint32_t)std::size(views), &viewCount, views);
    if (XR_FAILED(result)) return false;

    for (uint32_t i = 0; i < viewCount && i < 2; i++) {
        m_eyes[i].view = views[i];
    }

    return true;
}

bool XrManager::endXRFrame()
{
    if (!m_sessionRunning || m_eyeCount == 0) return false;

    XrCompositionLayerProjectionView projectionViews[2];
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = m_viewSpace;
    layer.viewCount = (uint32_t)m_eyeCount;
    layer.views = projectionViews;

    for (int i = 0; i < m_eyeCount && i < 2; i++) {
        auto& eye = m_eyes[i];

        projectionViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projectionViews[i].pose = eye.view.pose;
        projectionViews[i].fov = eye.view.fov;
        projectionViews[i].subImage.swapchain = eye.swapchain;
        projectionViews[i].subImage.imageRect.offset = {0, 0};
        projectionViews[i].subImage.imageRect.extent = {
            (int32_t)eye.swapchainImageWidth,
            (int32_t)eye.swapchainImageHeight
        };
        projectionViews[i].subImage.imageArrayIndex = 0;
    }

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
    };

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = 0;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;
    endInfo.layers = layers;

    XrResult result = s_dispatch.EndFrame(m_session, &endInfo);
    return XR_SUCCEEDED(result);
}

bool XrManager::beginEyeRender(int eyeIndex)
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) return false;
    auto& eye = m_eyes[eyeIndex];

    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t index = 0;
    XrResult result = s_dispatch.AcquireSwapchainImage(eye.swapchain, &acquireInfo, &index);
    if (XR_FAILED(result)) return false;

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = s_dispatch.WaitSwapchainImage(eye.swapchain, &waitInfo);
    if (XR_FAILED(result)) return false;

    eye.currentImageIndex = (int32_t)index;
    return true;
}

void XrManager::endEyeRender(int eyeIndex)
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) return;
    auto& eye = m_eyes[eyeIndex];

    if (eye.currentImageIndex >= 0) {
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        s_dispatch.ReleaseSwapchainImage(eye.swapchain, &releaseInfo);
        eye.currentImageIndex = -1;
    }
}

XrMat4 XrManager::projectionMatrix(int eyeIndex, float nearZ, float farZ) const
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) {
        return XrMat4::perspective(90.0f, 1.0f, nearZ, farZ);
    }

    const auto& fov = m_eyes[eyeIndex].view.fov;
    float l = tanf(fov.angleLeft) * nearZ;
    float r = tanf(fov.angleRight) * nearZ;
    float b = tanf(fov.angleDown) * nearZ;
    float t = tanf(fov.angleUp) * nearZ;

    XrMat4 m;
    m.setToIdentity();
    m(0, 0) = 2.0f * nearZ / (r - l);
    m(1, 1) = 2.0f * nearZ / (t - b);
    m(0, 2) = (r + l) / (r - l);
    m(1, 2) = (t + b) / (t - b);
    m(2, 2) = -(farZ + nearZ) / (farZ - nearZ);
    m(2, 3) = -2.0f * farZ * nearZ / (farZ - nearZ);
    m(3, 2) = -1.0f;
    m(3, 3) = 0.0f;
    return m;
}


XrMat4 XrManager::viewMatrix(int eyeIndex) const
{
    XrMat4 m;
    m.setToIdentity();
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) {
        return m;
    }

    const auto& pose = m_eyes[eyeIndex].view.pose;
    m.rotate(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
    m.translate(-pose.position.x, -pose.position.y, pose.position.z);
    return m;
}


XrPath XrManager::stringToPath(const char* str)
{
    XrPath path = XR_NULL_PATH;
    if (m_instance) s_dispatch.StringToPath(m_instance, str, &path);
    return path;
}

XrAction XrManager::createAction(XrActionSet actionSet, const char* name,
                                  const char* localizedName, XrActionType type,
                                  const std::vector<XrPath>& subactionPaths)
{
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(info.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.actionType = type;
    info.countSubactionPaths = (uint32_t)subactionPaths.size();
    info.subactionPaths = subactionPaths.data();

    XrAction action = XR_NULL_HANDLE;
    s_dispatch.CreateAction(actionSet, &info, &action);
    return action;
}

void XrManager::suggestInteractionProfileBindings(const char* profile,
                                                    const std::vector<XrActionSuggestedBinding>& bindings)
{
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = stringToPath(profile);
    suggested.countSuggestedBindings = (uint32_t)bindings.size();
    suggested.suggestedBindings = bindings.data();
    s_dispatch.SuggestInteractionProfileBindings(m_instance, &suggested);
}




} // namespace device
} // namespace ks

#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
