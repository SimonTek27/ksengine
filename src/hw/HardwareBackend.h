#pragma once
/**
 * Hardware facade for ksengine.
 *
 * The general engine (scene, physics, content, AI) does not talk to devices,
 * WASAPI or the Vulkan loader itself: it asks this module, whose backends are
 * compiled into ksenginehw (src/hw/CMakeLists.txt) and linked into the engine.
 * Everything here reports what the machine actually answers — no capability is
 * assumed and nothing is cached.
 */
#include <string>
#include <vector>

namespace ks::hw {

/** Friendly names of the audio outputs the machine reports (WASAPI). */
std::vector<std::string> audioOutputNames();
/** Friendly names of the audio inputs the machine reports (WASAPI). */
std::vector<std::string> audioInputNames();
/** Friendly name of the default audio output; empty when there is none. */
std::string defaultAudioOutputName();

/** Whether the platform's video runtime (vulkan-1) can be loaded. */
bool videoRuntimePresent();
/** Version the loader reports ("1.3.280"), empty when it cannot be queried. */
std::string videoRuntimeVersion();

/** One line per hardware group, for the engine's start-up log. */
std::string summarize();

} // namespace ks::hw
