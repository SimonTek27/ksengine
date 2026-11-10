#include "XrManager.h"
#include <cstdio>
#include <string>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>
#include <sstream>
#include <utility>
#include <memory>
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
    if (!createInstance(applicationName)) return false;
    if (!getSystem()) { shutdown(); return false; }
    if (!createSession()) { shutdown(); return false; }
    if (!createReferenceSpaces()) { shutdown(); return false; }
    if (!createSwapchains()) { shutdown(); return false; }
    if (!createActions()) { shutdown(); return false; }
    if (!suggestBindings() || !attachActions()) { shutdown(); return false; }
    m_initialized = true;
    emitInitialized(true);
    return true;
}

bool XrManager::prepareVulkanBootstrap(const std::string& applicationName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!createInstance(applicationName)) return false;
    s_dispatch.load(s_dispatch.GetVulkanInstanceExtensionsKHR,
                    "xrGetVulkanInstanceExtensionsKHR", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanDeviceExtensionsKHR,
                    "xrGetVulkanDeviceExtensionsKHR", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanGraphicsDeviceKHR,
                    "xrGetVulkanGraphicsDeviceKHR", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanGraphicsRequirementsKHR,
                    "xrGetVulkanGraphicsRequirementsKHR", m_instance);
    if (!getSystem()) return false;
    if (!s_dispatch.GetVulkanInstanceExtensionsKHR) {
        emitError("OpenXR runtime does not expose Vulkan instance requirements");
        return false;
    }

    uint32_t extensionBytes = 0;
    XrResult result = s_dispatch.GetVulkanInstanceExtensionsKHR(
        m_instance, m_systemId, 0, &extensionBytes, nullptr);
    if (XR_FAILED(result) || extensionBytes == 0) {
        emitError("Failed to query OpenXR Vulkan instance extensions");
        return false;
    }

    std::vector<char> extensionNames(extensionBytes, '\0');
    result = s_dispatch.GetVulkanInstanceExtensionsKHR(
        m_instance, m_systemId, extensionBytes, &extensionBytes, extensionNames.data());
    if (XR_FAILED(result)) {
        emitError("Failed to read OpenXR Vulkan instance extensions");
        return false;
    }

    m_vulkanInstanceExtensions.clear();
    std::istringstream names(std::string(extensionNames.data(), extensionBytes));
    for (std::string name; names >> name; )
        m_vulkanInstanceExtensions.push_back(std::move(name));
    return !m_vulkanInstanceExtensions.empty();
}

std::vector<std::string> XrManager::requiredVulkanDeviceExtensions(
    VkPhysicalDevice physicalDevice) const
{
    if (!m_instance || !s_dispatch.GetVulkanDeviceExtensionsKHR || !physicalDevice)
        return {};

    uint32_t extensionBytes = 0;
    XrResult result = s_dispatch.GetVulkanDeviceExtensionsKHR(
        m_instance, m_systemId, 0, &extensionBytes, nullptr);
    if (XR_FAILED(result) || extensionBytes == 0) return {};

    std::vector<char> extensionNames(extensionBytes, '\0');
    result = s_dispatch.GetVulkanDeviceExtensionsKHR(
        m_instance, m_systemId, extensionBytes,
        &extensionBytes, extensionNames.data());
    if (XR_FAILED(result)) return {};

    std::vector<std::string> extensions;
    std::istringstream names(std::string(extensionNames.data(), extensionBytes));
    for (std::string name; names >> name; )
        extensions.push_back(std::move(name));
    return extensions;
}

VkPhysicalDevice XrManager::vulkanGraphicsDevice(VkInstance instance) const
{
    if (!m_instance || !s_dispatch.GetVulkanGraphicsDeviceKHR || !instance)
        return VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    const XrResult result = s_dispatch.GetVulkanGraphicsDeviceKHR(
        m_instance, m_systemId, instance, &physicalDevice);
    return XR_SUCCEEDED(result) ? physicalDevice : VK_NULL_HANDLE;
}

