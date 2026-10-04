#include "VREditorModule.h"
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
#include "sdk/kseditor/engine/Graphics/VulkanRenderer.h"
#include "sdk/kseditor/engine/Graphics/VulkanIntegration.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"

#include <cstring>
#include <QDebug>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QCheckBox>
#include <QSlider>
#include <QFormLayout>
#include <QElapsedTimer>
#include <QDir>
#include <QProcess>
#include <QFile>
#include <optional>

namespace ks {

namespace {
std::optional<uint32_t> findHostVisibleMemoryType(uint32_t memoryTypeBits,
                                                   const VkPhysicalDeviceMemoryProperties& memProps)
{
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            return i;
        }
    }
    return std::nullopt;
}
}

// PBR Vertex Shader (GLSL 450)
static const char* s_pbrVertSource = R"(
#version 450 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform PushConsts {
    mat4 model;
    vec4 baseColor;
    float metallic;
    float roughness;
} pc;
layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} cam;
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outColor;
layout(location = 4) out float outMetallic;
layout(location = 5) out float outRoughness;
void main() {
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    outWorldPos = worldPos.xyz;
    outNormal = normalize(mat3(pc.model) * inNormal);
    outUV = inUV;
    outColor = pc.baseColor;
    outMetallic = pc.metallic;
    outRoughness = pc.roughness;
    gl_Position = cam.proj * cam.view * worldPos;
}
)";

// PBR Fragment Shader (Cook-Torrance BRDF)
static const char* s_pbrFragSource = R"(
#version 450 core
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;
layout(location = 4) in float inMetallic;
layout(location = 5) in float inRoughness;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
} cam;
const vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
const vec3 lightColor = vec3(1.0, 0.95, 0.9);
const float lightIntensity = 1.5;
const vec3 ambientColor = vec3(0.4, 0.4, 0.5);
const float ambientIntensity = 0.3;
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * denom * denom);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float a = roughness * roughness;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = 2.0 * NdotV / (NdotV + sqrt(a * a + (1.0 - a * a) * NdotV * NdotV));
    float ggx2 = 2.0 * NdotL / (NdotL + sqrt(a * a + (1.0 - a * a) * NdotL * NdotL));
    return ggx1 * ggx2;
}
void main() {
    vec3 N = normalize(inNormal);
    vec3 V = normalize(cam.cameraPos.xyz - inWorldPos);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);
    vec3 albedo = inColor.rgb;
    float metalness = inMetallic;
    float rough = max(inRoughness, 0.01);
    vec3 F0 = mix(vec3(0.04), albedo, metalness);
    float NDF = distributionGGX(N, H, rough);
    float G = geometrySmith(N, V, L, rough);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metalness);
    float NdotL = max(dot(N, L), 0.0);
    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    vec3 diffuse = kD * albedo / 3.14159;
    vec3 direct = (diffuse + specular) * lightColor * lightIntensity * NdotL;
    vec3 ambient = ambientColor * ambientIntensity * albedo;
    float rim = 1.0 - max(dot(N, V), 0.0);
    ambient += vec3(0.1, 0.1, 0.15) * rim * rim;
    outColor = vec4(direct + ambient, inColor.a);
}
)";

VREditorModule::VREditorModule(QWidget* parent)
    : EditorModule(parent)
{
    setLayout(new QVBoxLayout());
}

VREditorModule::~VREditorModule()
{
    shutdown();
}

bool VREditorModule::initialize()
{
    m_xrManager = vr::XrManager::instance();
    m_viewportRenderer = new vr::XrViewportRenderer(this);
    m_xrInput = new vr::XrInput(m_xrManager, this);

    connect(m_xrManager, &vr::XrManager::sessionStateChanged,
            this, &VREditorModule::onSessionStateChanged);
    connect(m_xrManager, &vr::XrManager::sessionRunningChanged,
            this, [this](bool running) {
        if (!running && m_vrActive) stopVR();
    });
    connect(m_xrManager, &vr::XrManager::error,
            this, [this](const QString& msg) {
        emit vrError(msg);
    });

    connect(m_xrInput, &vr::XrInput::buttonPressed,
            this, &VREditorModule::onControllerButton);
    connect(m_xrInput, &vr::XrInput::axisMoved,
            this, &VREditorModule::onControllerAxis);

    m_frameTimer = std::make_unique<QTimer>(this);
    connect(m_frameTimer.get(), &QTimer::timeout, this, &VREditorModule::updateFrame);

    m_fpsTimer.start();

    return true;
}

