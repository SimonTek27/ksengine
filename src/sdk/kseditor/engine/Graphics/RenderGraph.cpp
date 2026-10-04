#include "RenderGraph.h"
#include "VulkanRenderer.h"
#include "VulkanFunctions.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QSet>
#include <QQueue>
#include <QDebug>
#include <QImage>
#include <QFile>
#include <cmath>
#include <cstring>

namespace ks {

// â”€â”€ RenderGraph Implementation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

RenderGraph::RenderGraph(VulkanRenderer* renderer, QObject* parent)
    : QObject(parent), m_renderer(renderer)
{
}

RenderGraph::~RenderGraph()
{
    clear();
}

void RenderGraph::addResource(const Resource& resource)
{
    m_frameData.resources[resource.name] = resource;
}

void RenderGraph::addPass(const Pass& pass)
{
    m_frameData.passes.append(pass);
    
    // Track dependencies
    for (const auto& output : pass.outputs) {
        m_resourceProducers[output].append(pass.name);
    }
    for (const auto& input : pass.inputs) {
        m_resourceConsumers[input].append(pass.name);
    }
    if (!pass.depthStencil.isEmpty()) {
        m_resourceProducers[pass.depthStencil].append(pass.name);
        m_resourceConsumers[pass.depthStencil].append(pass.name);
    }
}

bool RenderGraph::compile()
{
    if (m_compiled) return true;
    
    buildExecutionOrder();
    allocateResources();
    createResourceViews();
    createFramebuffers();
    
    m_compiled = true;
    emit compiled();
    return true;
}

void RenderGraph::execute(VkCommandBuffer cmdBuffer)
{
    if (!m_compiled && !compile()) {
        emit error("Render graph not compiled");
        return;
    }
    
    for (const auto& passName : m_executionOrder) {
        auto it = std::find_if(m_frameData.passes.begin(), m_frameData.passes.end(),
                               [&passName](const Pass& p) { return p.name == passName; });
        if (it == m_frameData.passes.end()) continue;
        
        const Pass& pass = *it;
        
        if (pass.isCompute) {
            if (pass.computePipeline && pass.computeLayout) {
                g_vk.cmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                 pass.computePipeline);
                g_vk.cmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                       pass.computeLayout, 0,
                                                       pass.computeDescriptorSets.size(),
                                                       pass.computeDescriptorSets.constData(),
                                                       0, nullptr);
                g_vk.cmdDispatch(cmdBuffer,
                                             pass.computeDispatchSize.width,
                                             pass.computeDispatchSize.height,
                                             pass.computeDispatchSize.depth);
            }
        } else if (pass.execute) {
            QMap<QString, Resource*> resourcePtrs;
            for (auto it = m_frameData.resources.begin(); it != m_frameData.resources.end(); ++it) {
                resourcePtrs[it.key()] = &it.value();
            }
            pass.execute(cmdBuffer, resourcePtrs);
        }
    }
}

RenderGraph::Resource* RenderGraph::getResource(const QString& name)
{
    return m_frameData.resources.contains(name) ? &m_frameData.resources[name] : nullptr;
}

const RenderGraph::Resource* RenderGraph::getResource(const QString& name) const
{
    auto it = m_frameData.resources.constFind(name);
    return it != m_frameData.resources.constEnd() ? &it.value() : nullptr;
}

void RenderGraph::addColorTarget(const QString& name, VkFormat format, VkExtent2D extent,
                                 VkImageUsageFlags usage)
{
    Resource res;
    res.name = name;
    res.type = Resource::Type::Image;
    res.format = format;
    res.extent = extent;
    res.usage = usage;
    addResource(res);
}

void RenderGraph::addDepthTarget(const QString& name, VkExtent2D extent, VkFormat format)
{
    Resource res;
    res.name = name;
    res.type = Resource::Type::Image;
    res.format = format;
    res.extent = extent;
    res.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    addResource(res);
}

void RenderGraph::addSwapchainTarget(const QString& name, VkImage image, VkImageView view,
                                     VkFormat format, VkExtent2D extent)
{
    Resource res;
    res.name = name;
    res.type = Resource::Type::External;
    res.externalImage = image;
    res.externalView = view;
    res.format = format;
    res.extent = extent;
    addResource(res);
}

void RenderGraph::clear()
{
    // Cleanup Vulkan objects
    for (auto& fb : m_frameData.framebuffers) {
            if (fb && g_vk.destroyFramebuffer) {
                g_vk.destroyFramebuffer(m_renderer->device(), fb, nullptr);
            }
        }
        m_frameData.framebuffers.clear();
        
        for (auto& view : m_frameData.resourceViews) {
            if (view && g_vk.destroyImageView) {
                g_vk.destroyImageView(m_renderer->device(), view, nullptr);
        }
    }
    m_frameData.resourceViews.clear();
    
    m_frameData.resources.clear();
    m_frameData.passes.clear();
    m_resourceProducers.clear();
    m_resourceConsumers.clear();
    m_executionOrder.clear();
    m_compiled = false;
}

