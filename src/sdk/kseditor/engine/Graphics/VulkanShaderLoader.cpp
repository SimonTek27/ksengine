#include "VulkanShaderLoader.h"
#include "VulkanFunctions.h"
#include "ShaderParamRegistry.h"
#include "../Config/ConfigLoader.h"
#include "../Config/PPFilterPreset.h"
#include <QFile>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QDir>
#include <stdexcept>

namespace {

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    ks::g_vk.getPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}
}

namespace ks {

void ShaderModule::createFromGLSL(const QString& glsl, VkShaderStageFlagBits stage) {
    m_glslSource = glsl;
    m_stage = stage;

    // Attempt GLSL-to-SPIR-V compilation via glslangValidator
    QByteArray glslData = glsl.toUtf8();

    // Write temporary GLSL file
    QString tmpPath = QDir::tempPath() + "/ks_shader_" + QString::number(reinterpret_cast<quintptr>(this), 16) + ".glsl";
    {
        QFile f(tmpPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(glslData);
        }
    }

    QProcess glslang;
    QStringList args;
    args << "-V" << tmpPath;

    // Add stage-specific flags
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:   args << "-S" << "vert"; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT: args << "-S" << "frag"; break;
        case VK_SHADER_STAGE_COMPUTE_BIT:  args << "-S" << "comp"; break;
        default: break;
    }

    glslang.start("glslangValidator", args);
    if (glslang.waitForFinished(30000) && glslang.exitCode() == 0) {
        // Read compiled SPIR-V
        QString spvPath = tmpPath + ".spv";
        QFile spvFile(spvPath);
        if (spvFile.open(QIODevice::ReadOnly)) {
            QByteArray spirv = spvFile.readAll();
            spvFile.close();
            QFile::remove(spvPath);
            QFile::remove(tmpPath);

            if (!spirv.isEmpty()) {
                createFromSPIRV(spirv);
                return;
            }
        }
    }

    QFile::remove(tmpPath);

    throw std::runtime_error(
        "glslangValidator not available - cannot compile shader from GLSL source. "
        "Install Vulkan SDK or provide pre-compiled SPIR-V files.");
}

void ShaderModule::createFromSPIRV(const QByteArray& spirv) {
    if (spirv.isEmpty()) {
        throw std::runtime_error("Empty SPIR-V data");
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = static_cast<uint32_t>(spirv.size());
    createInfo.pCode = reinterpret_cast<const uint32_t*>(spirv.constData());

    VkResult result = g_vk.createShaderModule(m_device, &createInfo, nullptr, &m_module);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + std::to_string(result));
    }
}

void ShaderModule::createFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Failed to open shader file: " + path.toStdString());
    }
    QByteArray data = file.readAll();
    file.close();

    // If it's .glsl or .vert/.frag, compile from source
    if (path.endsWith(".glsl") || path.endsWith(".vert") || path.endsWith(".frag") || path.endsWith(".comp")) {
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_ALL;
        if (path.endsWith(".vert")) stage = VK_SHADER_STAGE_VERTEX_BIT;
        else if (path.endsWith(".frag")) stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        else if (path.endsWith(".comp")) stage = VK_SHADER_STAGE_COMPUTE_BIT;

        createFromGLSL(QString::fromUtf8(data), stage);
        return;
    }

    // Check if it's JSON (SPIR-V cross-compiler output) or raw SPIR-V
    if (data.startsWith("{")) {
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QByteArray spirv = QByteArray::fromBase64(doc.object().value("spirv").toString().toLatin1());
            createFromSPIRV(spirv);
        }
    } else {
        createFromSPIRV(data);
    }
}

void ShaderModule::destroy() {
    if (m_module != VK_NULL_HANDLE) {
        g_vk.destroyShaderModule(m_device, m_module, nullptr);
        m_module = VK_NULL_HANDLE;
    }
}

// ============== UniformBuffer ==============

