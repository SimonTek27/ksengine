// Native FMOD Studio bank playback behind LoadLibrary: no link dependency,
// no shipped DLLs, no FMOD headers. Every function used here was verified in
// the export table of the 1.08.12 fmodstudio64.dll shipped with Assetto
// Corsa, and every behaviour (init flags, sample pump, common.bank) was
// proven against the ferrari_499p bank before being written down.
#include "FmodBankPlayer.h"
#include "audio_backend_resource.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ks {
namespace audio {

namespace {

// Opaque FMOD handles: never dereferenced, only passed back.
struct SysT;
struct BankT;
struct DescT;
struct InstT;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Attr3D {
    Vec3 pos, vel, fwd, up;
};

typedef int Res;
typedef Res (*FnCreate)(SysT**, unsigned);
typedef Res (*FnInit)(SysT*, int, unsigned, unsigned, void*);
typedef Res (*FnRelease)(SysT*);
typedef Res (*FnUpdate)(SysT*);
typedef Res (*FnFlushCommands)(SysT*);
typedef Res (*FnLoadBank)(SysT*, const char*, unsigned, BankT**);
typedef Res (*FnUnloadBank)(BankT*);
typedef Res (*FnLoadSamples)(BankT*);
typedef Res (*FnSampleState)(BankT*, int*);
typedef Res (*FnEventCount)(BankT*, int*);
typedef Res (*FnEventList)(BankT*, DescT**, int, int*);
typedef Res (*FnDescGuid)(DescT*, FmodGuid*);
typedef Res (*FnGetByID)(SysT*, const FmodGuid*, DescT**);
typedef Res (*FnCreateInst)(DescT*, InstT**);
typedef Res (*FnReleaseInst)(InstT*);
typedef Res (*FnStart)(InstT*);
typedef Res (*FnStop)(InstT*, int);
typedef Res (*FnSetParam)(InstT*, const char*, float);
typedef Res (*FnSetVolume)(InstT*, float);
typedef Res (*FnIsPlaying)(InstT*, int*);
typedef Res (*FnTimeline)(InstT*, unsigned*);
typedef Res (*FnSetListener)(SysT*, int, const Attr3D*);

// FMOD_STUDIO_INIT_ALLOW_MISSING_PLUGINS: without it LoadBankFile fails on
// every AC bank tried (stock and mod alike); with it the metadata loads.
constexpr unsigned kInitAllowMissingPlugins = 0x00000002u;
// Header versions Create() is tried with, newest AC install first.
constexpr unsigned kHeaderVersions[] = {0x0001080Cu, 0x00020200u, 0x00020100u};

// FMOD_STUDIO_LOADING_STATE, for the sample pump below.
constexpr int kLoadingLoaded = 2;
// FMOD_STUDIO_PLAYBACK_STATE.
constexpr int kPlaybackPlaying = 0;

} // namespace

struct FmodBankPlayer::Impl {
#ifdef _WIN32
    HMODULE dll = nullptr;
#endif
    FnLoadBank loadBank = nullptr;
    FnUpdate update = nullptr;
    FnSetListener setListener = nullptr;
    FnUnloadBank unloadBank = nullptr;
    FnLoadSamples loadSamples = nullptr;
    FnSampleState sampleState = nullptr;
    FnEventCount eventCount = nullptr;
    FnEventList eventList = nullptr;
    FnDescGuid descGuid = nullptr;
    FnGetByID getByID = nullptr;
    FnCreateInst createInst = nullptr;
    FnReleaseInst releaseInst = nullptr;
    FnStart start = nullptr;
    FnStop stop = nullptr;
    FnSetParam setParam = nullptr;
    FnSetVolume setVolume = nullptr;
    FnIsPlaying isPlaying = nullptr;
    FnTimeline timeline = nullptr;
    FnRelease release = nullptr;
    SysT* sys = nullptr;
    std::vector<BankT*> banks;
    std::vector<InstT*> instances;
};

bool FmodBankPlayer::parseGuid(const std::string& text, FmodGuid& guid) {
    if (text.size() != 38 || text.front() != '{' || text.back() != '}') return false;
    unsigned d[11] = {};
    if (std::sscanf(text.c_str(), "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &d[0],
                     &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7], &d[8], &d[9],
                     &d[10]) != 11)
        return false;
    guid.data1 = d[0];
    guid.data2 = static_cast<std::uint16_t>(d[1]);
    guid.data3 = static_cast<std::uint16_t>(d[2]);
    for (int i = 0; i < 8; ++i) guid.data4[i] = static_cast<std::uint8_t>(d[3 + i]);
    return true;
}

