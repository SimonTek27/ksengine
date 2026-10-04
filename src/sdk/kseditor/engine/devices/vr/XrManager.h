#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>
#include <QMutex>

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
namespace vr {

struct XrEye {
    XrView view;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    XrSwapchain depthSwapchain = XR_NULL_HANDLE;
    uint32_t swapchainImageWidth = 0;
    uint32_t swapchainImageHeight = 0;
    QVector<VkImage> colorImages;
    QVector<VkImage> depthImages;
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
    QVector2D thumbstickValue;
    QVector2D trackpadValue;
    QMatrix4x4 aimPose;
    QMatrix4x4 gripPose;
    bool aimValid = false;
    bool gripValid = false;
};

class XrManager : public QObject {
    Q_OBJECT
public:
    static XrManager* instance();

    explicit XrManager(QObject* parent = nullptr);
    ~XrManager();

    bool initialize(const QString& applicationName = "ksEditor VR");
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

    QMatrix4x4 projectionMatrix(int eyeIndex, float nearZ = 0.1f, float farZ = 1000.0f) const;
    QMatrix4x4 viewMatrix(int eyeIndex) const;

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

signals:
    void initialized(bool success);
    void sessionStateChanged(XrSessionState oldState, XrSessionState newState);
    void sessionRunningChanged(bool running);
    void sessionFocusChanged(bool focused);
    void instanceLost();
    void error(const QString& message);

private:
    bool createInstance(const QString& applicationName);
    bool getSystem();
    bool createSession();
    bool createReferenceSpaces();
    bool createActions();
    bool suggestBindings();
    bool attachActions();
    void handleSessionStateChanged(const XrEventDataSessionStateChanged& event);
    void destroyActions();

    XrPath stringToPath(const char* str);
    XrAction createAction(XrActionSet actionSet, const char* name, const char* localizedName,
                          XrActionType type, const QVector<XrPath>& subactionPaths = {});
    void suggestInteractionProfileBindings(const char* profile, const QVector<XrActionSuggestedBinding>& bindings);

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
    QVector<int64_t> m_swapchainFormats;

    // Vulkan state (owned externally)
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    uint32_t m_queueIndex = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    // Paths
    XrPath m_leftHandPath = XR_NULL_PATH;
    XrPath m_rightHandPath = XR_NULL_PATH;

    QMutex m_mutex;
};

}} // namespace ks::vr

#else // OpenXR not available

namespace ks {
namespace vr {
// Stub types when OpenXR is not available
struct XrEye {};
struct XrControllerState {};
class XrManager : public QObject {
    Q_OBJECT
public:
    static XrManager* instance() { return nullptr; }
    explicit XrManager(QObject* parent = nullptr) : QObject(parent) {}
    ~XrManager() override = default;
    bool initialize(const QString& = QString()) { return false; }
    void shutdown() {}
    bool isInitialized() const { return false; }
    bool isSessionRunning() const { return false; }
    bool isSessionFocused() const { return false; }
signals:
    void initialized(bool);
    void error(const QString&);
};
}} // namespace ks::vr

#endif // XR_VERSION_1_0