void VREditorModule::shutdown()
{
    if (m_vrActive) stopVR();

    if (m_frameTimer) m_frameTimer->stop();
}

void VREditorModule::onActivation()
{
    if (!m_vrActive) {
        startVR();
    }
}

void VREditorModule::onDeactivation()
{
    if (m_vrActive) {
        stopVR();
    }
}

void VREditorModule::startVR()
{
    if (m_vrActive) return;

    setupVulkanForVR();
    if (!m_vkReady) {
        emit vrError("Vulkan not properly initialized for VR");
        return;
    }

    auto* renderer = VulkanRenderer::instance();
    if (!renderer || !renderer->isInitialized()) {
        emit vrError("Vulkan renderer not available");
        return;
    }

    VkDevice device = renderer->device();
    VkPhysicalDevice physicalDevice = renderer->physicalDevice();
    VkQueue graphicsQueue = renderer->graphicsQueue();
    VkCommandPool commandPool = renderer->commandPool();
    VkInstance vkInstance = renderer->vulkanInstance();
    uint32_t queueFamilyIndex = renderer->graphicsQueueFamilyIndex();

    if (!m_viewportRenderer->initialize(device, physicalDevice, vkInstance,
                                         queueFamilyIndex, 0, commandPool, graphicsQueue)) {
        emit vrError("Failed to initialize VR viewport renderer");
        return;
    }

    // Create PBR pipeline for scene rendering
    if (!createVRPipeline(device)) {
        qWarning() << "VREditor: Failed to create VR PBR pipeline - scene will not render";
    }

    // Create mesh buffers from scene graph if available
    if (m_scene) {
        createVRMeshBuffers(device, physicalDevice);
    }

    m_vrActive = true;
    m_frameTimer->start(16); // ~60 FPS
    m_frameElapsed.start();

    emit vrStarted();
}

void VREditorModule::stopVR()
{
    if (!m_vrActive) return;

    m_frameTimer->stop();

    // Ensure all in-flight XR frames complete before destroying resources
    m_viewportRenderer->shutdown();

    auto* renderer = VulkanRenderer::instance();
    if (renderer) {
        VkDevice device = renderer->device();
        vkDeviceWaitIdle(device);
        destroyVRMeshBuffers(device);
    }
    destroyVRPipeline();

    m_vrActive = false;

    emit vrStopped();
}

void VREditorModule::setupVulkanForVR()
{
    auto* renderer = VulkanRenderer::instance();
    if (!renderer || !renderer->isInitialized()) {
        qWarning() << "VREditor: Vulkan renderer not available - VR cannot start";
        m_vkReady = false;
        return;
    }

    m_vkReady = true;
}

