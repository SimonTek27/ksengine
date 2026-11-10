#include "HardwareBackend.h"

#include "Audio/AudioDevices.h"
#include "devices/DeviceManager.h"

#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ks::hw {
namespace {

std::vector<std::string> names(const std::vector<ks::audio::AudioDeviceInfo>& devices)
{
    std::vector<std::string> out;
    out.reserve(devices.size());
    for (const auto& device : devices) {
        if (device.isNull()) continue;
        out.push_back(device.description.empty() ? device.id : device.description);
    }
    return out;
}

/** The loader handle, or null when the platform has no video runtime. */
void* loadVideoRuntime()
{
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryA("vulkan-1.dll"));
#else
    return dlopen("libvulkan.so.1", RTLD_LAZY);
#endif
}

void unloadVideoRuntime(void* handle)
{
    if (handle == nullptr) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

/** vkEnumerateInstanceVersion, when the loader exports it (1.1 and later). */
void* videoVersionEntry(void* handle)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), "vkEnumerateInstanceVersion"));
#else
    return dlsym(handle, "vkEnumerateInstanceVersion");
#endif
}

/** What the device hub knows, when the application has created it. The engine
 *  never creates it: input belongs to the app, and a headless run has none. */
std::string controlsSummary()
{
    const ks::device::DeviceManager* devices = ks::device::DeviceManager::instance();
    if (devices == nullptr) return "controls=not initialised";
    const auto status = devices->status();
    std::string line = std::string("controls=") +
                       (status.steeringWheelConnected ? "wheel" : "none");
    if (status.vrConnected) line += "+vr";
    if (status.tripleMonitorConnected)
        line += "+" + std::to_string(status.monitorCount) + "monitors";
    if (!status.activeDeviceName.empty()) line += " (" + status.activeDeviceName + ")";
    return line;
}

} // namespace

std::vector<std::string> audioOutputNames() { return names(ks::audio::outputDevices()); }

std::vector<std::string> audioInputNames() { return names(ks::audio::inputDevices()); }

std::string defaultAudioOutputName()
{
    const auto device = ks::audio::defaultOutputDevice();
    if (device.isNull()) return {};
    return device.description.empty() ? device.id : device.description;
}

bool videoRuntimePresent()
{
    void* handle = loadVideoRuntime();
    if (handle == nullptr) return false;
    unloadVideoRuntime(handle);
    return true;
}

std::string videoRuntimeVersion()
{
    void* handle = loadVideoRuntime();
    if (handle == nullptr) return {};
    std::string version;
    using EnumerateVersion = unsigned int (*)(unsigned int*);
    auto enumerate = reinterpret_cast<EnumerateVersion>(videoVersionEntry(handle));
    if (enumerate != nullptr) {
        unsigned int api = 0;
        // VK_SUCCESS is 0; anything else leaves the version unknown.
        if (enumerate(&api) == 0) {
            version = std::to_string((api >> 22) & 0x7Fu) + "." +
                      std::to_string((api >> 12) & 0x3FFu) + "." +
                      std::to_string(api & 0xFFFu);
        }
    }
    unloadVideoRuntime(handle);
    return version;
}

std::string summarize()
{
    const auto outputs = audioOutputNames();
    std::string line = "hw: audio outputs=" + std::to_string(outputs.size());
    const std::string defaultOutput = defaultAudioOutputName();
    if (!defaultOutput.empty()) line += " default=\"" + defaultOutput + "\"";
    line += "; video=";
    if (videoRuntimePresent()) {
        line += "vulkan";
        const std::string version = videoRuntimeVersion();
        if (!version.empty()) line += " " + version;
    } else {
        line += "none";
    }
    line += "; " + controlsSummary();
    return line;
}

} // namespace ks::hw
