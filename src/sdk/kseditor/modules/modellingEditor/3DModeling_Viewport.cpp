#include "3DModeling_Viewport.h"
#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QDebug>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QtMath>
#include <QProcess>
#include <QDir>
#include <cfloat>
#include <optional>

#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif

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

QMatrix4x4 toQMatrix4x4(const Matrix4& matrix)
{
    QMatrix4x4 result;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result(row, col) = matrix.m[col][row];
        }
    }
    return result;
}
}

// ── PBR Vertex Shader (GLSL 450) ─────────────────────────────────────
// Input: position(loc0), normal(loc1), uv(loc2)
// Push constants: model matrix, baseColor, metallic, roughness
// UBO (binding=0): camera view/proj/cameraPos
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

// ── PBR Fragment Shader (GLSL 450) ────────────────────────────────────
// Cook-Torrance BRDF with simple hemisphere ambient
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

    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, rough);
    float G = geometrySmith(N, V, L, rough);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metalness);

    float NdotL = max(dot(N, L), 0.0);
    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    vec3 diffuse = kD * albedo / 3.14159;

    vec3 direct = (diffuse + specular) * lightColor * lightIntensity * NdotL;

    // Hemisphere ambient
    vec3 ambient = ambientColor * ambientIntensity * albedo;
    // Subtle rim from camera direction
    float rim = 1.0 - max(dot(N, V), 0.0);
    ambient += vec3(0.1, 0.1, 0.15) * rim * rim;

    outColor = vec4(direct + ambient, inColor.a);
}
)";

VulkanViewportRenderer::VulkanViewportRenderer(QVulkanWindow* w, ks::SceneGraph* scene)
    : m_window(w)
    , m_scene(scene)
{
}

void VulkanViewportRenderer::updateViewportSize(int w, int h) {
    Q_UNUSED(w)
    Q_UNUSED(h)
    // Qt6 manages swapchain size automatically from window size
}

void VulkanViewportRenderer::initResources() {
#if HAS_VULKAN
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) return;
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs) return;

    VkDevice dev = m_window->device();

    // ── Extended Camera UBO (view + proj + cameraPos) ──────────────
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sizeof(CameraUBO);
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &m_ubo) != VK_SUCCESS) {
        qWarning("Failed to create camera UBO");
        return;
    }

    VkMemoryRequirements memReqs;
    devFuncs->vkGetBufferMemoryRequirements(dev, m_ubo, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    inst->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &memProps);

    std::optional<uint32_t> memType = findHostVisibleMemoryType(memReqs.memoryTypeBits, memProps);
    if (!memType) {
        qWarning("Failed to find host-visible memory for camera UBO");
        devFuncs->vkDestroyBuffer(dev, m_ubo, nullptr);
        m_ubo = VK_NULL_HANDLE;
        return;
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = *memType;

    if (devFuncs->vkAllocateMemory(dev, &allocInfo, nullptr, &m_uboMemory) != VK_SUCCESS) {
        qWarning("Failed to allocate UBO memory");
        devFuncs->vkDestroyBuffer(dev, m_ubo, nullptr);
        m_ubo = VK_NULL_HANDLE;
        return;
    }
    devFuncs->vkBindBufferMemory(dev, m_ubo, m_uboMemory, 0);

    // ── Descriptor pool ────────────────────────────────────────────
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (devFuncs->vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        qWarning("Failed to create descriptor pool");
        return;
    }

    // ── Descriptor set layout ──────────────────────────────────────
    VkDescriptorSetLayoutBinding uboBinding = {};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    if (devFuncs->vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        qWarning("Failed to create descriptor set layout");
        return;
    }

    // ── Allocate & write descriptor set ────────────────────────────
    VkDescriptorSetAllocateInfo dsAlloc = {};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = m_descriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_descriptorSetLayout;

    if (devFuncs->vkAllocateDescriptorSets(dev, &dsAlloc, &m_descriptorSet) != VK_SUCCESS) {
        qWarning("Failed to allocate descriptor set");
        return;
    }

    VkDescriptorBufferInfo descBuf = {};
    descBuf.buffer = m_ubo;
    descBuf.range = sizeof(CameraUBO);

    VkWriteDescriptorSet writeDesc = {};
    writeDesc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDesc.dstSet = m_descriptorSet;
    writeDesc.dstBinding = 0;
    writeDesc.descriptorCount = 1;
    writeDesc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDesc.pBufferInfo = &descBuf;

    devFuncs->vkUpdateDescriptorSets(dev, 1, &writeDesc, 0, nullptr);
#endif
}

