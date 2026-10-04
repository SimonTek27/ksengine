#pragma once
/**
 * Race flag enum — shared by RaceSession and RaceSessionManager.
 * Kept in its own header so including it never drags in either session
 * type (they each define their own Penalty struct).
 */
#include <cstdint>

namespace ks::sim {

enum class RaceFlag : uint8_t {
    None = 0,
    Green = 1,
    Yellow = 2,
    Blue = 3,
    White = 4,
    Black = 5,
    Checkered = 6,
    Meatball = 7,  // mechanical black/orange
    SafetyCar = 8
};

} // namespace ks::sim
