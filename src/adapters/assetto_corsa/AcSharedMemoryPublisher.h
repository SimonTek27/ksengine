#pragma once
/**
 * Publishes AcPhysics / Graphics / Static for third-party readers.
 * Windows: CreateFileMapping Local\\acpmf_*
 * Linux:   shm_open /acpmf_* (link with -lrt if needed)
 * Soft mirror fallback when mapping fails.
 */
#include "AcSharedMemory.h"
#include <cstdio>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace ks {
namespace ac {

/**
 * acs.exe (SharedMemoryWriter, FUN_140185f70) creates EVERY acpmf_* mapping
 * with a fixed size of 0x250 = 592 bytes and maps exactly that:
 *   CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x250, L"Local\\"+name)
 *   MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0x250)
 * AC-compatible readers (Live Timers, SimTools, dashboards...) open our named
 * mappings and map 592 bytes explicitly — a smaller mapping makes THEIR
 * MapViewOfFile fail, so never create a mapping below AC's fixed page size.
 * Pages bigger than 592 B (our static page) keep their full size: readers
 * mapping a 592 B prefix stay compatible.
 */
constexpr size_t kAcShmMappingBytes = 0x250; // 592, per acs.exe

constexpr size_t acShmMappingSize(size_t pageBytes) {
    return pageBytes < kAcShmMappingBytes ? kAcShmMappingBytes : pageBytes;
}

class AcSharedMemoryPublisher {
public:
    bool open() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_ok) return true;

        const size_t physBytes = acShmMappingSize(sizeof(AcPhysicsPage));
        const size_t gfxBytes = acShmMappingSize(sizeof(AcGraphicsPage));
        const size_t statBytes = acShmMappingSize(sizeof(AcStaticPage));

#ifdef _WIN32
        m_physMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, static_cast<DWORD>(physBytes), L"Local\\acpmf_physics");
        m_gfxMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      0, static_cast<DWORD>(gfxBytes), L"Local\\acpmf_graphics");
        m_statMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                       0, static_cast<DWORD>(statBytes), L"Local\\acpmf_static");
        if (!m_physMap || !m_gfxMap || !m_statMap) {
            std::fprintf(stderr, "AcSharedMemory: CreateFileMapping failed (%lu)\n",
                         static_cast<unsigned long>(GetLastError()));
            closeUnlocked();
            useSoft();
            return m_ok;
        }
        m_phys = static_cast<AcPhysicsPage*>(
            MapViewOfFile(m_physMap, FILE_MAP_ALL_ACCESS, 0, 0, physBytes));
        m_gfx = static_cast<AcGraphicsPage*>(
            MapViewOfFile(m_gfxMap, FILE_MAP_ALL_ACCESS, 0, 0, gfxBytes));
        m_stat = static_cast<AcStaticPage*>(
            MapViewOfFile(m_statMap, FILE_MAP_ALL_ACCESS, 0, 0, statBytes));
        if (!m_phys || !m_gfx || !m_stat) {
            closeUnlocked();
            useSoft();
            return m_ok;
        }
#else
        auto mapOne = [](const char* name, size_t sz, int& fdOut, void*& ptrOut) -> bool {
            fdOut = shm_open(name, O_CREAT | O_RDWR, 0666);
            if (fdOut < 0) return false;
            if (ftruncate(fdOut, static_cast<off_t>(sz)) != 0) {
                close(fdOut); fdOut = -1; return false;
            }
            ptrOut = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fdOut, 0);
            if (ptrOut == MAP_FAILED) {
                close(fdOut); fdOut = -1; ptrOut = nullptr; return false;
            }
            return true;
        };
        void *pp = nullptr, *pg = nullptr, *ps = nullptr;
        if (!mapOne("/acpmf_physics", physBytes, m_physFd, pp) ||
            !mapOne("/acpmf_graphics", gfxBytes, m_gfxFd, pg) ||
            !mapOne("/acpmf_static", statBytes, m_statFd, ps)) {
            std::fprintf(stderr, "AcSharedMemory: POSIX shm_open failed — soft mirror\n");
            closeUnlocked();
            useSoft();
            return m_ok;
        }
        m_phys = static_cast<AcPhysicsPage*>(pp);
        m_gfx = static_cast<AcGraphicsPage*>(pg);
        m_stat = static_cast<AcStaticPage*>(ps);
