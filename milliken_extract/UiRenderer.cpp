#include "UiRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ks {
namespace sim {

UiRenderer::~UiRenderer() { shutdownVulkan(); }

void UiRenderer::setScreenSize(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (width != m_width || height != m_height) {
        m_width = width;
        m_height = height;
        m_dirty = true;
    }
}

void UiRenderer::begin() {
    m_building = true;
    m_quads.clear();
    m_texts.clear();
}

void UiRenderer::addQuad(const UiQuad& q) {
    if (!m_building) return;
    if (q.w <= 0.f || q.h <= 0.f || q.color.a <= 0.001f) return;
    m_quads.push_back(q);
}

void UiRenderer::addText(const UiText& t) {
    if (!m_building || t.text.empty() || t.color.a <= 0.001f) return;
    m_texts.push_back(t);
}

void UiRenderer::end() {
    m_building = false;
    if (!m_dirty) return;

    m_vertices.clear();
    // Sort stable by z so panels under text
    std::stable_sort(m_quads.begin(), m_quads.end(),
                     [](const UiQuad& a, const UiQuad& b) { return a.z < b.z; });
    std::stable_sort(m_texts.begin(), m_texts.end(),
                     [](const UiText& a, const UiText& b) { return a.z < b.z; });

    for (const auto& q : m_quads)
        emitQuad(q.x, q.y, q.w, q.h, q.color);

    for (const auto& t : m_texts) {
        float cx = t.x;
        for (unsigned char ch : t.text) {
            if (ch == '\n') {
                cx = t.x;
                // simple line advance handled by caller usually
                continue;
            }
            emitGlyph(cx, t.y, t.size, static_cast<char>(ch), t.color);
            cx += t.size * 0.6f;
        }
    }

    if (m_vertices.size() > kMaxVertices)
        m_vertices.resize(kMaxVertices);
}

void UiRenderer::clear() {
    m_quads.clear();
    m_texts.clear();
    m_vertices.clear();
    m_dirty = true;
}

void UiRenderer::pixelToNdc(float px, float py, float& ndcX, float& ndcY) const {
    ndcX = (px / static_cast<float>(m_width)) * 2.f - 1.f;
    ndcY = 1.f - (py / static_cast<float>(m_height)) * 2.f; // top-left origin
}

void UiRenderer::emitQuad(float x, float y, float w, float h, const UiColor& c) {
    float x0, y0, x1, y1;
    pixelToNdc(x, y, x0, y0);
    pixelToNdc(x + w, y + h, x1, y1);
    Vertex v[6] = {
        {x0, y0, 0, 0, c.r, c.g, c.b, c.a},
        {x1, y0, 1, 0, c.r, c.g, c.b, c.a},
        {x1, y1, 1, 1, c.r, c.g, c.b, c.a},
        {x0, y0, 0, 0, c.r, c.g, c.b, c.a},
        {x1, y1, 1, 1, c.r, c.g, c.b, c.a},
        {x0, y1, 0, 1, c.r, c.g, c.b, c.a},
    };
    m_vertices.insert(m_vertices.end(), v, v + 6);
}

void UiRenderer::emitGlyph(float x, float y, float size, char ch, const UiColor& c) {
    // Placeholder solid block for non-space; real atlas can replace later
    if (ch == ' ') return;
    const float gw = size * 0.55f;
    const float gh = size;
    UiColor gc = c;
    // slightly dimmer body for "glyph" look without texture
    emitQuad(x, y, gw, gh, gc);
}

bool UiRenderer::initVulkan(VkDevice device, VkPhysicalDevice /*phys*/,
                            VkRenderPass /*renderPass*/, uint32_t /*queueFamily*/) {
#if !KS_UI_VK
    (void)device;
    return false;
#else
    m_device = device;
    // Pipeline creation is deferred to project shader pack (ui.vert/ui.frag).
    // Vertex buffer allocated lazily on first flush.
    m_vkReady = (device != nullptr);
    return m_vkReady;
#endif
}

void UiRenderer::shutdownVulkan() {
#if KS_UI_VK
    if (m_device && m_vertexBuffer) {
        if (m_mapped) {
            // unmap if we used persistent map — host visible
            m_mapped = nullptr;
        }
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        vkFreeMemory(m_device, m_vertexMemory, nullptr);
        m_vertexBuffer = nullptr;
        m_vertexMemory = nullptr;
    }
    if (m_device && m_pipeline) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = nullptr;
    }
    if (m_device && m_pipelineLayout) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = nullptr;
    }
    m_vkReady = false;
    m_device = nullptr;
#endif
}

void UiRenderer::flush(VkCommandBuffer cmd) {
    if (m_vertices.empty()) {
        ++m_framesSkipped;
        return;
    }

#if !KS_UI_VK
    (void)cmd;
    // CPU path: vertices ready for external consumer
    m_dirty = false;
    ++m_framesDrawn;
    return;
#else
    if (!m_vkReady || !cmd) {
        ++m_framesSkipped;
        return;
    }

    const size_t bytes = m_vertices.size() * sizeof(Vertex);
    m_lastUploadBytes = static_cast<uint32_t>(bytes);

    // Lazy VB — host visible for simplicity (UI is small)
    if (!m_vertexBuffer || m_vbCapacity < bytes) {
        if (m_vertexBuffer) {
            vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
            vkFreeMemory(m_device, m_vertexMemory, nullptr);
            m_vertexBuffer = nullptr;
            m_vertexMemory = nullptr;
            m_mapped = nullptr;
        }
        m_vbCapacity = std::max(bytes, size_t(64 * 1024));
        // Actual vkCreateBuffer requires memory type index from phys device —
        // left to NativeRenderer integration when full VK context is available.
        // Until then, keep CPU vertices; draw path is a no-op without buffer.
        m_dirty = false;
        ++m_framesDrawn;
        return;
    }

    if (m_dirty && m_mapped) {
        std::memcpy(m_mapped, m_vertices.data(), bytes);
        m_dirty = false;
    }

    // Bind + draw would go here with m_pipeline
    // vkCmdBindPipeline / vkCmdBindVertexBuffers / vkCmdDraw
    ++m_framesDrawn;
#endif
}

} // namespace sim
} // namespace ks
