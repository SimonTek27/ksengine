#pragma once
/** Apply setup params to vehicle — Sprint 1 helper. */
#include <string>
#include <array>

namespace ks {
namespace sim {

struct SetupParams {
    std::array<float, 4> tirePressure{1.8f, 1.8f, 1.8f, 1.8f};
    float brakeBias = 0.55f;
    float frontWing = 5.f;
    float rearWing = 8.f;
    float ballast = 0.f;
    float fuel = 40.f;
    float camberFl = -2.5f, camberFr = -2.5f, camberRl = -1.5f, camberRr = -1.5f;
    float toeFl = 0.f, toeFr = 0.f, toeRl = 0.1f, toeRr = 0.1f;
};

/** Lightweight result of loading a setup file (path only — implementation in ApplySetup.cpp if present). */
struct SetupLoadResult {
    bool ok = false;
    SetupParams params;
    std::string message;
};

} // namespace sim
} // namespace ks