void VulkanViewportRenderer::releaseResources() {
#if HAS_VULKAN
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (inst) {
        QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
        if (devFuncs) {
            VkDevice dev = m_window->device();
            if (m_ubo != VK_NULL_HANDLE) devFuncs->vkDestroyBuffer(dev, m_ubo, nullptr);
            if (m_uboMemory != VK_NULL_HANDLE) devFuncs->vkFreeMemory(dev, m_uboMemory, nullptr);
            if (m_descriptorPool != VK_NULL_HANDLE) devFuncs->vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
            if (m_descriptorSetLayout != VK_NULL_HANDLE) devFuncs->vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
        }
    }
#endif
    destroyPipeline();
    destroyMeshBuffers();
    m_ubo = VK_NULL_HANDLE;
    m_uboMemory = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_descriptorSet = VK_NULL_HANDLE;
    m_descriptorSetLayout = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
}

void VulkanViewportRenderer::createPipeline()
{
#if HAS_VULKAN
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) return;
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs) return;
    VkDevice dev = m_window->device();

    m_renderPass = m_window->defaultRenderPass();
    if (m_renderPass == VK_NULL_HANDLE) return;
    if (m_descriptorSetLayout == VK_NULL_HANDLE) return;

    // ── Try to compile PBR shaders from GLSL, fall back to embedded ──
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    // Attempt GLSL→SPIR-V compilation via glslangValidator
    auto compileGLSL = [&](const char* source, VkShaderStageFlagBits stage) -> VkShaderModule {
        QString tmpPath = QDir::tempPath() + "/ks_viewport_"
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
                VkShaderModuleCreateInfo smi = {};
                smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                smi.codeSize = spirv.size();
                smi.pCode = reinterpret_cast<const uint32_t*>(spirv.constData());
                if (devFuncs->vkCreateShaderModule(dev, &smi, nullptr, &mod) != VK_SUCCESS)
                    mod = VK_NULL_HANDLE;
            }
        }
        QFile::remove(tmpPath);
        return mod;
    };

    vertModule = compileGLSL(s_pbrVertSource, VK_SHADER_STAGE_VERTEX_BIT);
    fragModule = compileGLSL(s_pbrFragSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vertModule != VK_NULL_HANDLE && fragModule != VK_NULL_HANDLE) {
        qInfo("VulkanViewportRenderer: using runtime-compiled PBR shaders");
    } else {
        // Fallback: embedded SPIR-V with flat color + identity transform
        if (vertModule != VK_NULL_HANDLE)
            devFuncs->vkDestroyShaderModule(dev, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE)
            devFuncs->vkDestroyShaderModule(dev, fragModule, nullptr);

        static const uint32_t vertFallback[] = {
            0x07230203,0x00010000,0x000d000b,0x0000002b,0x00000000,0x00020011,0x00000001,0x0006000b,
            0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
            0x0009000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000d,0x0000001d,0x00000028,
            0x00000029,0x00030003,0x00000002,0x000001c2,0x000a0004,0x475f4c47,0x4c474f4f,0x70635f45,
            0x74735f70,0x5f656c79,0x656e696c,0x7269645f,0x69746365,0x00006576,0x00080004,0x475f4c47,
            0x4c474f4f,0x6e695f45,0x64756c63,0x69645f65,0x74636572,0x00657669,0x00040005,0x00000004,
            0x6e69616d,0x00000000,0x00060005,0x0000000b,0x505f6c67,0x65567265,0x78657472,0x00000000,
            0x00060006,0x0000000b,0x00000000,0x505f6c67,0x7469736f,0x006e6f69,0x00070006,0x0000000b,
            0x00000001,0x505f6c67,0x746e696f,0x657a6953,0x00000000,0x00070006,0x0000000b,0x00000002,
            0x435f6c67,0x4470696c,0x61747369,0x0065636e,0x00070006,0x0000000b,0x00000003,0x435f6c67,
            0x446c6c75,0x61747369,0x0065636e,0x00030005,0x0000000d,0x00000000,0x00050005,0x00000011,
            0x656d6143,0x42556172,0x0000004f,0x00050006,0x00000011,0x00000000,0x77656976,0x00000000,
            0x00050006,0x00000011,0x00000001,0x6a6f7270,0x00000000,0x00040005,0x00000013,0x656d6163,
            0x00006172,0x00050005,0x0000001d,0x6f506e69,0x69746973,0x00006e6f,0x00050005,0x00000028,
            0x67617266,0x6f6c6f43,0x00000072,0x00040005,0x00000029,0x6f436e69,0x00726f6c,0x00030047,
            0x0000000b,0x00000002,0x00050048,0x0000000b,0x00000000,0x0000000b,0x00000000,0x00050048,
            0x0000000b,0x00000001,0x0000000b,0x00000001,0x00050048,0x0000000b,0x00000002,0x0000000b,
            0x00000003,0x00050048,0x0000000b,0x00000003,0x0000000b,0x00000004,0x00030047,0x00000011,
            0x00000002,0x00040048,0x00000011,0x00000000,0x00000005,0x00050048,0x00000011,0x00000000,
            0x00000007,0x00000010,0x00050048,0x00000011,0x00000000,0x00000023,0x00000000,0x00040048,
            0x00000011,0x00000001,0x00000005,0x00050048,0x00000011,0x00000001,0x00000007,0x00000010,
            0x00050048,0x00000011,0x00000001,0x00000023,0x00000040,0x00040047,0x00000013,0x00000021,
            0x00000000,0x00040047,0x00000013,0x00000022,0x00000000,0x00040047,0x0000001d,0x0000001e,
            0x00000000,0x00040047,0x00000028,0x0000001e,0x00000000,0x00040047,0x00000029,0x0000001e,
            0x00000001,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
            0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040015,0x00000008,0x00000020,
            0x00000000,0x0004002b,0x00000008,0x00000009,0x00000001,0x0004001c,0x0000000a,0x00000006,
            0x00000009,0x0006001e,0x0000000b,0x00000007,0x00000006,0x0000000a,0x0000000a,0x00040020,
            0x0000000c,0x00000003,0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000003,0x00040015,
            0x0000000e,0x00000020,0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040018,
            0x00000010,0x00000007,0x00000004,0x0004001e,0x00000011,0x00000010,0x00000010,0x00040020,
            0x00000012,0x00000002,0x00000011,0x0004003b,0x00000012,0x00000013,0x00000002,0x0004002b,
            0x0000000e,0x00000014,0x00000001,0x00040020,0x00000015,0x00000002,0x00000010,0x00040017,
            0x0000001b,0x00000006,0x00000003,0x00040020,0x0000001c,0x00000001,0x0000001b,0x0004003b,
            0x0000001c,0x0000001d,0x00000001,0x0004002b,0x00000006,0x0000001f,0x3f800000,0x00040020,
            0x00000025,0x00000003,0x00000007,0x00040020,0x00000027,0x00000003,0x0000001b,0x0004003b,
            0x00000027,0x00000028,0x00000003,0x0004003b,0x0000001c,0x00000029,0x00000001,0x00050036,
            0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000015,
            0x00000016,0x00000013,0x00000014,0x0004003d,0x00000010,0x00000017,0x00000016,0x00050041,
            0x00000015,0x00000018,0x00000013,0x0000000f,0x0004003d,0x00000010,0x00000019,0x00000018,
            0x00050092,0x00000010,0x0000001a,0x00000017,0x00000019,0x0004003d,0x0000001b,0x0000001e,
            0x0000001d,0x00050051,0x00000006,0x00000020,0x0000001e,0x00000000,0x00050051,0x00000006,
            0x00000021,0x0000001e,0x00000001,0x00050051,0x00000006,0x00000022,0x0000001e,0x00000002,
            0x00070050,0x00000007,0x00000023,0x00000020,0x00000021,0x00000022,0x0000001f,0x00050091,
            0x00000007,0x00000024,0x0000001a,0x00000023,0x00050041,0x00000025,0x00000026,0x0000000d,
            0x0000000f,0x0003003e,0x00000026,0x00000024,0x0004003d,0x0000001b,0x0000002a,0x00000029,
            0x0003003e,0x00000028,0x0000002a,0x000100fd,0x00010038
        };
        static const uint32_t fragFallback[] = {
            0x07230203,0x00010000,0x000d000b,0x00000013,0x00000000,0x00020011,0x00000001,0x0006000b,
            0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
            0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000c,0x00030010,
            0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x000a0004,0x475f4c47,0x4c474f4f,
            0x70635f45,0x74735f70,0x5f656c79,0x656e696c,0x7269645f,0x69746365,0x00006576,0x00080004,
            0x475f4c47,0x4c474f4f,0x6e695f45,0x64756c63,0x69645f65,0x74636572,0x00657669,0x00040005,
            0x00000004,0x6e69616d,0x00000000,0x00050005,0x00000009,0x4374756f,0x726f6c6f,0x00000000,
            0x00050005,0x0000000c,0x67617266,0x6f6c6f43,0x00000072,0x00040047,0x00000009,0x0000001e,
            0x00000000,0x00040047,0x0000000c,0x0000001e,0x00000000,0x00020013,0x00000002,0x00030021,
            0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,
            0x00000004,0x00040020,0x00000008,0x00000003,0x00000007,0x0004003b,0x00000008,0x00000009,
            0x00000003,0x00040017,0x0000000a,0x00000006,0x00000003,0x00040020,0x0000000b,0x00000001,
            0x0000000a,0x0004003b,0x0000000b,0x0000000c,0x00000001,0x0004002b,0x00000006,0x0000000e,
            0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,
            0x0004003d,0x0000000a,0x0000000d,0x0000000c,0x00050051,0x00000006,0x0000000f,0x0000000d,
            0x00000000,0x00050051,0x00000006,0x00000010,0x0000000d,0x00000001,0x00050051,0x00000006,
            0x00000011,0x0000000d,0x00000002,0x00070050,0x00000007,0x00000012,0x0000000f,0x00000010,
            0x00000011,0x0000000e,0x0003003e,0x00000009,0x00000012,0x000100fd,0x00010038
        };

        VkShaderModuleCreateInfo smi = {};
        smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smi.codeSize = sizeof(vertFallback);
        smi.pCode = vertFallback;
        if (devFuncs->vkCreateShaderModule(dev, &smi, nullptr, &vertModule) != VK_SUCCESS) return;

        smi.codeSize = sizeof(fragFallback);
        smi.pCode = fragFallback;
        if (devFuncs->vkCreateShaderModule(dev, &smi, nullptr, &fragModule) != VK_SUCCESS) {
            devFuncs->vkDestroyShaderModule(dev, vertModule, nullptr);
            return;
        }
    }

    // ── Pipeline layout with push constants ────────────────────────
    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshUBO);

    VkPipelineLayoutCreateInfo plInfo = {};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;

    if (devFuncs->vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_layout) != VK_SUCCESS) {
        qWarning("Failed to create pipeline layout");
        devFuncs->vkDestroyShaderModule(dev, vertModule, nullptr);
        devFuncs->vkDestroyShaderModule(dev, fragModule, nullptr);
        return;
    }

    // ── Vertex input (SceneVertex layout: pos(12) + color(12) + normal(12) + uv(8) = 44 bytes)
    VkVertexInputBindingDescription vertBind = {};
    vertBind.binding = 0;
    vertBind.stride = sizeof(SceneVertex);
    vertBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertAttrs[3] = {};
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

    VkPipelineVertexInputStateCreateInfo viInfo = {};
    viInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viInfo.vertexBindingDescriptionCount = 1;
    viInfo.pVertexBindingDescriptions = &vertBind;
    viInfo.vertexAttributeDescriptionCount = 3;
    viInfo.pVertexAttributeDescriptions = vertAttrs;

    VkPipelineInputAssemblyStateCreateInfo iaInfo = {};
    iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {};
    viewport.width = static_cast<float>(m_window->swapChainImageSize().width());
    viewport.height = static_cast<float>(m_window->swapChainImageSize().height());
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.extent = { static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height) };

    VkPipelineViewportStateCreateInfo vpInfo = {};
    vpInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpInfo.viewportCount = 1;
    vpInfo.pViewports = &viewport;
    vpInfo.scissorCount = 1;
    vpInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rsInfo = {};
    rsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rsInfo.lineWidth = 1.0f;
    rsInfo.cullMode = VK_CULL_MODE_NONE;
    rsInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo msInfo = {};
    msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt = {};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cbInfo = {};
    cbInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbInfo.attachmentCount = 1;
    cbInfo.pAttachments = &cbAtt;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkGraphicsPipelineCreateInfo gpInfo = {};
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &viInfo;
    gpInfo.pInputAssemblyState = &iaInfo;
    gpInfo.pViewportState = &vpInfo;
    gpInfo.pRasterizationState = &rsInfo;
    gpInfo.pMultisampleState = &msInfo;
    gpInfo.pColorBlendState = &cbInfo;
    gpInfo.layout = m_layout;
    gpInfo.renderPass = m_renderPass;

    if (devFuncs->vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        qWarning("Failed to create graphics pipeline");
    }

    devFuncs->vkDestroyShaderModule(dev, vertModule, nullptr);
    devFuncs->vkDestroyShaderModule(dev, fragModule, nullptr);
