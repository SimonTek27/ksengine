#pragma once

#include "editor/EditorModule.h"
#if __has_include("engine/devices/vr/XrManager.h")
#include "sdk/kseditor/engine/devices/vr/XrManager.h"
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#define HAS_XR 1
#else
#define HAS_XR 0
#endif
#else
#define HAS_XR 0
#endif
#if __has_include("engine/devices/vr/XrViewportRenderer.h")
#include "sdk/kseditor/engine/devices/vr/XrViewportRenderer.h"
#endif
#if __has_include("engine/devices/vr/XrInput.h")
#include "sdk/kseditor/engine/devices/vr/XrInput.h"
#endif

#include <QTimer>
#include <QElapsedTimer>
#include <QVector3D>
#include <QQuaternion>
#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#endif

namespace ks {
class SceneGraph;

#if HAS_XR
class VREditorModule : public EditorModule {
    Q_OBJECT
public:
    explicit VREditorModule(QWidget* parent = nullptr);
    ~VREditorModule() override;

    bool initialize() override;
    void shutdown() override;

    QString moduleName() const override { return "VR Viewport"; }
    QString moduleId() const override { return "vr"; }
    QString getModuleIcon() const override { return QString(); }
    int getModulePriority() const override { return 50; }

    vr::XrManager* xrManager() const { return m_xrManager; }
    vr::XrViewportRenderer* viewportRenderer() { return m_viewportRenderer; }
    vr::XrInput* xrInput() { return m_xrInput; }

    bool isVRModeActive() const { return m_vrActive; }

    void setSceneGraph(SceneGraph* scene) { m_scene = scene; m_buffersDirty = true; }
    void markSceneDirty() { m_buffersDirty = true; }

    QVector3D cameraPosition() const { return m_cameraPosition; }
    QVector3D cameraTarget() const { return m_cameraTarget; }
    float cameraYaw() const { return m_cameraYaw; }
    float cameraPitch() const { return m_cameraPitch; }

public slots:
    void startVR();
    void stopVR();
    void toggleVR() { if (m_vrActive) stopVR(); else startVR(); }

    void setCameraPosition(const QVector3D& pos);
    void setCameraTarget(const QVector3D& target);
    void setCameraYaw(float yaw);
    void setCameraPitch(float pitch);

    void onControllerButton(int hand, int button, bool pressed);
    void onControllerAxis(int hand, int axis, float x, float y);

signals:
    void vrStarted();
    void vrStopped();
    void vrError(const QString& error);
    void cameraChanged();
    void controllerEvent(int hand, int button, bool pressed);
    void controllerAxisEvent(int hand, int axis, float x, float y);

protected:
    void onActivation() override;
    void onDeactivation() override;

private slots:
    void updateFrame();
    void onSessionStateChanged(XrSessionState oldState, XrSessionState newState);

private:
    void setupVulkanForVR();
    void handleControllerInputs();
    QMatrix4x4 buildViewMatrixForEye(int eyeIndex) const;
    void drawScene(VkCommandBuffer cmd, int eyeIndex,
                   const QMatrix4x4& view, const QMatrix4x4& proj);

    bool createVRPipeline(VkDevice device);
    void destroyVRPipeline();
    void createVRMeshBuffers(VkDevice device, VkPhysicalDevice physicalDevice);
    void destroyVRMeshBuffers(VkDevice device);
    void updateCameraUBO(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& camPos);

    VkShaderModule compileGLSL(VkDevice device, const char* source, VkShaderStageFlagBits stage);

    vr::XrManager* m_xrManager = nullptr;
    vr::XrViewportRenderer* m_viewportRenderer = nullptr;
    vr::XrInput* m_xrInput = nullptr;

    SceneGraph* m_scene = nullptr;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkBuffer m_ubo = VK_NULL_HANDLE;
    VkDeviceMemory m_uboMemory = VK_NULL_HANDLE;

    struct PerMeshBuffer {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        QMatrix4x4 modelMatrix;
        QVector4D baseColor;
        float metallic = 0.0f;
        float roughness = 0.5f;
    };
    QVector<PerMeshBuffer> m_meshBuffers;
    int m_sceneObjectCount = 0;
    bool m_buffersDirty = true;

    struct CameraUBO {
        QMatrix4x4 view;
        QMatrix4x4 proj;
        QVector4D cameraPos;
    };

    struct MeshUBO {
        QMatrix4x4 model;
        QVector4D baseColor;
        float metallic;
        float roughness;
        float padding[2];
    };

    bool m_vrActive = false;
    bool m_vkReady = false;

    std::unique_ptr<QTimer> m_frameTimer;
    QElapsedTimer m_frameElapsed;

    QVector3D m_cameraPosition{0, 1.7f, -3};
    QVector3D m_cameraTarget{0, 0, 0};
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;

    float m_moveSpeed = 2.0f;
    float m_rotationSpeed = 90.0f;

    float m_deltaTime = 0.0f;
    int m_frameCount = 0;
    float m_fps = 0.0f;
    QElapsedTimer m_fpsTimer;
};
#else
class VREditorModule : public EditorModule {
    Q_OBJECT
public:
    explicit VREditorModule(QWidget* parent = nullptr) : EditorModule(parent) {}
    ~VREditorModule() override = default;
    bool initialize() override { return false; }
    void shutdown() override {}
    QString moduleName() const override { return "VR Viewport"; }
    QString moduleId() const override { return "vr"; }
    QString getModuleIcon() const override { return QString(); }
    int getModulePriority() const override { return 50; }
    bool isVRModeActive() const { return false; }
protected:
    void onActivation() override {}
    void onDeactivation() override {}
};
#endif

} // namespace ks