void VREditorModule::updateFrame()
{
    if (!m_vrActive) return;

    m_deltaTime = m_frameElapsed.elapsed() / 1000.0f;
    m_frameElapsed.restart();

    handleControllerInputs();

    if (m_viewportRenderer) {
        // Rebuild mesh buffers if scene has changed
        if (m_scene && (m_buffersDirty || m_scene->objectCount() != m_sceneObjectCount)) {
            auto* renderer = VulkanRenderer::instance();
            if (renderer) {
                destroyVRMeshBuffers(renderer->device());
                createVRMeshBuffers(renderer->device(), renderer->physicalDevice());
            }
            m_buffersDirty = false;
        }

        m_viewportRenderer->setDrawCallback(
            [this](VkCommandBuffer cmd, int eyeIndex,
                   const QMatrix4x4& view, const QMatrix4x4& proj) {
                QMatrix4x4 eyeView = xrManager()->viewMatrix(eyeIndex);
                QMatrix4x4 eyeProj = xrManager()->projectionMatrix(eyeIndex);
                eyeView = buildViewMatrixForEye(eyeIndex);
                this->drawScene(cmd, eyeIndex, eyeView, eyeProj);
            });

        m_viewportRenderer->renderFrame();
    }

    m_frameCount++;
    if (m_fpsTimer.elapsed() >= 1000) {
        m_fps = m_frameCount * 1000.0f / m_fpsTimer.elapsed();
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

QMatrix4x4 VREditorModule::buildViewMatrixForEye(int eyeIndex) const
{
    auto* xr = m_viewportRenderer ? xrManager() : nullptr;
    if (!xr || eyeIndex >= xr->eyeCount()) {
        QMatrix4x4 m;
        m.lookAt(m_cameraPosition, m_cameraTarget, QVector3D(0, 1, 0));
        return m;
    }

    return xr->viewMatrix(eyeIndex);
}

void VREditorModule::handleControllerInputs()
{
    if (!m_xrInput) return;

    auto& left = m_xrManager->leftController();
    auto& right = m_xrManager->rightController();

    if (left.thumbstickValue.length() > 0.3f) {
        QVector3D forward = (m_cameraTarget - m_cameraPosition).normalized();
        QVector3D rightVec = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();

        m_cameraPosition += forward * left.thumbstickValue.y() * m_moveSpeed * m_deltaTime;
        m_cameraPosition += rightVec * left.thumbstickValue.x() * m_moveSpeed * m_deltaTime;
        m_cameraTarget = m_cameraPosition + forward;
    }

    if (right.thumbstickValue.length() > 0.3f) {
        m_cameraYaw += right.thumbstickValue.x() * m_rotationSpeed * m_deltaTime;
        m_cameraPitch -= right.thumbstickValue.y() * m_rotationSpeed * m_deltaTime;
        m_cameraPitch = qBound(-89.0f, m_cameraPitch, 89.0f);

        QMatrix4x4 rot;
        rot.rotate(m_cameraYaw, 0, 1, 0);
        rot.rotate(m_cameraPitch, 1, 0, 0);
        QVector3D forward = rot * QVector3D(0, 0, -1);
        m_cameraTarget = m_cameraPosition + forward;
    }

    emit cameraChanged();
}

void VREditorModule::setCameraPosition(const QVector3D& pos)
{
    m_cameraPosition = pos;
    emit cameraChanged();
}

void VREditorModule::setCameraTarget(const QVector3D& target)
{
    m_cameraTarget = target;
    emit cameraChanged();
}

void VREditorModule::setCameraYaw(float yaw)
{
    m_cameraYaw = yaw;
    emit cameraChanged();
}

void VREditorModule::setCameraPitch(float pitch)
{
    m_cameraPitch = pitch;
    emit cameraChanged();
}

void VREditorModule::onControllerButton(int hand, int button, bool pressed)
{
    emit controllerEvent(hand, button, pressed);
}

void VREditorModule::onControllerAxis(int hand, int axis, float x, float y)
{
    emit controllerAxisEvent(hand, axis, x, y);
}

void VREditorModule::onSessionStateChanged(XrSessionState oldState, XrSessionState newState)
{
    qInfo() << "VR Session state:" << oldState << "->" << newState;
}

// ── Vulkan Scene Rendering ─────────────────────────────────────────

VkShaderModule VREditorModule::compileGLSL(VkDevice device, const char* source, VkShaderStageFlagBits stage)
{
    QString tmpPath = QDir::tempPath() + "/ks_vr_"
        + QString::number(reinterpret_cast<quintptr>(this), 16)
        + (stage == VK_SHADER_STAGE_VERTEX_BIT ? "_vert.glsl" : "_frag.glsl");
    {
        QFile f(tmpPath);
        if (f.open(QIODevice::WriteOnly)) f.write(source);
    }
    QProcess glslang;
    QStringList args;
    args << "-V" << tmpPath;
    args << "-S" << (stage == VK_SHADER_STAGE_VERTEX_BIT ? "vert" : "frag");
    glslang.start("glslangValidator", args);
    VkShaderModule mod = VK_NULL_HANDLE;
    if (glslang.waitForFinished(30000) && glslang.exitCode() == 0) {
        QString spvPath = tmpPath + ".spv";
        QFile spvFile(spvPath);
        if (spvFile.open(QIODevice::ReadOnly)) {
            QByteArray spirv = spvFile.readAll();
            spvFile.close();
            QFile::remove(spvPath);
            VkShaderModuleCreateInfo smi{};
            smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smi.codeSize = spirv.size();
            smi.pCode = reinterpret_cast<const uint32_t*>(spirv.constData());
            if (vkCreateShaderModule(device, &smi, nullptr, &mod) != VK_SUCCESS)
                mod = VK_NULL_HANDLE;
        }
    }
    QFile::remove(tmpPath);
    return mod;
}

bool VREditorModule::createVRPipeline(VkDevice device)
{
    if (m_descriptorSetLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
            qWarning("VREditor: Failed to create descriptor set layout");
            return false;
        }
    }

    if (m_descriptorPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            qWarning("VREditor: Failed to create descriptor pool");
            return false;
        }
    }

    if (m_descriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo dsAlloc{};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = m_descriptorPool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &m_descriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &dsAlloc, &m_descriptorSet) != VK_SUCCESS) {
            qWarning("VREditor: Failed to allocate descriptor set");
            return false;
        }
    }

    if (m_ubo == VK_NULL_HANDLE) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = sizeof(CameraUBO);
        bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufInfo, nullptr, &m_ubo) != VK_SUCCESS) {
            qWarning("VREditor: Failed to create UBO");
            return false;
        }

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, m_ubo, &memReqs);

        auto* renderer = VulkanRenderer::instance();
        VkPhysicalDevice physicalDevice = renderer ? renderer->physicalDevice() : VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        auto memType = findHostVisibleMemoryType(memReqs.memoryTypeBits, memProps);
        if (!memType) {
            qWarning("VREditor: Failed to find host-visible memory for UBO");
            vkDestroyBuffer(device, m_ubo, nullptr);
            m_ubo = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = *memType;

        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_uboMemory) != VK_SUCCESS) {
            qWarning("VREditor: Failed to allocate UBO memory");
            vkDestroyBuffer(device, m_ubo, nullptr);
            m_ubo = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(device, m_ubo, m_uboMemory, 0);

        // Write descriptor
        VkDescriptorBufferInfo descBuf{};
        descBuf.buffer = m_ubo;
        descBuf.range = sizeof(CameraUBO);

        VkWriteDescriptorSet writeDesc{};
        writeDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDesc.dstSet = m_descriptorSet;
        writeDesc.dstBinding = 0;
        writeDesc.descriptorCount = 1;
        writeDesc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDesc.pBufferInfo = &descBuf;
        vkUpdateDescriptorSets(device, 1, &writeDesc, 0, nullptr);
    }

    // Compile shaders
    VkShaderModule vertModule = compileGLSL(device, s_pbrVertSource, VK_SHADER_STAGE_VERTEX_BIT);
    VkShaderModule fragModule = compileGLSL(device, s_pbrFragSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        qWarning("VREditor: Failed to compile PBR shaders - scene rendering unavailable");
        return false;
    }

    // Pipeline layout
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshUBO);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        qWarning("VREditor: Failed to create pipeline layout");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Get the render pass from the VR viewport renderer
    VkRenderPass renderPass = m_viewportRenderer->eyeFramebuffer(0).renderPass;
    if (renderPass == VK_NULL_HANDLE) {
        qWarning("VREditor: No render pass available from XR viewport");
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Vertex input (SceneVertex layout)
    VkVertexInputBindingDescription vertBind{};
    vertBind.binding = 0;
    vertBind.stride = sizeof(SceneVertex);
    vertBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertAttrs[3]{};
    vertAttrs[0].location = 0;
    vertAttrs[0].binding = 0;
    vertAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertAttrs[0].offset = offsetof(SceneVertex, position);
    vertAttrs[1].location = 1;
    vertAttrs[1].binding = 0;
    vertAttrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertAttrs[1].offset = offsetof(SceneVertex, normal);
    vertAttrs[2].location = 2;
    vertAttrs[2].binding = 0;
    vertAttrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertAttrs[2].offset = offsetof(SceneVertex, uv);

    VkPipelineVertexInputStateCreateInfo viInfo{};
    viInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viInfo.vertexBindingDescriptionCount = 1;
    viInfo.pVertexBindingDescriptions = &vertBind;
    viInfo.vertexAttributeDescriptionCount = 3;
    viInfo.pVertexAttributeDescriptions = vertAttrs;

    VkPipelineInputAssemblyStateCreateInfo iaInfo{};
    iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpInfo{};
    vpInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpInfo.viewportCount = 1;
    vpInfo.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsInfo{};
    rsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rsInfo.lineWidth = 1.0f;
    rsInfo.cullMode = VK_CULL_MODE_NONE;
    rsInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo msInfo{};
    msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cbInfo{};
    cbInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbInfo.attachmentCount = 1;
    cbInfo.pAttachments = &cbAtt;

    VkPipelineDepthStencilStateCreateInfo dsInfo{};
    dsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsInfo.depthTestEnable = VK_TRUE;
    dsInfo.depthWriteEnable = VK_TRUE;
    dsInfo.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = dynStates;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkGraphicsPipelineCreateInfo gpInfo{};
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &viInfo;
    gpInfo.pInputAssemblyState = &iaInfo;
    gpInfo.pViewportState = &vpInfo;
    gpInfo.pRasterizationState = &rsInfo;
    gpInfo.pMultisampleState = &msInfo;
    gpInfo.pColorBlendState = &cbInfo;
    gpInfo.pDepthStencilState = &dsInfo;
    gpInfo.pDynamicState = &dynInfo;
    gpInfo.layout = m_pipelineLayout;
    gpInfo.renderPass = renderPass;

    VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (res != VK_SUCCESS) {
        qWarning("VREditor: Failed to create graphics pipeline");
        return false;
    }

    return true;
}

