#include "ShadowSystem.h"

#include <cstdio>
#include <cstring>
#include <cfloat>
#include <fstream>
#include <vector>
#include <algorithm>

// Deliberately no Qt includes anywhere in this file. VulkanFunctions.h (the
// engine's dynamically-loaded function-pointer table, "g_vk") pulls in
// <QtCore/QLibrary> just to load the Vulkan loader, so it isn't used here —
// this file links against the Vulkan loader directly instead, the same way
// SimulatorApp.cpp itself already does (vkCreateInstance, vkCreateWin32SurfaceKHR,
// etc. are called directly there too, not through g_vk).

namespace ks::sim {

namespace {

std::vector<char> readSpirvFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
}

} // namespace

CascadedShadowMap::~CascadedShadowMap() {
    shutdown();
}

bool CascadedShadowMap::initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                   VkCommandPool /*commandPool*/, VkQueue /*graphicsQueue*/,
                                   const std::string& shaderDir, uint32_t resolution) {
    m_physicalDevice = physicalDevice;
    m_device = device;
    m_resolution = resolution;

    // Practical split scheme default weighting; update() will re-derive the
    // actual distances from the real camera near/far each frame.
    m_splitFractions = {0.10f, 0.30f, 1.00f};

    // ---- Render pass: color attachment (manual depth written by
    // ksShadow.frag, matching what it already outputs) + a real depth
    // attachment for correct hardware depth testing while rasterizing. ----
    VkAttachmentDescription colorAtt{};
    colorAtt.format = VK_FORMAT_R32_SFLOAT;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAtt{};
    depthAtt.format = VK_FORMAT_D32_SFLOAT;
    depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription atts[2] = {colorAtt, depthAtt};

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpCi{};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = 2;
    rpCi.pAttachments = atts;
    rpCi.subpassCount = 1;
    rpCi.pSubpasses = &subpass;
    rpCi.dependencyCount = 2;
    rpCi.pDependencies = deps;

    if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_renderPass) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to create render pass\n");
        return false;
    }

    // ---- Color image: 2D array, one layer per cascade, sampled together
    // afterward as sampler2DArray. ----
    VkImageCreateInfo imgCi{};
    imgCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCi.imageType = VK_IMAGE_TYPE_2D;
    imgCi.extent = {resolution, resolution, 1};
    imgCi.mipLevels = 1;
    imgCi.arrayLayers = kCascadeCount;
    imgCi.format = VK_FORMAT_R32_SFLOAT;
    imgCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCi.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCi.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_device, &imgCi, nullptr, &m_depthImage) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to create cascade image array\n");
        return false;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &mai, nullptr, &m_depthMemory) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to allocate cascade image memory\n");
        return false;
    }
    vkBindImageMemory(m_device, m_depthImage, m_depthMemory, 0);

    // Array view (all layers), for sampling in the lighting shader.
    VkImageViewCreateInfo arrayViewCi{};
    arrayViewCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayViewCi.image = m_depthImage;
    arrayViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayViewCi.format = VK_FORMAT_R32_SFLOAT;
    arrayViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    arrayViewCi.subresourceRange.levelCount = 1;
    arrayViewCi.subresourceRange.layerCount = kCascadeCount;
    if (vkCreateImageView(m_device, &arrayViewCi, nullptr, &m_arrayView) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to create cascade array view\n");
        return false;
    }

    // Transient depth buffer, single layer, reused across all cascade passes
    // (each cascade renders sequentially, so hardware depth-testing scratch
    // space can be shared instead of allocated per layer).
    VkImage transientDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory transientDepthMemory = VK_NULL_HANDLE;
    VkImageView transientDepthView = VK_NULL_HANDLE;
    {
        VkImageCreateInfo dCi = imgCi;
        dCi.arrayLayers = 1;
        dCi.format = VK_FORMAT_D32_SFLOAT;
        dCi.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (vkCreateImage(m_device, &dCi, nullptr, &transientDepthImage) != VK_SUCCESS) return false;

        VkMemoryRequirements dmr;
        vkGetImageMemoryRequirements(m_device, transientDepthImage, &dmr);
        VkMemoryAllocateInfo dmai{};
        dmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        dmai.allocationSize = dmr.size;
        dmai.memoryTypeIndex = findMemoryType(m_physicalDevice, dmr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &dmai, nullptr, &transientDepthMemory) != VK_SUCCESS) return false;
        vkBindImageMemory(m_device, transientDepthImage, transientDepthMemory, 0);

        VkImageViewCreateInfo dvCi{};
        dvCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dvCi.image = transientDepthImage;
        dvCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dvCi.format = VK_FORMAT_D32_SFLOAT;
        dvCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        dvCi.subresourceRange.levelCount = 1;
        dvCi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &dvCi, nullptr, &transientDepthView) != VK_SUCCESS) return false;
    }
    // Leaked deliberately-simple: these two handles are captured by every
    // per-cascade framebuffer below and torn down once, in shutdown(), via
    // m_depthImage's sibling members added for that purpose.
    m_transientDepthImage = transientDepthImage;
    m_transientDepthMemory = transientDepthMemory;
    m_transientDepthView = transientDepthView;

    // Per-layer views + framebuffers (one per cascade).
    for (int i = 0; i < kCascadeCount; ++i) {
        VkImageViewCreateInfo layerViewCi{};
        layerViewCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        layerViewCi.image = m_depthImage;
        layerViewCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewCi.format = VK_FORMAT_R32_SFLOAT;
        layerViewCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        layerViewCi.subresourceRange.baseArrayLayer = static_cast<uint32_t>(i);
        layerViewCi.subresourceRange.layerCount = 1;
        layerViewCi.subresourceRange.levelCount = 1;
        if (vkCreateImageView(m_device, &layerViewCi, nullptr, &m_layerViews[static_cast<size_t>(i)]) != VK_SUCCESS) {
            fprintf(stderr, "[ShadowSystem] failed to create cascade layer view %d\n", i);
            return false;
        }

        VkImageView fbAtts[2] = {m_layerViews[static_cast<size_t>(i)], transientDepthView};
        VkFramebufferCreateInfo fbCi{};
        fbCi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCi.renderPass = m_renderPass;
        fbCi.attachmentCount = 2;
        fbCi.pAttachments = fbAtts;
        fbCi.width = resolution;
        fbCi.height = resolution;
        fbCi.layers = 1;
        if (vkCreateFramebuffer(m_device, &fbCi, nullptr, &m_framebuffers[static_cast<size_t>(i)]) != VK_SUCCESS) {
            fprintf(stderr, "[ShadowSystem] failed to create cascade framebuffer %d\n", i);
            return false;
        }
    }

    VkSamplerCreateInfo sCi{};
    sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sCi.magFilter = VK_FILTER_LINEAR;
    sCi.minFilter = VK_FILTER_LINEAR;
    sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sCi.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside cascade => far depth => unshadowed
    sCi.maxLod = 1.0f;
    if (vkCreateSampler(m_device, &sCi, nullptr, &m_sampler) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to create shadow sampler\n");
        return false;
    }

    // ---- Pipeline: loads precompiled SPIR-V only, never invokes a shader
    // compiler at runtime (SimulatorApp must not depend on one). ----
    m_vertModule = createShaderModule(m_device, readSpirvFile(shaderDir + "/ksShadow.vert.spv"));
    m_fragModule = createShaderModule(m_device, readSpirvFile(shaderDir + "/ksShadow.frag.spv"));
    if (!m_vertModule || !m_fragModule) {
        fprintf(stderr, "[ShadowSystem] failed to load ksShadow.{vert,frag}.spv from %s "
                        "(precompile shaders at build time; this runtime never shells out to a compiler)\n",
                shaderDir.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_fragModule;
    stages[1].pName = "main";

    // Position-only, matching ksShadow.vert's single `in vec3 inPosition`.
    // Stride matches ks::VulkanRenderer::Vertex (position, normal, uv, color
    // = 3+3+2+4 floats) so the same mesh vertex buffers used for the main
    // geometry pass can be bound here unmodified.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 12;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    // Front-face (not back-face) culling for the shadow pass is a standard
    // trick to reduce peter-panning/self-shadowing acne on thin casters;
    // depthBias further pushes the recorded depth away from the surface.
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    blendAtt.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAtt;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 16 * 2; // lightSpaceMatrix + modelMatrix

    VkPipelineLayoutCreateInfo layoutCi{};
    layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCi.pushConstantRangeCount = 1;
    layoutCi.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to create pipeline layout\n");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeCi{};
    pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeCi.stageCount = 2;
    pipeCi.pStages = stages;
    pipeCi.pVertexInputState = &vertexInput;
    pipeCi.pInputAssemblyState = &inputAssembly;
    pipeCi.pViewportState = &viewportState;
    pipeCi.pRasterizationState = &rasterizer;
    pipeCi.pMultisampleState = &multisampling;
    pipeCi.pColorBlendState = &colorBlend;
    pipeCi.pDepthStencilState = &depthStencil;
    pipeCi.pDynamicState = &dynState;
    pipeCi.layout = m_pipelineLayout;
    pipeCi.renderPass = m_renderPass;
    pipeCi.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_pipeline) != VK_SUCCESS) {
        fprintf(stderr, "[ShadowSystem] failed to create shadow pipeline\n");
        return false;
    }

    return true;
}

