#pragma once
#include "KsExport.h"
#include "KsTireModel.h"

#include <string>
#include <vector>

namespace ks {
namespace physics {

class VehicleSimulator;

/**
 * How a car starts, from the manifest's "start" block.
 *
 * The block is the car's own declaration (the content pipeline writes it from
 * [STARTER] and the engine's idle/antistall numbers), and its TYPE says which
 * hardware cranks the engine: a hybrid cranks on the electric motor and may
 * restart on the move, a turbo or a plain engine need the starter and a
 * standstill. A car with no block is not started at all: it keeps the old
 * "always running" behaviour.
 */
struct CarStartConfig {
    enum class Type { Petrol, Turbo, Hybrid };
    bool declared = false;
    Type type = Type::Petrol;
    double starterRpm = 200.0;   // cranking speed the starter reaches
    double crankTime = 0.15;     // seconds at starterRpm before the engine catches
    double idleRpm = 900.0;      // where a running engine settles
    double minimumRpm = 500.0;   // below this the engine has stalled
    double antistallRpm = 0.0;   // 0 = the car declares no antistall
    double antistallTime = 0.0;  // seconds the antistall may hold the idle
    double pitstopRpm = 0.0;

    bool cranksFromErs() const { return type == Type::Hybrid; }
    bool restartsWhileMoving() const { return type == Type::Hybrid; }
};

/**
 * The clutch the car declares: drivetrain.ini [CLUTCH] MAX_TORQUE is what it can
 * hold, [AUTOCLUTCH] MIN_RPM/MAX_RPM the range the car itself opens it in. A car
 * that declares neither gets no clutch model, so it keeps the behaviour it had.
 */
struct CarClutchConfig {
    bool declared = false;
    double maxTorqueNm = 700.0;
    double autoClutchMinRpm = 1000.0;
    double autoClutchMaxRpm = 5800.0;
};

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

    /**
     * AC-style single manifest (<car>/data/data.json), the form the content
     * pipeline writes: one JSON document whose sections keep the .ini
     * spelling ("fuel": { "MAX_FUEL": 90, "FUEL": 50 }). Only the tank size
     * is read from it, because that is the one AC field the .ini loaders
     * above also cover; the rest still comes from physics.json.
     */
    static std::string resolveAcManifestPath(const std::string& carDir);

    /**
     * Tank size (litres) from the manifest's "fuel" section. "FUEL" is the
     * current load, never the capacity, so only MAX_FUEL (and the
     * physics.json spellings) count. false when absent or unreadable.
     */
    static bool loadAcFuelCapacity(const std::string& carDir, double& litres,
                                   std::string* error = nullptr);

    /**
     * The manifest's "start" block: how this car is started and by what. false
     * when the car declares none, which is not an error — it means "no start
     * sequence", not "start with guesses".
     */
    static bool loadAcStart(const std::string& carDir, CarStartConfig& out,
                            std::string* error = nullptr);

    /** The manifest's drivetrain [CLUTCH] and [AUTOCLUTCH] blocks. */
    static bool loadAcClutch(const std::string& carDir, CarClutchConfig& out,
                             std::string* error = nullptr);

    /**
     * One axle's brake temperature profile: the manifest's
     * "brakes": { "TEMPS_FRONT": {...}, "TEMPS_REAR": {...} }, the block AC keeps
     * out of brakes.ini. The fields keep AC's names; the model's shape is
     * derived from them (see loadAcBrakeTempsForCarDir).
     */
    struct AcBrakeTemps {
        bool valid = false;
        float transfer = 0.4f;          // disc -> pad
        float coolTransfer = 0.002f;    // disc -> air
        float coolSpeedFactor = 0.06f;  // per m/s
        float coolSpeedFactor0 = 0.05f; // constant airflow term
    };

    /** Read both axles' profiles. false when the manifest or the block is
     *  missing; a single missing axle leaves that half invalid. */
    static bool loadAcBrakeTemps(const std::string& carDir, AcBrakeTemps& front,
                                 AcBrakeTemps& rear, std::string* error = nullptr);

    /**
     * loadAcBrakeTemps() + push it into the vehicle's thermal model, one config
     * per axle. The converter writes a reference profile (TRANSFER 0.4,
     * COOL_TRANSFER 0.002, COOL_SPEED_FACTOR 0.06, COOL_SPEED_FACTOR_0 0.05) that
     * has to reproduce the model's own defaults, so each key is normalised
     * against that reference rather than read as a physical unit. TORQUE_K and
     * CORE_TRANSFER_* belong to AC's core-temperature model, which this engine
     * does not have, and are therefore not consumed.
     */
    static bool loadAcBrakeTempsForCarDir(const std::string& carDir, VehicleSimulator& v,
                                          std::string* error = nullptr);

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