void VREditorModule::destroyVRPipeline()
{
    auto* renderer = VulkanRenderer::instance();
    if (!renderer) return;
    VkDevice device = renderer->device();

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, m_descriptorPool, 1, &m_descriptorSet);
        m_descriptorSet = VK_NULL_HANDLE;
    }
    if (m_uboMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_uboMemory, nullptr);
        m_uboMemory = VK_NULL_HANDLE;
    }
    if (m_ubo != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_ubo, nullptr);
        m_ubo = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VREditorModule::createVRMeshBuffers(VkDevice device, VkPhysicalDevice physicalDevice)
{
    destroyVRMeshBuffers(device);
    if (!m_scene) return;

    m_scene->updateAllTransforms();

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (auto* obj : m_scene->allObjects()) {
        if (!obj->hasMesh() || !obj->isVisible()) continue;
        auto* mesh = obj->mesh();
        const auto& verts = mesh->geometry().vertices;
        const auto& indices = mesh->geometry().indices;
        if (verts.isEmpty() || indices.isEmpty()) continue;

        PerMeshBuffer buf;
        buf.indexCount = static_cast<uint32_t>(indices.size());
        buf.modelMatrix = obj->worldTransform();
        QColor col = obj->baseColor();
        buf.baseColor = QVector4D(col.redF(), col.greenF(), col.blueF(), col.alphaF());
        buf.metallic = obj->metallic();
        buf.roughness = obj->roughness();

        // Vertex buffer
        VkBufferCreateInfo vbi{};
        vbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbi.size = verts.size() * sizeof(SceneVertex);
        vbi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &vbi, nullptr, &buf.vertexBuffer) != VK_SUCCESS) continue;

        VkMemoryRequirements vmr;
        vkGetBufferMemoryRequirements(device, buf.vertexBuffer, &vmr);
        auto vertexMemType = findHostVisibleMemoryType(vmr.memoryTypeBits, memProps);
        if (!vertexMemType) {
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            continue;
        }

        VkMemoryAllocateInfo vmai{};
        vmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vmai.allocationSize = vmr.size;
        vmai.memoryTypeIndex = *vertexMemType;
        if (vkAllocateMemory(device, &vmai, nullptr, &buf.vertexMemory) != VK_SUCCESS) {
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            continue;
        }
        void* mapped;
        if (vkMapMemory(device, buf.vertexMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            vkFreeMemory(device, buf.vertexMemory, nullptr);
            continue;
        }
        memcpy(mapped, verts.data(), verts.size() * sizeof(SceneVertex));
        VkMappedMemoryRange mmr{};
        mmr.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mmr.memory = buf.vertexMemory;
        mmr.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(device, 1, &mmr);
        vkUnmapMemory(device, buf.vertexMemory);
        vkBindBufferMemory(device, buf.vertexBuffer, buf.vertexMemory, 0);

        // Index buffer
        VkBufferCreateInfo ibi{};
        ibi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibi.size = indices.size() * sizeof(uint32_t);
        ibi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        ibi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &ibi, nullptr, &buf.indexBuffer) != VK_SUCCESS) {
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            vkFreeMemory(device, buf.vertexMemory, nullptr);
            continue;
        }

        VkMemoryRequirements imr;
        vkGetBufferMemoryRequirements(device, buf.indexBuffer, &imr);
        auto indexMemType = findHostVisibleMemoryType(imr.memoryTypeBits, memProps);
        if (!indexMemType) {
            vkDestroyBuffer(device, buf.indexBuffer, nullptr);
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            vkFreeMemory(device, buf.vertexMemory, nullptr);
            continue;
        }
        VkMemoryAllocateInfo imai{};
        imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        imai.allocationSize = imr.size;
        imai.memoryTypeIndex = *indexMemType;
        if (vkAllocateMemory(device, &imai, nullptr, &buf.indexMemory) != VK_SUCCESS) {
            vkDestroyBuffer(device, buf.indexBuffer, nullptr);
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            vkFreeMemory(device, buf.vertexMemory, nullptr);
            continue;
        }
        if (vkMapMemory(device, buf.indexMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
            vkDestroyBuffer(device, buf.indexBuffer, nullptr);
            vkFreeMemory(device, buf.indexMemory, nullptr);
            vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
            vkFreeMemory(device, buf.vertexMemory, nullptr);
            continue;
        }
        memcpy(mapped, indices.data(), indices.size() * sizeof(uint32_t));
        VkMappedMemoryRange indexFlushRange{};
        indexFlushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        indexFlushRange.memory = buf.indexMemory;
        indexFlushRange.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(device, 1, &indexFlushRange);
        vkUnmapMemory(device, buf.indexMemory);
        vkBindBufferMemory(device, buf.indexBuffer, buf.indexMemory, 0);

        m_meshBuffers.append(buf);
    }
    m_sceneObjectCount = m_scene->objectCount();
}

