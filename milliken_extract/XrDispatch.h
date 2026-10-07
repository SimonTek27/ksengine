#pragma once
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include <openxr/openxr.h>
#if __has_include(<openxr/openxr_platform.h>)
#include <openxr/openxr_platform.h>
#endif

namespace ks {
namespace device {

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


extern XrDispatch s_dispatch;

} // namespace device
} // namespace ks

#endif