FmodBankPlayer::FmodBankPlayer() : m_impl(new Impl()) {}
FmodBankPlayer::~FmodBankPlayer() {
    shutdown();
    delete m_impl;
    m_impl = nullptr;
}

#ifdef _WIN32

namespace {

template <typename Fn>
bool resolve(HMODULE dll, Fn& fn, const char* name, std::string& error) {
    fn = reinterpret_cast<Fn>(GetProcAddress(dll, name));
    if (fn == nullptr) {
        error = std::string("fmodstudio64.dll has no export ") + name;
        return false;
    }
    return true;
}

} // namespace

bool FmodBankPlayer::initialize(const std::string& dllDirectory) {
    if (m_impl->sys != nullptr) return true;

    std::string dir = dllDirectory;
    // If the DLLs are not on disk, try extracting them from the embedded
    // resources (compiled into this binary via fmod.rc).
    std::string dllPath = dir;
    if (!dllPath.empty() && dllPath.back() != '\\' && dllPath.back() != '/') dllPath += '\\';
    dllPath += "fmodstudio64.dll";
    if (!std::ifstream(dllPath).good()) {
        // Extract from resources
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string extractDir = std::string(tempPath) + "ksengine_fmod\\";
        CreateDirectoryA(extractDir.c_str(), nullptr);

        auto extractResource = [&](int resId, const char* name) -> bool {
            HRSRC hRes = FindResourceA(nullptr, MAKEINTRESOURCEA(resId), RT_RCDATA);
            if (!hRes) return false;
            HGLOBAL hData = LoadResource(nullptr, hRes);
            if (!hData) return false;
            DWORD size = SizeofResource(nullptr, hRes);
            void* data = LockResource(hData);
            if (!data || size == 0) return false;
            std::string outPath = extractDir + name;
            std::ofstream out(outPath, std::ios::binary);
            if (!out) return false;
            out.write(static_cast<const char*>(data), size);
            out.close();
            return true;
        };

        if (extractResource(IDR_AUDIO_BACKEND_64, "fmod64.dll") &&
            extractResource(IDR_AUDIO_BACKEND_STUDIO_64, "fmodstudio64.dll")) {
            dir = extractDir;
        } else {
            m_lastError = "FMOD DLLs not found and could not be extracted from resources";
            return false;
        }
    }

    if (dir.empty()) {
        m_lastError = "no FMOD directory: native bank playback stays off";
        return false;
    }
    SetDllDirectoryA(dir.c_str());
    std::string path = dir;
    if (!path.empty() && path.back() != '\\' && path.back() != '/') path += '\\';
    path += "fmodstudio64.dll";
    HMODULE dll = LoadLibraryA(path.c_str());
    if (dll == nullptr) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "cannot load %s (win32 %lu)", path.c_str(),
                      static_cast<unsigned long>(GetLastError()));
        m_lastError = buf;
        return false;
    }
    Impl& impl = *m_impl;
    impl.dll = dll;
    FnCreate create = nullptr;
    FnInit init = nullptr;
    FnRelease release = nullptr;
    bool tableOk = resolve(dll, create, "FMOD_Studio_System_Create", m_lastError) &&
                   resolve(dll, init, "FMOD_Studio_System_Initialize", m_lastError) &&
                   resolve(dll, release, "FMOD_Studio_System_Release", m_lastError) &&
                   resolve(dll, impl.update, "FMOD_Studio_System_Update", m_lastError) &&
                   resolve(dll, impl.setListener, "FMOD_Studio_System_SetListenerAttributes",
                           m_lastError) &&
                   resolve(dll, impl.unloadBank, "FMOD_Studio_Bank_Unload", m_lastError) &&
                   resolve(dll, impl.loadSamples, "FMOD_Studio_Bank_LoadSampleData",
                           m_lastError) &&
                   resolve(dll, impl.sampleState, "FMOD_Studio_Bank_GetSampleLoadingState",
                           m_lastError) &&
                   resolve(dll, impl.eventCount, "FMOD_Studio_Bank_GetEventCount",
                           m_lastError) &&
                   resolve(dll, impl.eventList, "FMOD_Studio_Bank_GetEventList", m_lastError) &&
                   resolve(dll, impl.descGuid, "FMOD_Studio_EventDescription_GetID",
                           m_lastError) &&
                   resolve(dll, impl.getByID, "FMOD_Studio_System_GetEventByID", m_lastError) &&
                   resolve(dll, impl.createInst, "FMOD_Studio_EventDescription_CreateInstance",
                           m_lastError) &&
                   resolve(dll, impl.releaseInst, "FMOD_Studio_EventInstance_Release",
                           m_lastError) &&
                   resolve(dll, impl.start, "FMOD_Studio_EventInstance_Start", m_lastError) &&
                   resolve(dll, impl.stop, "FMOD_Studio_EventInstance_Stop", m_lastError) &&
                   resolve(dll, impl.setParam, "FMOD_Studio_EventInstance_SetParameterValue",
                           m_lastError) &&
                   resolve(dll, impl.setVolume, "FMOD_Studio_EventInstance_SetVolume",
                           m_lastError) &&
                   resolve(dll, impl.isPlaying, "FMOD_Studio_EventInstance_GetPlaybackState",
                           m_lastError) &&
                   resolve(dll, impl.timeline, "FMOD_Studio_EventInstance_GetTimelinePosition",
                           m_lastError) &&
                   resolve(dll, impl.loadBank, "FMOD_Studio_System_LoadBankFile",
                           m_lastError);
    if (!tableOk) {
        FreeLibrary(dll);
        impl.dll = nullptr;
        return false;
    }
    impl.release = release;
    for (unsigned version : kHeaderVersions) {
        if (create(&impl.sys, version) == 0 && impl.sys != nullptr) break;
    }
    if (impl.sys == nullptr) {
        m_lastError = "FMOD_Studio_System_Create refused every known header version";
        FreeLibrary(dll);
        impl.dll = nullptr;
        return false;
    }
    if (init(impl.sys, 128, kInitAllowMissingPlugins, 0, nullptr) != 0) {
        m_lastError = "FMOD_Studio_System_Initialize failed (no audio device?)";
        release(impl.sys);
        impl.sys = nullptr;
        FreeLibrary(dll);
        impl.dll = nullptr;
        return false;
    }
    Attr3D ear;
    ear.fwd.z = 1.0f;
    ear.up.y = 1.0f;
    impl.setListener(impl.sys, 0, &ear);
    return true;
}

