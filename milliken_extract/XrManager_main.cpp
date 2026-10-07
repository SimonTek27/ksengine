#include "XrManager.h"
#include <cstdio>
#include <string>
#include <mutex>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include <vulkan/vulkan.h>

#include "XrDispatch.h"

namespace ks {
namespace device {

XrDispatch s_dispatch;

XrManager* XrManager::s_instance = nullptr;

XrManager* XrManager::instance()
{
    if (!s_instance) s_instance = new XrManager();
    return s_instance;
}

XrManager::XrManager()
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

bool XrManager::initialize(const std::string& applicationName)
{
    if (m_initialized) return true;

    std::lock_guard<std::mutex> lock(m_mutex);

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
        emitError("Failed to enumerate view configurations");
        shutdown(); return false;
    }

    // Get properties for stereo configuration
    result = s_dispatch.GetViewConfigurationProperties(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &m_viewConfigProps);
    if (XR_FAILED(result)) {
        emitError("Stereo view configuration not supported");
        shutdown(); return false;
    }

    // Enumerate views
    uint32_t viewCount = 0;
    result = s_dispatch.EnumerateViewConfigurationViews(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    if (XR_FAILED(result) || viewCount == 0) {
        emitError("No views available for stereo configuration");
        shutdown(); return false;
    }

    std::vector<XrViewConfigurationView> views(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    result = s_dispatch.EnumerateViewConfigurationViews(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data());
    if (XR_FAILED(result)) {
        emitError("Failed to enumerate view configuration views");
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
    emitInitialized(true);
    return true;
}

void XrManager::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sessionRunning) {
        XrResult result = s_dispatch.EndSession(m_session);
        if (XR_FAILED(result)) std::fprintf(stderr, "Warning: Failed to end session: %d
", (int)(result));
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

bool XrManager::createInstance(const std::string& applicationName)
{
    const std::string& appNameBytes = applicationName;

    // Load OpenXR runtime dynamically
    using PFN_xrGetInstanceProcAddr_t = XrResult (XRAPI_PTR *)(XrInstance, const char*, PFN_xrVoidFunction*);
    using PFN_xrEnumerateApiLayerProperties_t = XrResult (XRAPI_PTR *)(uint32_t, uint32_t*, XrApiLayerProperties*);
    using PFN_xrEnumerateInstanceExtensionProperties_t = XrResult (XRAPI_PTR *)(const char*, uint32_t, uint32_t*, XrExtensionProperties*);
    using PFN_xrCreateInstance_t = XrResult (XRAPI_PTR *)(const XrInstanceCreateInfo*, XrInstance*);

    PFN_xrGetInstanceProcAddr_t pfnGetInstanceProcAddr = nullptr;
    PFN_xrEnumerateApiLayerProperties_t pfnEnumerateApiLayerProperties = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties_t pfnEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance_t pfnCreateInstance = nullptr;

#if defined(_WIN32)
    HMODULE openxrLib = LoadLibraryA("openxr_loader.dll");
    if (openxrLib) {
        pfnGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr_t)GetProcAddress(openxrLib, "xrGetInstanceProcAddr");
        pfnEnumerateApiLayerProperties = (PFN_xrEnumerateApiLayerProperties_t)GetProcAddress(openxrLib, "xrEnumerateApiLayerProperties");
        pfnEnumerateInstanceExtensionProperties = (PFN_xrEnumerateInstanceExtensionProperties_t)GetProcAddress(openxrLib, "xrEnumerateInstanceExtensionProperties");
        pfnCreateInstance = (PFN_xrCreateInstance_t)GetProcAddress(openxrLib, "xrCreateInstance");
    }
#else
    void* openxrLib = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
    if (!openxrLib) openxrLib = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
    if (openxrLib) {
        pfnGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr_t)dlsym(openxrLib, "xrGetInstanceProcAddr");
        pfnEnumerateApiLayerProperties = (PFN_xrEnumerateApiLayerProperties_t)dlsym(openxrLib, "xrEnumerateApiLayerProperties");
        pfnEnumerateInstanceExtensionProperties = (PFN_xrEnumerateInstanceExtensionProperties_t)dlsym(openxrLib, "xrEnumerateInstanceExtensionProperties");
        pfnCreateInstance = (PFN_xrCreateInstance_t)dlsym(openxrLib, "xrCreateInstance");
    }
#endif

    if (!pfnGetInstanceProcAddr) {
        emitError("OpenXR runtime not found. Please ensure SteamVR or Windows Mixed Reality is installed.");
        return false;
    }

    // Store the core function pointers into dispatch
    s_dispatch.GetInstanceProcAddr = pfnGetInstanceProcAddr;
    s_dispatch.EnumerateApiLayerProperties = pfnEnumerateApiLayerProperties;
    s_dispatch.EnumerateInstanceExtensionProperties = pfnEnumerateInstanceExtensionProperties;
    s_dispatch.CreateInstance = pfnCreateInstance;

    if (!s_dispatch.CreateInstance) {
        emitError("Failed to load core OpenXR functions");
        return false;
    }

    // Load pre-instance functions
    s_dispatch.loadPreInstance(s_dispatch.EnumerateApiLayerProperties, "xrEnumerateApiLayerProperties");
    s_dispatch.loadPreInstance(s_dispatch.EnumerateInstanceExtensionProperties, "xrEnumerateInstanceExtensionProperties");

    // Check for required extensions
    uint32_t extensionCount = 0;
    s_dispatch.EnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
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
        emitError("OpenXR runtime does not support Vulkan rendering extension");
        return false;
    }

    const char* enabledExtensions[4];
    uint32_t enabledExtCount = 0;
    if (hasVulkanExt) enabledExtensions[enabledExtCount++] = XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
    if (hasVulkanExt2) enabledExtensions[enabledExtCount++] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;

    XrApplicationInfo appInfo{};
    strncpy(appInfo.applicationName, appNameBytes.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
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
        emitError((std::string("Failed to create OpenXR instance: ") + std::to_string(result)));
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
        emitError("No VR headset detected. Please connect your headset.");
        return false;
    }

    XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
    result = s_dispatch.GetSystemProperties(m_instance, m_systemId, &systemProps);
    if (XR_SUCCEEDED(result)) {
        std::fprintf(stderr, "VR System: %d
", (int)(systemProps.systemName
                << "Vendor:" << systemProps.vendorId));
    }

    return true;
}

bool XrManager::createSession()
{
    if (!m_vkDevice || !m_vkPhysicalDevice || !m_vkInstance) {
        emitError("Vulkan device not set before creating session");
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
        emitError((std::string("Failed to create OpenXR session: ") + std::to_string(result)));
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


} // namespace device
} // namespace ks

#endif
