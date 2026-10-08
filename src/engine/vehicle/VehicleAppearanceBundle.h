#pragma once
/**
 * Resolved appearance for a car at a given race round:
 *   nodes + livery + sound + physics
 */
#include "RaceComponentConfig.h"
#include "VehicleUpgradeSystem.h"
#include <string>
#include <vector>
#include <map>

namespace ks {
namespace vehicle {

struct VehicleAppearanceBundle {
    RaceComponentConfig components;
    LiveryRef livery;
    SoundPackRef sound;
    std::vector<std::string> enableNodes;
    std::vector<std::string> disableNodes;
    std::map<std::string, std::string> instanceSwaps;
    PhysicsMods physics;

    bool hasLivery() const {
        return !livery.folder.empty() || !livery.diffuse.empty() || !livery.id.empty();
    }
    bool hasSound() const {
        return !sound.bankPath.empty() || !sound.soundsIni.empty() || !sound.id.empty();
    }
};

/**
 * Priority for livery/sound:
 *   1. Selected UpgradeCategory::Livery / Sound (Engine may embed SoundBank)
 *   2. Race _rd*.ini [Livery] / [Sound]
 *   3. Empty → app keeps car defaults
 */
inline VehicleAppearanceBundle resolveAppearance(
    const std::string& kn5Path,
    int round,
    const VehicleUpgradeSystem* upgrades = nullptr) {

    VehicleAppearanceBundle out;
    out.components = RaceComponentConfigLoader::loadForRound(kn5Path, round);
    out.livery = out.components.livery;
    out.sound = out.components.sound;

    if (upgrades) {
        out.physics = upgrades->appliedMods();
        upgrades->collectNodeOverrides(out.enableNodes, out.disableNodes, out.instanceSwaps);

        for (const auto& t : upgrades->types()) {
            const auto* L = t.current();
            if (!L) continue;
            if (t.category == UpgradeCategory::Livery && L->hasLivery())
                out.livery = L->livery;
            if (t.category == UpgradeCategory::Sound && L->hasSound())
                out.sound = L->sound;
            if (t.category == UpgradeCategory::Engine && L->hasSound() && !out.hasSound())
                out.sound = L->sound;
        }
    }

    for (const auto& n : out.enableNodes)
        out.components.active.insert(n);
    for (const auto& n : out.disableNodes)
        out.components.inactive.insert(n);
    if (!out.enableNodes.empty() || !out.disableNodes.empty())
        out.components.hasExplicitList = true;

    return out;
}

/**
 * Node visibility for the resolved appearance (roadmap 2.9): a node listed
 * as disabled — race [Nodes] name=0 or upgrade DisableNode — is hidden;
 * everything else renders. The enable list never hides anything: the bake
 * manifest already carries every node, and treating `active` as a whitelist
 * (what RaceComponentConfig::isActive does once it is non-empty) would blank
 * the whole car whenever an ini lists only the wings.
 */
inline bool appearanceNodeVisible(const VehicleAppearanceBundle& app, const std::string& name)
{
    return app.components.inactive.count(name) == 0;
}

} // namespace vehicle
} // namespace ks