void UniformBuffer::create(VkDeviceSize size, VkBufferUsageFlags usage) {
    m_size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = g_vk.createBuffer(m_device, &bufferInfo, nullptr, &m_buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create uniform buffer: " + std::to_string(result));
    }

    VkMemoryRequirements memRequirements;
    g_vk.getBufferMemoryRequirements(m_device, m_buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = ::findMemoryType(m_physicalDevice, memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    result = g_vk.allocateMemory(m_device, &allocInfo, nullptr, &m_memory);
    if (result != VK_SUCCESS) {
        g_vk.destroyBuffer(m_device, m_buffer, nullptr);
        throw std::runtime_error("Failed to allocate uniform buffer memory: " + std::to_string(result));
    }

    g_vk.bindBufferMemory(m_device, m_buffer, m_memory, 0);
}

void UniformBuffer::destroy() {
    if (m_mapped) {
        g_vk.unmapMemory(m_device, m_memory);
        m_mapped = nullptr;
    }
    if (m_memory != VK_NULL_HANDLE) {
        g_vk.freeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
    if (m_buffer != VK_NULL_HANDLE) {
        g_vk.destroyBuffer(m_device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    m_size = 0;
}

void* UniformBuffer::map() {
    if (!m_mapped) {
        VkResult result = g_vk.mapMemory(m_device, m_memory, 0, m_size, 0, &m_mapped);
        if (result != VK_SUCCESS) {
            qWarning() << "Failed to map uniform buffer memory:" << result;
            return nullptr;
        }
    }
    return m_mapped;
}

void UniformBuffer::unmap() {
    if (m_mapped) {
        g_vk.unmapMemory(m_device, m_memory);
        m_mapped = nullptr;
    }
}

// ============== VulkanShaderLoader ==============

VulkanShaderLoader::VulkanShaderLoader(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue)
    : m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_graphicsQueue(graphicsQueue)
{
    // Create uniform buffers
    m_cameraUBO = std::make_unique<UniformBuffer>(physicalDevice, device);
    m_materialUBO = std::make_unique<UniformBuffer>(physicalDevice, device);
    m_lightingUBO = std::make_unique<UniformBuffer>(physicalDevice, device);

    // Initialize with default sizes
    m_cameraUBO->create(sizeof(CameraUBO));
    m_materialUBO->create(sizeof(MaterialUBO));
    m_lightingUBO->create(sizeof(LightingUBO));
}

VulkanShaderLoader::~VulkanShaderLoader() {
    cleanup();
}

void VulkanShaderLoader::cleanup() {
    for (auto it = m_pipelines.begin(); it != m_pipelines.end(); ++it) {
        if (it.value() != VK_NULL_HANDLE) {
            g_vk.destroyPipeline(m_device, it.value(), nullptr);
        }
    }
    m_pipelines.clear();

    for (auto it = m_pipelineLayouts.begin(); it != m_pipelineLayouts.end(); ++it) {
        if (it.value() != VK_NULL_HANDLE) {
            g_vk.destroyPipelineLayout(m_device, it.value(), nullptr);
        }
    }
    m_pipelineLayouts.clear();

    for (auto it = m_descriptorSets.begin(); it != m_descriptorSets.end(); ++it) {
        VkDescriptorSet set = it.value();
        if (set != VK_NULL_HANDLE) {
            g_vk.freeDescriptorSets(m_device, m_descriptorPool, 1, &set);
        }
    }
    m_descriptorSets.clear();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        g_vk.destroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    for (auto it = m_descriptorSetLayouts.begin(); it != m_descriptorSetLayouts.end(); ++it) {
        if (it.value() != VK_NULL_HANDLE) {
            g_vk.destroyDescriptorSetLayout(m_device, it.value(), nullptr);
        }
    }
    m_descriptorSetLayouts.clear();

    for (auto it = m_samplers.begin(); it != m_samplers.end(); ++it) {
        if (it.value() != VK_NULL_HANDLE) {
            g_vk.destroySampler(m_device, it.value(), nullptr);
        }
    }
    m_samplers.clear();

    m_cameraUBO.reset();
    m_materialUBO.reset();
    m_lightingUBO.reset();
    m_storageBuffers.clear();

    for (auto it = m_shaderModules.begin(); it != m_shaderModules.end(); ++it) {
        delete it.value();
    }
    m_shaderModules.clear();
}

bool VulkanShaderLoader::loadShader(const QString& name, VkShaderStageFlagBits stage, const QString& path) {
    try {
        ShaderModule* module = new ShaderModule(m_device);
        if (path.endsWith(".vert")) {
            module->m_stage = VK_SHADER_STAGE_VERTEX_BIT;
            module->createFromFile(path);
        } else if (path.endsWith(".frag")) {
            module->m_stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            module->createFromFile(path);
        } else if (path.endsWith(".comp")) {
            module->m_stage = VK_SHADER_STAGE_COMPUTE_BIT;
            module->createFromFile(path);
        } else {
            module->m_stage = stage;
            module->createFromFile(path);
        }
        m_shaderModules[name] = module;
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Failed to load shader" << name << ":" << e.what();
        return false;
    }
}

bool VulkanShaderLoader::allocateDescriptorSets(const QString& layoutName, const QString& setName) {
    auto layoutIt = m_descriptorSetLayouts.find(layoutName);
    if (layoutIt == m_descriptorSetLayouts.end()) {
        qWarning() << "Descriptor set layout not found:" << layoutName;
        return false;
    }

    if (m_descriptorPool == VK_NULL_HANDLE) {
        if (!createDescriptorPool()) {
            return false;
        }
    }

    VkDescriptorSetLayout layout = layoutIt.value();
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    VkResult result = g_vk.allocateDescriptorSets(m_device, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to allocate descriptor set for" << setName;
        return false;
    }

    m_descriptorSets[setName] = descriptorSet;
    return true;
}

bool VulkanShaderLoader::writeDescriptorSets(const QString& setName,
                                              const QVector<VkDescriptorBufferInfo>& bufferInfos,
                                              const QVector<VkDescriptorImageInfo>& imageInfos) {
    auto it = m_descriptorSets.find(setName);
    if (it == m_descriptorSets.end()) {
        qWarning() << "Descriptor set not found:" << setName;
        return false;
    }

    VkDescriptorSet descriptorSet = it.value();
    QVector<VkWriteDescriptorSet> writes;

    for (int i = 0; i < bufferInfos.size(); ++i) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = static_cast<uint32_t>(i);
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfos[i];
        writes.append(write);
    }

    for (int i = 0; i < imageInfos.size(); ++i) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = static_cast<uint32_t>(bufferInfos.size() + i);
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[i];
        writes.append(write);
    }

    if (!writes.isEmpty()) {
        g_vk.updateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.constData(), 0, nullptr);
    }

    return true;
}

bool VulkanShaderLoader::createStorageBuffer(const QString& name, VkDeviceSize size, VkBufferUsageFlags extraUsage) {
	if (size == 0 || m_device == VK_NULL_HANDLE) return false;

	// Remove existing buffer with same name
	auto it = m_storageBuffers.find(name);
	if (it != m_storageBuffers.end()) {
		m_storageBuffers.erase(it);
	}

	auto buffer = std::make_unique<UniformBuffer>(m_physicalDevice, m_device);
	try {
		buffer->create(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | extraUsage);
		m_storageBuffers[name] = std::move(buffer);
		return true;
	} catch (const std::exception& e) {
		qWarning() << "Failed to create storage buffer" << name << ":" << e.what();
		return false;
	}
}

bool VulkanShaderLoader::createSampler(const QString& name, VkFilter magFilter, VkFilter minFilter, VkSamplerAddressMode addressMode) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = minFilter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler sampler;
    VkResult result = g_vk.createSampler(m_device, &samplerInfo, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create sampler:" << name;
        return false;
    }

    m_samplers[name] = sampler;
    return true;
}

bool VulkanShaderLoader::createPipeline(const QString& name,
                                        const QVector<VkPipelineShaderStageCreateInfo>& stages,
                                        const VkPipelineVertexInputStateCreateInfo& vertexInput,
                                        const VkPipelineInputAssemblyStateCreateInfo& inputAssembly,
                                        const VkPipelineViewportStateCreateInfo& viewportState,
                                        const VkPipelineRasterizationStateCreateInfo& rasterizer,
                                        const VkPipelineMultisampleStateCreateInfo& multisampling,
                                        const VkPipelineColorBlendStateCreateInfo& colorBlend,
                                        const VkPipelineDepthStencilStateCreateInfo& depthStencil,
                                        VkRenderPass renderPass)
{
    // Wrap QRenderPass to VkRenderPass if needed
    VkRenderPass vkRenderPass = static_cast<VkRenderPass>(renderPass);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // Use descriptor set layout if available
    auto it = m_descriptorSetLayouts.find(name);
    if (it != m_descriptorSetLayouts.end()) {
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &it.value();
    }

    VkPipelineLayout pipelineLayout;
    VkResult result = g_vk.createPipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create pipeline layout for" << name;
        return false;
    }
    m_pipelineLayouts[name] = pipelineLayout;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.constData();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = vkRenderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    result = g_vk.createGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create pipeline for" << name;
        g_vk.destroyPipelineLayout(m_device, pipelineLayout, nullptr);
        return false;
    }

    m_pipelines[name] = pipeline;
    return true;
}

