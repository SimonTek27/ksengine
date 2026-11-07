#pragma once
/**
 * The electronic aids a car declares, and the slip each one works at.
 *
 * data/electronics.ini says which systems the car has and where it lets the
 * tyres slip: [ABS] and [TRACTION_CONTROL] both carry SLIP_RATIO_LIMIT. The
 * conversion keeps them in data.json under "electronics" (and derives the newer
 * ABS_V2 block from the older [ABS] one, carrying the limit over), so the
 * simulator cuts drive and brake torque where the car was built to, instead of
 * cutting every car at one value.
 *
 * These are the limits AC and CSP themselves read for the same systems: a
 * traction control that starts cutting at 0.08 and one that waits for 0.12 are
 * different cars, and the number is the content's to state.
 *
 * A car that declares neither keeps the limits this model used before the file
 * was read, so nothing moves for content that says nothing.
 */
#include "FileFormat/Json.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ks {
namespace physics {

struct ElectronicsLimits {
    bool declaredTractionControl = false;
    bool declaredAbs = false;
    float tcSlipRatioLimit = 0.10f;  ///< drive torque is cut above this |kappa|
    float absSlipRatioLimit = 0.12f; ///< brake torque is cut above this |kappa|

    /** [EDL]: the brake-based aid that slows one spinning rear wheel instead of
     *  cutting the engine. Spin is the wheel's ratio over the car's speed, so
     *  0.55 means "may spin half again as fast as the car is moving". The
     *  conversion writes this block for every converted car, carrying AC's own
     *  reference values where the car declares none, so `declaredEdl` says the
     *  document has them, not that the car was built with the system. */
    bool declaredEdl = false;
    float edlMaxSpinPower = 0.55f;
    float edlMaxSpinCoast = 0.40f;
    float edlBrakeTorquePower = 140.0f;
    float edlBrakeTorqueCoast = 420.0f;
    float edlDeadZonePower = 0.12f;
    float edlDeadZoneCoast = 0.12f;
};

namespace electronics_detail {

inline bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

/** A member by name, compared folded: content is not consistent about case, so
 *  "Traction_Control" and "TRACTION_CONTROL" are one section here. */
inline const Json* member(const Json& object, const char* name) {
    if (!object.isObject()) return nullptr;
    if (object.contains(name)) return &object[name];
    for (auto it = object.begin(); it != object.end(); ++it)
        if (sameName(it.key(), name)) return &it.value();
    return nullptr;
}

/** The object member `name`, or null when it is absent or not an object. */
inline const Json* child(const Json& object, const char* name) {
    const Json* found = member(object, name);
    return (found != nullptr && found->isObject()) ? found : nullptr;
}

/** The number a declared value carries. Content writes "0.08" as a string
 *  through the conversion, but a hand-edited document may hold a number, and
 *  a hand-edited key may be lower case: all three are read the same way. */
inline bool numberOf(const Json& object, const char* name, double& out) {
    const Json* value = member(object, name);
    if (value == nullptr) return false;
    const std::string text = value->toString();
    if (!text.empty()) {
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        // A string that is not a number at all ("gentle") is not a limit: the
        // caller must not see a 0 it would then have to guess about.
        if (end == nullptr || end == text.c_str() || *end != 0) return false;
        out = parsed;
        return true;
    }
    // Held as a number in a hand-edited document, not as the string the
    // conversion writes.
    out = value->toDouble();
    return true;
}

/** A slip ratio limit worth honouring: a positive fraction well below 1. */
inline bool usableLimit(double value) { return value > 0.001 && value < 0.5; }

} // namespace electronics_detail

/** Reads the electronics block of a car document (data.json). Absent, unreadable
 *  or nonsense values leave the defaults in place and nothing declared. */
inline ElectronicsLimits readElectronicsLimits(const Json& document) {
    using namespace electronics_detail;
    ElectronicsLimits out;
    if (!document.isObject() || !document.contains("electronics")) return out;
    const Json& electronics = document["electronics"];
    if (!electronics.isObject()) return out;

    // Traction control: the car's own [TRACTION_CONTROL] block. EDL is derived
    // from it by the conversion and carries spin power, not a slip limit.
    if (const Json* tc = child(electronics, "TRACTION_CONTROL")) {
        double limit = 0.0;
        if (numberOf(*tc, "SLIP_RATIO_LIMIT", limit) && usableLimit(limit)) {
            out.tcSlipRatioLimit = static_cast<float>(limit);
            out.declaredTractionControl = true;
        }
    }
    // ABS: the car's [ABS] block, or the ABS_V2 the conversion derived from it.
    for (const char* name : {"ABS", "ABS_V2"}) {
        const Json* abs = child(electronics, name);
        if (abs == nullptr) continue;
        double limit = 0.0;
        if (numberOf(*abs, "SLIP_RATIO_LIMIT", limit) && usableLimit(limit)) {
            out.absSlipRatioLimit = static_cast<float>(limit);
            out.declaredAbs = true;
            break;
        }
    }
    // EDL: the brake-based traction aid. A spin ratio belongs in (0, 1), a brake
    // torque in a range a car can actually produce, a dead zone in [0, 0.5);
    // anything else keeps the default instead of being clamped into a shape the
    // car never asked for.
    if (const Json* edl = child(electronics, "EDL")) {
        bool declared = false;
        const auto readSpin = [&](const char* name, float& target) {
            double value = 0.0;
            if (!numberOf(*edl, name, value) || value <= 0.0 || value >= 1.0) return;
            target = static_cast<float>(value);
            declared = true;
        };
        readSpin("MAX_SPIN_POWER", out.edlMaxSpinPower);
        readSpin("MAX_SPIN_COAST", out.edlMaxSpinCoast);
        double value = 0.0;
        if (numberOf(*edl, "BRAKE_TORQUE_POWER", value) && value > 0.0 && value < 10000.0) {
            out.edlBrakeTorquePower = static_cast<float>(value);
            declared = true;
        }
        if (numberOf(*edl, "BRAKE_TORQUE_COAST", value) && value > 0.0 && value < 10000.0) {
            out.edlBrakeTorqueCoast = static_cast<float>(value);
            declared = true;
        }
        if (numberOf(*edl, "DEAD_ZONE_POWER", value) && value >= 0.0 && value < 0.5)
            out.edlDeadZonePower = static_cast<float>(value);
        if (numberOf(*edl, "DEAD_ZONE_COAST", value) && value >= 0.0 && value < 0.5)
            out.edlDeadZoneCoast = static_cast<float>(value);
        out.declaredEdl = declared;
    }
    return out;
}

/** Reads the electronics of a car folder: data/data.json, or data.json for the
 *  cars that keep it at the root. A car with no manifest declares nothing. */
inline ElectronicsLimits readElectronicsLimitsForCar(const std::string& carDir) {
    if (carDir.empty()) return ElectronicsLimits{};
    const std::filesystem::path root(carDir);
    std::string text;
    for (const std::filesystem::path& rel : {std::filesystem::path("data/data.json"),
                                             std::filesystem::path("data.json")}) {
        std::ifstream in(root / rel, std::ios::binary);
        if (!in) continue;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
        break;
    }
    if (text.empty()) return ElectronicsLimits{};
    bool ok = false;
    const Json document = Json::parse(text, &ok);
    if (!ok) return ElectronicsLimits{};
    return readElectronicsLimits(document);
}

} // namespace physics
} // namespace ks
