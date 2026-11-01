#ifndef OPENXR_PLATFORM_DEFINES_H_
#define OPENXR_PLATFORM_DEFINES_H_

#ifdef _WIN32
#define XR_USE_PLATFORM_WIN32 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Provide IUnknown for openxr_platform.h D3D types (excluded by WIN32_LEAN_AND_MEAN)
#include <unknwn.h>
#endif

#if defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID 1
#endif

#if defined(__APPLE__)
#define XR_USE_PLATFORM_MACOS 1
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#define XR_USE_PLATFORM_LINUX 1
// The engine drives OpenXR through Vulkan on every platform, but
// openxr_platform.h only exposes XrGraphicsBindingVulkanKHR and the
// PFN_xrGetVulkan* entry points inside #ifdef XR_USE_GRAPHICS_API_VULKAN.
// Without this the VR translation units see no bindings at all on Linux.
#define XR_USE_GRAPHICS_API_VULKAN 1
#endif

#if !defined(XR_PTR_SIZE)
#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64) || defined(__aarch64__) || defined(__arm64__) || defined(__64BIT__)
#define XR_PTR_SIZE 8
#else
#define XR_PTR_SIZE 4
#endif
#endif

#if defined(__cplusplus) && (__cplusplus >= 201103L)
#define XR_CPP_NULLPTR_SUPPORTED 1
#else
#define XR_CPP_NULLPTR_SUPPORTED 0
#endif

#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64)
#define XR_PTR_SIZE 8
#else
#define XR_PTR_SIZE 4
#endif

#if defined(_WIN32)
#define XRAPI_ATTR __declspec(dllimport)
#define XRAPI_CALL __stdcall
#define XRAPIV __stdcall
#define XRAPI_PTR __stdcall
#elif defined(__ANDROID__)
#define XRAPI_ATTR
#define XRAPI_CALL
#define XRAPIV
#define XRAPI_PTR
#else
#define XRAPI_ATTR
#define XRAPI_CALL
#define XRAPIV
#define XRAPI_PTR
#endif

#if !defined(XR_MAY_ALIAS)
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 4))
#define XR_MAY_ALIAS __attribute__((__may_alias__))
#else
#define XR_MAY_ALIAS
#endif
#endif

// Provide VkInstance/VkPhysicalDevice/VkDevice forward declarations for the header
// since we don't want to force inclusion of vulkan.h here
#include <stdint.h>
#include <stddef.h>

// When Vulkan SDK is NOT already included, provide minimal forward declarations
// so openxr.h can compile standalone. When Vulkan SDK IS included, skip entirely.
#ifndef VK_VERSION_1_0
#ifndef VK_DEFINE_HANDLE
#if (XR_PTR_SIZE == 8)
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
#else
#define VK_DEFINE_HANDLE(object) typedef uint64_t object;
#endif
#endif

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkPhysicalDevice)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkQueue)
VK_DEFINE_HANDLE(VkCommandBuffer)
VK_DEFINE_HANDLE(VkImage)
VK_DEFINE_HANDLE(VkImageView)
VK_DEFINE_HANDLE(VkSampler)
VK_DEFINE_HANDLE(VkRenderPass)
VK_DEFINE_HANDLE(VkPipeline)
VK_DEFINE_HANDLE(VkPipelineLayout)
VK_DEFINE_HANDLE(VkShaderModule)
VK_DEFINE_HANDLE(VkDescriptorSetLayout)
VK_DEFINE_HANDLE(VkDescriptorPool)
VK_DEFINE_HANDLE(VkDescriptorSet)
VK_DEFINE_HANDLE(VkBuffer)
VK_DEFINE_HANDLE(VkDeviceMemory)
VK_DEFINE_HANDLE(VkFence)
VK_DEFINE_HANDLE(VkSemaphore)
VK_DEFINE_HANDLE(VkEvent)
VK_DEFINE_HANDLE(VkSwapchainKHR)
VK_DEFINE_HANDLE(VkSurfaceKHR)

typedef uint32_t VkBool32;
typedef uint32_t VkFlags;
typedef int32_t VkResult;
typedef uint64_t VkDeviceSize;

typedef int VkFormat;
typedef int VkImageLayout;
typedef int VkStructureType;

typedef void* PFN_vkGetInstanceProcAddr;
#endif

// Forward declarations for types used by openxr.h structs before their definitions
// (openxr.h is generated C code, and MSVC 2026 C++17 mode requires forward declarations
//  when types are used as member types before the typedef appears)
typedef struct XrVector2i XrVector2i;
typedef struct XrFovf XrFovf;
typedef struct XrApplicationInfo XrApplicationInfo;
typedef struct XrSystemGraphicsProperties XrSystemGraphicsProperties;
typedef struct XrSystemTrackingProperties XrSystemTrackingProperties;
typedef struct XrCompositionLayerBaseHeader XrCompositionLayerBaseHeader;
typedef struct XrActiveActionSet XrActiveActionSet;
typedef struct XrActionSuggestedBinding XrActionSuggestedBinding;
typedef struct XrDebugUtilsMessengerCallbackDataEXT XrDebugUtilsMessengerCallbackDataEXT;
typedef struct XrDebugUtilsLabelEXT XrDebugUtilsLabelEXT;
typedef struct XrInteractionProfileState XrInteractionProfileState;
typedef struct XrBoundSourcesForActionEnumerateInfo XrBoundSourcesForActionEnumerateInfo;
typedef struct XrInputSourceLocalizedNameGetInfo XrInputSourceLocalizedNameGetInfo;
typedef struct XrHapticActionInfo XrHapticActionInfo;
typedef struct XrHapticBaseHeader XrHapticBaseHeader;
typedef uint64_t XrInstanceCreateFlags;
typedef uint64_t XrSessionCreateFlags;
typedef uint64_t XrSwapchainCreateFlags;
typedef uint64_t XrSwapchainUsageFlags;
typedef uint64_t XrCompositionLayerFlags;
typedef uint64_t XrViewStateFlags;
typedef uint64_t XrSpaceLocationFlags;
typedef uint64_t XrSpaceVelocityFlags;
typedef uint64_t XrVulkanInstanceCreateFlagsKHR;
typedef uint64_t XrVulkanDeviceCreateFlagsKHR;
typedef uint64_t XrDebugUtilsMessageSeverityFlagsEXT;
typedef uint64_t XrDebugUtilsMessageTypeFlagsEXT;

// Extension name defines required by XrManager.cpp
// (not provided by the official openxr.h; these are Khronos-standard extension names)
#ifndef XR_KHR_VULKAN_ENABLE_EXTENSION_NAME
#define XR_KHR_VULKAN_ENABLE_EXTENSION_NAME "XR_KHR_vulkan_enable"
#endif
#ifndef XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
#define XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME "XR_KHR_vulkan_enable2"
#endif

// Interaction profile path defines
// (not provided by official headers; these are runtime-specific string paths)
#ifndef XR_INTERACTION_PROFILE_KHR_SIMPLE_CONTROLLER
#define XR_INTERACTION_PROFILE_KHR_SIMPLE_CONTROLLER "/interaction_profiles/khr/simple_controller"
#endif

#endif // OPENXR_PLATFORM_DEFINES_H_