bool VulkanShaderLoader::createDescriptorSetLayout(const QString& name) {
    // Standard AC shader descriptor set layout
    // Set 0: Camera (view, projection, etc.)
    // Set 1: Material (albedo, normal, etc.)
    // Set 2: Lighting (lights array)

    QVector<VkDescriptorSetLayoutBinding> bindings;

    // Camera UBO
    VkDescriptorSetLayoutBinding camBinding{};
    camBinding.binding = 0;
    camBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camBinding.descriptorCount = 1;
    camBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.append(camBinding);

    // Material UBO
    VkDescriptorSetLayoutBinding matBinding{};
    matBinding.binding = 1;
    matBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matBinding.descriptorCount = 1;
    matBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.append(matBinding);

    // Lighting UBO
    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 2;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.append(lightBinding);

    // Texture samplers (up to 8)
    for (int i = 0; i < 8; ++i) {
        VkDescriptorSetLayoutBinding texBinding{};
        texBinding.binding = 3 + i;
        texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBinding.descriptorCount = 1;
        texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.append(texBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.constData();

    VkDescriptorSetLayout layout;
    VkResult result = g_vk.createDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create descriptor set layout for" << name;
        return false;
    }

    m_descriptorSetLayouts[name] = layout;
    return true;
}

bool VulkanShaderLoader::createDescriptorPool() {
    QVector<VkDescriptorPoolSize> poolSizes;

    VkDescriptorPoolSize uboPool{};
    uboPool.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPool.descriptorCount = 100; // Enough for multiple descriptor sets
    poolSizes.append(uboPool);

    VkDescriptorPoolSize samplerPool{};
    samplerPool.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerPool.descriptorCount = 100;
    poolSizes.append(samplerPool);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.constData();
    poolInfo.maxSets = 50;

    VkResult result = g_vk.createDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);
    if (result != VK_SUCCESS) {
        qWarning() << "Failed to create descriptor pool";
        return false;
    }

    return true;
}