void FmodBankPlayer::shutdown() {
    Impl& impl = *m_impl;
    if (impl.sys == nullptr) return;
    for (InstT* inst : impl.instances) {
        if (inst != nullptr && impl.releaseInst != nullptr) impl.releaseInst(inst);
    }
    impl.instances.clear();
    for (BankT* bank : impl.banks) {
        if (bank != nullptr) impl.unloadBank(bank);
    }
    impl.banks.clear();
    if (impl.release != nullptr) impl.release(impl.sys);
    impl.sys = nullptr;
    if (impl.dll != nullptr) {
        FreeLibrary(impl.dll);
        impl.dll = nullptr;
    }
}

bool FmodBankPlayer::initialized() const { return m_impl->sys != nullptr; }

void FmodBankPlayer::update() {
    if (m_impl->sys != nullptr) m_impl->update(m_impl->sys);
}

int FmodBankPlayer::loadBank(const std::string& path) {
    Impl& impl = *m_impl;
    if (impl.sys == nullptr) {
        m_lastError = "native player is not initialized";
        return -1;
    }
    BankT* bank = nullptr;
    if (impl.loadBank(impl.sys, path.c_str(), 0, &bank) != 0 || bank == nullptr) {
        m_lastError = std::string("FMOD rejected the bank: ") + path;
        return -1;
    }
    impl.update(impl.sys);
    if (impl.loadSamples(bank) != 0) {
        m_lastError = "Bank_LoadSampleData refused the bank";
        impl.unloadBank(bank);
        return -1;
    }
    // The samples arrive asynchronously; a started instance can only go
    // PLAYING once they are LOADED. FlushSampleLoading was proven to leave
    // the load stuck, so this pumps Update() with a bounded wait instead.
    int state = -1;
    for (int i = 0; i < 400; ++i) {
        impl.update(impl.sys);
        impl.sampleState(bank, &state);
        if (state == kLoadingLoaded) break;
        if (state != 1) break; // 1 is LOADING; anything else is final
        Sleep(10);
    }
    if (state != kLoadingLoaded) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "bank samples never loaded (state %d)", state);
        m_lastError = buf;
        impl.unloadBank(bank);
        return -1;
    }
    impl.banks.push_back(bank);
    return static_cast<int>(impl.banks.size()) - 1;
}