#endif
}

void VulkanViewportRenderer::destroyPipeline()
{
#if HAS_VULKAN
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) return;
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs) return;
    VkDevice dev = m_window->device();

    if (m_pipeline != VK_NULL_HANDLE) {
        devFuncs->vkDestroyPipeline(dev, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        devFuncs->vkDestroyPipelineLayout(dev, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
#endif
}

void VulkanViewportRenderer::initSwapChainResources()
{
#if HAS_VULKAN
    createPipeline();
#endif
}

void VulkanViewportRenderer::releaseSwapChainResources()
{
#if HAS_VULKAN
    destroyPipeline();
#endif
}

void VulkanViewportRenderer::startNextFrame()
{
#if HAS_VULKAN
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) { m_window->frameReady(); return; }
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs || m_renderPass == VK_NULL_HANDLE) { m_window->frameReady(); return; }

    // Rebuild mesh buffers if scene changed or not yet built
    if (m_scene && m_scene->objectCount() != m_sceneObjectCount) {
        m_buffersDirty = true;
    }
    if ((m_buffersDirty || m_meshBuffers.isEmpty()) && m_scene) {
        createMeshBuffers();
        m_buffersDirty = false;
    }

    VkCommandBuffer cmdBuf = m_window->currentCommandBuffer();
    if (cmdBuf == VK_NULL_HANDLE) { m_window->frameReady(); return; }

    VkClearValue clearColor = {};
    clearColor.color = {{ 0.098f, 0.098f, 0.110f, 1.0f }};

    VkRenderPassBeginInfo rpBegin = {};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_window->currentFramebuffer();
    if (rpBegin.framebuffer == VK_NULL_HANDLE) { m_window->frameReady(); return; }
    rpBegin.renderArea.extent = { static_cast<uint32_t>(m_window->swapChainImageSize().width()),
                                  static_cast<uint32_t>(m_window->swapChainImageSize().height()) };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    devFuncs->vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    if (m_pipeline != VK_NULL_HANDLE && m_descriptorSet != VK_NULL_HANDLE) {
        devFuncs->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        // Update camera UBO
        Viewport3D* vp = Viewport3D::instance();
        float yawRad = qDegreesToRadians(vp->cameraYaw());
        float pitchRad = qDegreesToRadians(vp->cameraPitch());
        QVector3D target(vp->cameraPanX(), -vp->cameraPanY(), 0.0f);
        QVector3D eye(
            target.x() + vp->cameraDistance() * qCos(pitchRad) * qSin(yawRad),
            target.y() + vp->cameraDistance() * qSin(pitchRad),
            target.z() + vp->cameraDistance() * qCos(pitchRad) * qCos(yawRad)
        );
        QMatrix4x4 view;
        view.lookAt(eye, target, QVector3D(0, 1, 0));
        QMatrix4x4 proj;
        proj.perspective(45.0f,
            static_cast<float>(m_window->swapChainImageSize().width()) /
            static_cast<float>(m_window->swapChainImageSize().height()),
            0.1f, 1000.0f);
        updateCameraUBO(view, proj, eye);

        devFuncs->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_layout, 0, 1, &m_descriptorSet, 0, nullptr);

        // Draw each mesh with per-object push constants
        for (const auto& buf : m_meshBuffers) {
            VkDeviceSize offsets = 0;
            devFuncs->vkCmdBindVertexBuffers(cmdBuf, 0, 1, &buf.vertexBuffer, &offsets);
            devFuncs->vkCmdBindIndexBuffer(cmdBuf, buf.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // Push per-object data
            MeshUBO meshUbo;
            meshUbo.model = buf.modelMatrix;
            meshUbo.baseColor = buf.baseColor;
            meshUbo.metallic = buf.metallic;
            meshUbo.roughness = buf.roughness;
            meshUbo.padding[0] = 0.0f;
            meshUbo.padding[1] = 0.0f;

            devFuncs->vkCmdPushConstants(cmdBuf, m_layout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(MeshUBO), &meshUbo);

            devFuncs->vkCmdDrawIndexed(cmdBuf, buf.indexCount, 1, 0, 0, 0);
        }
    }

    devFuncs->vkCmdEndRenderPass(cmdBuf);
    m_window->frameReady();
    m_window->requestUpdate();
#else
    if (m_window) m_window->frameReady();
#endif
}