void VulkanShaderLoader::updateCameraUBO(const QMatrix4x4& view, const QMatrix4x4& proj,
                                          const QVector3D& cameraPos, float nearPlane, float farPlane) {
    CameraUBO ubo{};
    ubo.view = view;
    ubo.projection = proj;
    ubo.viewProjection = proj * view;
    ubo.cameraPosition = QVector4D(cameraPos, 1.0f);
    ubo.nearPlane = nearPlane;
    ubo.farPlane = farPlane;

    void* data = m_cameraUBO->map();
    if (!data) return;
    memcpy(data, &ubo, sizeof(CameraUBO));
    m_cameraUBO->unmap();
}

void VulkanShaderLoader::updateMaterialUBO(const MaterialParams& params) {
    MaterialUBO ubo{};
    ubo.albedo = params.albedo;
    ubo.normalScale = params.normalScale;
    ubo.metallic = params.metallic;
    ubo.roughness = params.roughness;
    ubo.emissiveColor = params.emissiveColor;
    ubo.emissiveIntensity = params.emissiveIntensity;
    ubo.uvScale = params.uvScale;
    ubo.uvOffset = params.uvOffset;

    void* data = m_materialUBO->map();
    if (!data) return;
    memcpy(data, &ubo, sizeof(MaterialUBO));
    m_materialUBO->unmap();
}