int FmodBankPlayer::bankCount() const {
    return static_cast<int>(m_impl->banks.size());
}

void FmodBankPlayer::unloadBanks() {
    Impl& impl = *m_impl;
    for (InstT* inst : impl.instances) {
        if (inst != nullptr) impl.releaseInst(inst);
    }
    impl.instances.clear();
    for (BankT* bank : impl.banks) {
        if (bank != nullptr) impl.unloadBank(bank);
    }
    impl.banks.clear();
}

int FmodBankPlayer::eventCount(int bank) const {
    const Impl& impl = *m_impl;
    if (impl.sys == nullptr || bank < 0 ||
        bank >= static_cast<int>(impl.banks.size()))
        return 0;
    int count = 0;
    if (impl.eventCount(impl.banks[static_cast<std::size_t>(bank)], &count) != 0)
        return 0;
    return count;
}

bool FmodBankPlayer::eventGuid(int bank, int index, FmodGuid& guid) const {
    const Impl& impl = *m_impl;
    if (impl.sys == nullptr || bank < 0 ||
        bank >= static_cast<int>(impl.banks.size()))
        return false;
    BankT* handle = impl.banks[static_cast<std::size_t>(bank)];
    int count = 0;
    if (impl.eventCount(handle, &count) != 0 || index < 0 || index >= count)
        return false;
    std::vector<DescT*> list(static_cast<std::size_t>(count), nullptr);
    int got = 0;
    if (impl.eventList(handle, list.data(), count, &got) != 0 || index >= got)
        return false;
    return impl.descGuid(list[static_cast<std::size_t>(index)], &guid) == 0;
}

int FmodBankPlayer::findEvent(int bank, const FmodGuid& guid) const {
    const Impl& impl = *m_impl;
    if (impl.sys == nullptr || bank < 0 ||
        bank >= static_cast<int>(impl.banks.size()))
        return -1;
    DescT* found = nullptr;
    if (impl.getByID(impl.sys, &guid, &found) != 0 || found == nullptr) return -1;
    // Confirm the description belongs to this bank: enumerate and compare.
    BankT* handle = impl.banks[static_cast<std::size_t>(bank)];
    int count = 0;
    if (impl.eventCount(handle, &count) != 0) return -1;
    std::vector<DescT*> list(static_cast<std::size_t>(count), nullptr);
    int got = 0;
    if (impl.eventList(handle, list.data(), count, &got) != 0) return -1;
    for (int i = 0; i < got; ++i) {
        if (list[static_cast<std::size_t>(i)] == found) return i;
    }
    return -1;
}

