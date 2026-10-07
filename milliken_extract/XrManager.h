#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>

#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#endif
#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#endif
#if __has_include(<openxr/openxr_platform.h>)
#include <openxr/openxr_platform.h>
#endif

#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)

namespace ks {
namespace device {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

/** Column-major 4x4 (OpenGL). */
struct XrMat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    float& operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }

    void setToIdentity() {
        for (int i = 0; i < 16; ++i) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1;
    }

    static XrMat4 identity() { return XrMat4{}; }

    static XrMat4 perspective(float fovYDeg, float aspect, float zNear, float zFar) {
        XrMat4 r{};
        const float f = 1.0f / std::tan(fovYDeg * 0.5f * 0.017453292519943295f);
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        r.m[15] = 0.0f;
        return r;
    }

    void translate(float x, float y, float z) {
        m[12] += m[0]*x + m[4]*y + m[8]*z;
        m[13] += m[1]*x + m[5]*y + m[9]*z;
        m[14] += m[2]*x + m[6]*y + m[10]*z;
        m[15] += m[3]*x + m[7]*y + m[11]*z;
    }

    void rotate(const float qw, float qx, float qy, float qz) {
        // Apply quaternion rotation (column-major)
        const float xx = qx*qx, yy = qy*qy, zz = qz*qz;
        const float xy = qx*qy, xz = qx*qz, yz = qy*qz;
        const float wx = qw*qx, wy = qw*qy, wz = qw*qz;
        XrMat4 R{};
        R.m[0] = 1 - 2*(yy+zz); R.m[4] = 2*(xy-wz);     R.m[8]  = 2*(xz+wy);
        R.m[1] = 2*(xy+wz);     R.m[5] = 1 - 2*(xx+zz); R.m[9]  = 2*(yz-wx);
        R.m[2] = 2*(xz-wy);     R.m[6] = 2*(yz+wx);     R.m[10] = 1 - 2*(xx+yy);
        // multiply R * this
        XrMat4 out{};
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                out.m[c*4+r] = R.m[0*4+r]*m[c*4+0] + R.m[1*4+r]*m[c*4+1] +
                               R.m[2*4+r]*m[c*4+2] + R.m[3*4+r]*m[c*4+3];
        *this = out;
    }
};

struct XrEye {
    XrView view{};
    XrSwapchain swapchain = XR_NULL_HANDLE;
    XrSwapchain depthSwapchain = XR_NULL_HANDLE;
    uint32_t swapchainImageWidth = 0;
    uint32_t swapchainImageHeight = 0;
    std::vector<VkImage> colorImages;
    std::vector<VkImage> depthImages;
    int32_t currentImageIndex = -1;
};

struct XrControllerState {
    bool connected = false;
    XrPath handPath = XR_NULL_PATH;
    XrSpace aimSpace = XR_NULL_HANDLE;
    XrSpace gripSpace = XR_NULL_HANDLE;
    XrAction aimAction = XR_NULL_HANDLE;
    XrAction gripAction = XR_NULL_HANDLE;
    XrAction squeezeAction = XR_NULL_HANDLE;
    XrAction triggerAction = XR_NULL_HANDLE;
    XrAction thumbstickAction = XR_NULL_HANDLE;
    XrAction trackpadAction = XR_NULL_HANDLE;
    XrAction menuAction = XR_NULL_HANDLE;
    bool squeezeValue = false;
    bool triggerClicked = false;
    bool menuClicked = false;
    float triggerValue = 0.0f;
    float squeezeValueFloat = 0.0f;
    Vec2 thumbstickValue;
    Vec2 trackpadValue;
    XrMat4 aimPose;
    XrMat4 gripPose;
    bool aimValid = false;
    bool gripValid = false;
};

/**
 * OpenXR manager — Qt-free.
 * Events via std::function callbacks (no QObject/signals).
 */
class XrManager {
public:
    static XrManager* instance();

    XrManager();
    ~XrManager();

    XrManager(const XrManager&) = delete;
    XrManager& operator=(const XrManager&) = delete;