void VREditorModule::destroyVRMeshBuffers(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;
    for (auto& buf : m_meshBuffers) {
        if (buf.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
        if (buf.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(device, buf.vertexMemory, nullptr);
        if (buf.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buf.indexBuffer, nullptr);
        if (buf.indexMemory != VK_NULL_HANDLE) vkFreeMemory(device, buf.indexMemory, nullptr);
    }
    m_meshBuffers.clear();
}

void VREditorModule::updateCameraUBO(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& camPos)
{
    if (m_ubo == VK_NULL_HANDLE || m_uboMemory == VK_NULL_HANDLE) return;

    CameraUBO uboData;
    uboData.view = view;
    uboData.proj = proj;
    uboData.cameraPos = QVector4D(camPos, 1.0f);

    void* mapped;
    VkDevice device = VulkanRenderer::instance()->device();
    if (vkMapMemory(device, m_uboMemory, 0, sizeof(CameraUBO), 0, &mapped) == VK_SUCCESS) {
        memcpy(mapped, &uboData, sizeof(CameraUBO));
        VkMappedMemoryRange flushRange{};
        flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flushRange.memory = m_uboMemory;
        flushRange.size = VK_WHOLE_SIZE;
        vkFlushMappedMemoryRanges(device, 1, &flushRange);
        vkUnmapMemory(device, m_uboMemory);
    }
}

void VREditorModule::drawScene(VkCommandBuffer cmd, int eyeIndex,
                                const QMatrix4x4& view, const QMatrix4x4& proj)
{
    Q_UNUSED(eyeIndex)

    if (m_pipeline == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;
    if (m_meshBuffers.isEmpty()) return;

    auto* renderer = VulkanRenderer::instance();
    if (!renderer) return;
    VkDevice device = renderer->device();

    // Set dynamic viewport and scissor from the VR eye framebuffer size
    int eyeW = 0, eyeH = 0;
    if (m_viewportRenderer) {
        auto& eye = m_xrManager->eye(eyeIndex);
        eyeW = static_cast<int>(eye.swapchainImageWidth);
        eyeH = static_cast<int>(eye.swapchainImageHeight);
    }
    if (eyeW == 0 || eyeH == 0) return;

    VkViewport vp{};
    vp.width = static_cast<float>(eyeW);
    vp.height = static_cast<float>(eyeH);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent.width = static_cast<uint32_t>(eyeW);
    scissor.extent.height = static_cast<uint32_t>(eyeH);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind PBR pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Extract camera position from view matrix inverse
    QMatrix4x4 invView = view.inverted();
    QVector3D cameraPos(invView(0, 3), invView(1, 3), invView(2, 3));

    // Update camera UBO with per-eye view/projection
    updateCameraUBO(view, proj, cameraPos);

    // Bind descriptor set (camera UBO)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Draw each mesh
    for (const auto& buf : m_meshBuffers) {
        VkDeviceSize offsets = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &buf.vertexBuffer, &offsets);
        vkCmdBindIndexBuffer(cmd, buf.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        MeshUBO meshUbo;
        meshUbo.model = buf.modelMatrix;
        meshUbo.baseColor = buf.baseColor;
        meshUbo.metallic = buf.metallic;
        meshUbo.roughness = buf.roughness;
        meshUbo.padding[0] = 0.0f;
        meshUbo.padding[1] = 0.0f;

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(MeshUBO), &meshUbo);

        vkCmdDrawIndexed(cmd, buf.indexCount, 1, 0, 0, 0);
    }
}

} // namespace ks
#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