QJsonObject RenderGraph::toJson() const
{
    QJsonObject obj;
    obj["compiled"] = m_compiled;
    
    QJsonArray resources;
    for (auto it = m_frameData.resources.constBegin(); it != m_frameData.resources.constEnd(); ++it) {
        QJsonObject r;
        r["name"] = it.key();
        r["type"] = static_cast<int>(it->type);
        r["format"] = static_cast<int>(it->format);
        QJsonObject extentObj;
        extentObj["width"] = static_cast<qint64>(it->extent.width);
        extentObj["height"] = static_cast<qint64>(it->extent.height);
        r["extent"] = extentObj;
        r["persistent"] = it->persistent;
        resources.append(r);
    }
    obj["resources"] = resources;
    
    QJsonArray passes;
    for (const auto& pass : m_frameData.passes) {
        QJsonObject p;
        p["name"] = pass.name;
        p["inputs"] = QJsonArray::fromStringList(QStringList(pass.inputs.begin(), pass.inputs.end()));
        p["outputs"] = QJsonArray::fromStringList(QStringList(pass.outputs.begin(), pass.outputs.end()));
        p["isCompute"] = pass.isCompute;
        passes.append(p);
    }
    obj["passes"] = passes;
    
    obj["executionOrder"] = QJsonArray::fromStringList(QStringList(m_executionOrder.begin(), m_executionOrder.end()));
    
    return obj;
}

void RenderGraph::buildExecutionOrder()
{
    // Topological sort based on resource dependencies
    QMap<QString, int> inDegree;
    QMap<QString, QSet<QString>> adjacency;
    
    // Initialize
    for (const auto& pass : m_frameData.passes) {
        inDegree[pass.name] = 0;
    }
    
    // Build edges: pass A -> pass B if A produces a resource B consumes
    for (const auto& pass : m_frameData.passes) {
        QSet<QString> dependents;
        for (const auto& output : pass.outputs) {
            for (const auto& consumer : m_resourceConsumers[output]) {
                if (consumer != pass.name) dependents.insert(consumer);
            }
        }
        for (const auto& dep : dependents) {
            adjacency[pass.name].insert(dep);
            inDegree[dep]++;
        }
    }
    
    // Kahn's algorithm
    QQueue<QString> queue;
    for (auto it = inDegree.constBegin(); it != inDegree.constEnd(); ++it) {
        if (it.value() == 0) queue.enqueue(it.key());
    }
    
    while (!queue.isEmpty()) {
        QString current = queue.dequeue();
        m_executionOrder.append(current);
        
        for (const auto& next : adjacency[current]) {
            if (--inDegree[next] == 0) {
                queue.enqueue(next);
            }
        }
    }
    
    // Check for cycles
    if (m_executionOrder.size() != m_frameData.passes.size()) {
        emit error("Render graph has circular dependency!");
        // Fallback: use declaration order
        m_executionOrder.clear();
        for (const auto& pass : m_frameData.passes) {
            m_executionOrder.append(pass.name);
        }
    }
}

