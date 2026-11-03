#pragma once
/**
 * The menu icon set.
 *
 * Each icon is a plain SVG document - 24x24 viewBox, stroke only, round-join
 * line art - so it can be pasted into any editor, edited and pasted back. The
 * instances are parsed once on first use and cached; parsing a dozen <path>
 * elements per frame would be work for nothing.
 *
 * Ids are the ones the menu rows carry (MenuItem::icon): a row whose id is
 * unknown simply draws without an icon.
 */
#include "SvgIcon.h"

#include <cstring>
#include <map>
#include <string>

namespace ks {
namespace sim {
namespace ui {
namespace icons {

// A steering wheel: the "drive / resume" glyph.
inline constexpr const char* kDrive =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.6\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"3\"/>"
    "<path d=\"M12 15v5.6M9.4 13.9 L4.2 11.4M14.6 13.9 L19.8 11.4\"/>"
    "</svg>";

// Restart: a circular arrow, counter-clockwise.
inline constexpr const char* kRestart =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M20 12a8 8 0 1 1-2.7-6\"/>"
    "<path d=\"M20 3.4V8.2h-4.8\"/>"
    "</svg>";

// Setup: three sliders, the garage glyph.
inline constexpr const char* kSetup =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M3.5 7h17M3.5 12h17M3.5 17h17\"/>"
    "<circle cx=\"9\" cy=\"7\" r=\"2.1\"/>"
    "<circle cx=\"15\" cy=\"12\" r=\"2.1\"/>"
    "<circle cx=\"8\" cy=\"17\" r=\"2.1\"/>"
    "</svg>";

// Settings: a gear.
inline constexpr const char* kSettings =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<circle cx=\"12\" cy=\"12\" r=\"3.1\"/>"
    "<path d=\"M12 2.8v3M12 18.2v3M2.8 12h3M18.2 12h3"
    "M5.5 5.5l2.1 2.1M16.4 16.4l2.1 2.1M18.5 5.5l-2.1 2.1M7.6 16.4l-2.1 2.1\"/>"
    "</svg>";

// Exit: a door with the arrow leaving it.
inline constexpr const char* kExit =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M14 3.8h4.2a1.8 1.8 0 0 1 1.8 1.8v12.8a1.8 1.8 0 0 1-1.8 1.8H14\"/>"
    "<path d=\"M3.6 12h10.2M10.4 8.6L13.8 12l-3.4 3.4\"/>"
    "</svg>";

/** SVG source for an id, or nullptr when the id is not in the set. */
inline const char* source(const char* id) {
    if (id == nullptr || *id == '\0') return nullptr;
    static const struct { const char* id; const char* svg; } kTable[] = {
        {"drive", kDrive},
        {"restart", kRestart},
        {"setup", kSetup},
        {"settings", kSettings},
        {"exit", kExit},
    };
    for (const auto& entry : kTable) {
        if (std::strcmp(entry.id, id) == 0) return entry.svg;
    }
    return nullptr;
}

/** Parsed icon for an id, or nullptr. Parsed on first use, then cached. */
inline const SvgIcon* get(const char* id) {
    const char* svg = source(id);
    if (svg == nullptr) return nullptr;
    static std::map<std::string, SvgIcon> cache;
    const auto found = cache.find(id);
    if (found != cache.end()) return &found->second;
    SvgIcon icon;
    if (!icon.parse(svg)) return nullptr;
    return &cache.emplace(id, std::move(icon)).first->second;
}

} // namespace icons
} // namespace ui
} // namespace sim
} // namespace ks