#endif
        // Zero the whole mapping (incl. the 592 B padding AC readers see).
        std::memset(static_cast<void*>(m_phys), 0, physBytes);
        std::memset(static_cast<void*>(m_gfx), 0, gfxBytes);
        std::memset(static_cast<void*>(m_stat), 0, statBytes);
        m_ok = true;
        m_mapped = true;
        std::fprintf(stderr, "AcSharedMemory: publisher ready (mapped)\n");
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        closeUnlocked();
    }

    /** Write live snapshot to shared pages (thread-safe). */
    void publish(const AcLiveInput& in) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_ok) {
            useSoft();
        }
        if (!m_phys || !m_gfx || !m_stat) return;

        fillPhysics(*m_phys, in);
        fillGraphics(*m_gfx, in);
        if (!m_staticFilled) {
            fillStatic(*m_stat, in);
            m_staticFilled = true;
        } else {
            // keep dynamic static fields fresh
            m_stat->maxRpm = in.maxRpm;
            m_stat->maxFuel = in.maxFuel;
            m_stat->trackSPlineLength = in.trackSplineLength;
            m_stat->sectorCount = in.sectorCount;
        }
    }

    void invalidateStatic() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_staticFilled = false;
    }

    bool isOpen() const { return m_ok; }
    bool isMapped() const { return m_mapped; }

    const AcPhysicsPage* physics() const { return m_phys; }
    const AcGraphicsPage* graphics() const { return m_gfx; }
    const AcStaticPage* staticPage() const { return m_stat; }

    /** Last packet ids (for tests). */
    int physicsPacketId() const { return m_phys ? m_phys->packetId : 0; }

private:
    void useSoft() {
        m_physSoft = {};
        m_gfxSoft = {};
        m_statSoft = {};
        m_phys = &m_physSoft;
        m_gfx = &m_gfxSoft;
        m_stat = &m_statSoft;
        m_ok = true;
        m_mapped = false;
        std::fprintf(stderr, "AcSharedMemory: soft in-process mirror active\n");
    }

    void closeUnlocked() {
#ifdef _WIN32
        if (m_phys && m_phys != &m_physSoft) { UnmapViewOfFile(m_phys); }
        if (m_gfx && m_gfx != &m_gfxSoft) { UnmapViewOfFile(m_gfx); }
        if (m_stat && m_stat != &m_statSoft) { UnmapViewOfFile(m_stat); }
        m_phys = nullptr;
        m_gfx = nullptr;
        m_stat = nullptr;
        if (m_physMap) { CloseHandle(m_physMap); m_physMap = nullptr; }
        if (m_gfxMap) { CloseHandle(m_gfxMap); m_gfxMap = nullptr; }
        if (m_statMap) { CloseHandle(m_statMap); m_statMap = nullptr; }
#else
        auto unmap = [](void* p, size_t sz, int fd) {
            if (p && p != MAP_FAILED) munmap(p, sz);
            if (fd >= 0) close(fd);
        };
        if (m_phys && m_phys != &m_physSoft)
            unmap(m_phys, acShmMappingSize(sizeof(AcPhysicsPage)), m_physFd);
        if (m_gfx && m_gfx != &m_gfxSoft)
            unmap(m_gfx, acShmMappingSize(sizeof(AcGraphicsPage)), m_gfxFd);
        if (m_stat && m_stat != &m_statSoft)
            unmap(m_stat, acShmMappingSize(sizeof(AcStaticPage)), m_statFd);
        m_phys = nullptr;
        m_gfx = nullptr;
        m_stat = nullptr;
        m_physFd = m_gfxFd = m_statFd = -1;
#endif
        m_ok = false;
        m_mapped = false;
        m_staticFilled = false;
    }

    std::mutex m_mutex;
    bool m_ok = false;
    bool m_mapped = false;
    bool m_staticFilled = false;
    AcPhysicsPage* m_phys = nullptr;
    AcGraphicsPage* m_gfx = nullptr;
    AcStaticPage* m_stat = nullptr;
    AcPhysicsPage m_physSoft{};
    AcGraphicsPage m_gfxSoft{};
    AcStaticPage m_statSoft{};
#ifdef _WIN32
    HANDLE m_physMap = nullptr, m_gfxMap = nullptr, m_statMap = nullptr;
#else
    int m_physFd = -1, m_gfxFd = -1, m_statFd = -1;
#endif
};

} // namespace ac
} // namespace ks