void VulkanViewportRenderer::createMeshBuffers() {
#if HAS_VULKAN
    destroyMeshBuffers();
    if (!m_scene || !m_window) return;

    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) return;
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs) return;
    VkDevice dev = m_window->device();

    // Update scene transforms first
    m_scene->updateAllTransforms();

    for (SceneObject* obj : m_scene->allObjects()) {
        if (!obj->hasMesh() || !obj->isVisible()) continue;
        SceneMesh* mesh = obj->mesh();
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
        VkBufferCreateInfo vbi = {};
        vbi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbi.size = verts.size() * sizeof(SceneVertex);
        vbi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (devFuncs->vkCreateBuffer(dev, &vbi, nullptr, &buf.vertexBuffer) != VK_SUCCESS) continue;

        VkMemoryRequirements vmr;
        devFuncs->vkGetBufferMemoryRequirements(dev, buf.vertexBuffer, &vmr);
        VkPhysicalDeviceMemoryProperties memProps;
        inst->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(), &memProps);

        std::optional<uint32_t> vertexMemType = findHostVisibleMemoryType(vmr.memoryTypeBits, memProps);
        if (!vertexMemType) {
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            continue;
        }

        VkMemoryAllocateInfo mai = {};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = vmr.size;
        mai.memoryTypeIndex = *vertexMemType;
        if (devFuncs->vkAllocateMemory(dev, &mai, nullptr, &buf.vertexMemory) != VK_SUCCESS) {
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            continue;
        }
        void* mapped;
        if (devFuncs->vkMapMemory(dev, buf.vertexMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            devFuncs->vkFreeMemory(dev, buf.vertexMemory, nullptr);
            continue;
        }
        memcpy(mapped, verts.data(), verts.size() * sizeof(SceneVertex));
        VkMappedMemoryRange mmr = {};
        mmr.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mmr.memory = buf.vertexMemory;
        mmr.size = VK_WHOLE_SIZE;
        devFuncs->vkFlushMappedMemoryRanges(dev, 1, &mmr);
        devFuncs->vkUnmapMemory(dev, buf.vertexMemory);
        devFuncs->vkBindBufferMemory(dev, buf.vertexBuffer, buf.vertexMemory, 0);

        // Index buffer
        VkBufferCreateInfo ibi = {};
        ibi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibi.size = indices.size() * sizeof(uint32_t);
        ibi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        ibi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (devFuncs->vkCreateBuffer(dev, &ibi, nullptr, &buf.indexBuffer) != VK_SUCCESS) {
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            devFuncs->vkFreeMemory(dev, buf.vertexMemory, nullptr);
            continue;
        }

        VkMemoryRequirements imr;
        devFuncs->vkGetBufferMemoryRequirements(dev, buf.indexBuffer, &imr);
        std::optional<uint32_t> indexMemType = findHostVisibleMemoryType(imr.memoryTypeBits, memProps);
        if (!indexMemType) {
            devFuncs->vkDestroyBuffer(dev, buf.indexBuffer, nullptr);
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            devFuncs->vkFreeMemory(dev, buf.vertexMemory, nullptr);
            continue;
        }
        VkMemoryAllocateInfo imai = {};
        imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        imai.allocationSize = imr.size;
        imai.memoryTypeIndex = *indexMemType;
        if (devFuncs->vkAllocateMemory(dev, &imai, nullptr, &buf.indexMemory) != VK_SUCCESS) {
            devFuncs->vkDestroyBuffer(dev, buf.indexBuffer, nullptr);
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            devFuncs->vkFreeMemory(dev, buf.vertexMemory, nullptr);
            continue;
        }
        if (devFuncs->vkMapMemory(dev, buf.indexMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
            devFuncs->vkDestroyBuffer(dev, buf.indexBuffer, nullptr);
            devFuncs->vkFreeMemory(dev, buf.indexMemory, nullptr);
            devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
            devFuncs->vkFreeMemory(dev, buf.vertexMemory, nullptr);
            continue;
        }
        memcpy(mapped, indices.data(), indices.size() * sizeof(uint32_t));
        VkMappedMemoryRange indexFlushRange = {};
        indexFlushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        indexFlushRange.memory = buf.indexMemory;
        indexFlushRange.size = VK_WHOLE_SIZE;
        devFuncs->vkFlushMappedMemoryRanges(dev, 1, &indexFlushRange);
        devFuncs->vkUnmapMemory(dev, buf.indexMemory);
        devFuncs->vkBindBufferMemory(dev, buf.indexBuffer, buf.indexMemory, 0);

        m_meshBuffers.append(buf);
    }
    m_sceneObjectCount = m_scene->objectCount();
#endif
}

