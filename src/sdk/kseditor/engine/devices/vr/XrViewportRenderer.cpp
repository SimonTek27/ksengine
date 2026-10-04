#include "XrViewportRenderer.h"
#include <QDebug>

#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)

namespace ks {
namespace vr {

XrViewportRenderer::XrViewportRenderer(QObject* parent)
    : QObject(parent)
{
    m_xr = XrManager::instance();
}

XrViewportRenderer::~XrViewportRenderer()
{
    shutdown();
}

bool XrViewportRenderer::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkInstance vkInstance, uint32_t queueFamilyIndex,
                                     uint32_t queueIndex, VkCommandPool commandPool,
                                     VkQueue graphicsQueue)
{
    if (m_initialized) return true;

    m_device = device;
    m_physicalDevice = physicalDevice;
    m_commandPool = commandPool;
    m_graphicsQueue = graphicsQueue;

    m_xr->setVulkanDevice(device, physicalDevice, vkInstance, queueFamilyIndex, queueIndex);
    m_xr->setVulkanCommandResources(commandPool, graphicsQueue);

    if (!m_xr->initialize()) {
        emit error("Failed to initialize XR Manager");
        return false;
    }

    if (!createEyeFramebuffers()) {
        emit error("Failed to create eye framebuffers");
        shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

void XrViewportRenderer::shutdown()
{
    if (!m_initialized) return;

    destroyEyeFramebuffers();
    m_xr->shutdown();
    m_initialized = false;
}

bool XrViewportRenderer::createEyeFramebuffers()
{
    for (int i = 0; i < m_xr->eyeCount() && i < 2; i++) {
        auto& eye = m_xr->eye(i);
        auto& fb = m_eyeFBs[i];

        // Create render pass
        VkAttachmentDescription colorAtt{};
        colorAtt.format = (VkFormat)m_xr->selectedColorFormat();
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = nullptr;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAtt;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &fb.renderPass) != VK_SUCCESS) {
            qWarning() << "Failed to create render pass for eye" << i;
            return false;
        }

        // If we have a depth swapchain, add depth attachment
        if (!eye.depthImages.isEmpty()) {
            // Recreate with depth attachment
            VkAttachmentDescription depthAtt{};
            depthAtt.format = (VkFormat)m_xr->selectedDepthFormat();
            depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

            VkAttachmentDescription attachments[2] = {colorAtt, depthAtt};
            subpass.pDepthStencilAttachment = &depthRef;

            VkRenderPassCreateInfo rpInfo2{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rpInfo2.attachmentCount = 2;
            rpInfo2.pAttachments = attachments;
            rpInfo2.subpassCount = 1;
            rpInfo2.pSubpasses = &subpass;
            rpInfo2.dependencyCount = 1;
            rpInfo2.pDependencies = &dep;

            vkDestroyRenderPass(m_device, fb.renderPass, nullptr);
            if (vkCreateRenderPass(m_device, &rpInfo2, nullptr, &fb.renderPass) != VK_SUCCESS) {
                qWarning() << "Failed to create render pass with depth for eye" << i;
                return false;
            }
        }

        // Create image views
        fb.colorView = createImageView(
            eye.colorImages.isEmpty() ? VK_NULL_HANDLE : eye.colorImages[0],
            (VkFormat)m_xr->selectedColorFormat(),
            VK_IMAGE_ASPECT_COLOR_BIT);

        if (!eye.depthImages.isEmpty()) {
            fb.depthView = createImageView(
                eye.depthImages[0],
                (VkFormat)m_xr->selectedDepthFormat(),
                VK_IMAGE_ASPECT_DEPTH_BIT);
        }

        // Create framebuffer
        uint32_t attachmentCount = 1;
        VkImageView attachments[2] = {fb.colorView, fb.depthView};
        if (eye.depthImages.isEmpty()) attachmentCount = 1;

        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = fb.renderPass;
        fbInfo.attachmentCount = eye.depthImages.isEmpty() ? 1u : 2u;
        fbInfo.pAttachments = attachments;
        fbInfo.width = eye.swapchainImageWidth;
        fbInfo.height = eye.swapchainImageHeight;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &fb.framebuffer) != VK_SUCCESS) {
            qWarning() << "Failed to create framebuffer for eye" << i;
            return false;
        }

        // Create command buffer
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_device, &allocInfo, &fb.commandBuffer) != VK_SUCCESS) {
            qWarning() << "Failed to allocate command buffer for eye" << i;
            return false;
        }

        // Create semaphore and fence
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(m_device, &semInfo, nullptr, &fb.semaphore);

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_device, &fenceInfo, nullptr, &fb.fence);
    }

    return true;
}

