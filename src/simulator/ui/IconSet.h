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

// Two people: online and LAN play.
inline constexpr const char* kUsers =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<circle cx=\"9\" cy=\"8\" r=\"3.6\"/>"
    "<path d=\"M2.6 20.4c0-3.5 2.9-6 6.4-6s6.4 2.5 6.4 6\"/>"
    "<path d=\"M16.2 5.2a3.6 3.6 0 0 1 0 6.9M17.6 14.8c2.3.7 3.9 2.6 3.9 5.1\"/>"
    "</svg>";

// One person: a session on your own.
inline constexpr const char* kUser =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<circle cx=\"12\" cy=\"8\" r=\"3.8\"/>"
    "<path d=\"M4.6 20.4c0-3.9 3.3-6.8 7.4-6.8s7.4 2.9 7.4 6.8\"/>"
    "</svg>";

// Download: an arrow into a tray, for extra content.
inline constexpr const char* kDownload =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M12 3.4v11.2M7.8 10.4L12 14.6l4.2-4.2\"/>"
    "<path d=\"M3.8 17.4v1.8a1.6 1.6 0 0 0 1.6 1.6h13.2a1.6 1.6 0 0 0 1.6-1.6v-1.8\"/>"
    "</svg>";

// Helmet: the driver's own page.
inline constexpr const char* kHelmet =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M4 15.4a8 8 0 0 1 16 0v2.2a2.4 2.4 0 0 1-2.4 2.4H6.4A2.4 2.4 0 0 1 4 17.6z\"/>"
    "<path d=\"M4 15.4h9.6a3 3 0 0 0 3-3\"/>"
    "</svg>";

// Info: a disc with the stem of an i, for the credits row.
inline constexpr const char* kInfo =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.6\"/>"
    "<path d=\"M12 10.8v5.8M12 7.4v.2\"/>"
    "</svg>";

// Exit: a door with the arrow leaving it.
inline constexpr const char* kExit =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M14 3.8h4.2a1.8 1.8 0 0 1 1.8 1.8v12.8a1.8 1.8 0 0 1-1.8 1.8H14\"/>"
    "<path d=\"M3.6 12h10.2M10.4 8.6L13.8 12l-3.4 3.4\"/>"
    "</svg>";

// Headset: the VR toggle.
inline constexpr const char* kVr =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M2.8 9.4h18.4v4.2a2.2 2.2 0 0 1-2.2 2.2h-3.4l-1.6-1.7h-2l-1.6 1.7H5a2.2 2.2 0 0 1-2.2-2.2z\"/>"
    "<circle cx=\"8\" cy=\"12.2\" r=\"1.7\"/>"
    "<circle cx=\"16\" cy=\"12.2\" r=\"1.7\"/>"
    "</svg>";

// Speaker with two waves: audio settings.
inline constexpr const char* kSpeaker =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<path d=\"M4 9.6h3.4L12 6.2v11.6L7.4 14.4H4z\"/>"
    "<path d=\"M15.2 9.4a3.8 3.8 0 0 1 0 5.2\"/>"
    "<path d=\"M17.6 6.9a7 7 0 0 1 0 10.2\"/>"
    "</svg>";

// Keyboard: change bindings.
inline constexpr const char* kKeyboard =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<rect x=\"2.6\" y=\"6.8\" width=\"18.8\" height=\"10.4\" rx=\"2\"/>"
    "<path d=\"M6.4 10.4h1.4M10.1 10.4h1.4M13.8 10.4h1.4M17.5 10.4h1.4\"/>"
    "<path d=\"M8 13.9h8\"/>"
    "</svg>";

// Clock: system info.
inline constexpr const char* kClock =
    "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.8\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.4\"/>"
    "<path d=\"M12 7.6V12l3.2 2\"/>"
    "</svg>";

/** SVG source for an id, or nullptr when the id is not in the set. */
inline const char* source(const char* id) {
    if (id == nullptr || *id == '\0') return nullptr;
    static const struct { const char* id; const char* svg; } kTable[] = {
        {"drive", kDrive},
        {"restart", kRestart},
        {"setup", kSetup},
        {"settings", kSettings},
        {"users", kUsers},
        {"user", kUser},
        {"download", kDownload},
        {"helmet", kHelmet},
        {"info", kInfo},
        {"exit", kExit},
        {"vr", kVr},
        {"speaker", kSpeaker},
        {"keyboard", kKeyboard},
        {"clock", kClock},
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
