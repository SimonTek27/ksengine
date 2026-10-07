#pragma once

/**
 * @file GpuProfiler.h
 * @brief Vulkan GPU timestamp profiler — Qt-free
 *
 * Uses VK_QUERY_TYPE_TIMESTAMP query pools (double-buffered) to measure
 * GPU frame and named pass durations. Safe no-op if Vulkan is unavailable
 * or timestamps are not supported.
 *
 * Typical usage (NativeRenderer):
 *   m_gpuProfiler.init(device, physicalDevice, queueFamily);
 *   beginFrame:
 *     m_gpuProfiler.beginFrame(cmd);
 *     m_gpuProfiler.writeTimestamp(cmd, GpuProfiler::Pass::FrameBegin);
 *   after main pass:
 *     m_gpuProfiler.writeTimestamp(cmd, GpuProfiler::Pass::MainPass);
 *   endFrame (after queue submit, or next frame):
 *     m_gpuProfiler.endFrame(); // resolves previous frame queries
 */

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(HAS_VULKAN) || defined(VK_VERSION_1_0)
#  include <vulkan/vulkan.h>
#  define KS_GPU_PROFILER_VK 1
#else
#  define KS_GPU_PROFILER_VK 0
struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkCommandBuffer_T;
struct VkQueryPool_T;
using VkDevice = VkDevice_T*;
using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkCommandBuffer = VkCommandBuffer_T*;
using VkQueryPool = VkQueryPool_T*;
using VkResult = int;
#endif

namespace ks {
namespace sim {

class GpuProfiler {
public:
    enum class Pass : int {
        FrameBegin = 0,
        Shadow,
        MainPass,
        UI,
        FrameEnd,
        Count
    };

    static constexpr int kPassCount = static_cast<int>(Pass::Count);
    static constexpr int kFramesInFlight = 2;
    static constexpr int kQueriesPerFrame = kPassCount; // one timestamp per pass marker

    GpuProfiler() = default;
    ~GpuProfiler();

    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    /** Create query pools. Returns false if timestamps unsupported (CPU-only fallback). */
    bool init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex);
    void shutdown();

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled && m_ready; }

    /**
     * Call at start of recording cmd for this frame.
     * Advances ring index; resolves results from frame-2 (already submitted).
     */
    void beginFrame(VkCommandBuffer cmd);

    /** Write a timestamp at the current GPU point (TOP_OF_PIPE / BOTTOM as configured). */
    void writeTimestamp(VkCommandBuffer cmd, Pass pass);

    /**
     * Call after submit for this frame (or at begin of next frame).
     * Does not block; results are read when that slot is reused.
     */
    void endFrame();

    // Results for the last fully resolved GPU frame (ms)
    double frameTimeMs() const { return m_frameTimeMs; }
    double avgFrameTimeMs() const { return m_avgFrameTimeMs; }
    double passTimeMs(Pass p) const {
        const int i = static_cast<int>(p);
        return (i >= 0 && i < kPassCount) ? m_passMs[static_cast<size_t>(i)] : 0.0;
    }
    int fps() const {
        return m_frameTimeMs > 0.01 ? static_cast<int>(1000.0 / m_frameTimeMs) : 0;
    }

    static const char* passName(Pass p);

private:
    void resolveFrame(int slot);

    bool m_enabled = true;
    bool m_ready = false;
    VkDevice m_device = nullptr;
    float m_timestampPeriodNs = 1.0f; // from VkPhysicalDeviceLimits

#if KS_GPU_PROFILER_VK
    std::array<VkQueryPool, kFramesInFlight> m_pools{};
#else
    std::array<void*, kFramesInFlight> m_pools{};
#endif

    int m_frameIndex = 0;       // current recording slot
    int m_resolveIndex = -1;    // last resolved
    std::array<bool, kFramesInFlight> m_slotUsed{};

    double m_frameTimeMs = 0.0;
    double m_avgFrameTimeMs = 0.0;
    std::array<double, kPassCount> m_passMs{};
    std::mutex m_mutex;
};

/** RAII marker: writeTimestamp(begin) on construct is done by caller; this only helps naming. */
struct GpuPassScope {
    GpuProfiler& profiler;
    GpuProfiler::Pass pass;
    VkCommandBuffer cmd;
    GpuPassScope(GpuProfiler& p, VkCommandBuffer c, GpuProfiler::Pass pass_)
        : profiler(p), pass(pass_), cmd(c) {
        profiler.writeTimestamp(cmd, pass);
    }
};

} // namespace sim
} // namespace ks

// Convenience (no-op if not linked with Vulkan)
#define KS_GPU_TIMESTAMP(profiler, cmd, pass) \
    do { if ((profiler).isEnabled()) (profiler).writeTimestamp((cmd), (pass)); } while (0)