    bool initialize(const std::string& applicationName = "ksEditor VR");
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    bool isSessionRunning() const { return m_sessionRunning; }
    bool isSessionFocused() const { return m_sessionFocused; }

    bool beginXRFrame();
    bool endXRFrame();
    bool beginEyeRender(int eyeIndex);
    void endEyeRender(int eyeIndex);

    XrEye& eye(int index) { return m_eyes[index]; }
    int eyeCount() const { return m_eyeCount; }

    XrInstance xrInstance() const { return m_instance; }
    XrSession xrSession() const { return m_session; }
    XrSystemId systemId() const { return m_systemId; }

    XrMat4 projectionMatrix(int eyeIndex, float nearZ = 0.1f, float farZ = 1000.0f) const;
    XrMat4 viewMatrix(int eyeIndex) const;

    bool pollEvents();
    bool pollActions();

    XrControllerState& leftController() { return m_leftController; }
    XrControllerState& rightController() { return m_rightController; }

    int64_t selectedColorFormat() const { return m_colorFormat; }
    int64_t selectedDepthFormat() const { return m_depthFormat; }

    void setVulkanDevice(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkInstance vkInstance, uint32_t queueFamilyIndex, uint32_t queueIndex);
    void setVulkanCommandResources(VkCommandPool pool, VkQueue queue);

    bool createSwapchains();
    void destroySwapchains();

    // Callbacks (replace Qt signals)
    std::function<void(bool success)> onInitialized;
    std::function<void(XrSessionState oldState, XrSessionState newState)> onSessionStateChanged;
    std::function<void(bool running)> onSessionRunningChanged;
    std::function<void(bool focused)> onSessionFocusChanged;
    std::function<void()> onInstanceLost;
    std::function<void(const std::string& message)> onError;

private:
    bool createInstance(const std::string& applicationName);
    bool getSystem();
    bool createSession();
    bool createReferenceSpaces();
    bool createActions();
    bool suggestBindings();
    bool attachActions();
    void handleSessionStateChanged(const XrEventDataSessionStateChanged& event);
    void destroyActions();

    void emitError(const std::string& message);
    void emitInitialized(bool success);

    XrPath stringToPath(const char* str);
    XrAction createAction(XrActionSet actionSet, const char* name, const char* localizedName,
                          XrActionType type, const std::vector<XrPath>& subactionPaths = {});
    void suggestInteractionProfileBindings(const char* profile,
                                           const std::vector<XrActionSuggestedBinding>& bindings);

    static XrManager* s_instance;

    bool m_initialized = false;
    bool m_sessionRunning = false;
    bool m_sessionFocused = false;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSession m_session = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;

    int m_eyeCount = 0;
    XrEye m_eyes[2];
    XrViewConfigurationProperties m_viewConfigProps{};

    XrSpace m_stageSpace = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    XrActionSet m_gameActionSet = XR_NULL_HANDLE;
    XrControllerState m_leftController;
    XrControllerState m_rightController;

    int64_t m_colorFormat = 0;
    int64_t m_depthFormat = 0;
    std::vector<int64_t> m_swapchainFormats;

    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    uint32_t m_queueIndex = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    XrPath m_leftHandPath = XR_NULL_PATH;
    XrPath m_rightHandPath = XR_NULL_PATH;

    mutable std::mutex m_mutex;
};

} // namespace device
} // namespace ks

#else // OpenXR not available

namespace ks {
namespace device {

struct XrEye {};
struct XrControllerState {};

class XrManager {
public:
    static XrManager* instance() { return nullptr; }
    XrManager() = default;
    ~XrManager() = default;
    bool initialize(const std::string& = {}) { return false; }
    void shutdown() {}
    bool isInitialized() const { return false; }
    bool isSessionRunning() const { return false; }
    bool isSessionFocused() const { return false; }

    std::function<void(bool)> onInitialized;
    std::function<void(const std::string&)> onError;
};

} // namespace device
} // namespace ks

#endif // XR_VERSION_1_0
