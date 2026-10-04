#include "XrManager.h"
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include <QDebug>
#include <QScopeGuard>
#include <QLibrary>
#include <vulkan/vulkan.h>

namespace ks {
namespace vr {

// OpenXR dispatch table - all functions loaded dynamically
struct XrDispatch {
    PFN_xrGetInstanceProcAddr GetInstanceProcAddr = nullptr;

    PFN_xrEnumerateApiLayerProperties EnumerateApiLayerProperties = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance CreateInstance = nullptr;
    PFN_xrDestroyInstance DestroyInstance = nullptr;
    PFN_xrGetInstanceProperties GetInstanceProperties = nullptr;
    PFN_xrPollEvent PollEvent = nullptr;
    PFN_xrResultToString ResultToString = nullptr;
    PFN_xrGetSystem GetSystem = nullptr;
    PFN_xrGetSystemProperties GetSystemProperties = nullptr;
    PFN_xrEnumerateEnvironmentBlendModes EnumerateEnvironmentBlendModes = nullptr;
    PFN_xrCreateSession CreateSession = nullptr;
    PFN_xrDestroySession DestroySession = nullptr;
    PFN_xrEnumerateReferenceSpaces EnumerateReferenceSpaces = nullptr;
    PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
    PFN_xrGetReferenceSpaceBoundsRect GetReferenceSpaceBoundsRect = nullptr;
    PFN_xrLocateSpace LocateSpace = nullptr;
    PFN_xrDestroySpace DestroySpace = nullptr;
    PFN_xrEnumerateViewConfigurations EnumerateViewConfigurations = nullptr;
    PFN_xrGetViewConfigurationProperties GetViewConfigurationProperties = nullptr;
    PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews = nullptr;
    PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain CreateSwapchain = nullptr;
    PFN_xrDestroySwapchain DestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage AcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage WaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
    PFN_xrBeginSession BeginSession = nullptr;
    PFN_xrEndSession EndSession = nullptr;
    PFN_xrRequestExitSession RequestExitSession = nullptr;
    PFN_xrWaitFrame WaitFrame = nullptr;
    PFN_xrBeginFrame BeginFrame = nullptr;
    PFN_xrEndFrame EndFrame = nullptr;
    PFN_xrLocateViews LocateViews = nullptr;
    PFN_xrStringToPath StringToPath = nullptr;
    PFN_xrPathToString PathToString = nullptr;
    PFN_xrCreateActionSet CreateActionSet = nullptr;
    PFN_xrDestroyActionSet DestroyActionSet = nullptr;
    PFN_xrCreateAction CreateAction = nullptr;
    PFN_xrDestroyAction DestroyAction = nullptr;
    PFN_xrSuggestInteractionProfileBindings SuggestInteractionProfileBindings = nullptr;
    PFN_xrAttachSessionActionSets AttachSessionActionSets = nullptr;
    PFN_xrGetCurrentInteractionProfile GetCurrentInteractionProfile = nullptr;
    PFN_xrGetActionStateBoolean GetActionStateBoolean = nullptr;
    PFN_xrGetActionStateFloat GetActionStateFloat = nullptr;
    PFN_xrGetActionStateVector2f GetActionStateVector2f = nullptr;
    PFN_xrGetActionStatePose GetActionStatePose = nullptr;
    PFN_xrSyncActions SyncActions = nullptr;
    PFN_xrEnumerateBoundSourcesForAction EnumerateBoundSourcesForAction = nullptr;
    PFN_xrGetInputSourceLocalizedName GetInputSourceLocalizedName = nullptr;
    PFN_xrApplyHapticFeedback ApplyHapticFeedback = nullptr;
    PFN_xrStopHapticFeedback StopHapticFeedback = nullptr;
    PFN_xrGetVulkanInstanceExtensionsKHR GetVulkanInstanceExtensionsKHR = nullptr;
    PFN_xrGetVulkanDeviceExtensionsKHR GetVulkanDeviceExtensionsKHR = nullptr;
    PFN_xrGetVulkanGraphicsDeviceKHR GetVulkanGraphicsDeviceKHR = nullptr;
    PFN_xrGetVulkanGraphicsRequirementsKHR GetVulkanGraphicsRequirementsKHR = nullptr;
    PFN_xrCreateActionSpace CreateActionSpace = nullptr;