void VulkanViewportRenderer::destroyMeshBuffers() {
#if HAS_VULKAN
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) return;
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs) return;
    VkDevice dev = m_window->device();

    for (auto& buf : m_meshBuffers) {
        if (buf.vertexBuffer != VK_NULL_HANDLE) devFuncs->vkDestroyBuffer(dev, buf.vertexBuffer, nullptr);
        if (buf.vertexMemory != VK_NULL_HANDLE) devFuncs->vkFreeMemory(dev, buf.vertexMemory, nullptr);
        if (buf.indexBuffer != VK_NULL_HANDLE) devFuncs->vkDestroyBuffer(dev, buf.indexBuffer, nullptr);
        if (buf.indexMemory != VK_NULL_HANDLE) devFuncs->vkFreeMemory(dev, buf.indexMemory, nullptr);
    }
    m_meshBuffers.clear();
#endif
}

void VulkanViewportRenderer::markBuffersDirty() {
    m_buffersDirty = true;
}

void VulkanViewportRenderer::updateCameraUBO(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& camPos) {
#if HAS_VULKAN
    if (m_ubo == VK_NULL_HANDLE || m_uboMemory == VK_NULL_HANDLE) return;
    if (!m_window) return;
    QVulkanInstance* inst = m_window->vulkanInstance();
    if (!inst) return;
    QVulkanDeviceFunctions* devFuncs = inst->deviceFunctions(m_window->device());
    if (!devFuncs) return;

    CameraUBO uboData;
    uboData.view = view;
    uboData.proj = proj;
    uboData.cameraPos = QVector4D(camPos, 1.0f);

    void* mapped;
    if (devFuncs->vkMapMemory(m_window->device(), m_uboMemory, 0, sizeof(CameraUBO), 0, &mapped) == VK_SUCCESS) {
        memcpy(mapped, &uboData, sizeof(CameraUBO));
        VkMappedMemoryRange flushRange = {};
        flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flushRange.memory = m_uboMemory;
        flushRange.size = VK_WHOLE_SIZE;
        devFuncs->vkFlushMappedMemoryRanges(m_window->device(), 1, &flushRange);
        devFuncs->vkUnmapMemory(m_window->device(), m_uboMemory);
    }
#else
    Q_UNUSED(view); Q_UNUSED(proj); Q_UNUSED(camPos);
#endif
}

}
