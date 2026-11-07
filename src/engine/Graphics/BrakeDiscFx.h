#pragma once
/**
 * The content's own brake-disc glow.
 *
 * Content describes its discs in [BRAKEDISC_FX], with [BRAKEDISC_FX_FRONT] and
 * [BRAKEDISC_FX_REAR] overriding one axis. The conversion stores those sections
 * verbatim in the car's model document, under "sections" (see Overrides.h), and
 * this reads the two keys that land on something this engine already has - the
 * glow it computes from disc temperature:
 *
 *   ACTIVE      0 turns the glow off, for a car whose disc shader the content
 *               replaced. It is the whole-FX switch, so [BRAKEDISC_FX] carries
 *               it; an axis that turns itself off leaves the other axis glowing.
 *   GLOW_MULT   scales the glow. There is one glow scalar per car, so when both
 *               axes declare one, the brighter of the two is what the discs can
 *               reach.
 *
 * The rest of the section (ceramic look, carved lines, holes, rim masking,
 * roughness) describes a disc shader this engine does not have, so it stays
 * where the conversion put it instead of being guessed at here. A car that
 * declares neither section keeps its glow exactly as it was.
 */
#include "FileFormat/Json.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ks {
namespace graphics {

struct BrakeDiscFx {
    bool declared = false; ///< the content said something about its discs
    bool active = true;    ///< [BRAKEDISC_FX] ACTIVE = 0 turns the glow off
    double glowMult = 1.0; ///< [BRAKEDISC_FX] GLOW_MULT, 1 = as computed
};

namespace brake_disc_detail {

inline bool readText(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

/** The number a content value carries, or `fallback` when it carries none.
 *  Values arrive as strings (the conversion quotes them, and cuts the inline
 *  comment before this point); "bright" must not become 0. */
inline double numberOr(const Json& value, double fallback) {
    if (!value.isString()) return fallback;
    const std::string text = value.toString();
    if (text.empty()) return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == nullptr || *end != 0) return fallback;
    return parsed;
}

/** One [BRAKEDISC_FX*] block of the model document, or null when absent. */
inline const Json* findSection(const Json& document, const char* name) {
    if (!document.isObject() || !document.contains("sections")) return nullptr;
    const Json& sections = document["sections"];
    if (!sections.isObject() || !sections.contains(name)) return nullptr;
    const Json& block = sections[name];
    return block.isObject() ? &block : nullptr;
}

} // namespace brake_disc_detail

/** Reads <car>/data/model.json (or <car>/model.json) and returns what the content
 *  says about its discs. Absent, unreadable or malformed input means "nothing
 *  declared", which leaves the glow untouched. */
inline BrakeDiscFx readBrakeDiscFx(const std::string& carDir) {
    using namespace brake_disc_detail;
    BrakeDiscFx out;
    if (carDir.empty()) return out;

    const std::filesystem::path root(carDir);
    std::string text;
    if (!readText(root / "data" / "model.json", text) && !readText(root / "model.json", text))
        return out;
    bool ok = false;
    const Json document = Json::parse(text, &ok);
    if (!ok) return out;

    const Json* base = findSection(document, "BRAKEDISC_FX");
    const Json* front = findSection(document, "BRAKEDISC_FX_FRONT");
    const Json* rear = findSection(document, "BRAKEDISC_FX_REAR");
    if (base == nullptr && front == nullptr && rear == nullptr) return out;

    out.declared = true;
    const auto axisActive = [](const Json* block) {
        if (block == nullptr || !block->contains("ACTIVE")) return true;
        return numberOr((*block)["ACTIVE"], 1.0) >= 0.5;
    };
    // ACTIVE is the whole-FX switch: the base section can turn everything off,
    // an axis only itself.
    out.active = axisActive(base) && (axisActive(front) || axisActive(rear));

    // The brighter of the axes is what the discs can reach, but only a declared
    // value counts: 0.5 must stay 0.5 and not be raised to the default 1.
    bool declaredMult = false;
    double glow = 1.0;
    for (const Json* block : {base, front, rear}) {
        if (block == nullptr || !block->contains("GLOW_MULT")) continue;
        const double value = std::max(0.0, numberOr((*block)["GLOW_MULT"], 1.0));
        glow = declaredMult ? std::max(glow, value) : value;
        declaredMult = true;
    }
    if (declaredMult) out.glowMult = glow;
    return out;
}

} // namespace graphics
} // namespace ks