void XrManager::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sessionRunning && s_dispatch.EndSession) {
        s_dispatch.EndSession(m_session);
        m_sessionRunning = false;
    }
    destroySwapchains();
    destroyActions();
    if (m_stageSpace && s_dispatch.DestroySpace) { s_dispatch.DestroySpace(m_stageSpace); m_stageSpace = XR_NULL_HANDLE; }
    if (m_localSpace && s_dispatch.DestroySpace) { s_dispatch.DestroySpace(m_localSpace); m_localSpace = XR_NULL_HANDLE; }
    if (m_viewSpace && s_dispatch.DestroySpace) { s_dispatch.DestroySpace(m_viewSpace); m_viewSpace = XR_NULL_HANDLE; }
    if (m_session && s_dispatch.DestroySession) { s_dispatch.DestroySession(m_session); m_session = XR_NULL_HANDLE; }
    if (m_instance && s_dispatch.DestroyInstance) { s_dispatch.DestroyInstance(m_instance); m_instance = XR_NULL_HANDLE; }
    m_initialized = false;
    m_sessionRunning = false;
    m_sessionFocused = false;
    m_frameBegun = false;
    m_shouldRenderFrame = false;
    m_predictedDisplayTime = 0;
    m_headPoseRaw = XrPosef{{0, 0, 0, 1}, {0, 0, 0}};
    m_headPoseValid = false;
    m_eyeCount = 0;
    m_systemId = XR_NULL_SYSTEM_ID;
    m_vulkanInstanceExtensions.clear();
    m_vkDevice = VK_NULL_HANDLE;
    m_vkPhysicalDevice = VK_NULL_HANDLE;
    m_vkInstance = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
}

bool XrManager::createInstance(const std::string& applicationName)
{
    if (m_instance) return true;
    using PFN_xrGetInstanceProcAddr_t = XrResult (XRAPI_PTR *)(XrInstance, const char*, PFN_xrVoidFunction*);
    using PFN_xrCreateInstance_t = XrResult (XRAPI_PTR *)(const XrInstanceCreateInfo*, XrInstance*);
    PFN_xrGetInstanceProcAddr_t pfnGetInstanceProcAddr = nullptr;
    PFN_xrCreateInstance_t pfnCreateInstance = nullptr;
#if defined(_WIN32)
    static HMODULE openxrLib = LoadLibraryA("openxr_loader.dll");
    if (openxrLib) {
        pfnGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr_t>(
            GetProcAddress(openxrLib, "xrGetInstanceProcAddr"));
        pfnCreateInstance = reinterpret_cast<PFN_xrCreateInstance_t>(
            GetProcAddress(openxrLib, "xrCreateInstance"));
    }
#else
    static void* openxrLib = [] {
        void* library = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) library = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
        return library;
    }();
    if (openxrLib) {
        pfnGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr_t)dlsym(openxrLib, "xrGetInstanceProcAddr");
        pfnCreateInstance = (PFN_xrCreateInstance_t)dlsym(openxrLib, "xrCreateInstance");
    }