void CascadedShadowMap::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_vertModule) vkDestroyShaderModule(m_device, m_vertModule, nullptr);
    if (m_fragModule) vkDestroyShaderModule(m_device, m_fragModule, nullptr);
    for (int i = 0; i < kCascadeCount; ++i) {
        if (m_framebuffers[static_cast<size_t>(i)]) vkDestroyFramebuffer(m_device, m_framebuffers[static_cast<size_t>(i)], nullptr);
        if (m_layerViews[static_cast<size_t>(i)]) vkDestroyImageView(m_device, m_layerViews[static_cast<size_t>(i)], nullptr);
    }
    if (m_transientDepthView) vkDestroyImageView(m_device, m_transientDepthView, nullptr);
    if (m_transientDepthImage) vkDestroyImage(m_device, m_transientDepthImage, nullptr);
    if (m_transientDepthMemory) vkFreeMemory(m_device, m_transientDepthMemory, nullptr);
    if (m_arrayView) vkDestroyImageView(m_device, m_arrayView, nullptr);
    if (m_depthImage) vkDestroyImage(m_device, m_depthImage, nullptr);
    if (m_depthMemory) vkFreeMemory(m_device, m_depthMemory, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    m_physicalDevice = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_depthImage = VK_NULL_HANDLE;
    m_depthMemory = VK_NULL_HANDLE;
    m_arrayView = VK_NULL_HANDLE;
    m_layerViews.fill(VK_NULL_HANDLE);
    m_framebuffers.fill(VK_NULL_HANDLE);
    m_renderPass = VK_NULL_HANDLE;
    m_sampler = VK_NULL_HANDLE;
    m_transientDepthImage = VK_NULL_HANDLE;
    m_transientDepthMemory = VK_NULL_HANDLE;
    m_transientDepthView = VK_NULL_HANDLE;
    m_vertModule = VK_NULL_HANDLE;
    m_fragModule = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}

