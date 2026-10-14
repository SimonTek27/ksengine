#include "CarPhysicsJson.h"
#include "VehicleSimulator.h"
#include "FileFormat/Json.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>

namespace fs = std::filesystem;

namespace ks {
namespace physics {

namespace {

// HP -> kW (mechanical horsepower), the one unit conversion the schema needs.
constexpr double kHpToKw = 0.745699872;

/** First key present in `obj` (aliases in call order) wins; false if none. */
bool takeNum(const Json& obj, std::initializer_list<const char*> keys,
             bool& has, double& out) {
    for (const char* k : keys) {
        const Json& v = obj[k];
        if (v.isDouble()) {
            has = true;
            out = v.toDouble();
            return true;
        }
    }
    return false;
}

void takeCoeff(const Json& obj, const char* key,
               float KsTireModel::TireCoefficients::* member,
               KsTireModel::TireCoefficients& c) {
    const Json& v = obj[key];
    if (v.isDouble()) c.*member = static_cast<float>(v.toDouble());
}

/** Magic-Formula + load-sensitivity coefficients (same names as tyres.ini). */
void readTireCoefficients(const Json& obj, KsTireModel::TireCoefficients& c) {
    takeCoeff(obj, "a1", &KsTireModel::TireCoefficients::a1, c);
    takeCoeff(obj, "a2", &KsTireModel::TireCoefficients::a2, c);
    takeCoeff(obj, "a3", &KsTireModel::TireCoefficients::a3, c);
    takeCoeff(obj, "a4", &KsTireModel::TireCoefficients::a4, c);
    takeCoeff(obj, "a5", &KsTireModel::TireCoefficients::a5, c);
    takeCoeff(obj, "a6", &KsTireModel::TireCoefficients::a6, c);
    takeCoeff(obj, "a7", &KsTireModel::TireCoefficients::a7, c);
    takeCoeff(obj, "a8", &KsTireModel::TireCoefficients::a8, c);
    takeCoeff(obj, "a9", &KsTireModel::TireCoefficients::a9, c);
    takeCoeff(obj, "a10", &KsTireModel::TireCoefficients::a10, c);
    takeCoeff(obj, "a11", &KsTireModel::TireCoefficients::a11, c);
    takeCoeff(obj, "a12", &KsTireModel::TireCoefficients::a12, c);
    takeCoeff(obj, "a13", &KsTireModel::TireCoefficients::a13, c);
    takeCoeff(obj, "b1", &KsTireModel::TireCoefficients::b1, c);
    takeCoeff(obj, "b2", &KsTireModel::TireCoefficients::b2, c);
    takeCoeff(obj, "b3", &KsTireModel::TireCoefficients::b3, c);
    takeCoeff(obj, "b4", &KsTireModel::TireCoefficients::b4, c);
    takeCoeff(obj, "b5", &KsTireModel::TireCoefficients::b5, c);
    takeCoeff(obj, "b6", &KsTireModel::TireCoefficients::b6, c);
    takeCoeff(obj, "b7", &KsTireModel::TireCoefficients::b7, c);
    takeCoeff(obj, "b8", &KsTireModel::TireCoefficients::b8, c);
    takeCoeff(obj, "c1", &KsTireModel::TireCoefficients::c1, c);
    takeCoeff(obj, "c2", &KsTireModel::TireCoefficients::c2, c);
    takeCoeff(obj, "c3", &KsTireModel::TireCoefficients::c3, c);
    takeCoeff(obj, "c4", &KsTireModel::TireCoefficients::c4, c);
    takeCoeff(obj, "c5", &KsTireModel::TireCoefficients::c5, c);
    takeCoeff(obj, "c6", &KsTireModel::TireCoefficients::c6, c);
    takeCoeff(obj, "c7", &KsTireModel::TireCoefficients::c7, c);
    takeCoeff(obj, "c8", &KsTireModel::TireCoefficients::c8, c);
    takeCoeff(obj, "c9", &KsTireModel::TireCoefficients::c9, c);
    takeCoeff(obj, "c10", &KsTireModel::TireCoefficients::c10, c);
    takeCoeff(obj, "c11", &KsTireModel::TireCoefficients::c11, c);
    takeCoeff(obj, "c12", &KsTireModel::TireCoefficients::c12, c);
    takeCoeff(obj, "c13", &KsTireModel::TireCoefficients::c13, c);
    takeCoeff(obj, "nominalLoadN", &KsTireModel::TireCoefficients::nominalLoadN, c);
    takeCoeff(obj, "loadSensExp", &KsTireModel::TireCoefficients::loadSensExp, c);
    takeCoeff(obj, "loadSensMin", &KsTireModel::TireCoefficients::loadSensMin, c);
    takeCoeff(obj, "loadSensMax", &KsTireModel::TireCoefficients::loadSensMax, c);
    takeCoeff(obj, "optPressurePsi", &KsTireModel::TireCoefficients::optPressurePsi, c);
    takeCoeff(obj, "optTempC", &KsTireModel::TireCoefficients::optTempC, c);
}

} // namespace

const char* CarPhysicsJson::kFileName = "physics.json";

bool CarPhysicsData::empty() const {
    return !hasMass && !hasPowerKw && !hasLimiter && !hasFuelCapacity &&
           !hasTyreRadius && !hasTyreCoefficients && !hasFinalDrive &&
           gears.empty() && !hasDragCoeff && !hasFrontalArea &&
           !hasWheelbase && !hasTrack;
}

std::string CarPhysicsJson::resolvePath(const std::string& carDir) {
    if (carDir.empty()) return std::string();
    const fs::path inData = fs::path(carDir) / "data" / kFileName;
    if (fs::exists(inData)) return inData.string();
    const fs::path inRoot = fs::path(carDir) / kFileName;
    if (fs::exists(inRoot)) return inRoot.string();
    return std::string();
}

bool CarPhysicsJson::parse(const std::string& jsonText, CarPhysicsData& out,
                           std::string* error) {
    out = CarPhysicsData{};
    bool ok = false;
    std::string parseError;
    const Json root = Json::parse(jsonText, &ok, &parseError);
    if (!ok || !root.isObject()) {
        if (error)
            *error = std::string("physics.json: invalid JSON (") + parseError + ")";
        return false;
    }

    // --- [CAR] ---------------------------------------------------------
    const Json& car = root["car"];
    if (car.isObject()) takeNum(car, {"massKg", "mass", "MASS"}, out.hasMass, out.massKg);

    // --- [ENGINE] (engine.ini) ----------------------------------------
    const Json& engine = root["engine"];
    if (engine.isObject()) {
        if (!takeNum(engine, {"powerKw", "power", "POWER"}, out.hasPowerKw, out.powerKw)) {
            // car.json-style authoring units: horsepower.
            bool hasHp = false;
            double hp = 0.0;
            if (takeNum(engine, {"horsepower", "hp"}, hasHp, hp) && hasHp) {
                out.hasPowerKw = true;
                out.powerKw = hp * kHpToKw;
            }
        }
        takeNum(engine, {"limiter", "maxRpm", "rpm", "LIMITER"}, out.hasLimiter, out.limiterRpm);
        takeNum(engine, {"fuelCapacityL", "fuelTankL", "FUEL_CAPACITY"},
                out.hasFuelCapacity, out.fuelCapacityL);
    }

    // --- [TYRES] (tyres.ini / tires.ini) ------------------------------
    const Json& tyres = root.contains("tyres") ? root["tyres"] : root["tires"];
    if (tyres.isObject()) {
        takeNum(tyres, {"radius", "radiusM", "RADIUS"}, out.hasTyreRadius, out.tyreRadiusM);
        const Json& coeffs = tyres["coefficients"];
        if (coeffs.isObject()) {
            KsTireModel::TireCoefficients c; // defaults = the model's own
            readTireCoefficients(coeffs, c);
            std::string coeffError;
            if (!KsTireModel::validateCoefficients(c, &coeffError)) {
                if (error) *error = "physics.json: invalid tyre coefficients: " + coeffError;
                return false;
            }
            out.hasTyreCoefficients = true;
            out.tyreCoefficients = c;
        }
    }

    // --- [DRIVETRAIN] (drivetrain.ini) --------------------------------
    const Json& drivetrain = root["drivetrain"];
    if (drivetrain.isObject()) {
        takeNum(drivetrain, {"finalDrive", "final", "FINAL_DRIVE"},
                out.hasFinalDrive, out.finalDrive);
        const Json& gears = drivetrain["gears"];
        if (gears.isArray()) {
            for (int i = 0; i < gears.size(); ++i) {
                const Json& g = gears[i];
                if (g.isDouble() && g.toDouble() > 0.01) out.gears.push_back(g.toDouble());
            }
        } else {
            // Tolerate the .ini spelling GEAR_1..GEAR_10.
            for (int i = 1; i <= 10; ++i) {
                const Json& g = drivetrain["GEAR_" + std::to_string(i)];
                if (g.isDouble() && g.toDouble() > 0.01) out.gears.push_back(g.toDouble());
            }
        }
    }

    // --- [AERO] (aero.ini) --------------------------------------------
    const Json& aero = root["aero"];
    if (aero.isObject()) {
        takeNum(aero, {"cd", "dragCoefficient", "CD"}, out.hasDragCoeff, out.dragCoeff);
        takeNum(aero, {"area", "frontalArea", "AREA"}, out.hasFrontalArea, out.frontalAreaM2);
    }

    // --- [SUSPENSION] (suspension.ini) --------------------------------
    const Json& susp = root["suspension"];
    if (susp.isObject()) {
        takeNum(susp, {"wheelbase", "wheelbaseM", "WHEELBASE"}, out.hasWheelbase, out.wheelbaseM);
        takeNum(susp, {"track", "trackWidth", "trackM", "TRACK"}, out.hasTrack, out.trackM);
    }

    // A JSON file without any physics section is not this format — let the
    // caller fall back to the .ini set instead of silently using defaults.
    if (out.empty()) {
        if (error) *error = "physics.json: no recognised physics sections";
        return false;
    }
    return true;
}

bool CarPhysicsJson::load(const std::string& path, CarPhysicsData& out,
                          std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "physics.json: cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), out, error);
}

void CarPhysicsJson::apply(VehicleSimulator& v, const CarPhysicsData& data) {
    // Same order (and the same setter clamps) as the .ini loaders, so a
    // converted car behaves exactly like it did as five .ini files.
    if (data.hasMass) v.setMass(data.massKg);
    if (data.hasPowerKw) v.setEnginePower(data.powerKw);
    if (data.hasLimiter) v.setMaxRpm(data.limiterRpm);
    if (data.hasFuelCapacity) v.setFuelTankCapacity(static_cast<float>(data.fuelCapacityL));
    if (data.hasTyreRadius) v.setWheelRadius(data.tyreRadiusM);
    if (data.hasTyreCoefficients) v.tires().setCoefficients(data.tyreCoefficients);
    if (data.hasFinalDrive) v.setFinalDrive(data.finalDrive);
    if (!data.gears.empty()) v.setGearRatios(data.gears);
    if (data.hasDragCoeff) v.setDragCoeff(data.dragCoeff);
    if (data.hasFrontalArea) v.setFrontalArea(data.frontalAreaM2);
    if (data.hasWheelbase) v.setWheelBase(data.wheelbaseM);
    if (data.hasTrack) v.setTrackWidth(data.trackM);
}

bool CarPhysicsJson::loadForCarDir(const std::string& carDir, VehicleSimulator& v,
                                   std::string* error) {
    const std::string path = resolvePath(carDir);
    if (path.empty()) return false; // no unified file: caller falls back to .ini
    CarPhysicsData data;
    if (!load(path, data, error)) return false;
    apply(v, data);
    return true;
}

std::string CarPhysicsJson::save(const VehicleSimulator& v) {
    Json root = Json::object();
    root.insert("version", Json(1));

    Json car = Json::object();
    car.insert("massKg", Json(v.baseMass())); // dry: fuel/ballast are setup, not the car
    root.insert("car", car);

    Json engine = Json::object();
    engine.insert("powerKw", Json(v.enginePowerKw()));
    engine.insert("limiter", Json(v.maxRpm()));
    engine.insert("fuelCapacityL", Json(static_cast<double>(v.fuelTankCapacityL())));
    root.insert("engine", engine);

    Json tyres = Json::object();
    tyres.insert("radius", Json(static_cast<double>(v.wheelRadius())));
    root.insert("tyres", tyres);

    Json drivetrain = Json::object();
    drivetrain.insert("finalDrive", Json(static_cast<double>(v.finalDrive())));
    Json gears = Json::array();
    for (double g : v.gearRatios()) gears.append(Json(g));
    drivetrain.insert("gears", gears);
    root.insert("drivetrain", drivetrain);

    Json aero = Json::object();
    aero.insert("cd", Json(v.dragCoeff()));
    aero.insert("area", Json(v.frontalArea()));
    root.insert("aero", aero);

    Json susp = Json::object();
    susp.insert("wheelbase", Json(v.wheelBase()));
    susp.insert("track", Json(v.trackWidth()));
    root.insert("suspension", susp);

    return root.dump(true) + "\n";
}

bool CarPhysicsJson::convertFromIni(const std::string& carDir, std::string* error) {
    const fs::path dir(carDir);
    if (!fs::is_directory(dir)) {
        if (error) *error = "physics.json: not a car directory: " + carDir;
        return false;
    }
    VehicleSimulator scratch;
    scratch.loadVehicleParamsFromIni(carDir);
    const fs::path target =
        fs::is_directory(dir / "data") ? dir / "data" / kFileName : dir / kFileName;
    std::ofstream out(target, std::ios::trunc);
    if (!out) {
        if (error) *error = "physics.json: cannot write " + target.string();
        return false;
    }
    out << save(scratch);
    return static_cast<bool>(out);
}

} // namespace physics
} // namespace ks