void VulkanShaderLoader::updateLightingUBO(const LightingUBO& lighting) {
    LightingUBO ubo = lighting;

    void* data = m_lightingUBO->map();
    if (!data) return;
    memcpy(data, &ubo, sizeof(LightingUBO));
    m_lightingUBO->unmap();
}

void VulkanShaderLoader::applyLightingFromConfig(const LightingSettings& settings) {
    LightingUBO lighting{};

    // Apply AC lighting settings
    lighting.ambientColor = QVector3D(settings.ambientColor[0],
                                       settings.ambientColor[1],
                                       settings.ambientColor[2]);

    // Convert from AC's float-based intensity to our HDR format
    float intensity = settings.ambientIntensity / 100.0f;
    lighting.ambientColor *= intensity;

    // Sun light
    QVector3D sunDir(settings.sunDirection[0], settings.sunDirection[1], settings.sunDirection[2]);
    sunDir.normalize();
    lighting.sunDirection = sunDir;
    lighting.sunColor = QVector3D(settings.sunColor[0],
                                   settings.sunColor[1],
                                   settings.sunColor[2]) * (settings.sunIntensity / 100.0f);

    // Enable HDR
    lighting.exposure = settings.hdrExposure;
    lighting.toneMapping = static_cast<int>(settings.toneMapping);

    updateLightingUBO(lighting);
}

void VulkanShaderLoader::applyGraphicsFromConfig(const GraphicsSettings& settings) {
    ShaderParamRegistry::instance().setParam(QString(), "MIP_LOD_BIAS", settings.mipLodBias);
    ShaderParamRegistry::instance().setParam(QString(), "SHADOW_BIAS", settings.shadowMapBias);
}

void VulkanShaderLoader::beginRenderPass(VkCommandBuffer cmdBuffer, VkRenderPass renderPass,
                                          VkFramebuffer framebuffer, const QSize& size) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {static_cast<uint32_t>(size.width()),
                                        static_cast<uint32_t>(size.height())};

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkClearValue clearDepth = {{{1.0f, 0}}};
    VkClearValue clearValues[] = {clearColor, clearDepth};

    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    g_vk.cmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanShaderLoader::endRenderPass(VkCommandBuffer cmdBuffer) {
    g_vk.cmdEndRenderPass(cmdBuffer);
}

void VulkanShaderLoader::bindPipeline(VkCommandBuffer cmdBuffer, const QString& name) {
    auto it = m_pipelines.find(name);
    if (it != m_pipelines.end()) {
        g_vk.cmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, it.value());
    }
}

void VulkanShaderLoader::bindDescriptorSets(VkCommandBuffer cmdBuffer, const QString& pipelineName,
                                             VkCommandBuffer* cmdBufferRef) {
    auto it = m_descriptorSets.find(pipelineName);
    if (it != m_descriptorSets.end()) {
        auto layoutIt = m_pipelineLayouts.find(pipelineName);
        if (layoutIt != m_pipelineLayouts.end()) {
            VkDescriptorSet set = it.value();
            g_vk.cmdBindDescriptorSets(cmdBufferRef ? *cmdBufferRef : cmdBuffer,
                                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     layoutIt.value(), 0, 1, &set,
                                     0, nullptr);
        }
    }
}

VkDescriptorSetLayout VulkanShaderLoader::descriptorSetLayout(const QString& name) const {
    auto it = m_descriptorSetLayouts.find(name);
    return it != m_descriptorSetLayouts.end() ? it.value() : VK_NULL_HANDLE;
}