void CascadedShadowMap::update(const mat4& camView, const mat4& camProj, float camNear, float camFar, vec3 sunDirectionWS) {
    vec3 sunDir = sunDirectionWS.normalized();
    if (sunDir.length() < 0.5f) sunDir = vec3(0.0f, -1.0f, 0.0f); // degenerate input guard

    mat4 invViewProj = (camProj * camView).inverse();

    // Unproject the full frustum's near/far corner rings once. Depth is
    // Vulkan clip space (z in [0,1]), matching mat4::perspective/ortho — the
    // near ring sits at z=0, not the OpenGL z=-1.
    vec3 ndcNear[4] = { {-1,-1, 0}, {1,-1, 0}, {1,1, 0}, {-1,1, 0} };
    vec3 ndcFar[4]  = { {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1} };
    vec3 worldNear[4], worldFar[4];
    for (int i = 0; i < 4; ++i) {
        vec4 pn = invViewProj * vec4(ndcNear[i], 1.0f);
        vec4 pf = invViewProj * vec4(ndcFar[i], 1.0f);
        worldNear[i] = vec3(pn.x / pn.w, pn.y / pn.w, pn.z / pn.w);
        worldFar[i]  = vec3(pf.x / pf.w, pf.y / pf.w, pf.z / pf.w);
    }

    vec3 up(0.0f, 1.0f, 0.0f);
    if (std::fabs(sunDir.dot(up)) > 0.99f) up = vec3(0.0f, 0.0f, 1.0f); // avoid a degenerate lookAt when the sun is near-vertical

    float prevSplit = camNear;
    for (int c = 0; c < kCascadeCount; ++c) {
        float splitDist = camNear + (camFar - camNear) * m_splitFractions[static_cast<size_t>(c)];

        float tNear = (prevSplit - camNear) / (camFar - camNear);
        float tFar = (splitDist - camNear) / (camFar - camNear);

        vec3 corners[8];
        for (int i = 0; i < 4; ++i) {
            corners[i]     = worldNear[i] + (worldFar[i] - worldNear[i]) * tNear;
            corners[i + 4] = worldNear[i] + (worldFar[i] - worldNear[i]) * tFar;
        }

        vec3 center{};
        for (auto& p : corners) center += p * (1.0f / 8.0f);

        float radius = 0.0f;
        for (auto& p : corners) {
            float d = (p - center).length();
            if (d > radius) radius = d;
        }
        radius = std::ceil(radius * 16.0f) / 16.0f; // quantize to reduce shimmer as the camera moves

        mat4 lightView = mat4::lookAt(center - sunDir * (radius * 2.0f), center, up);

        vec3 minE(FLT_MAX, FLT_MAX, FLT_MAX), maxE(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (auto& p : corners) {
            vec3 lp = lightView * p;
            minE.x = std::min(minE.x, lp.x); maxE.x = std::max(maxE.x, lp.x);
            minE.y = std::min(minE.y, lp.y); maxE.y = std::max(maxE.y, lp.y);
            minE.z = std::min(minE.z, lp.z); maxE.z = std::max(maxE.z, lp.z);
        }

        float zPad = (maxE.z - minE.z) * 0.5f + 5.0f; // headroom for casters just outside the tight fit
        float zNear = std::max(-maxE.z - zPad, 0.05f);
        float zFar = -minE.z + zPad;

        mat4 lightProj = mat4::ortho(minE.x, maxE.x, minE.y, maxE.y, zNear, zFar);

        m_cascades[static_cast<size_t>(c)].viewProj = lightProj * lightView;
        m_cascades[static_cast<size_t>(c)].splitDepth = splitDist;

        prevSplit = splitDist;
    }
}

void CascadedShadowMap::beginCascadePass(VkCommandBuffer cmd, int cascadeIndex) const {
    if (cascadeIndex < 0 || cascadeIndex >= kCascadeCount) return;

    VkClearValue clears[2];
    clears[0].color = {{1.0f, 1.0f, 1.0f, 1.0f}}; // far depth outside any drawn geometry => unshadowed
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_framebuffers[static_cast<size_t>(cascadeIndex)];
    rpBegin.renderArea.extent = {m_resolution, m_resolution};
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0, 0, float(m_resolution), float(m_resolution), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {m_resolution, m_resolution}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void CascadedShadowMap::endCascadePass(VkCommandBuffer cmd) const {
    vkCmdEndRenderPass(cmd);
}

void CascadedShadowMap::pushLightSpaceMatrix(VkCommandBuffer cmd, int cascadeIndex, const mat4& modelMatrix) const {
    if (cascadeIndex < 0 || cascadeIndex >= kCascadeCount) return;
    float data[32];
    std::memcpy(data, m_cascades[static_cast<size_t>(cascadeIndex)].viewProj.data(), sizeof(float) * 16);
    std::memcpy(data + 16, modelMatrix.data(), sizeof(float) * 16);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(data), data);
}

} // namespace ks::sim