int FmodBankPlayer::createInstance(int bank, int eventIndex) {
    Impl& impl = *m_impl;
    if (impl.sys == nullptr || bank < 0 ||
        bank >= static_cast<int>(impl.banks.size()))
        return -1;
    BankT* handle = impl.banks[static_cast<std::size_t>(bank)];
    int count = 0;
    if (impl.eventCount(handle, &count) != 0) return -1;
    std::vector<DescT*> list(static_cast<std::size_t>(count), nullptr);
    int got = 0;
    if (impl.eventList(handle, list.data(), count, &got) != 0) return -1;
    if (eventIndex < 0 || eventIndex >= got) return -1;
    InstT* inst = nullptr;
    DescT* desc = list[static_cast<std::size_t>(eventIndex)];
    if (impl.createInst(desc, &inst) != 0 || inst == nullptr) {
        m_lastError = "EventDescription_CreateInstance failed";
        return -1;
    }
    impl.instances.push_back(inst);
    return static_cast<int>(impl.instances.size()) - 1;
}

void FmodBankPlayer::releaseInstance(int instance) {
    Impl& impl = *m_impl;
    if (instance < 0 || instance >= static_cast<int>(impl.instances.size())) return;
    InstT*& slot = impl.instances[static_cast<std::size_t>(instance)];
    if (slot != nullptr) impl.releaseInst(slot);
    slot = nullptr;
}

static InstT* at(std::vector<InstT*>& v, int i) {
    if (i < 0 || i >= static_cast<int>(v.size())) return nullptr;
    return v[static_cast<std::size_t>(i)];
}

bool FmodBankPlayer::start(int instance) {
    Impl& impl = *m_impl;
    InstT* inst = at(impl.instances, instance);
    return inst != nullptr && impl.start(inst) == 0;
}

bool FmodBankPlayer::stop(int instance, bool fade) {
    Impl& impl = *m_impl;
    InstT* inst = at(impl.instances, instance);
    // FMOD_STUDIO_STOP_ALLOWFADEOUT = 0, IMMEDIATE = 1.
    return inst != nullptr && impl.stop(inst, fade ? 0 : 1) == 0;
}

bool FmodBankPlayer::setParameter(int instance, const char* name, float value) {
    Impl& impl = *m_impl;
    InstT* inst = at(impl.instances, instance);
    return inst != nullptr && name != nullptr && impl.setParam(inst, name, value) == 0;
}

bool FmodBankPlayer::setVolume(int instance, float volume) {
    Impl& impl = *m_impl;
    InstT* inst = at(impl.instances, instance);
    return inst != nullptr && impl.setVolume(inst, volume) == 0;
}

bool FmodBankPlayer::isPlaying(int instance) const {
    const Impl& impl = *m_impl;
    if (instance < 0 || instance >= static_cast<int>(impl.instances.size())) return false;
    InstT* inst = impl.instances[static_cast<std::size_t>(instance)];
    if (inst == nullptr) return false;
    int state = -1;
    if (impl.isPlaying(inst, &state) != 0) return false;
    return state == kPlaybackPlaying;
}

bool FmodBankPlayer::timelineMs(int instance, unsigned& ms) const {
    const Impl& impl = *m_impl;
    if (instance < 0 || instance >= static_cast<int>(impl.instances.size())) return false;
    InstT* inst = impl.instances[static_cast<std::size_t>(instance)];
    if (inst == nullptr) return false;
    unsigned value = 0;
    if (impl.timeline(inst, &value) != 0) return false;
    ms = value;
    return true;
}

bool FmodBankPlayer::setListener(float px, float py, float pz, float vx, float vy, float vz,
                                 float fx, float fy, float fz, float ux, float uy, float uz) {
    Impl& impl = *m_impl;
    if (impl.sys == nullptr) return false;
    Attr3D ear;
    ear.pos = {px, py, pz};
    ear.vel = {vx, vy, vz};
    ear.fwd = {fx, fy, fz};
    ear.up = {ux, uy, uz};
    return impl.setListener(impl.sys, 0, &ear) == 0;
}

#else // non-Windows: same API, always off.

bool FmodBankPlayer::initialize(const std::string&) {
    m_lastError = "native FMOD playback needs the Windows game DLLs";
    return false;
}
void FmodBankPlayer::shutdown() {}
bool FmodBankPlayer::initialized() const { return false; }
void FmodBankPlayer::update() {}
int FmodBankPlayer::loadBank(const std::string&) { return -1; }

#endif

} // namespace audio
} // namespace ks