void XrViewportRenderer::destroyEyeFramebuffers()
{
    for (int i = 0; i < 2; i++) {
        auto& fb = m_eyeFBs[i];
        if (fb.fence) { vkWaitForFences(m_device, 1, &fb.fence, VK_TRUE, UINT64_MAX); vkDestroyFence(m_device, fb.fence, nullptr); fb.fence = VK_NULL_HANDLE; }
        if (fb.semaphore) { vkDestroySemaphore(m_device, fb.semaphore, nullptr); fb.semaphore = VK_NULL_HANDLE; }
        if (fb.commandBuffer) { vkFreeCommandBuffers(m_device, m_commandPool, 1, &fb.commandBuffer); fb.commandBuffer = VK_NULL_HANDLE; }
        if (fb.framebuffer) { vkDestroyFramebuffer(m_device, fb.framebuffer, nullptr); fb.framebuffer = VK_NULL_HANDLE; }
        if (fb.colorView) { vkDestroyImageView(m_device, fb.colorView, nullptr); fb.colorView = VK_NULL_HANDLE; }
        if (fb.depthView) { vkDestroyImageView(m_device, fb.depthView, nullptr); fb.depthView = VK_NULL_HANDLE; }
        if (fb.renderPass) { vkDestroyRenderPass(m_device, fb.renderPass, nullptr); fb.renderPass = VK_NULL_HANDLE; }
    }
}

VkImageView XrViewportRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect)
{
    if (image == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(m_device, &info, nullptr, &view);
    return view;
}

bool XrViewportRenderer::renderFrame()
{
    if (!m_initialized) return false;

    // Poll events
    m_xr->pollEvents();
    if (!m_xr->isSessionRunning()) return false;

    // Poll actions
    m_xr->pollActions();

    // Begin XR frame
    if (!m_xr->beginXRFrame()) return false;

    // Render each eye
    for (int eyeIndex = 0; eyeIndex < m_xr->eyeCount() && eyeIndex < 2; eyeIndex++) {
        if (!m_xr->beginEyeRender(eyeIndex)) continue;

        auto& fb = m_eyeFBs[eyeIndex];
        auto& eye = m_xr->eye(eyeIndex);

        // Wait for fence
        vkWaitForFences(m_device, 1, &fb.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_device, 1, &fb.fence);

        // Get XR view/projection matrices
        QMatrix4x4 view = m_xr->viewMatrix(eyeIndex);
        QMatrix4x4 proj = m_xr->projectionMatrix(eyeIndex);

        // Record command buffer
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(fb.commandBuffer, &beginInfo);

        // Begin render pass
        VkClearValue clearColor = {{{m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]}}};

        VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBegin.renderPass = fb.renderPass;
        rpBegin.framebuffer = fb.framebuffer;
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = {eye.swapchainImageWidth, eye.swapchainImageHeight};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(fb.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        // Execute draw callback
        if (m_drawCallback) {
            m_drawCallback(fb.commandBuffer, eyeIndex, view, proj);
        }

        vkCmdEndRenderPass(fb.commandBuffer);
        vkEndCommandBuffer(fb.commandBuffer);

        // Submit
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &fb.commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &fb.semaphore;

        vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fb.fence);

        m_xr->endEyeRender(eyeIndex);
    }

    // End XR frame
    m_xr->endXRFrame();

    emit frameRendered();
    return true;
}

bool XrViewportRenderer::isSessionActive() const
{
    return m_xr && m_xr->isSessionRunning() && m_xr->isSessionFocused();
}

void XrViewportRenderer::setClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

}} // namespace ks::vr

#endif // XR_VERSION_1_0

#if !defined(XR_VERSION_1_0) && !defined(XR_NULL_HANDLE)
namespace ks { namespace vr {
XrViewportRenderer::XrViewportRenderer(QObject* parent) : QObject(parent) {}
XrViewportRenderer::~XrViewportRenderer() {}
bool XrViewportRenderer::initialize(VkDevice, VkPhysicalDevice, VkInstance, uint32_t, uint32_t, VkCommandPool, VkQueue) { return false; }
void XrViewportRenderer::shutdown() {}
bool XrViewportRenderer::renderFrame() { return false; }
bool XrViewportRenderer::isSessionActive() const { return false; }
void XrViewportRenderer::setClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }
}} // namespace ks::vr
#endif
