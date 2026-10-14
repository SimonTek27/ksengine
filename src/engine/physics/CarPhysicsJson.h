#pragma once
#include "KsExport.h"
#include "KsTireModel.h"

#include <string>
#include <vector>

namespace ks {
namespace physics {

class VehicleSimulator;

/**
 * Unified car physics data — the in-memory form of data/physics.json.
 *
 * One file replaces the runtime physics .ini set a car folder used to carry
 * (engine.ini, tyres.ini, drivetrain.ini, aero.ini, suspension.ini). Every
 * field keeps the semantics of the .ini key it came from, so an AC car folder
 * converts 1:1. "has*" flags distinguish "absent, keep the vehicle default"
 * from "present, apply it" — the same rule the .ini loaders use with their
 * fallback arguments.
 */
struct CarPhysicsData {
    std::string name; ///< optional NAME (car identity only, not physics)

    // [CAR]
    bool hasMass = false;
    double massKg = 0.0;

    // [ENGINE] (was engine.ini)
    bool hasPowerKw = false;
    double powerKw = 0.0;
    bool hasLimiter = false;
    double limiterRpm = 0.0;
    bool hasFuelCapacity = false;
    double fuelCapacityL = 0.0;

    // [TYRES] (was tyres.ini / tires.ini)
    bool hasTyreRadius = false;
    double tyreRadiusM = 0.0;
    bool hasTyreCoefficients = false;
    KsTireModel::TireCoefficients tyreCoefficients{};

    // [DRIVETRAIN] (was drivetrain.ini)
    bool hasFinalDrive = false;
    double finalDrive = 0.0;
    std::vector<double> gears; ///< empty = keep the vehicle defaults

    // [AERO] (was aero.ini)
    bool hasDragCoeff = false;
    double dragCoeff = 0.0;
    bool hasFrontalArea = false;
    double frontalAreaM2 = 0.0;

    // [SUSPENSION] (was suspension.ini)
    bool hasWheelbase = false;
    double wheelbaseM = 0.0;
    bool hasTrack = false;
    double trackM = 0.0;

    /** True when no physics-bearing key was recognised. */
    bool empty() const;
};

/**
 * Loader/saver for the unified car physics file (data/physics.json).
 *
 * The runtime prefers it over the .ini set (loadVehicleParams/loadCar try it
 * first and fall back to the individual .ini files when it is absent), so
 * stock AC content keeps working while ksengine-native cars carry a single
 * file. parse()/apply() are split so tools can inspect the data without a
 * vehicle; convertFromIni() migrates an .ini car folder in place.
 */
class KSENGINE_API CarPhysicsJson {
public:
    /** File name looked up in <car>/data/ and then directly in <car>/. */
    static const char* kFileName;

    /** Absolute-ish path of the unified file for a car dir, or "" if absent. */
    static std::string resolvePath(const std::string& carDir);

    /** Parse JSON text. false on syntax error or when no section matches. */
    static bool parse(const std::string& jsonText, CarPhysicsData& out,
                      std::string* error = nullptr);

    /** Read + parse the unified file at `path`. */
    static bool load(const std::string& path, CarPhysicsData& out,
                     std::string* error = nullptr);

    /** Push parsed data into a vehicle (setters clamp, same as the inis). */
    static void apply(VehicleSimulator& v, const CarPhysicsData& data);

    /** resolvePath + load + apply in one call. true when JSON was applied. */
    static bool loadForCarDir(const std::string& carDir, VehicleSimulator& v,
                              std::string* error = nullptr);

    /** Serialise the vehicle's current baseline as unified JSON text. */
    static std::string save(const VehicleSimulator& v);

    /**
     * Migration: load a car folder's .ini set into a scratch vehicle and write
     * <car>/data/physics.json from it. Existing .ini files are left in place —
     * they stay a valid fallback until the car folder is cleaned up.
     */
    static bool convertFromIni(const std::string& carDir, std::string* error = nullptr);
};

} // namespace physics
} // namespace ks
