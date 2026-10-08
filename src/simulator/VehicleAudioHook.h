#pragma once
/**
 * Bridge vehicle SoundPackRef → SimulatorAudio.
 * Call after loadCarAudio() when race config / upgrades resolve a sound pack.
 */
#include "SimulatorAudio.h"
#include "engine/vehicle/RaceComponentConfig.h"
#include <string>
#include <unordered_map>
#include <cstdio>

namespace ks {
namespace sim {

/**
 * Resolve path relative to car directory (or leave absolute).
 */
inline std::string resolveAudioPath(const std::string& carDirectory, const std::string& path) {
    if (path.empty()) return {};
    if (path.size() > 1 && (path[0] == '/' || (path.size() > 2 && path[1] == ':')))
        return path; // absolute Unix / Windows
    if (carDirectory.empty()) return path;
    if (carDirectory.back() == '/' || carDirectory.back() == '\\')
        return carDirectory + path;
    return carDirectory + "/" + path;
}

/**
 * Apply appearance sound pack on top of car baseline audio.
 * @return true if bank and/or sounds.ini applied successfully (partial ok).
 */
inline bool applyVehicleSoundPack(SimulatorAudio& audio,
                                  const ks::vehicle::SoundPackRef& pack,
                                  const std::string& carDirectory) {
    if (pack.bankPath.empty() && pack.soundsIni.empty() && pack.sampleOverrides.empty()
        && pack.engineIni.empty()) {
        std::fprintf(stderr, "VehicleAudioHook: empty sound pack, skip\n");
        return false;
    }
    // SoundPackRef carries sample overrides in an ordered map, the audio
    // bank API speaks unordered_map: convert once (the refs are tiny).
    const std::unordered_map<std::string, std::string> overrides(pack.sampleOverrides.begin(),
                                                                 pack.sampleOverrides.end());
    return audio.applySoundPack(
        resolveAudioPath(carDirectory, pack.bankPath),
        resolveAudioPath(carDirectory, pack.soundsIni),
        resolveAudioPath(carDirectory, pack.engineIni),
        pack.engineGain,
        pack.exteriorGain,
        pack.turboGain,
        overrides,
        carDirectory);
}

/** Convenience from full appearance + car dir. */
template <typename AppearanceT>
inline bool applyAppearanceAudio(SimulatorAudio& audio,
                                 const AppearanceT& appearance,
                                 const std::string& carDirectory) {
    if (!appearance.hasSound()) return false;
    return applyVehicleSoundPack(audio, appearance.sound, carDirectory);
}

} // namespace sim
} // namespace ks
