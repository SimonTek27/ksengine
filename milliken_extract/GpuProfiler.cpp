#include "GpuProfiler.h"

#include <algorithm>
#include <cstring>

namespace ks {
namespace sim {

const char* GpuProfiler::passName(Pass p) {
    switch (p) {
    case Pass::FrameBegin: return "FrameBegin";
    case Pass::Shadow: return "Shadow";
    case Pass::MainPass: return "MainPass";
    case Pass::UI: return "UI";
    case Pass::FrameEnd: return "FrameEnd";
    default: return "?";
    }
}

GpuProfiler::~GpuProfiler() { shutdown(); }

bool GpuProfiler::init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t /*queueFamilyIndex*/) {
    shutdown();
    m_device = device;
    if (!device) return false;

#if !KS_GPU_PROFILER_VK
    (void)physicalDevice;
    m_ready = false;
    return false;
#else
    if (!physicalDevice) return false;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    m_timestampPeriodNs = props.limits.timestampPeriod;
    if (m_timestampPeriodNs <= 0.0f || props.limits.timestampPeriod == 0.0f) {
        // Timestamps not meaningful
        m_ready = false;
        return false;
    }

    // Check queue timestamp support via props (limit only) — full check needs queue family flags
    for (int i = 0; i < kFramesInFlight; ++i) {
        VkQueryPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        ci.queryCount = static_cast<uint32_t>(kQueriesPerFrame);
        VkQueryPool pool = VK_NULL_HANDLE;
        if (vkCreateQueryPool(m_device, &ci, nullptr, &pool) != VK_SUCCESS) {
            shutdown();
            return false;
        }
        m_pools[static_cast<size_t>(i)] = pool;
        m_slotUsed[static_cast<size_t>(i)] = false;
    }

    m_frameIndex = 0;
    m_resolveIndex = -1;
    m_ready = true;
    return true;
#endif
}

void GpuProfiler::shutdown() {
#if KS_GPU_PROFILER_VK
    if (m_device) {
        for (auto& pool : m_pools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(m_device, pool, nullptr);
                pool = VK_NULL_HANDLE;
            }
        }
    }
#endif
    m_device = nullptr;
    m_ready = false;
    m_slotUsed.fill(false);
}

void GpuProfiler::resolveFrame(int slot) {
#if !KS_GPU_PROFILER_VK
    (void)slot;
    return;
#else
    if (slot < 0 || slot >= kFramesInFlight) return;
    if (!m_slotUsed[static_cast<size_t>(slot)]) return;

    std::array<uint64_t, kQueriesPerFrame> data{};
    VkResult r = vkGetQueryPoolResults(
        m_device,
        m_pools[static_cast<size_t>(slot)],
        0,
        kQueriesPerFrame,
        sizeof(data),
        data.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (r != VK_SUCCESS && r != VK_NOT_READY) return;
    if (r == VK_NOT_READY) return;

    auto ticksToMs = [&](uint64_t a, uint64_t b) -> double {
        if (b < a) return 0.0;
        return (static_cast<double>(b - a) * static_cast<double>(m_timestampPeriodNs)) / 1.0e6;
    };

    const uint64_t t0 = data[static_cast<size_t>(Pass::FrameBegin)];
    const uint64_t tEnd = data[static_cast<size_t>(Pass::FrameEnd)];
    if (tEnd > t0) {
        m_frameTimeMs = ticksToMs(t0, tEnd);
        const double a = 0.08;
        m_avgFrameTimeMs = (m_avgFrameTimeMs <= 0.0)
            ? m_frameTimeMs
            : (m_avgFrameTimeMs * (1.0 - a) + m_frameTimeMs * a);
    }

    auto passDelta = [&](Pass from, Pass to) {
        return ticksToMs(data[static_cast<size_t>(from)], data[static_cast<size_t>(to)]);
    };
    m_passMs[static_cast<size_t>(Pass::FrameBegin)] = 0.0;
    m_passMs[static_cast<size_t>(Pass::Shadow)] =
        passDelta(Pass::FrameBegin, Pass::Shadow);
    m_passMs[static_cast<size_t>(Pass::MainPass)] =
        passDelta(Pass::Shadow, Pass::MainPass);
    m_passMs[static_cast<size_t>(Pass::UI)] =
        passDelta(Pass::MainPass, Pass::UI);
    m_passMs[static_cast<size_t>(Pass::FrameEnd)] =
        passDelta(Pass::UI, Pass::FrameEnd);

    m_resolveIndex = slot;
#endif
}

void GpuProfiler::beginFrame(VkCommandBuffer cmd) {
    if (!isEnabled()) return;
#if !KS_GPU_PROFILER_VK
    (void)cmd;
    return;
#else
    std::lock_guard<std::mutex> lock(m_mutex);
    // Resolve the slot we are about to overwrite (2 frames old)
    const int slot = m_frameIndex;
    resolveFrame(slot);

    vkResetQueryPool(m_device, m_pools[static_cast<size_t>(slot)], 0, kQueriesPerFrame);
    m_slotUsed[static_cast<size_t>(slot)] = true;

    // First timestamp
    if (cmd) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            m_pools[static_cast<size_t>(slot)],
                            static_cast<uint32_t>(Pass::FrameBegin));
    }
#endif
}

void GpuProfiler::writeTimestamp(VkCommandBuffer cmd, Pass pass) {
    if (!isEnabled() || !cmd) return;
#if !KS_GPU_PROFILER_VK
    (void)pass;
    return;
#else
    const int idx = static_cast<int>(pass);
    if (idx < 0 || idx >= kPassCount) return;
    // FrameBegin is written in beginFrame; allow overwrite for simplicity
    const int slot = m_frameIndex;
    const VkPipelineStageFlagBits stage =
        (pass == Pass::FrameEnd || pass == Pass::MainPass || pass == Pass::UI)
            ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    vkCmdWriteTimestamp(cmd, stage,
                        m_pools[static_cast<size_t>(slot)],
                        static_cast<uint32_t>(idx));
#endif
}

void GpuProfiler::endFrame() {
    if (!isEnabled()) return;
#if !KS_GPU_PROFILER_VK
    return;
#else
    std::lock_guard<std::mutex> lock(m_mutex);
    // Advance ring; results resolved on reuse in beginFrame
    m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
#endif
}

} // namespace sim
} // namespace ks