VkPipeline VulkanShaderLoader::pipeline(const QString& name) const {
    auto it = m_pipelines.find(name);
    return it != m_pipelines.end() ? it.value() : VK_NULL_HANDLE;
}

// ============== PPFilterRenderer ==============

PPFilterRenderer::PPFilterRenderer(VkPhysicalDevice physicalDevice, VkDevice device,
                                   VkQueue graphicsQueue, VkCommandPool commandPool)
    : VulkanShaderLoader(physicalDevice, device, graphicsQueue)
    , m_commandPool(commandPool)
    , m_ppParamsUBO(std::make_unique<UniformBuffer>(physicalDevice, device))
{
    m_ppParamsUBO->create(sizeof(PPParamsUBO));
}

PPFilterRenderer::~PPFilterRenderer() {
    m_ppParamsUBO.reset();
}

bool PPFilterRenderer::initialize(const QString& shaderDir, VkRenderPass renderPass,
                                  const VkPipelineVertexInputStateCreateInfo& vertexInput) {
    bool success = true;

    // Load post-processing shaders
    success &= loadShader("pp_vertex", VK_SHADER_STAGE_VERTEX_BIT,
                          shaderDir + "/ksPostProcess.vert.spv");
    success &= loadShader("pp_tonemapping", VK_SHADER_STAGE_FRAGMENT_BIT,
                          shaderDir + "/ksPostProcess.frag.spv");

    if (!success) {
        qWarning() << "Failed to load post-processing shaders";
        return false;
    }

    // Create descriptor set layout for PP (camera UBO + PP params UBO)
    success &= createDescriptorSetLayout("pp_pipeline");

    if (!success) return false;

    // Create descriptor pool if needed
    if (m_descriptorPool == VK_NULL_HANDLE) {
        success &= createDescriptorPool();
        if (!success) return false;
    }

    // Allocate descriptor set
    success &= allocateDescriptorSets("pp_pipeline", "pp_set");
    if (!success) return false;

    // Write descriptor set with camera and PP params UBOs
    VkDescriptorBufferInfo camBufInfo{};
    camBufInfo.buffer = m_cameraUBO->m_buffer;
    camBufInfo.range = sizeof(CameraUBO);

    VkDescriptorBufferInfo ppBufInfo{};
    ppBufInfo.buffer = m_ppParamsUBO->m_buffer;
    ppBufInfo.range = sizeof(PPParamsUBO);

    QVector<VkDescriptorBufferInfo> bufInfos = {camBufInfo, ppBufInfo};
    success &= writeDescriptorSets("pp_set", bufInfos);

    // Create pipeline stages
    auto vertIt = m_shaderModules.find("pp_vertex");
    auto fragIt = m_shaderModules.find("pp_tonemapping");
    if (vertIt == m_shaderModules.end() || fragIt == m_shaderModules.end()) {
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertIt.value()->m_module;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragIt.value()->m_module;
    fragStage.pName = "main";

    QVector<VkPipelineShaderStageCreateInfo> stages = {vertStage, fragStage};

    // Pipeline state for fullscreen quad
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    success = createPipeline("pp_pipeline", stages, vertexInput, inputAssembly,
                             viewportState, rasterizer, multisampling, colorBlend,
                             depthStencil, renderPass);

    return success;
}

void PPFilterRenderer::updateFromPreset(const PPFilterPreset& preset) {
    PPParamsUBO params{};
    params.exposure = preset.toneMapping().exposure;
    params.gamma = preset.toneMapping().gamma;
    params.toneMappingFunc = preset.toneMapping().function;
    params.saturation = preset.colorGrading().saturation;
    params.brightness = preset.colorGrading().brightness;
    params.contrast = preset.colorGrading().contrast;
    params.vignetteStrength = preset.vignetting().strength;
    params.chromaticAberration = preset.chromaticAberration().lateralDispersionX;

    void* data = m_ppParamsUBO->map();
    if (data) {
        memcpy(data, &params, sizeof(PPParamsUBO));
        m_ppParamsUBO->unmap();
    }
}

} // namespace ks