#endif
    if (!pfnGetInstanceProcAddr) {
        emitError("OpenXR runtime not found. Install SteamVR / OpenXR runtime.");
        return false;
    }
    s_dispatch.GetInstanceProcAddr = pfnGetInstanceProcAddr;
    s_dispatch.CreateInstance = pfnCreateInstance;
    if (!s_dispatch.CreateInstance) {
        emitError("Failed to load OpenXR CreateInstance");
        return false;
    }
    s_dispatch.loadPreInstance(s_dispatch.EnumerateInstanceExtensionProperties, "xrEnumerateInstanceExtensionProperties");
    if (!s_dispatch.EnumerateInstanceExtensionProperties) {
        emitError("Failed to load OpenXR extension enumeration");
        return false;
    }

    uint32_t extensionCount = 0;
    if (XR_FAILED(s_dispatch.EnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr)))
        return false;
    std::vector<XrExtensionProperties> availableExtensions(extensionCount);
    for (auto& extension : availableExtensions)
        extension.type = XR_TYPE_EXTENSION_PROPERTIES;
    if (XR_FAILED(s_dispatch.EnumerateInstanceExtensionProperties(
            nullptr, extensionCount, &extensionCount, availableExtensions.data())))
        return false;
    const bool vulkanEnableAvailable = std::any_of(
        availableExtensions.begin(), availableExtensions.end(), [](const XrExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) == 0;
        });
    if (!vulkanEnableAvailable) {
        emitError("OpenXR runtime does not support XR_KHR_vulkan_enable");
        return false;
    }

    XrApplicationInfo appInfo{};
    strncpy(appInfo.applicationName, applicationName.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    strncpy(appInfo.engineName, "ksEditor", XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    const char* enabledExtensions[] = { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo = appInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrResult result = s_dispatch.CreateInstance(&createInfo, &m_instance);
    if (XR_FAILED(result)) {
        emitError("Failed to create OpenXR instance");
        return false;
    }
#define LOAD_FN(name) s_dispatch.load(s_dispatch.name, "xr" #name, m_instance)
    // Load critical entry points (names without xr prefix handled in load calls)
#undef LOAD_FN
    s_dispatch.load(s_dispatch.DestroyInstance, "xrDestroyInstance", m_instance);
    s_dispatch.load(s_dispatch.GetSystem, "xrGetSystem", m_instance);
    s_dispatch.load(s_dispatch.GetSystemProperties, "xrGetSystemProperties", m_instance);
    s_dispatch.load(s_dispatch.CreateSession, "xrCreateSession", m_instance);
    s_dispatch.load(s_dispatch.DestroySession, "xrDestroySession", m_instance);
    s_dispatch.load(s_dispatch.CreateReferenceSpace, "xrCreateReferenceSpace", m_instance);
    s_dispatch.load(s_dispatch.DestroySpace, "xrDestroySpace", m_instance);
    s_dispatch.load(s_dispatch.EnumerateViewConfigurations, "xrEnumerateViewConfigurations", m_instance);
    s_dispatch.load(s_dispatch.GetViewConfigurationProperties, "xrGetViewConfigurationProperties", m_instance);
    s_dispatch.load(s_dispatch.EnumerateViewConfigurationViews, "xrEnumerateViewConfigurationViews", m_instance);
    s_dispatch.load(s_dispatch.EnumerateSwapchainFormats, "xrEnumerateSwapchainFormats", m_instance);
    s_dispatch.load(s_dispatch.CreateSwapchain, "xrCreateSwapchain", m_instance);
    s_dispatch.load(s_dispatch.DestroySwapchain, "xrDestroySwapchain", m_instance);
    s_dispatch.load(s_dispatch.EnumerateSwapchainImages, "xrEnumerateSwapchainImages", m_instance);
    s_dispatch.load(s_dispatch.AcquireSwapchainImage, "xrAcquireSwapchainImage", m_instance);
    s_dispatch.load(s_dispatch.WaitSwapchainImage, "xrWaitSwapchainImage", m_instance);
    s_dispatch.load(s_dispatch.ReleaseSwapchainImage, "xrReleaseSwapchainImage", m_instance);
    s_dispatch.load(s_dispatch.BeginSession, "xrBeginSession", m_instance);
    s_dispatch.load(s_dispatch.EndSession, "xrEndSession", m_instance);
    s_dispatch.load(s_dispatch.WaitFrame, "xrWaitFrame", m_instance);
    s_dispatch.load(s_dispatch.BeginFrame, "xrBeginFrame", m_instance);
    s_dispatch.load(s_dispatch.EndFrame, "xrEndFrame", m_instance);
    s_dispatch.load(s_dispatch.LocateViews, "xrLocateViews", m_instance);
    s_dispatch.load(s_dispatch.PollEvent, "xrPollEvent", m_instance);
    s_dispatch.load(s_dispatch.StringToPath, "xrStringToPath", m_instance);
    s_dispatch.load(s_dispatch.CreateActionSet, "xrCreateActionSet", m_instance);
    s_dispatch.load(s_dispatch.DestroyActionSet, "xrDestroyActionSet", m_instance);
    s_dispatch.load(s_dispatch.CreateAction, "xrCreateAction", m_instance);
    s_dispatch.load(s_dispatch.SuggestInteractionProfileBindings, "xrSuggestInteractionProfileBindings", m_instance);
    s_dispatch.load(s_dispatch.AttachSessionActionSets, "xrAttachSessionActionSets", m_instance);
    s_dispatch.load(s_dispatch.SyncActions, "xrSyncActions", m_instance);
    s_dispatch.load(s_dispatch.GetActionStateBoolean, "xrGetActionStateBoolean", m_instance);
    s_dispatch.load(s_dispatch.GetActionStateFloat, "xrGetActionStateFloat", m_instance);
    s_dispatch.load(s_dispatch.GetActionStateVector2f, "xrGetActionStateVector2f", m_instance);
    s_dispatch.load(s_dispatch.GetActionStatePose, "xrGetActionStatePose", m_instance);
    s_dispatch.load(s_dispatch.CreateActionSpace, "xrCreateActionSpace", m_instance);
    s_dispatch.load(s_dispatch.LocateSpace, "xrLocateSpace", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanInstanceExtensionsKHR,
                    "xrGetVulkanInstanceExtensionsKHR", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanDeviceExtensionsKHR,
                    "xrGetVulkanDeviceExtensionsKHR", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanGraphicsDeviceKHR,
                    "xrGetVulkanGraphicsDeviceKHR", m_instance);
    s_dispatch.load(s_dispatch.GetVulkanGraphicsRequirementsKHR,
                    "xrGetVulkanGraphicsRequirementsKHR", m_instance);
    return true;
}

bool XrManager::getSystem()
{
    if (m_systemId != XR_NULL_SYSTEM_ID) return true;
    if (!s_dispatch.GetSystem) return false;
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult result = s_dispatch.GetSystem(m_instance, &systemInfo, &m_systemId);
    if (XR_FAILED(result)) {
        emitError("No VR headset detected");
        return false;
    }

    if (!s_dispatch.EnumerateViewConfigurations ||
        !s_dispatch.EnumerateViewConfigurationViews ||
        !s_dispatch.GetViewConfigurationProperties)
        return false;

    uint32_t configurationCount = 0;
    result = s_dispatch.EnumerateViewConfigurations(
        m_instance, m_systemId, 0, &configurationCount, nullptr);
    if (XR_FAILED(result) || configurationCount == 0) return false;
    std::vector<XrViewConfigurationType> configurations(configurationCount);
    result = s_dispatch.EnumerateViewConfigurations(
        m_instance, m_systemId, configurationCount, &configurationCount,
        configurations.data());
    if (XR_FAILED(result) || std::find(configurations.begin(), configurations.end(),
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) == configurations.end()) {
        emitError("OpenXR runtime has no primary stereo view configuration");
        return false;
    }

    m_viewConfigProps = {XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
    result = s_dispatch.GetViewConfigurationProperties(
        m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        &m_viewConfigProps);
    if (XR_FAILED(result)) return false;

    uint32_t viewCount = 0;
    result = s_dispatch.EnumerateViewConfigurationViews(
        m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &viewCount, nullptr);
    if (XR_FAILED(result) || viewCount < 2) return false;
    std::vector<XrViewConfigurationView> views(viewCount);
    for (auto& view : views) view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    result = s_dispatch.EnumerateViewConfigurationViews(
        m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        viewCount, &viewCount, views.data());
    if (XR_FAILED(result)) return false;

    m_eyeCount = std::min<int>(static_cast<int>(viewCount), 2);
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (s_dispatch.GetSystemProperties && XR_SUCCEEDED(
            s_dispatch.GetSystemProperties(m_instance, m_systemId, &systemProperties))) {
        std::snprintf(m_systemName, sizeof(m_systemName), "%s",
                      systemProperties.systemName);
    }
    for (int eyeIndex = 0; eyeIndex < m_eyeCount; ++eyeIndex) {
        m_eyes[eyeIndex] = XrEye{};
        m_eyes[eyeIndex].view = {XR_TYPE_VIEW};
        m_eyes[eyeIndex].swapchainImageWidth = views[eyeIndex].recommendedImageRectWidth;
        m_eyes[eyeIndex].swapchainImageHeight = views[eyeIndex].recommendedImageRectHeight;
    }
    return m_eyeCount == 2;
}

bool XrManager::createSession()
{
    if (!m_vkDevice || !m_vkPhysicalDevice || !m_vkInstance) {
        emitError("Vulkan device not set before creating session");
        return false;
    }
    if (!s_dispatch.CreateSession) return false;
    XrGraphicsBindingVulkanKHR vulkanBinding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    vulkanBinding.instance = m_vkInstance;
    vulkanBinding.physicalDevice = m_vkPhysicalDevice;
    vulkanBinding.device = m_vkDevice;
    vulkanBinding.queueFamilyIndex = m_queueFamilyIndex;
    vulkanBinding.queueIndex = m_queueIndex;
    if (!s_dispatch.GetVulkanGraphicsRequirementsKHR) return false;
    XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    if (XR_FAILED(s_dispatch.GetVulkanGraphicsRequirementsKHR(
            m_instance, m_systemId, &requirements))) {
        emitError("Failed to query OpenXR Vulkan graphics requirements");
        return false;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_vkPhysicalDevice, &properties);
    const XrVersion apiVersion = XR_MAKE_VERSION(
        VK_VERSION_MAJOR(properties.apiVersion),
        VK_VERSION_MINOR(properties.apiVersion),
        VK_VERSION_PATCH(properties.apiVersion));
    if (apiVersion < requirements.minApiVersionSupported ||
        apiVersion > requirements.maxApiVersionSupported) {
        emitError("Selected Vulkan device API version is outside OpenXR requirements");
        return false;
    }
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &vulkanBinding;
    sessionInfo.systemId = m_systemId;
    XrResult result = s_dispatch.CreateSession(m_instance, &sessionInfo, &m_session);
    if (XR_FAILED(result)) {
        emitError("Failed to create OpenXR session");
        return false;
    }
    return true;
}

bool XrManager::createReferenceSpaces()
{
    if (!s_dispatch.CreateReferenceSpace) return false;
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
    return XR_SUCCEEDED(result);
}

void XrManager::emitError(const std::string& message)
{
    if (onError) onError(message);
    else std::fprintf(stderr, "XrManager: %s\n", message.c_str());
}

void XrManager::emitInitialized(bool success)
{
    if (onInitialized) onInitialized(success);
}

} // namespace device
} // namespace ks

#endif