void RenderGraph::allocateResources()
{
    if (!m_renderer || !m_renderer->device()) return;
    
    for (auto& res : m_frameData.resources) {
        if (res.type == Resource::Type::External) continue; // Managed externally
        
        if (res.type == Resource::Type::Image) {
            // Create image and memory
            VkImageCreateInfo ici{};
            ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.format = res.format;
            ici.extent = {res.extent.width, res.extent.height, 1};
            ici.mipLevels = res.mipLevels;
            ici.arrayLayers = res.arrayLayers;
            ici.samples = res.samples;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.usage = res.usage;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            
            VkImage image = VK_NULL_HANDLE;
            if (g_vk.createImage(m_renderer->device(), &ici, nullptr, &image) == VK_SUCCESS) {
                VkMemoryRequirements memReqs;
                g_vk.getImageMemoryRequirements(m_renderer->device(), image, &memReqs);
                
                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = memReqs.size;
                ai.memoryTypeIndex = VulkanRenderer::findMemoryType(m_renderer->physicalDevice(),
                                                                     memReqs.memoryTypeBits,
                                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                
                VkDeviceMemory memory = VK_NULL_HANDLE;
                if (g_vk.allocateMemory(m_renderer->device(), &ai, nullptr, &memory) == VK_SUCCESS) {
                    g_vk.bindImageMemory(m_renderer->device(), image, memory, 0);
                    res.image = image;
                    res.memory = memory;
                } else {
                    g_vk.destroyImage(m_renderer->device(), image, nullptr);
                }
            }
        }
    }
}

void RenderGraph::createResourceViews()
{
    if (!m_renderer || !m_renderer->device()) return;
    
    for (auto& res : m_frameData.resources) {
        if (res.type == Resource::Type::External && res.externalView) {
            m_frameData.resourceViews[res.name] = res.externalView;
            continue;
        }
        
        if (res.type == Resource::Type::Image) {
            // Create image view from stored VkImage handle
            VkImage imageToView = res.image;
            if (imageToView == VK_NULL_HANDLE) {
                imageToView = res.externalImage;
            }
            
            if (imageToView != VK_NULL_HANDLE) {
                VkImageViewCreateInfo ivci{};
                ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                ivci.image = imageToView;
                ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                ivci.format = res.format;
                ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                ivci.subresourceRange.baseMipLevel = 0;
                ivci.subresourceRange.levelCount = res.mipLevels;
                ivci.subresourceRange.baseArrayLayer = 0;
                ivci.subresourceRange.layerCount = res.arrayLayers;
                
                VkImageView view = VK_NULL_HANDLE;
                if (g_vk.createImageView(m_renderer->device(), &ivci, nullptr, &view) == VK_SUCCESS) {
                    m_frameData.resourceViews[res.name] = view;
                }
            }
        }
    }
}

void RenderGraph::createFramebuffers()
{
    // Framebuffers created per-pass based on outputs
    for (const auto& pass : m_frameData.passes) {
        if (pass.isCompute || pass.outputs.isEmpty()) continue;
        
        QVector<VkImageView> attachments;
        for (const auto& output : pass.outputs) {
            auto it = m_frameData.resourceViews.find(output);
            if (it != m_frameData.resourceViews.end()) {
                attachments.append(*it);
            }
        }
        
        VkImageView depthView = VK_NULL_HANDLE;
        if (!pass.depthStencil.isEmpty()) {
            auto it = m_frameData.resourceViews.find(pass.depthStencil);
            if (it != m_frameData.resourceViews.end()) depthView = *it;
        }
        
        if (!attachments.isEmpty() || depthView) {
            VkFramebufferCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fci.renderPass = VK_NULL_HANDLE; // Would need pass-specific render pass
            fci.attachmentCount = attachments.size() + (depthView ? 1 : 0);
            QVector<VkImageView> allAttachments = attachments;
            if (depthView) allAttachments.append(depthView);
            fci.pAttachments = allAttachments.constData();
            fci.width = m_frameData.resources[attachments.isEmpty() ? pass.depthStencil : pass.outputs.first()].extent.width;
            fci.height = m_frameData.resources[attachments.isEmpty() ? pass.depthStencil : pass.outputs.first()].extent.height;
            fci.layers = 1;
            
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (m_renderer && g_vk.createFramebuffer(m_renderer->device(), &fci, nullptr, &fb) == VK_SUCCESS) {
                m_frameData.framebuffers[pass.name] = fb;
            }
        }
    }
}

void RenderGraph::computeResourceLifetimes()
{
    // Track first and last use of each resource for aliasing
    QMap<QString, int> firstUse, lastUse;
    
    for (int i = 0; i < m_executionOrder.size(); ++i) {
        const auto& passName = m_executionOrder[i];
        auto it = std::find_if(m_frameData.passes.begin(), m_frameData.passes.end(),
                               [&passName](const Pass& p) { return p.name == passName; });
        if (it == m_frameData.passes.end()) continue;
        
        for (const auto& input : it->inputs) {
            if (!firstUse.contains(input)) firstUse[input] = i;
            lastUse[input] = i;
        }
        for (const auto& output : it->outputs) {
            if (!firstUse.contains(output)) firstUse[output] = i;
            lastUse[output] = i;
        }
    }
    // Could use this for memory aliasing optimization
}

// â”€â”€ PBRMaterial Implementation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

PBRMaterial::PBRMaterial(QObject* parent) : QObject(parent)
{
}

PBRMaterial::~PBRMaterial()
{
    if (m_device && g_vk.destroyShaderModule) {
        if (m_vertModule) { g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
        if (m_fragModule) { g_vk.destroyShaderModule(m_device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
    }
    if (m_pipeline && m_device && g_vk.destroyPipeline) {
        g_vk.destroyPipeline(m_device, m_pipeline, nullptr);
    }
    if (m_pipelineLayout && m_device && g_vk.destroyPipelineLayout) {
        g_vk.destroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    }
    if (m_descriptorLayout && m_device && g_vk.destroyDescriptorSetLayout) {
        g_vk.destroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
    }
    for (auto& tex : m_textures) {
        if (tex.sampler && m_device && g_vk.destroySampler) {
            g_vk.destroySampler(m_device, tex.sampler, nullptr);
        }
    }
}

void PBRMaterial::setParameters(const PBRParameters& params)
{
    m_params = params;
    emit parametersChanged();
}

VkPipeline PBRMaterial::createPipeline(VkDevice device, VkPipelineLayout layout,
                                       VkRenderPass renderPass,
                                       const QVector<VkVertexInputBindingDescription>& bindings,
                                       const QVector<VkVertexInputAttributeDescription>& attrs)
{
    m_device = device;
    m_pipelineLayout = layout;
    if (!device || !layout || !renderPass) return VK_NULL_HANDLE;

    // Shader stages â€” callers must set m_vertModule/m_fragModule before calling
    VkPipelineShaderStageCreateInfo stages[2] = {};
    uint32_t stageCount = 0;

    if (m_vertModule) {
        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[stageCount].module = m_vertModule;
        stages[stageCount].pName = "main";
        ++stageCount;
    }
    if (m_fragModule) {
        stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stageCount].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stageCount].module = m_fragModule;
        stages[stageCount].pName = "main";
        ++stageCount;
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vi.pVertexBindingDescriptions = bindings.constData();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.constData();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo gpCi{};
    gpCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpCi.stageCount = stageCount;
    gpCi.pStages = stages;
    gpCi.pVertexInputState = &vi;
    gpCi.pInputAssemblyState = &ia;
    gpCi.pViewportState = &vp;
    gpCi.pRasterizationState = &rs;
    gpCi.pMultisampleState = &ms;
    gpCi.pColorBlendState = &cb;
    gpCi.pDepthStencilState = &ds;
    gpCi.pDynamicState = &dyn;
    gpCi.layout = layout;
    gpCi.renderPass = renderPass;
    gpCi.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (g_vk.createGraphicsPipelines) {
        g_vk.createGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpCi, nullptr, &pipeline);
    }
    m_pipeline = pipeline;
    if (pipeline) emit pipelineCreated(pipeline);
    return pipeline;
}

VkDescriptorSetLayout PBRMaterial::createDescriptorSetLayout(VkDevice device)
{
    QVector<VkDescriptorSetLayoutBinding> bindings = getStandardBindings();
    
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = bindings.size();
    ci.pBindings = bindings.constData();
    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    m_device = device;
    if (g_vk.createDescriptorSetLayout) {
        g_vk.createDescriptorSetLayout(device, &ci, nullptr, &layout);
    }
    m_descriptorLayout = layout;
    return layout;
}

void PBRMaterial::updateDescriptorSet(VkDevice device, VkDescriptorSet set, VkDescriptorPool pool)
{
    // Update descriptor set with current textures and parameters
    QVector<VkWriteDescriptorSet> writes;
    
    // Uniform buffer for material parameters
    // Texture descriptors
    for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
        if (!it->enabled || it->view == VK_NULL_HANDLE) continue;
        
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &it->descriptorInfo;
        writes.append(write);
    }
    
    if (!writes.isEmpty() && g_vk.updateDescriptorSets) {
        g_vk.updateDescriptorSets(device, writes.size(), writes.constData(), 0, nullptr);
    }
}

bool PBRMaterial::loadTexture(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, VkCommandPool pool,
                              const QString& name, const QString& filePath)
{
    if (name.isEmpty() || filePath.isEmpty()) return false;
    m_device = device;

    QImage image(filePath);
    if (image.isNull()) return false;

    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    int w = rgba.width(), h = rgba.height();
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w * h * 4);
    QByteArray pixels(reinterpret_cast<const char*>(rgba.constBits()),
                      static_cast<int>(imageSize));

    VkImage texImage = VK_NULL_HANDLE;
    VkDeviceMemory texMemory = VK_NULL_HANDLE;
    VkImageView texView = VK_NULL_HANDLE;
    VkSampler texSampler = VK_NULL_HANDLE;

    if (g_vk.createBuffer && g_vk.createImage && g_vk.allocateMemory) {
        // Create staging buffer
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;

        VkBufferCreateInfo sbCi{};
        sbCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbCi.size = imageSize;
        sbCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        sbCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (g_vk.createBuffer(device, &sbCi, nullptr, &stagingBuf) == VK_SUCCESS) {
            VkMemoryRequirements smr;
            g_vk.getBufferMemoryRequirements(device, stagingBuf, &smr);
            VkMemoryAllocateInfo sai{};
            sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            sai.allocationSize = smr.size;
            sai.memoryTypeIndex = VulkanRenderer::findMemoryType(physDev, smr.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (g_vk.allocateMemory(device, &sai, nullptr, &stagingMem) == VK_SUCCESS) {
                g_vk.bindBufferMemory(device, stagingBuf, stagingMem, 0);
                void* mapped = nullptr;
                if (g_vk.mapMemory(device, stagingMem, 0, imageSize, 0, &mapped) == VK_SUCCESS) {
                    memcpy(mapped, pixels.constData(), static_cast<size_t>(imageSize));
                    g_vk.unmapMemory(device, stagingMem);
                }
            }
        }

        // Create VkImage
        VkImageCreateInfo imgCi{};
        imgCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgCi.imageType = VK_IMAGE_TYPE_2D;
        imgCi.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        imgCi.mipLevels = 1;
        imgCi.arrayLayers = 1;
        imgCi.format = VK_FORMAT_R8G8B8A8_SRGB;
        imgCi.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgCi.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgCi.samples = VK_SAMPLE_COUNT_1_BIT;

        if (g_vk.createImage(device, &imgCi, nullptr, &texImage) == VK_SUCCESS) {
            VkMemoryRequirements imr;
            g_vk.getImageMemoryRequirements(device, texImage, &imr);
            VkMemoryAllocateInfo iai{};
            iai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            iai.allocationSize = imr.size;
            iai.memoryTypeIndex = VulkanRenderer::findMemoryType(physDev, imr.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (g_vk.allocateMemory(device, &iai, nullptr, &texMemory) == VK_SUCCESS) {
                g_vk.bindImageMemory(device, texImage, texMemory, 0);
            } else {
                g_vk.destroyImage(device, texImage, nullptr);
                texImage = VK_NULL_HANDLE;
            }
        }

        // Copy staging to image via one-shot command buffer
        if (stagingBuf && stagingMem && texImage && texMemory) {
            VkCommandBufferAllocateInfo cmdAi{};
            cmdAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAi.commandPool = pool;
            cmdAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAi.commandBufferCount = 1;

            VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
            if (g_vk.allocateCommandBuffers(device, &cmdAi, &cmdBuf) == VK_SUCCESS) {
                VkCommandBufferBeginInfo bi{};
                bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                if (g_vk.beginCommandBuffer(cmdBuf, &bi) == VK_SUCCESS) {
                    VkImageMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = texImage;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = 1;
                    barrier.srcAccessMask = 0;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    g_vk.cmdPipelineBarrier(cmdBuf,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

                    VkBufferImageCopy region{};
                    region.bufferOffset = 0;
                    region.bufferRowLength = 0;
                    region.bufferImageHeight = 0;
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.mipLevel = 0;
                    region.imageSubresource.baseArrayLayer = 0;
                    region.imageSubresource.layerCount = 1;
                    region.imageOffset = {0, 0, 0};
                    region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
                    g_vk.cmdCopyBufferToImage(cmdBuf, stagingBuf, texImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    g_vk.cmdPipelineBarrier(cmdBuf,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

                    g_vk.endCommandBuffer(cmdBuf);

                    VkSubmitInfo si{};
                    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    si.commandBufferCount = 1;
                    si.pCommandBuffers = &cmdBuf;
                    g_vk.queueSubmit(queue, 1, &si, VK_NULL_HANDLE);
                    g_vk.queueWaitIdle(queue);
                    g_vk.freeCommandBuffers(device, pool, 1, &cmdBuf);
                }
            }
        }

        // Clean up staging
        if (stagingBuf) g_vk.destroyBuffer(device, stagingBuf, nullptr);
        if (stagingMem) g_vk.freeMemory(device, stagingMem, nullptr);

        // Create image view
        if (texImage) {
            VkImageViewCreateInfo ivCi{};
            ivCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ivCi.image = texImage;
            ivCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivCi.format = VK_FORMAT_R8G8B8A8_SRGB;
            ivCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ivCi.subresourceRange.baseMipLevel = 0;
            ivCi.subresourceRange.levelCount = 1;
            ivCi.subresourceRange.baseArrayLayer = 0;
            ivCi.subresourceRange.layerCount = 1;
            g_vk.createImageView(device, &ivCi, nullptr, &texView);
        }

        // Create sampler
        VkSamplerCreateInfo sCi{};
        sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sCi.magFilter = VK_FILTER_LINEAR;
        sCi.minFilter = VK_FILTER_LINEAR;
        sCi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sCi.anisotropyEnable = VK_TRUE;
        sCi.maxAnisotropy = 16.0f;
        sCi.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sCi.unnormalizedCoordinates = VK_FALSE;
        sCi.compareEnable = VK_FALSE;
        sCi.minLod = 0.0f;
        sCi.maxLod = 1.0f;
        sCi.mipLodBias = 0.0f;
        g_vk.createSampler(device, &sCi, nullptr, &texSampler);
    }

    if (!texImage || !texView || !texSampler) {
        if (texImage && texMemory) { g_vk.destroyImage(device, texImage, nullptr); g_vk.freeMemory(device, texMemory, nullptr); }
        if (texView) { g_vk.destroyImageView(device, texView, nullptr); }
        if (texSampler) { g_vk.destroySampler(device, texSampler, nullptr); }
        return false;
    }

    TextureInfo info;
    info.name = name;
    info.view = texView;
    info.sampler = texSampler;
    info.descriptorInfo = {};
    info.descriptorInfo.imageView = texView;
    info.descriptorInfo.sampler = texSampler;
    info.descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.enabled = true;
    m_textures[name] = info;

    return true;
}

QVector<VkDescriptorSetLayoutBinding> PBRMaterial::getStandardBindings()
{
    QVector<VkDescriptorSetLayoutBinding> bindings;
    
    // Binding 0: Material uniform buffer
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.append(ubo);
    
    // Binding 1: Combined image sampler (base color)
    VkDescriptorSetLayoutBinding sampler{};
    sampler.binding = 1;
    sampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler.descriptorCount = 1;
    sampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.append(sampler);
    
    // Additional texture bindings (metallicRoughness, normal, etc.)
    for (int i = 2; i < 10; ++i) {
        VkDescriptorSetLayoutBinding tex{};
        tex.binding = i;
        tex.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex.descriptorCount = 1;
        tex.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.append(tex);
    }
    
    return bindings;
}

QVector<VkDescriptorSetLayoutBinding> PBRMaterial::getTextureBindings(uint32_t baseBinding)
{
    QVector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < 16; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = baseBinding + i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.append(b);
    }
    return bindings;
}

VkSampler PBRMaterial::createSampler(VkDevice device, VkFilter filter, VkSamplerAddressMode mode)
{
    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = filter;
    ci.minFilter = filter;
    ci.addressModeU = mode;
    ci.addressModeV = mode;
    ci.addressModeW = mode;
    ci.anisotropyEnable = VK_TRUE;
    ci.maxAnisotropy = 16.0f;
    ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    ci.unnormalizedCoordinates = VK_FALSE;
    ci.compareEnable = VK_FALSE;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    ci.mipLodBias = 0.0f;
    ci.minLod = 0.0f;
    ci.maxLod = 1000.0f;
    
    VkSampler sampler = VK_NULL_HANDLE;
    if (g_vk.createSampler) {
        g_vk.createSampler(device, &ci, nullptr, &sampler);
    }
    return sampler;
}

// â”€â”€ PBRPipelineFactory Implementation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

PBRPipelineFactory::PBRPipelineFactory(VulkanRenderer* renderer, QObject* parent)
    : QObject(parent), m_renderer(renderer)
{
}

VkPipeline PBRPipelineFactory::createStandardPBR(VkDevice device, VkRenderPass renderPass,
                                                  VkPipelineLayout layout,
                                                  const PipelineConfig& config)
{
    return createPipeline(device, renderPass, layout, ":/shaders/pbr.vert", ":/shaders/pbr.frag", config);
}

VkPipeline PBRPipelineFactory::createUnlit(VkDevice device, VkRenderPass renderPass,
                                           VkPipelineLayout layout)
{
    PipelineConfig config;
    config.enableDepthTest = true;
    config.enableDepthWrite = true;
    return createPipeline(device, renderPass, layout, ":/shaders/unlit.vert", ":/shaders/unlit.frag", config);
}

VkPipeline PBRPipelineFactory::createSkybox(VkDevice device, VkRenderPass renderPass,
                                            VkPipelineLayout layout)
{
    PipelineConfig config;
    config.enableDepthTest = true;
    config.enableDepthWrite = false;
    config.cullMode = VK_CULL_MODE_NONE;
    return createPipeline(device, renderPass, layout, ":/shaders/skybox.vert", ":/shaders/skybox.frag", config);
}

VkPipeline PBRPipelineFactory::createShadow(VkDevice device, VkRenderPass renderPass,
                                            VkPipelineLayout layout)
{
    PipelineConfig config;
    config.enableDepthTest = true;
    config.enableDepthWrite = true;
    config.cullMode = VK_CULL_MODE_FRONT_BIT; // Front face culling for shadows
    return createPipeline(device, renderPass, layout, ":/shaders/shadow.vert", ":/shaders/shadow.frag", config);
}

VkPipeline PBRPipelineFactory::createWireframe(VkDevice device, VkRenderPass renderPass,
                                               VkPipelineLayout layout)
{
    PipelineConfig config;
    config.polygonMode = VK_POLYGON_MODE_LINE;
    config.cullMode = VK_CULL_MODE_NONE;
    return createPipeline(device, renderPass, layout, ":/shaders/unlit.vert", ":/shaders/unlit.frag", config);
}

VkPipeline PBRPipelineFactory::createCustom(VkDevice device, VkRenderPass renderPass,
                                            VkPipelineLayout layout,
                                            const QString& vertShader, const QString& fragShader,
                                            const PipelineConfig& config)
{
    return createPipeline(device, renderPass, layout, vertShader, fragShader, config);
}

VkPipelineLayout PBRPipelineFactory::createStandardLayout(VkDevice device, uint32_t descriptorSetCount)
{
    QVector<VkDescriptorSetLayout> layouts;
    layouts.append(createMaterialLayout(device));
    layouts.append(createCameraLayout(device));
    layouts.append(createLightLayout(device));
    
    VkPipelineLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = layouts.size();
    ci.pSetLayouts = layouts.constData();
    
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (m_renderer && g_vk.createPipelineLayout) {
        g_vk.createPipelineLayout(device, &ci, nullptr, &layout);
    }
    return layout;
}

VkDescriptorSetLayout PBRPipelineFactory::createMaterialLayout(VkDevice device)
{
    QVector<VkDescriptorSetLayoutBinding> bindings;
    
    // Binding 0: Material uniform buffer
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.append(ubo);
    
    // Bindings 1-15: Textures
    for (int i = 1; i < 16; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.append(b);
    }
    
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = bindings.size();
    ci.pBindings = bindings.constData();
    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (m_renderer && g_vk.createDescriptorSetLayout) {
        g_vk.createDescriptorSetLayout(device, &ci, nullptr, &layout);
    }
    return layout;
}

VkDescriptorSetLayout PBRPipelineFactory::createCameraLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &ubo;
    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (m_renderer && g_vk.createDescriptorSetLayout) {
        g_vk.createDescriptorSetLayout(device, &ci, nullptr, &layout);
    }
    return layout;
}

VkDescriptorSetLayout PBRPipelineFactory::createLightLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding storage{};
    storage.binding = 1;
    storage.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storage.descriptorCount = 1;
    storage.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding bindings[2] = {ubo, storage};
    
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 2;
    ci.pBindings = bindings;
    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (m_renderer && g_vk.createDescriptorSetLayout) {
        g_vk.createDescriptorSetLayout(device, &ci, nullptr, &layout);
    }
    return layout;
}

VkDescriptorSetLayout PBRPipelineFactory::createTextureArrayLayout(VkDevice device, uint32_t textureCount)
{
    QVector<VkDescriptorSetLayoutBinding> bindings;
    
    for (uint32_t i = 0; i < textureCount; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.append(b);
    }
    
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = bindings.size();
    ci.pBindings = bindings.constData();
    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (m_renderer && g_vk.createDescriptorSetLayout) {
        g_vk.createDescriptorSetLayout(device, &ci, nullptr, &layout);
    }
    return layout;
}

VkPipeline PBRPipelineFactory::createPipeline(VkDevice device, VkRenderPass renderPass,
                                              VkPipelineLayout layout,
                                              const QString& vertShader, const QString& fragShader,
                                              const PipelineConfig& config, bool hasDepth)
{
    QByteArray vertCode, fragCode;
    QFile vFile(vertShader);
    if (vFile.open(QIODevice::ReadOnly)) {
        vertCode = vFile.readAll();
        vFile.close();
    }
    QFile fFile(fragShader);
    if (fFile.open(QIODevice::ReadOnly)) {
        fragCode = fFile.readAll();
        fFile.close();
    }

    auto createShaderModule = [&](const QByteArray& code) -> VkShaderModule {
        if (code.isEmpty() || !g_vk.createShaderModule) return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.constData());
        VkShaderModule module = VK_NULL_HANDLE;
        g_vk.createShaderModule(device, &ci, nullptr, &module);
        return module;
    };

    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";
    
    // Vertex input
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(VulkanRenderer::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attrs[4] = {};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = 12;
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = 24;
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[3].offset = 32;
    
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    // Viewport state
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    
    // Rasterization
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = config.polygonMode;
    rs.cullMode = config.cullMode;
    rs.frontFace = config.frontFace;
    rs.lineWidth = 1.0f;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.depthBiasEnable = VK_FALSE;
    
    // Multisample
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = config.samples;
    
    // Color blend
    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = config.enableBlend ? VK_TRUE : VK_FALSE;
    cbAtt.srcColorBlendFactor = config.srcColorBlendFactor;
    cbAtt.dstColorBlendFactor = config.dstColorBlendFactor;
    cbAtt.colorBlendOp = config.colorBlendOp;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;
    
    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = config.enableDepthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = config.enableDepthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    
    // Dynamic state
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;
    
    // Pipeline layout
    VkPipelineLayout pl = layout;
    
    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo gpCi{};
    gpCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpCi.stageCount = 2;
    gpCi.pStages = stages;
    gpCi.pVertexInputState = &vi;
    gpCi.pInputAssemblyState = &ia;
    gpCi.pViewportState = &vp;
    gpCi.pRasterizationState = &rs;
    gpCi.pMultisampleState = &ms;
    gpCi.pColorBlendState = &cb;
    gpCi.pDepthStencilState = hasDepth ? &ds : nullptr;
    gpCi.pDynamicState = &dyn;
    gpCi.layout = pl;
    gpCi.renderPass = renderPass;
    gpCi.subpass = 0;
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (m_renderer && g_vk.createGraphicsPipelines) {
        g_vk.createGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpCi, nullptr, &pipeline);
    }

    if (g_vk.destroyShaderModule) {
        if (vertModule) g_vk.destroyShaderModule(device, vertModule, nullptr);
        if (fragModule) g_vk.destroyShaderModule(device, fragModule, nullptr);
    }

    return pipeline;
}

// â”€â”€ SceneGraph Implementation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

RenderSceneGraph::RenderSceneGraph(QObject* parent) : QObject(parent)
{
}

RenderSceneGraph::~RenderSceneGraph()
{
    // Clean up all entities and components
    for (auto e : m_entities) delete e;
    for (auto c : m_transforms) delete c;
    for (auto c : m_meshes) delete c;
    for (auto c : m_lights) delete c;
    for (auto c : m_cameras) delete c;
    if (m_environment) delete m_environment;
}

QUuid RenderSceneGraph::createEntity(const QString& name)
{
    QUuid id = QUuid::createUuid();
    Entity* entity = new Entity();
    entity->id = id;
    entity->name = name.isEmpty() ? "Entity_" + id.toString().mid(0, 8) : name;
    m_entities[id] = entity;
    emit entityCreated(id);
    return id;
}

void RenderSceneGraph::destroyEntity(QUuid id)
{
    if (auto* entity = getEntity(id)) {
        // Remove all components
        for (const auto& compId : entity->components) {
            if (m_transforms.contains(compId)) delete m_transforms.take(compId);
            else if (m_meshes.contains(compId)) delete m_meshes.take(compId);
            else if (m_lights.contains(compId)) delete m_lights.take(compId);
            else if (m_cameras.contains(compId)) delete m_cameras.take(compId);
        }
        
        if (m_environment && m_environment->entityId == id) {
            delete m_environment;
            m_environment = nullptr;
        }
        
        delete entity;
        m_entities.remove(id);
        emit entityDestroyed(id);
    }
}

RenderSceneGraph::Entity* RenderSceneGraph::getEntity(QUuid id)
{
    return m_entities.value(id, nullptr);
}

const RenderSceneGraph::Entity* RenderSceneGraph::getEntity(QUuid id) const
{
    return m_entities.value(id, nullptr);
}

QVector<RenderSceneGraph::Entity*> RenderSceneGraph::getEntitiesByTag(const QString& tag)
{
    QVector<Entity*> result;
    for (auto* e : m_entities) {
        if (e->tag == tag) result.append(e);
    }
    return result;
}

QVector<RenderSceneGraph::Entity*> RenderSceneGraph::getAllEntities() const
{
    return m_entities.values();
}

void RenderSceneGraph::setParent(QUuid child, QUuid parent)
{
    if (child == parent) return;
    
    if (auto* childEntity = getEntity(child)) {
        // Remove from old parent
        if (!childEntity->parent.isNull() && getEntity(childEntity->parent)) {
            auto& siblings = getEntity(childEntity->parent)->children;
            siblings.removeAll(child);
        }
        
        childEntity->parent = parent;
        
        // Add to new parent
        if (!parent.isNull() && getEntity(parent)) {
            getEntity(parent)->children.append(child);
        }
    }
}

void RenderSceneGraph::updateTransforms()
{
    // Update root entities first
    for (auto* entity : m_entities) {
        if (entity->parent.isNull()) {
            updateTransformRecursive(entity);
        }
    }
}

void RenderSceneGraph::updateTransformRecursive(Entity* entity)
{
    auto* transform = getComponent<TransformComponent>(entity->id);
    if (!transform) return;
    
    // Compute local matrix
    transform->localMatrix.setToIdentity();
    transform->localMatrix.translate(transform->position);
    transform->localMatrix.rotate(transform->rotation);
    transform->localMatrix.scale(transform->scale);
    
    // Compute world matrix
    if (entity->parent.isNull()) {
        transform->worldMatrix = transform->localMatrix;
    } else {
        auto* parentTransform = getComponent<TransformComponent>(entity->parent);
        if (parentTransform) {
            transform->worldMatrix = parentTransform->worldMatrix * transform->localMatrix;
        } else {
            transform->worldMatrix = transform->localMatrix;
        }
    }
    
    // Extract position/rotation/scale from world matrix if needed
    transform->dirty = false;
    
    // Recurse to children
    for (const auto& childId : entity->children) {
        if (auto* child = getEntity(childId)) {
            updateTransformRecursive(child);
        }
    }
}

QMatrix4x4 RenderSceneGraph::getWorldTransform(QUuid id) const
{
    const auto* transform = getComponent<TransformComponent>(id);
    if (transform) {
        return transform->worldMatrix;
    }
    return QMatrix4x4();
}

QVector<QUuid> RenderSceneGraph::getEntitiesWithMesh() const
{
    QVector<QUuid> result;
    for (auto it = m_meshes.constBegin(); it != m_meshes.constEnd(); ++it) {
        result.append(it.value()->entityId);
    }
    return result;
}

QVector<QUuid> RenderSceneGraph::getEntitiesWithLight() const
{
    QVector<QUuid> result;
    for (auto it = m_lights.constBegin(); it != m_lights.constEnd(); ++it) {
        result.append(it.value()->entityId);
    }
    return result;
}

RenderSceneGraph::CameraComponent* RenderSceneGraph::getMainCamera() const
{
    // Return first active camera
    for (auto* cam : m_cameras) {
        if (auto* entity = getEntity(cam->entityId)) {
            if (entity->active && entity->visible) return cam;
        }
    }
    return nullptr;
}

QJsonObject RenderSceneGraph::serialize() const
{
    QJsonObject obj;
    QJsonArray entitiesArray;
    
    for (auto* entity : m_entities) {
        QJsonObject e;
        e["id"] = entity->id.toString();
        e["name"] = entity->name;
        e["tag"] = entity->tag;
        e["active"] = entity->active;
        e["visible"] = entity->visible;
        e["parent"] = entity->parent.toString();
        
        QJsonArray comps;
        for (const auto& compId : entity->components) {
            comps.append(compId.toString());
        }
        e["components"] = comps;
        
        // Serialize components
        if (auto* t = m_transforms.value(entity->components.first())) {
            QJsonObject tc;
            tc["type"] = "Transform";
            tc["position"] = QJsonArray{t->position.x(), t->position.y(), t->position.z()};
            tc["rotation"] = QJsonArray{t->rotation.x(), t->rotation.y(), t->rotation.z(), t->rotation.scalar()};
            tc["scale"] = QJsonArray{t->scale.x(), t->scale.y(), t->scale.z()};
            e["transform"] = tc;
        }
        
        entitiesArray.append(e);
    }
    
    obj["entities"] = entitiesArray;
    
    if (m_environment) {
        QJsonObject env;
        env["ambientColor"] = QJsonArray{m_environment->ambientColor.x(), m_environment->ambientColor.y(), m_environment->ambientColor.z()};
        env["ambientIntensity"] = m_environment->ambientIntensity;
        env["skyboxTexture"] = m_environment->skyboxTexture;
        env["exposure"] = m_environment->exposure;
        obj["environment"] = env;
    }
    
    return obj;
}

void RenderSceneGraph::clear()
{
    for (auto e : m_entities) delete e;
    for (auto c : m_transforms) delete c;
    for (auto c : m_meshes) delete c;
    for (auto c : m_lights) delete c;
    for (auto c : m_cameras) delete c;
    if (m_environment) delete m_environment;
    m_entities.clear();
    m_transforms.clear();
    m_meshes.clear();
    m_lights.clear();
    m_cameras.clear();
    m_environment = nullptr;
}

bool RenderSceneGraph::deserialize(const QJsonObject& data)
{
    clear(); // Clear existing
    
    QJsonArray entitiesArray = data["entities"].toArray();
    for (const auto& v : entitiesArray) {
        QJsonObject e = v.toObject();
        QUuid id = QUuid::fromString(e["id"].toString());
        
        Entity* entity = new Entity();
        entity->id = id;
        entity->name = e["name"].toString();
        entity->tag = e["tag"].toString();
        entity->active = e["active"].toBool(true);
        entity->visible = e["visible"].toBool(true);
        entity->parent = QUuid::fromString(e["parent"].toString());
        
        m_entities[id] = entity;
        
        // Reconstruct hierarchy
        if (!entity->parent.isNull()) {
            if (auto* parent = getEntity(entity->parent)) {
                parent->children.append(id);
            }
        }
        
        // Deserialize transform
        if (e.contains("transform")) {
            QJsonObject tc = e["transform"].toObject();
            auto* transform = addComponent<TransformComponent>(id);
            QJsonArray pos = tc["position"].toArray();
            QJsonArray rot = tc["rotation"].toArray();
            QJsonArray scale = tc["scale"].toArray();
            transform->position = {static_cast<float>(pos[0].toDouble()), static_cast<float>(pos[1].toDouble()), static_cast<float>(pos[2].toDouble())};
            transform->rotation = {static_cast<float>(rot[0].toDouble()), static_cast<float>(rot[1].toDouble()), static_cast<float>(rot[2].toDouble()), static_cast<float>(rot[3].toDouble())};
            transform->scale = {static_cast<float>(scale[0].toDouble()), static_cast<float>(scale[1].toDouble()), static_cast<float>(scale[2].toDouble())};
        }
    }
    
    if (data.contains("environment")) {
        QJsonObject env = data["environment"].toObject();
        m_environment = new EnvironmentComponent();
        QJsonArray ac = env["ambientColor"].toArray();
        m_environment->ambientColor = {static_cast<float>(ac[0].toDouble()), static_cast<float>(ac[1].toDouble()), static_cast<float>(ac[2].toDouble())};
        m_environment->ambientIntensity = static_cast<float>(env["ambientIntensity"].toDouble(1.0));
        m_environment->skyboxTexture = env["skyboxTexture"].toString();
        m_environment->exposure = static_cast<float>(env["exposure"].toDouble(1.0));
    }
    
    updateTransforms();
    return true;
}

} // namespace ks
