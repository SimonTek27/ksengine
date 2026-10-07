#pragma once

/**
 * @file UiRenderer.h
 * @brief Batched 2D UI for Vulkan/Native path — Qt-free
 *
 * Optimizations:
 *  - Dirty-flag: rebuild CPU vertex list only when UI content changes
 *  - Single dynamic vertex buffer upload per dirty frame
 *  - One draw call for all solid quads (optional second for text)
 *  - Skip entirely when invisible / alpha ~ 0
 *  - Screen-space NDC conversion once per frame size change
 */

#include <cstdint>
#include <string>
#include <vector>

#if defined(HAS_VULKAN) || defined(VK_VERSION_1_0)
#  include <vulkan/vulkan.h>
#  define KS_UI_VK 1
#else
#  define KS_UI_VK 0
using VkDevice = void*;
using VkPhysicalDevice = void*;
using VkCommandBuffer = void*;
using VkBuffer = void*;
using VkDeviceMemory = void*;
using VkPipeline = void*;
using VkPipelineLayout = void*;
using VkRenderPass = void*;
using VkShaderModule = void*;
using VkExtent2D = struct { uint32_t width, height; };
#endif

namespace ks {
namespace sim {

struct UiColor {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
    static UiColor rgba(float r, float g, float b, float a = 1.f) { return {r, g, b, a}; }
    static UiColor rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
    }
};

/** Screen-space pixel quad (top-left origin). */
struct UiQuad {
    float x = 0, y = 0, w = 0, h = 0;
    UiColor color;
    int z = 0; // higher drawn later
};

struct UiText {
    float x = 0, y = 0;
    float size = 16.f;
    std::string text;
    UiColor color;
    int z = 0;
};

/**
 * Builds a display list of quads/text; uploads only when markDirty().
 * Without Vulkan shaders present, still maintains CPU lists for software blit hooks.
 */
class UiRenderer {
public:
    static constexpr size_t kMaxVertices = 65536; // ~10k quads

    struct Vertex {
        float x, y;     // NDC
        float u, v;     // font atlas (0 for solid)
        float r, g, b, a;
    };

    UiRenderer() = default;
    ~UiRenderer();

    void setScreenSize(int width, int height);
    int width() const { return m_width; }
    int height() const { return m_height; }

    void begin();
    void addQuad(const UiQuad& q);
    void addText(const UiText& t); // expands to glyph quads (bitmap 8x8)
    void end(); // sorts by z if needed

    void markDirty() { m_dirty = true; }
    bool isDirty() const { return m_dirty; }
    void clear();

    const std::vector<Vertex>& vertices() const { return m_vertices; }
    uint32_t vertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }
    uint32_t quadCount() const { return static_cast<uint32_t>(m_vertices.size() / 6); }

    /** Upload VB if dirty; record draw. No-op if empty or not initialized. */
    void flush(VkCommandBuffer cmd);

    /** Optional Vulkan resources (call after device create). */
    bool initVulkan(VkDevice device, VkPhysicalDevice phys, VkRenderPass renderPass,
                    uint32_t queueFamily);
    void shutdownVulkan();

    // Stats for GpuProfiler / HUD
    uint32_t lastUploadBytes() const { return m_lastUploadBytes; }
    uint32_t framesSkipped() const { return m_framesSkipped; }
    uint32_t framesDrawn() const { return m_framesDrawn; }

private:
    void pixelToNdc(float px, float py, float& ndcX, float& ndcY) const;
    void emitQuad(float x, float y, float w, float h, const UiColor& c);
    void emitGlyph(float x, float y, float size, char ch, const UiColor& c);

    int m_width = 1;
    int m_height = 1;
    bool m_dirty = true;
    bool m_building = false;
    std::vector<UiQuad> m_quads;
    std::vector<UiText> m_texts;
    std::vector<Vertex> m_vertices;

    uint32_t m_lastUploadBytes = 0;
    uint32_t m_framesSkipped = 0;
    uint32_t m_framesDrawn = 0;

#if KS_UI_VK
    VkDevice m_device = nullptr;
    VkBuffer m_vertexBuffer = nullptr;
    VkDeviceMemory m_vertexMemory = nullptr;
    void* m_mapped = nullptr;
    size_t m_vbCapacity = 0;
    VkPipeline m_pipeline = nullptr;
    VkPipelineLayout m_pipelineLayout = nullptr;
    VkShaderModule m_vert = nullptr;
    VkShaderModule m_frag = nullptr;
    bool m_vkReady = false;
#endif
};

} // namespace sim
} // namespace ks