    template<typename T>
    void loadPreInstance(T& fnPtr, const char* name) {
        if (!GetInstanceProcAddr) return;
        PFN_xrVoidFunction pfn;
        if (XR_SUCCEEDED(GetInstanceProcAddr(XR_NULL_HANDLE, name, &pfn)) && pfn)
            fnPtr = reinterpret_cast<T>(pfn);
    }

    template<typename T>
    void load(T& fnPtr, const char* name, XrInstance instance) {
        if (!GetInstanceProcAddr) return;
        PFN_xrVoidFunction pfn;
        if (XR_SUCCEEDED(GetInstanceProcAddr(instance, name, &pfn)) && pfn)
            fnPtr = reinterpret_cast<T>(pfn);
    }
};

static XrDispatch s_dispatch;

XrManager* XrManager::s_instance = nullptr;

XrManager* XrManager::instance()
{
    if (!s_instance) s_instance = new XrManager();
    return s_instance;
}

XrManager::XrManager(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

XrManager::~XrManager()
{
    shutdown();
    if (s_instance == this) s_instance = nullptr;
}

void XrManager::setVulkanDevice(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkInstance vkInstance, uint32_t queueFamilyIndex, uint32_t queueIndex)
{
    m_vkDevice = device;
    m_vkPhysicalDevice = physicalDevice;
    m_vkInstance = vkInstance;
    m_queueFamilyIndex = queueFamilyIndex;
    m_queueIndex = queueIndex;
}

void XrManager::setVulkanCommandResources(VkCommandPool pool, VkQueue queue)
{
    m_commandPool = pool;
    m_graphicsQueue = queue;
}

bool XrManager::initialize(const QString& applicationName)
{
    if (m_initialized) return true;

    QMutexLocker lock(&m_mutex);

    if (!createInstance(applicationName)) return false;
    if (!getSystem()) { shutdown(); return false; }
    if (!createSession()) { shutdown(); return false; }
    if (!createReferenceSpaces()) { shutdown(); return false; }

    // Enumerate view configurations
    uint32_t viewConfigCount = 0;
    XrViewConfigurationType viewConfigs[8];
    XrResult result = s_dispatch.EnumerateViewConfigurations(m_instance, m_systemId,
        (uint32_t)std::size(viewConfigs), &viewConfigCount, viewConfigs);
    if (XR_FAILED(result)) {
        emit error("Failed to enumerate view configurations");
        shutdown(); return false;
    }

    // Get properties for stereo configuration
    result = s_dispatch.GetViewConfigurationProperties(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &m_viewConfigProps);
    if (XR_FAILED(result)) {
        emit error("Stereo view configuration not supported");
        shutdown(); return false;
    }

    // Enumerate views
    uint32_t viewCount = 0;
    result = s_dispatch.EnumerateViewConfigurationViews(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    if (XR_FAILED(result) || viewCount == 0) {
        emit error("No views available for stereo configuration");
        shutdown(); return false;
    }

    QVector<XrViewConfigurationView> views(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    result = s_dispatch.EnumerateViewConfigurationViews(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data());
    if (XR_FAILED(result)) {
        emit error("Failed to enumerate view configuration views");
        shutdown(); return false;
    }

    m_eyeCount = viewCount;

    for (int i = 0; i < m_eyeCount && i < 2; i++) {
        m_eyes[i].swapchainImageWidth = views[i].recommendedImageRectWidth;
        m_eyes[i].swapchainImageHeight = views[i].recommendedImageRectHeight;
    }

    if (!createSwapchains()) { shutdown(); return false; }
    if (!createActions()) { shutdown(); return false; }
    if (!suggestBindings()) { /* non-fatal */ }
    if (!attachActions()) { /* non-fatal */ }

    m_initialized = true;
    emit initialized(true);
    return true;
}

void XrManager::shutdown()
{
    QMutexLocker lock(&m_mutex);

    if (m_sessionRunning) {
        XrResult result = s_dispatch.EndSession(m_session);
        if (XR_FAILED(result)) qWarning() << "Failed to end session:" << result;
        m_sessionRunning = false;
    }

    destroySwapchains();
    destroyActions();

    if (m_stageSpace) { s_dispatch.DestroySpace(m_stageSpace); m_stageSpace = XR_NULL_HANDLE; }
    if (m_localSpace) { s_dispatch.DestroySpace(m_localSpace); m_localSpace = XR_NULL_HANDLE; }
    if (m_viewSpace) { s_dispatch.DestroySpace(m_viewSpace); m_viewSpace = XR_NULL_HANDLE; }

    if (m_session) { s_dispatch.DestroySession(m_session); m_session = XR_NULL_HANDLE; }
    if (m_instance) { s_dispatch.DestroyInstance(m_instance); m_instance = XR_NULL_HANDLE; }

    m_initialized = false;
    m_sessionRunning = false;
    m_sessionFocused = false;
    m_eyeCount = 0;
    m_systemId = XR_NULL_SYSTEM_ID;
}

bool XrManager::createInstance(const QString& applicationName)
{
    QByteArray appNameBytes = applicationName.toUtf8();

    // Load OpenXR runtime dynamically
    using PFN_xrGetInstanceProcAddr_t = XrResult (XRAPI_PTR *)(XrInstance, const char*, PFN_xrVoidFunction*);
    using PFN_xrEnumerateApiLayerProperties_t = XrResult (XRAPI_PTR *)(uint32_t, uint32_t*, XrApiLayerProperties*);
    using PFN_xrEnumerateInstanceExtensionProperties_t = XrResult (XRAPI_PTR *)(const char*, uint32_t, uint32_t*, XrExtensionProperties*);
    using PFN_xrCreateInstance_t = XrResult (XRAPI_PTR *)(const XrInstanceCreateInfo*, XrInstance*);

    QLibrary openxrLib("openxr_loader");
    PFN_xrGetInstanceProcAddr_t pfnGetInstanceProcAddr = nullptr;
    PFN_xrEnumerateApiLayerProperties_t pfnEnumerateApiLayerProperties = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties_t pfnEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance_t pfnCreateInstance = nullptr;

    if (openxrLib.load()) {
        pfnGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr_t)openxrLib.resolve("xrGetInstanceProcAddr");
        pfnEnumerateApiLayerProperties = (PFN_xrEnumerateApiLayerProperties_t)openxrLib.resolve("xrEnumerateApiLayerProperties");
        pfnEnumerateInstanceExtensionProperties = (PFN_xrEnumerateInstanceExtensionProperties_t)openxrLib.resolve("xrEnumerateInstanceExtensionProperties");
        pfnCreateInstance = (PFN_xrCreateInstance_t)openxrLib.resolve("xrCreateInstance");
    }

    if (!pfnGetInstanceProcAddr) {
        emit error("OpenXR runtime not found. Please ensure SteamVR or Windows Mixed Reality is installed.");
        return false;
    }

    // Store the core function pointers into dispatch
    s_dispatch.GetInstanceProcAddr = pfnGetInstanceProcAddr;
    s_dispatch.EnumerateApiLayerProperties = pfnEnumerateApiLayerProperties;
    s_dispatch.EnumerateInstanceExtensionProperties = pfnEnumerateInstanceExtensionProperties;
    s_dispatch.CreateInstance = pfnCreateInstance;

    if (!s_dispatch.CreateInstance) {
        emit error("Failed to load core OpenXR functions");
        return false;
    }

    // Load pre-instance functions
    s_dispatch.loadPreInstance(s_dispatch.EnumerateApiLayerProperties, "xrEnumerateApiLayerProperties");
    s_dispatch.loadPreInstance(s_dispatch.EnumerateInstanceExtensionProperties, "xrEnumerateInstanceExtensionProperties");

    // Check for required extensions
    uint32_t extensionCount = 0;
    s_dispatch.EnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    QVector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    if (extensionCount > 0) {
        s_dispatch.EnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
    }

    bool hasVulkanExt = false;
    bool hasVulkanExt2 = false;
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) == 0) hasVulkanExt = true;
        if (strcmp(ext.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0) hasVulkanExt2 = true;
    }

    if (!hasVulkanExt && !hasVulkanExt2) {
        emit error("OpenXR runtime does not support Vulkan rendering extension");
        return false;
    }

    const char* enabledExtensions[4];
    uint32_t enabledExtCount = 0;
    if (hasVulkanExt) enabledExtensions[enabledExtCount++] = XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
    if (hasVulkanExt2) enabledExtensions[enabledExtCount++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;

    XrApplicationInfo appInfo{};
    strncpy(appInfo.applicationName, appNameBytes.constData(), XR_MAX_APPLICATION_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    strncpy(appInfo.engineName, "ksEditor", XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo = appInfo;
    createInfo.enabledExtensionCount = enabledExtCount;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = s_dispatch.CreateInstance(&createInfo, &instance);
    if (XR_FAILED(result)) {
        emit error("Failed to create OpenXR instance: " + QString::number(result));
        return false;
    }
    m_instance = instance;

    // Reload GetInstanceProcAddr with the instance for better loading
    PFN_xrGetInstanceProcAddr instanceProcAddr = nullptr;
    {
        PFN_xrVoidFunction pfn;
        if (XR_SUCCEEDED(s_dispatch.GetInstanceProcAddr(m_instance, "xrGetInstanceProcAddr", &pfn)) && pfn)
            instanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(pfn);
    }
    if (instanceProcAddr) {
        s_dispatch.GetInstanceProcAddr = instanceProcAddr;
    }

#define LOAD_FN(name) s_dispatch.load(s_dispatch.name, #name, m_instance)
    LOAD_FN(GetInstanceProperties);
    LOAD_FN(PollEvent);
    LOAD_FN(ResultToString);
    LOAD_FN(GetSystem);
    LOAD_FN(GetSystemProperties);
    LOAD_FN(EnumerateEnvironmentBlendModes);
    LOAD_FN(CreateSession);
    LOAD_FN(DestroySession);
    LOAD_FN(EnumerateReferenceSpaces);
    LOAD_FN(CreateReferenceSpace);
    LOAD_FN(GetReferenceSpaceBoundsRect);
    LOAD_FN(LocateSpace);
    LOAD_FN(DestroySpace);
    LOAD_FN(EnumerateViewConfigurations);
    LOAD_FN(GetViewConfigurationProperties);
    LOAD_FN(EnumerateViewConfigurationViews);
    LOAD_FN(EnumerateSwapchainFormats);
    LOAD_FN(CreateSwapchain);
    LOAD_FN(DestroySwapchain);
    LOAD_FN(EnumerateSwapchainImages);
    LOAD_FN(AcquireSwapchainImage);
    LOAD_FN(WaitSwapchainImage);
    LOAD_FN(ReleaseSwapchainImage);
    LOAD_FN(BeginSession);
    LOAD_FN(EndSession);
    LOAD_FN(RequestExitSession);
    LOAD_FN(WaitFrame);
    LOAD_FN(BeginFrame);
    LOAD_FN(EndFrame);
    LOAD_FN(LocateViews);
    LOAD_FN(StringToPath);
    LOAD_FN(PathToString);
    LOAD_FN(CreateActionSet);
    LOAD_FN(DestroyActionSet);
    LOAD_FN(CreateAction);
    LOAD_FN(DestroyAction);
    LOAD_FN(SuggestInteractionProfileBindings);
    LOAD_FN(AttachSessionActionSets);
    LOAD_FN(GetCurrentInteractionProfile);
    LOAD_FN(GetActionStateBoolean);
    LOAD_FN(GetActionStateFloat);
    LOAD_FN(GetActionStateVector2f);
    LOAD_FN(GetActionStatePose);
    LOAD_FN(SyncActions);
    LOAD_FN(EnumerateBoundSourcesForAction);
    LOAD_FN(GetInputSourceLocalizedName);
    LOAD_FN(ApplyHapticFeedback);
    LOAD_FN(StopHapticFeedback);
    LOAD_FN(GetVulkanInstanceExtensionsKHR);
    LOAD_FN(GetVulkanDeviceExtensionsKHR);
    LOAD_FN(GetVulkanGraphicsDeviceKHR);
    LOAD_FN(GetVulkanGraphicsRequirementsKHR);
    LOAD_FN(CreateActionSpace);
#undef LOAD_FN

    return true;
}

bool XrManager::getSystem()
{
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = s_dispatch.GetSystem(m_instance, &systemInfo, &m_systemId);
    if (XR_FAILED(result)) {
        emit error("No VR headset detected. Please connect your headset.");
        return false;
    }

    XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
    result = s_dispatch.GetSystemProperties(m_instance, m_systemId, &systemProps);
    if (XR_SUCCEEDED(result)) {
        qInfo() << "VR System:" << systemProps.systemName
                << "Vendor:" << systemProps.vendorId;
    }

    return true;
}

bool XrManager::createSession()
{
    if (!m_vkDevice || !m_vkPhysicalDevice || !m_vkInstance) {
        emit error("Vulkan device not set before creating session");
        return false;
    }

    XrGraphicsBindingVulkanKHR vulkanBinding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    vulkanBinding.instance = m_vkInstance;
    vulkanBinding.physicalDevice = m_vkPhysicalDevice;
    vulkanBinding.device = m_vkDevice;
    vulkanBinding.queueFamilyIndex = m_queueFamilyIndex;
    vulkanBinding.queueIndex = m_queueIndex;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &vulkanBinding;
    sessionInfo.systemId = m_systemId;

    XrResult result = s_dispatch.CreateSession(m_instance, &sessionInfo, &m_session);
    if (XR_FAILED(result)) {
        emit error("Failed to create OpenXR session: " + QString::number(result));
        return false;
    }

    return true;
}

bool XrManager::createReferenceSpaces()
{
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};

    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    spaceInfo.poseInReferenceSpace = {{0,0,0,1}, {0,0,0}};
    XrResult result = s_dispatch.CreateReferenceSpace(m_session, &spaceInfo, &m_stageSpace);
    if (XR_FAILED(result)) {
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        result = s_dispatch.CreateReferenceSpace(m_session, &spaceInfo, &m_localSpace);
        if (XR_FAILED(result)) return false;
    }

    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    result = s_dispatch.CreateReferenceSpace(m_session, &spaceInfo, &m_viewSpace);
    if (XR_FAILED(result)) return false;

    return true;
}

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
    if (m_colorFormat == 0 && !m_swapchainFormats.isEmpty()) {
        m_colorFormat = m_swapchainFormats.first();
    }

    QVector<int64_t> preferredDepthFormats = {
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
            emit error("Failed to create swapchain for eye " + QString::number(i));
            return false;
        }

        uint32_t imageCount = 0;
        s_dispatch.EnumerateSwapchainImages(eye.swapchain, 0, &imageCount, nullptr);
        QVector<XrSwapchainImageVulkanKHR> colorImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        s_dispatch.EnumerateSwapchainImages(eye.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(colorImages.data()));

        eye.colorImages.clear();
        for (const auto& img : colorImages) {
            eye.colorImages.append(img.image);
        }

        if (m_depthFormat != 0) {
            swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            swapInfo.format = m_depthFormat;

            result = s_dispatch.CreateSwapchain(m_session, &swapInfo, &eye.depthSwapchain);
            if (XR_SUCCEEDED(result)) {
                uint32_t depthCount = 0;
                s_dispatch.EnumerateSwapchainImages(eye.depthSwapchain, 0, &depthCount, nullptr);
                QVector<XrSwapchainImageVulkanKHR> depthImages(depthCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
                s_dispatch.EnumerateSwapchainImages(eye.depthSwapchain, depthCount, &depthCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(depthImages.data()));

                eye.depthImages.clear();
                for (const auto& img : depthImages) {
                    eye.depthImages.append(img.image);
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

    QVector<XrPath> handPaths = { m_leftHandPath, m_rightHandPath };

    auto createCtrlActions = [&](XrControllerState& ctrl, const char* handStr, XrPath handPath) {
        ctrl.handPath = handPath;

        QString prefix = QString("hand_") + handStr + "_";

        ctrl.aimAction = createAction(m_gameActionSet,
            (prefix + "aim_pose").toUtf8().constData(),
            (QString(handStr).toUpper() + " Hand Aim Pose").toUtf8().constData(),
            XR_ACTION_TYPE_POSE_INPUT, handPaths);

        ctrl.gripAction = createAction(m_gameActionSet,
            (prefix + "grip_pose").toUtf8().constData(),
            (QString(handStr).toUpper() + " Hand Grip Pose").toUtf8().constData(),
            XR_ACTION_TYPE_POSE_INPUT, handPaths);

        ctrl.squeezeAction = createAction(m_gameActionSet,
            (prefix + "squeeze").toUtf8().constData(),
            (QString(handStr).toUpper() + " Hand Squeeze").toUtf8().constData(),
            XR_ACTION_TYPE_FLOAT_INPUT, handPaths);

        ctrl.triggerAction = createAction(m_gameActionSet,
            (prefix + "trigger").toUtf8().constData(),
            (QString(handStr).toUpper() + " Hand Trigger").toUtf8().constData(),
            XR_ACTION_TYPE_FLOAT_INPUT, handPaths);

        ctrl.thumbstickAction = createAction(m_gameActionSet,
            (prefix + "thumbstick").toUtf8().constData(),
            (QString(handStr).toUpper() + " Thumbstick").toUtf8().constData(),
            XR_ACTION_TYPE_VECTOR2F_INPUT, handPaths);

        ctrl.trackpadAction = createAction(m_gameActionSet,
            (prefix + "trackpad").toUtf8().constData(),
            (QString(handStr).toUpper() + " Trackpad").toUtf8().constData(),
            XR_ACTION_TYPE_VECTOR2F_INPUT, handPaths);

        ctrl.menuAction = createAction(m_gameActionSet,
            (prefix + "menu").toUtf8().constData(),
            (QString(handStr).toUpper() + " Menu Button").toUtf8().constData(),
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

    QVector<XrActionSuggestedBinding> khrBindings;
    auto addBinding = [&](XrAction action, const char* path) {
        khrBindings.append(getBinding(action, path));
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
            emit instanceLost();
            return false;
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            qInfo() << "Interaction profile changed for session";
            break;
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            qInfo() << "Reference space change pending";
            break;
        case XR_TYPE_EVENT_DATA_EVENTS_LOST:
            qInfo() << "Events lost";
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

    qInfo() << "XR Session state changed:" << oldState << "->" << m_sessionState;

    switch (m_sessionState) {
    case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XrResult result = s_dispatch.BeginSession(m_session, &beginInfo);
        if (XR_SUCCEEDED(result)) {
            m_sessionRunning = true;
            emit sessionRunningChanged(true);
        }
        break;
    }
    case XR_SESSION_STATE_SYNCHRONIZED:
    case XR_SESSION_STATE_VISIBLE:
        break;
    case XR_SESSION_STATE_FOCUSED:
        m_sessionFocused = true;
        emit sessionFocusChanged(true);
        break;
    case XR_SESSION_STATE_STOPPING:
        m_sessionRunning = false;
        m_sessionFocused = false;
        emit sessionFocusChanged(false);
        emit sessionRunningChanged(false);
        s_dispatch.EndSession(m_session);
        break;
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
        m_sessionRunning = false;
        m_sessionFocused = false;
        emit sessionFocusChanged(false);
        emit sessionRunningChanged(false);
        break;
    default:
        break;
    }

    emit sessionStateChanged(oldState, m_sessionState);
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
            ctrl.thumbstickValue = QVector2D(thumbstickState.currentState.x, thumbstickState.currentState.y);
        }

        getInfo.action = ctrl.trackpadAction;
        XrActionStateVector2f trackpadState{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(s_dispatch.GetActionStateVector2f(m_session, &getInfo, &trackpadState)) && trackpadState.isActive) {
            ctrl.trackpadValue = QVector2D(trackpadState.currentState.x, trackpadState.currentState.y);
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
                QMatrix4x4 mat;
                mat.setToIdentity();
                QQuaternion q(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
                QVector3D pos(p.position.x, p.position.y, -p.position.z);
                mat.translate(pos);
                mat.rotate(q);
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
                QMatrix4x4 mat;
                mat.setToIdentity();
                QQuaternion q(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
                QVector3D pos(p.position.x, p.position.y, -p.position.z);
                mat.translate(pos);
                mat.rotate(q);
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

QMatrix4x4 XrManager::projectionMatrix(int eyeIndex, float nearZ, float farZ) const
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) {
        QMatrix4x4 m;
        m.perspective(90.0f, 1.0f, nearZ, farZ);
        return m;
    }

    const auto& fov = m_eyes[eyeIndex].view.fov;
    float l = tanf(fov.angleLeft) * nearZ;
    float r = tanf(fov.angleRight) * nearZ;
    float b = tanf(fov.angleDown) * nearZ;
    float t = tanf(fov.angleUp) * nearZ;

    QMatrix4x4 m;
    m.setToIdentity();
    m(0,0) = 2.0f * nearZ / (r - l);
    m(1,1) = 2.0f * nearZ / (t - b);
    m(0,2) = (r + l) / (r - l);
    m(1,2) = (t + b) / (t - b);
    m(2,2) = -(farZ + nearZ) / (farZ - nearZ);
    m(2,3) = -2.0f * farZ * nearZ / (farZ - nearZ);
    m(3,2) = -1.0f;
    m(3,3) = 0.0f;
    return m;
}

QMatrix4x4 XrManager::viewMatrix(int eyeIndex) const
{
    if (eyeIndex < 0 || eyeIndex >= m_eyeCount) {
        QMatrix4x4 m;
        m.lookAt(QVector3D(0,0,0), QVector3D(0,0,-1), QVector3D(0,1,0));
        return m;
    }

    const auto& pose = m_eyes[eyeIndex].view.pose;
    QQuaternion q(pose.orientation.w, pose.orientation.x,
                  pose.orientation.y, pose.orientation.z);
    QVector3D pos(pose.position.x, pose.position.y, -pose.position.z);

    QMatrix4x4 m;
    m.setToIdentity();
    m.translate(-pos);
    m.rotate(q.conjugated());
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
                                  const QVector<XrPath>& subactionPaths)
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
                                                    const QVector<XrActionSuggestedBinding>& bindings)
{
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = stringToPath(profile);
    suggested.countSuggestedBindings = (uint32_t)bindings.size();
    suggested.suggestedBindings = bindings.data();
    s_dispatch.SuggestInteractionProfileBindings(m_instance, &suggested);
}

} // namespace vr
} // namespace ks

#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
