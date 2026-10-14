/**
 * AcSharedMemoryPublisher — AC mapping-size parity test.
 *
 * acs.exe (SharedMemoryWriter, FUN_140185f70) creates every acpmf_* mapping
 * with a FIXED size of 0x250 = 592 bytes. AC-compatible readers (Live Timers,
 * SimTools, dashboards) open the named mapping and map 592 bytes explicitly:
 * if kssim created a smaller mapping their MapViewOfFile would FAIL.
 *
 * This test opens the publisher, then re-opens each mapping by name exactly
 * like an external AC client would and maps 0x250 bytes explicitly — the map
 * must succeed (and the POSIX shm object must be at least 592 B).
 */
#include "KsTest.h"
#include "adapters/assetto_corsa/AcSharedMemoryPublisher.h"

#include <cstring>

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

using ks::ac::AcSharedMemoryPublisher;
using ks::ac::AcGraphicsPage;
using ks::ac::AcPhysicsPage;
using ks::ac::AcStaticPage;
using ks::ac::acShmMappingSize;
using ks::ac::kAcShmMappingBytes;

namespace {

// The size helper must never go below AC's fixed page size.
bool checkMappingSize() {
    KS_CHECK(kAcShmMappingBytes == 0x250);
    KS_CHECK(acShmMappingSize(sizeof(AcPhysicsPage)) >= kAcShmMappingBytes);
    KS_CHECK(acShmMappingSize(sizeof(AcGraphicsPage)) >= kAcShmMappingBytes);
    KS_CHECK(acShmMappingSize(sizeof(AcStaticPage)) >= kAcShmMappingBytes);
    // Idempotent on already-large pages: static is bigger than AC's 592 B.
    KS_CHECK(acShmMappingSize(kAcShmMappingBytes) == kAcShmMappingBytes);
    KS_CHECK(acShmMappingSize(kAcShmMappingBytes + 1) == kAcShmMappingBytes + 1);
    return true;
}

#ifdef _WIN32
// Map 0x250 bytes explicitly, exactly like an AC reader does.
bool checkNamedMapping(const wchar_t* name) {
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!map) {
        std::fprintf(stderr, "OpenFileMappingW(%ls) failed: %lu\n",
                     name, static_cast<unsigned long>(GetLastError()));
        return false;
    }
    void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, kAcShmMappingBytes);
    bool ok = (view != nullptr);
    if (!ok) {
        std::fprintf(stderr, "MapViewOfFile(%ls, 0x%zX) failed: %lu — "
                     "mapping smaller than AC's fixed page size?\n",
                     name, kAcShmMappingBytes,
                     static_cast<unsigned long>(GetLastError()));
    }
    if (view) UnmapViewOfFile(view);
    CloseHandle(map);
    return ok;
}
#else
// fstat the shm object: must be at least AC's fixed 592 B, then mmap 592 B.
bool checkNamedMapping(const char* name) {
    int fd = shm_open(name, O_RDONLY, 0);
    if (fd < 0) {
        std::fprintf(stderr, "shm_open(%s) failed\n", name);
        return false;
    }
    struct stat st;
    bool ok = (fstat(fd, &st) == 0) &&
              (static_cast<size_t>(st.st_size) >= kAcShmMappingBytes);
    if (!ok)
        std::fprintf(stderr, "%s is smaller than AC's fixed page size\n", name);
    void* view = ok ? mmap(nullptr, kAcShmMappingBytes, PROT_READ, MAP_SHARED, fd, 0)
                    : MAP_FAILED;
    if (view == MAP_FAILED) {
        std::fprintf(stderr, "mmap(%s, 0x%zX) failed\n", name, kAcShmMappingBytes);
        ok = false;
    } else if (view) {
        munmap(view, kAcShmMappingBytes);
    }
    close(fd);
    return ok;
}
#endif

} // namespace

int main() {
    KS_CHECK(checkMappingSize());

    AcSharedMemoryPublisher pub;
    // On a headless CI the mapping may fail (soft mirror) — that path is fine
    // for this test's target, which is the mapping SIZE when it does exist.
    pub.open();
#ifdef _WIN32
    KS_CHECK(checkNamedMapping(L"Local\\acpmf_physics"));
    KS_CHECK(checkNamedMapping(L"Local\\acpmf_graphics"));
    KS_CHECK(checkNamedMapping(L"Local\\acpmf_static"));
#else
    KS_CHECK(checkNamedMapping("/acpmf_physics"));
    KS_CHECK(checkNamedMapping("/acpmf_graphics"));
    KS_CHECK(checkNamedMapping("/acpmf_static"));
#endif
    pub.close();

    return KS_TEST_RESULT("ac_shared_memory_publisher_test");
}
