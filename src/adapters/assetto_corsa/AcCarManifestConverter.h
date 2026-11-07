#pragma once

#include "Config/IniFile.h"
#include "FileFormat/Json.h"
#include "Overrides.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


#include "engine/FileFormat/AcdWriter.h"
#include "engine/FileFormat/Kn5Reader.h"
namespace ks {
namespace assetto_corsa {

namespace fs = std::filesystem;

// Brings a stock Assetto Corsa car folder (one .ini per section family) to the
// six documents the content pipeline writes (data/driver/camera/setup/model/
// sound .json), so a car that is "not ready to be initialised" is converted first
// loaded afterwards.
//
// The shape is the one the converted cars carry:
//   - ui/ui_car.json metadata at the root (numeric strings coerced);
//   - one root object per .ini, named after the file stem, holding its
//     sections ("aero": { "WING_0": {...} }, "engine": {...}, ...), routed to
//     the document its nature belongs to: data.json (physics and metadata),
//     driver.json (the driver), camera.json (cameras and dash cam),
//     setup.json (the tunable setup), model.json (the model node table),
//     sound.json (audio). All six keep the shape;
//   - car.ini keeps the same rule (stem "car"), except [FUEL] and [FUELTANK]
//     which feed the root "fuel" object together with fuel_cons.ini.
//
// Entries AC only implies are added here and only here, never invented per
// car: fuel density, the minimum ride height, the brake temperature profile,
// engine power curve standard and humidity reference, the AI tyre hint and wing
// variation, the ABS_V2/EDL shapes older cars predate, and the camera pitches
// an unwritten [GRAPHICS] leaves out. A naturally aspirated
// car gets no turbo/ERS block, because nothing in its .ini set declares one --
// that is the "only what belongs to this vehicle type" rule.
class AcCarManifestConverter {
public:
    struct Result {
        bool converted = false;
        bool verified = false;
        std::string manifestPath;
        int iniFiles = 0;
        int sections = 0;
        int documents = 0;
        // Required entries the produced data document does not carry. Empty
        // when the car satisfies the rule.
        std::vector<std::string> missing;
        // Verification: claims produced, outcomes recorded, disagreements.
        int claims = 0;
        int checked = 0;
        int mismatches = 0;
        // Corrections applied (accepted proposals) and claims left untouched.
        int applied = 0;
        int skipped = 0;
        // Third step: .ini files removed once the documents supersede them.
        int pruned = 0;
        bool pruneDone = false;
        // Final step: data.acd rebuilt from the folder as it is now.
        int packed = 0;
        bool packDone = false;
        std::string error;
    };

    // Petrol density in kg/l. AC never serialises it (fuel mass is implied),
    // while the manifest carries it for the fuel/weight model. The reference
    // reference manifest uses the same 0.76.
    static constexpr double kFuelDensityKgPerL = 0.76;

    // Warning light thresholds, the companions of FUEL_LIGHT_MIN_LITERS: water
    // over-temperature and oil low pressure. AC never serialises them, these are
    // the generic values that apply to any car and are not tuned per vehicle.
    static constexpr double kWaterLightMaxC = 110.0;
    static constexpr double kOilLightMinBar = 1.0;

    static constexpr const char* kDataDocument = "data.json";
    static constexpr const char* kDriverDocument = "driver.json";
    static constexpr const char* kCameraDocument = "camera.json";
    static constexpr const char* kSetupDocument = "setup.json";
    static constexpr const char* kModelDocument = "model.json";
    static constexpr const char* kSoundDocument = "sound.json";
    static constexpr const char* kAiDocument = "ai.json";

    // The seven documents a ready car folder carries: physics, driver, cameras,
    // tunable setup, 3D model description, audio, driver aids.
    static std::vector<std::string> documents() {
        return {kDataDocument,  kDriverDocument, kCameraDocument, kSetupDocument,
                kModelDocument, kSoundDocument,  kAiDocument};
    }

    static fs::path documentPath(const fs::path& carDir, const std::string& name) {
        return carDir / "data" / name;
    }

    // Kept for the physics readers: tank, engine and aero curves all live in
    // the data document.
    static fs::path manifestPath(const fs::path& carDir) {
        return documentPath(carDir, kDataDocument);
    }

    // Ready means all seven. A folder that only has the physics has not been
    // converted yet (the legacy physics.json name still counts as the data
    // document, so older content keeps loading).
    static bool hasManifest(const fs::path& carDir) {
        const bool hasData = fs::exists(documentPath(carDir, kDataDocument)) ||
                             fs::exists(carDir / "data" / "physics.json") ||
                             fs::exists(carDir / "physics.json");
        if (!hasData) return false;
        return fs::exists(documentPath(carDir, kDriverDocument)) &&
               fs::exists(documentPath(carDir, kCameraDocument)) &&
               fs::exists(documentPath(carDir, kSetupDocument)) &&
               fs::exists(documentPath(carDir, kModelDocument)) &&
               fs::exists(documentPath(carDir, kSoundDocument)) &&
               fs::exists(documentPath(carDir, kAiDocument));
    }

    // True for the "not ready yet" case: no unified manifest, but the AC .ini
    // set is there, so the conversion has something to read.
    static bool needsConversion(const fs::path& carDir) {
        if (carDir.empty() || !fs::exists(carDir)) return false;
        if (hasManifest(carDir)) return false;
        if (fs::exists(carDir / "data" / "car.ini") || fs::exists(carDir / "car.ini")) return true;
        // No .ini set left: a folder carrying only the legacy data document is
        // not ready either, it just needs splitting.
        return fs::exists(manifestPath(carDir)) ||
               fs::exists(carDir / "data" / "physics.json") ||
               fs::exists(carDir / "physics.json");
    }

    // Which document a root key belongs to: the key is the .ini stem and the
    // stem decides the nature of the data. Metadata and anything unclassified
    // stay in the data document, so nothing is ever dropped on the floor.
    static std::string documentFor(const std::string& key) {
        const std::string stem = lower(key);
        if (stem == "setup") return kSetupDocument;
        if (stem == "sounds") return kSoundDocument;
        // The driver is the driver: cameras and the dash cam are views, they
        // belong to camera.json.
        if (stem == "driver3d") return kDriverDocument;
        if (stem == "cameras" || stem == "dash_cam") return kCameraDocument;
        // The model document owns the model itself: the node table, the LODs
        // and the collision hull. Lights, instruments, shadows, ac_tyres and
        // the graphics offsets are settings, they stay with the car data.
        if (stem == "proview_nodes" || stem == "lods" || stem == "colliders") return kModelDocument;
        // Everything the AI drives with, and the per-compound wear table
        // that goes with it.
        if (stem == "ai" || stem == "ai_tyres") return kAiDocument;
        return kDataDocument;
    }

    // Entries every car must carry whatever its type, each with the document
    // that owns it. Derived from the reference documents minus everything
    // typology-bound (ERS, turbo, torque maps, controllers, script params) and
    // minus the counts that depend on the model (lights, instruments,
    // compounds, gears).
    struct Requirement {
        std::string document;
        std::string path;
    };

    static std::vector<Requirement> requiredEntries() {
        return {
            {kDataDocument, "dampers"},
            {kDataDocument, "bumpstops"},
            {kDataDocument, "fuel.FUEL_LIGHT_MIN_LITERS"},
            {kDataDocument, "brakes.TEMPS_FRONT"},
            {kDataDocument, "brakes.TEMPS_REAR"},
            {kDataDocument, "engine.HEADER.POWER_CURVE_STANDARD"},
            {kDataDocument, "engine.HEADER.HUMIDITY_REF"},
            {kDataDocument, "engine.ENGINE_DATA.AMBIENT_EFFECT"},
            {kDataDocument, "engine.WATER_LIGHT_MAX_C"},
            {kDataDocument, "engine.OIL_LIGHT_MIN_BAR"},
            {kAiDocument, "ai.LOOKAHEAD.SPEED_GAIN"},
            {kAiDocument, "ai.TYRES"},
            {kAiDocument, "ai.WING_VARIATION"},
            {kDataDocument, "electronics.ABS_V2"},
            {kDataDocument, "electronics.EDL"},
            {kDataDocument, "drivetrain.UPSHIFT_PROFILE"},
            {kDataDocument, "drivetrain.DAMAGE"},
            {kDataDocument, "specs.range"},
            {kDriverDocument, "GRAPHICS.DRIVEREYES"},
            {kDriverDocument, "GRAPHICS.ON_BOARD_PITCH_ANGLE"},
            {kDriverDocument, "GRAPHICS.ONBOARD_EXPOSURE"},
            {kCameraDocument, "GRAPHICS.BONNET_CAMERA_POS"},
            {kCameraDocument, "GRAPHICS.BONNET_CAMERA_PITCH"},
            {kCameraDocument, "GRAPHICS.BUMPER_CAMERA_POS"},
            {kCameraDocument, "GRAPHICS.BUMPER_CAMERA_PITCH"},
            {kCameraDocument, "GRAPHICS.CHASE_CAMERA_PITCH"},
            {kCameraDocument, "GRAPHICS.OUTBOARD_EXPOSURE"},
            {kCameraDocument, "GRAPHICS.SHAKE_MUL"},
            {kModelDocument, "GRAPHICS.GRAPHICS_OFFSET"},
            {kModelDocument, "GRAPHICS.GRAPHICS_PITCH_ROTATION"},
            {kModelDocument, "GRAPHICS.USE_ANIMATED_SUSPENSIONS"},
            {kDataDocument, "mirrors.MIRROR_POSITION"}};
    }

    static std::vector<std::string> missingRequired(const std::vector<Json>& docs,
                                                    const std::vector<std::string>& names) {
        std::vector<std::string> missing;
        for (const Requirement& requirement : requiredEntries()) {
            const Json* node = nullptr;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (names[i] == requirement.document) { node = &docs[i]; break; }
            }
            bool found = node != nullptr;
            std::size_t start = 0;
            while (found) {
                const std::size_t dot = requirement.path.find('.', start);
                const std::string part = requirement.path.substr(
                    start, dot == std::string::npos ? std::string::npos : dot - start);
                if (!node->isObject() || !node->contains(part)) { found = false; break; }
                node = &(*node)[part];
                if (dot == std::string::npos) break;
                start = dot + 1;
            }
            if (!found) missing.push_back(requirement.document + ": " + requirement.path);
        }
        return missing;
    }

    // car [GRAPHICS] mixes four subjects: what the driver sees, the external
    // cameras, where the model sits and the remaining display settings. Each key
    // goes to the document that owns its subject instead of piling up in the
    // physics document.
    static std::string graphicsSubject(const std::string& key) {
        const std::string name = upper(key);
        static const char* kDriverKeys[] = {"DRIVEREYES", "ON_BOARD_PITCH_ANGLE",
                                            "ONBOARD_EXPOSURE"};
        static const char* kCameraKeys[] = {"BONNET_CAMERA_POS", "BONNET_CAMERA_PITCH",
                                            "BUMPER_CAMERA_POS", "BUMPER_CAMERA_PITCH",
                                            "CHASE_CAMERA_PITCH", "OUTBOARD_EXPOSURE",
                                            "SHAKE_MUL"};
        static const char* kModelKeys[] = {"GRAPHICS_OFFSET", "GRAPHICS_PITCH_ROTATION",
                                           "USE_ANIMATED_SUSPENSIONS"};
        // The mirror is body geometry, not model geometry: MIRROR_POSITION is
        // not listed here, so it falls through to the physics document, where
        // writeDocuments files it under the mirrors section.
        for (const char* candidate : kDriverKeys) {
            if (name == candidate) return kDriverDocument;
        }
        for (const char* candidate : kCameraKeys) {
            if (name == candidate) return kCameraDocument;
        }
        for (const char* candidate : kModelKeys) {
            if (name == candidate) return kModelDocument;
        }
        return kDataDocument;
    }

    // Content older than the current format writes the graphics offsets in
    // car [BASIC]; the current format keeps them in car [GRAPHICS]. Same data,
    // so it is moved, never duplicated.
    static void normalizeGraphics(Json& root) {
        if (!root.contains("car") || !root["car"].isObject()) return;
        Json& car = root["car"];
        if (!car.contains("BASIC") || !car["BASIC"].isObject()) return;
        // Read first, write later: inserting into car reallocates its member
        // vector, which would leave any reference taken from car dangling.
        Json offsets = Json::object();
        for (const char* key : {"GRAPHICS_OFFSET", "GRAPHICS_PITCH_ROTATION"}) {
            if (!car["BASIC"].contains(key)) continue;
            offsets.insert(key, car["BASIC"][key]);
        }
        if (offsets.isEmpty()) return;
        Json& graphics = objectAt(car, "GRAPHICS");
        for (const std::string& key : offsets.keys()) {
            if (!graphics.contains(key)) graphics.insert(key, offsets[key]);
        }
        Json& basic = car["BASIC"];
        for (const std::string& key : offsets.keys()) basic.remove(key);
    }

    // Older content carries the damper and bump stop data inline in
    // suspensions.ini. The rule wants them as their own roots, so the classic
    // keys are projected per axle with their names untouched: nothing is
    // rewritten into a model this car does not use.
    static void projectDamperAndBumpstop(Json& root) {
        if (!root.contains("suspensions") || !root["suspensions"].isObject()) return;
        Json& suspensions = root["suspensions"];
        Json dampers = root.contains("dampers") ? root["dampers"] : Json::object();
        Json bumpstops = root.contains("bumpstops") ? root["bumpstops"] : Json::object();
        bool anyDamper = false;
        bool anyBumpstop = false;
        for (const char* axle : {"FRONT", "REAR"}) {
            if (!suspensions.contains(axle) || !suspensions[axle].isObject()) continue;
            Json& source = suspensions[axle];
            Json damperAxle = Json::object();
            Json bumpstopAxle = Json::object();
            for (const std::string& key : source.keys()) {
                const std::string name = upper(key);
                if (name.rfind("DAMP_", 0) == 0) {
                    damperAxle.insert(key, source[key]);
                    anyDamper = true;
                } else if (name.rfind("BUMPSTOP_", 0) == 0 || name == "BUMP_STOP_RATE" ||
                           name == "PACKER_RANGE") {
                    bumpstopAxle.insert(key, source[key]);
                    anyBumpstop = true;
                }
            }
            if (!damperAxle.isEmpty()) dampers.insert(axle, damperAxle);
            if (!bumpstopAxle.isEmpty()) bumpstops.insert(axle, bumpstopAxle);
        }
        if (anyDamper) root.insert("dampers", dampers);
        if (anyBumpstop) root.insert("bumpstops", bumpstops);
    }

// brakes.ini keeps the disc glow and the response lag inside the section that
// names the disc meshes. They describe how the brakes behave, not what they
// look like, so they join DATA and the section keeps only the meshes, under a
// name that says so.
static void normalizeBrakes(Json& root) {
    if (!root.contains("brakes") || !root["brakes"].isObject()) return;
    Json& brakes = root["brakes"];
    if (!brakes.contains("DISCS_GRAPHICS") || !brakes["DISCS_GRAPHICS"].isObject()) {
        return;
    }
    const Json source = brakes["DISCS_GRAPHICS"];
    Json components = Json::object();
    Json thermal = Json::object();
    static const char* kThermalKeys[] = {"FRONT_MAX_GLOW", "REAR_MAX_GLOW", "LAG_HOT",
                                         "LAG_COOL"};
    for (const std::string& key : source.keys()) {
        bool isThermal = false;
        for (const char* candidate : kThermalKeys) {
            if (iequals(key, candidate)) isThermal = true;
        }
        if (isThermal) thermal.insert(key, source[key]);
        else components.insert(key, source[key]);
    }
    if (!thermal.isEmpty()) {
        Json& data = objectAt(brakes, "DATA");
        for (const std::string& key : thermal.keys()) {
            if (!data.contains(key)) data.insert(key, thermal[key]);
        }
    }
    brakes.remove("DISCS_GRAPHICS");
    if (!brakes.contains("DISCS_COMPONENTS")) brakes.insert("DISCS_COMPONENTS", components);
}

    // car [FUEL_EXT] carries one thing: the fuel density, which the fuel object
    // already holds as KG_PER_LITER. The section is not a subject of its own, so
    // it is folded there and disappears: one value, in the place the fuel model
    // reads. A car that declares its own density keeps it over the default.
    static void foldFuelExtension(Json& root) {
        if (!root.contains("car") || !root["car"].isObject()) return;
        Json& car = root["car"];
        if (!car.contains("FUEL_EXT")) return;
        const Json ext = car["FUEL_EXT"];
        car.remove("FUEL_EXT");
        if (!ext.isObject()) return;
        Json& fuel = objectAt(root, "fuel");
        for (const std::string& key : ext.keys()) {
            const std::string name = iequals(key, "KG_PER_LITER") ? std::string("KG_PER_LITER")
                                                                 : upper(key);
            if (fuel.contains(name)) fuel.remove(name);
            fuel.insert(name, ext[key]);
        }
    }

    static void normalize(Json& root, const fs::path& carDir) {
        normalizeGraphics(root);
        normalizeBrakes(root);
        addComponentMaps(root, collectModelNodeNames(carDir));
        projectDamperAndBumpstop(root);
        Json& fuel = objectAt(root, "fuel");
        addTypeDefaults(root, fuel);
        foldFuelExtension(root);
    }

    // Every .ini section repeats a HEADER carrying the format VERSION, and
    // most of them carry nothing else. That tells nothing about the car: the
    // value goes, and a section that held only that disappears with it.
    static void stripVersionHeaders(Json& node) {
        if (!node.isObject()) return;
        for (const std::string& key : node.keys()) {
            if (!node.contains(key)) continue;
            Json& child = node[key];
            if (!child.isObject()) continue;
            if (iequals(key, "HEADER")) {
                if (child.contains("VERSION")) child.remove("VERSION");
                if (child.isEmpty()) {
                    node.remove(key);
                    continue;
                }
            }
            stripVersionHeaders(child);
        }
    }

    // digital_instruments.ini stamps VERSION on the sections an instrument list
    // uses. It is the format's number, not the car's, so it goes at any depth
    // under that root and nothing else is touched.
    static void stripDigitalInstrumentVersions(Json& node) {
        if (!node.isObject()) return;
        for (const std::string& key : node.keys()) {
            if (!node.contains(key)) continue;
            if (iequals(key, "VERSION")) {
                node.remove(key);
                continue;
            }
            stripDigitalInstrumentVersions(node[key]);
        }
    }

    // A section called _EXTENSION (or any key carrying EXTENSION/EXTENDED in
    // its name) says which build of the other simulator reads the file, not how
    // the car behaves: REQUIRED_VERSION, RAIN_DEV, SCRIPT_INPUTS_REPLAY. This
    // project has no use for it, so it never reaches a document, at any depth.
    static void stripExtensionKeys(Json& node) {
        if (!node.isObject()) return;
        for (const std::string& key : node.keys()) {
            if (!node.contains(key)) continue;
            const std::string name = upper(key);
            if (name.find("EXTENSION") != std::string::npos ||
                name.find("EXTENDED") != std::string::npos) {
                node.remove(key);
                continue;
            }
            stripExtensionKeys(node[key]);
        }
    }

    // wing_animations.ini pairs each aero wing with the .ksanim that moves it:
    // the two halves belong to different documents. The animation table is a
    // model fact (which file moves which wing), so it goes to the model
    // document; MIN, MAX and SPEED describe the wing's own travel and speed and
    // are written on the wing the entry names, in data.json, where aero.ini
    // already put it. Nothing stays at the root: a third copy is a place for
    // the two halves to disagree.
    static void splitWingAnimations(std::vector<Json>& docs,
                                    const std::vector<std::string>& names) {
        std::size_t dataIndex = 0;
        std::size_t modelIndex = 0;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == kDataDocument) dataIndex = i;
            if (names[i] == kModelDocument) modelIndex = i;
        }
        if (!docs[dataIndex].contains("wing_animations")) return;
        const Json table = docs[dataIndex]["wing_animations"];
        docs[dataIndex].remove("wing_animations");
        if (!table.isObject() || table.isEmpty()) return;
        Json& animations = objectAt(docs[modelIndex], "wing_animations");
        const bool hasAero = docs[dataIndex].contains("aero") &&
                             docs[dataIndex]["aero"].isObject();
        for (const std::string& section : table.keys()) {
            const Json& animation = table[section];
            // Without a wing index there is nothing to attach the entry to, and
            // guessing one would invent content the car never declared.
            if (!animation.isObject() || !animation.contains("WING")) continue;
            Json entry = Json::object();
            for (const char* field : {"WING", "FILE", "MIN", "MAX", "SPEED"}) {
                if (animation.contains(field)) entry.insert(field, animation[field]);
            }
            animations.insert(section, entry);
            if (!hasAero) continue;
            const int wing = static_cast<int>(animation["WING"].toDouble());
            const std::string wingName = "WING_" + std::to_string(wing);
            Json& aero = docs[dataIndex]["aero"];
            if (!aero.contains(wingName) || !aero[wingName].isObject()) continue;
            Json& target = aero[wingName];
            if (target.contains("ANIMATION")) continue;
            Json description = Json::object();
            if (animation.contains("FILE")) description.insert("FILE", animation["FILE"]);
            for (const char* field : {"MIN", "MAX", "SPEED"}) {
                if (animation.contains(field)) description.insert(field, animation[field]);
            }
            target.insert("ANIMATION", description);
        }
    }

    // Writes the six documents from an assembled root object; the same routing
    // serves the .ini conversion and the legacy split.
    static bool writeDocuments(const fs::path& carDir, const Json& root, Result& result) {
        std::error_code ec;
        if (!writeBrakeTempLuts(carDir)) {
            result.error = "cannot write the brake temperature curves";
            return false;
        }
        const std::vector<std::string> names = documents();
        std::vector<Json> docs;
        docs.reserve(names.size());
        for (std::size_t i = 0; i < names.size(); ++i) docs.push_back(Json::object());
        for (const std::string& key : root.keys()) {
            const std::string name = documentFor(key);
            std::size_t index = 0;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (names[i] == name) { index = i; break; }
            }
            docs[index].insert(key, root[key]);
        }
        // Hand every car [GRAPHICS] entry to the document that owns its subject.
        if (docs[0].contains("car") && docs[0]["car"].isObject() &&
            docs[0]["car"].contains("GRAPHICS") && docs[0]["car"]["GRAPHICS"].isObject()) {
            const Json graphics = docs[0]["car"]["GRAPHICS"];
            for (const std::string& key : graphics.keys()) {
                // The fuel light threshold is a fuel fact, not a graphics one.
                if (upper(key) == "FUEL_LIGHT_MIN_LITERS") {
                    Json& fuel = objectAt(docs[0], "fuel");
                    if (!fuel.contains(key)) fuel.insert(key, graphics[key]);
                    docs[0]["car"]["GRAPHICS"].remove(key);
                    continue;
                }
                // The mirror sits on the body: it joins the mirrors section built
                // from mirrors.ini instead of staying in the graphics block.
                if (upper(key) == "MIRROR_POSITION") {
                    Json& mirrors = objectAt(docs[0], "mirrors");
                    if (!mirrors.contains(key)) mirrors.insert(key, graphics[key]);
                    docs[0]["car"]["GRAPHICS"].remove(key);
                    continue;
                }
                const std::string subject = graphicsSubject(key);
                if (subject == kDataDocument) continue;
                std::size_t target = 0;
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (names[i] == subject) { target = i; break; }
                }
                Json& bucket = objectAt(docs[target], "GRAPHICS");
                if (!bucket.contains(key)) bucket.insert(key, graphics[key]);
                docs[0]["car"]["GRAPHICS"].remove(key);
            }
        }
        // An emptied graphics block is not a block.
        if (docs[0].contains("car") && docs[0]["car"].isObject() &&
            docs[0]["car"].contains("GRAPHICS") && docs[0]["car"]["GRAPHICS"].isEmpty()) {
            docs[0]["car"].remove("GRAPHICS");
        }
// The per-wheel graphical offsets say where the wheels and the suspension
// legs sit on the model, so the section moves whole into the model document
// under its own name. Current content writes it in suspensions.ini, older
// content in car.ini: both count.
for (const char* owner : {"suspensions", "car"}) {
    if (!docs[0].contains(owner) || !docs[0][owner].isObject()) continue;
    Json& parent = docs[0][owner];
    if (!parent.contains("GRAPHICS_OFFSETS") || !parent["GRAPHICS_OFFSETS"].isObject()) {
        continue;
    }
    const Json offsets = parent["GRAPHICS_OFFSETS"];
    std::size_t modelIndex = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == kModelDocument) { modelIndex = i; break; }
    }
    objectAt(docs[modelIndex], "GRAPHICS_OFFSETS");
    Json& bucket = docs[modelIndex]["GRAPHICS_OFFSETS"];
    for (const std::string& key : offsets.keys()) {
        if (!bucket.contains(key)) bucket.insert(key, offsets[key]);
    }
    parent.remove("GRAPHICS_OFFSETS");
}
        // The two rendering-only stems never reach a document, whichever path
        // built the root: not the .ini conversion (which skips them above) and
        // not a document written before that rule, whose copy is dropped here.
        for (Json& doc : docs) {
            for (const char* skipped : {"blurred_objects", "ambient_shadows"}) {
                for (const std::string& key : doc.keys()) {
                    if (iequals(key, skipped)) doc.remove(key);
                }
            }
        }
        splitWingAnimations(docs, names);
        // No document ships the format version of the ini sections, and none
        // carries the extension bookkeeping the other simulator reads.
        for (Json& doc : docs) {
            stripVersionHeaders(doc);
            stripExtensionKeys(doc);
            for (const std::string& key : doc.keys()) {
                if (iequals(key, "digital_instruments"))
                    stripDigitalInstrumentVersions(doc[key]);
            }
        }
        // The car's overrides carry two subjects and each goes to its own
        // document: the driver model and the driver's hidden nodes to the driver
        // document, the model's sections and hidden nodes to the model one. A
        // skin's own overrides are not car data: they are written to that skin's
        // skin.json, where the skin's textures already live.
        {
            std::size_t modelIndex = 0;
            std::size_t driverIndex = 0;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (names[i] == kModelDocument) modelIndex = i;
                if (names[i] == kDriverDocument) driverIndex = i;
            }
            mergeOverrides(carDir, docs[modelIndex], docs[driverIndex]);
            writeSkinOverrides(carDir);
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            const fs::path out = documentPath(carDir, names[i]);
            fs::create_directories(out.parent_path(), ec);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            if (!file) {
                result.error = "cannot write " + out.string();
                return false;
            }
            file << docs[i].dump(true);
            file.put(static_cast<char>(10));
            file.close();
            if (!file) {
                result.error = "write failed for " + out.string();
                return false;
            }
            ++result.documents;
            if (names[i] == kDataDocument) result.manifestPath = out.string();
        }
        result.missing = missingRequired(docs, names);
        return true;
    }

    // Completes a folder that carries the single legacy data document and no
    // .ini set: same routing, applied to what is already there.
    static Result split(const fs::path& carDir) {
        Result result;
        fs::path source = manifestPath(carDir);
        if (!fs::exists(source)) {
            if (fs::exists(carDir / "data" / "physics.json")) source = carDir / "data" / "physics.json";
            else if (fs::exists(carDir / "physics.json")) source = carDir / "physics.json";
        }
        std::ifstream in(source, std::ios::binary);
        if (!in) {
            result.error = "no data document in " + carDir.string();
            return result;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        bool ok = false;
        std::string error;
        Json root = Json::parse(buffer.str(), &ok, &error);
        if (!ok || !root.isObject()) {
            result.error = "invalid data document `" + source.filename().string() + "`: " + error;
            return result;
        }
        // Re-splitting has to be lossless: the other documents already hold the
        // content that lives in them by subject, so they are folded back before
        // the routing runs again.
        for (const std::string& name : documents()) {
            if (name == kDataDocument) continue;
            std::ifstream other(carDir / "data" / name, std::ios::binary);
            if (!other) continue;
            std::ostringstream otherBuffer;
            otherBuffer << other.rdbuf();
            bool otherOk = false;
            std::string otherError;
            Json doc = Json::parse(otherBuffer.str(), &otherOk, &otherError);
            if (!otherOk || !doc.isObject()) continue;
            for (const std::string& key : doc.keys()) {
                if (key == "GRAPHICS") {
                    // The mixed block goes back where the distribution reads it.
                    Json& graphics = objectAt(objectAt(root, "car"), "GRAPHICS");
                    for (const std::string& entry : doc[key].keys()) {
                        if (!graphics.contains(entry)) graphics.insert(entry, doc[key][entry]);
                    }
                    continue;
                }
                if (!root.contains(key)) root.insert(key, doc[key]);
            }
        }
        if (root.contains("escmode")) root.remove("escmode");
        if (root.contains("ac_tyres")) root.remove("ac_tyres");
        normalize(root, carDir);
        if (!writeDocuments(carDir, root, result)) return result;
        const Result audit = verify(carDir);
        result.claims = audit.claims;
        result.checked = audit.checked;
        result.mismatches = audit.mismatches;
        result.verified = audit.verified;
        result.converted = true;
        return result;
    }

    // --- post-conversion verification ---------------------------------------
    //
    // Converting is not the same as being right: AC content carries numbers that
    // may be invented. The conversion therefore writes the list of claims to
    // check, one ready-made search query per claim, and keeps the outcome
    // recorded for every one of them, so a wrong value cannot pass silently.
    // The lookup itself runs outside the engine (there is no HTTP client here);
    // `ks_cartool verify` prints the queries and reports the coverage.
    static constexpr const char* kVerifyDocument = "verify.json";

    static fs::path verifyPath(const fs::path& carDir) {
        return carDir / "data" / kVerifyDocument;
    }

    struct Claim {
        std::string path;
        std::string value;
        std::string unit;
        std::string query;
    };

    static const Json* findPath(const Json& root, const std::string& path) {
        const Json* node = &root;
        std::size_t start = 0;
        while (true) {
            const std::size_t dot = path.find('.', start);
            const std::string part =
                path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!node->isObject() || !node->contains(part)) return nullptr;
            node = &(*node)[part];
            if (dot == std::string::npos) return node;
            start = dot + 1;
        }
    }

    static std::string numberText(const Json& value) {
        if (value.isString()) return value.toString();
        std::ostringstream out;
        if (value.isInt()) out << value.toInt64();
        else out << value.toDouble();
        return out.str();
    }

    static void addCurvePeak(const Json& data, const std::string& key, const std::string& unit,
                             const std::string& name, std::vector<Claim>& claims) {
        if (!data.contains(key) || !data[key].isArray()) return;
        const Json& curve = data[key];
        double best = 0.0;
        double at = 0.0;
        for (int i = 0; i < curve.size(); ++i) {
            const Json& point = curve.at(i);
            if (!point.isArray() || point.size() < 2) continue;
            const double value = point.at(1).toDouble();
            if (value > best) {
                best = value;
                at = point.at(0).toDouble();
            }
        }
        if (best <= 0.0) return;
        Claim claim;
        claim.path = key + " (peak)";
        claim.value = numberText(Json(best)) + " at " + numberText(Json(at)) + " rpm";
        claim.unit = unit;
        claim.query = name + " " + key + " " + unit + " rpm";
        claims.push_back(claim);
    }

    static std::vector<Claim> verifyClaims(const Json& data) {
        struct Lookup {
            const char* path;
            const char* unit;
            const char* topic;
        };
        static const Lookup kLookups[] = {
            {"specs.bhp", "bhp as declared", "power bhp rpm"},
            {"specs.torque", "Nm as declared", "torque Nm rpm"},
            {"specs.weight", "kg as declared", "weight kg"},
            {"car.BASIC.TOTALMASS", "kg", "kerb weight kg"},
            {"engine.ENGINE_DATA.LIMITER", "rpm", "limiter redline rpm"},
            {"drivetrain.GEARS.COUNT", "gears", "gearbox number of gears"},
            {"tyres.FRONT.WIDTH", "m", "front tyre width"},
            {"tyres.FRONT.RADIUS", "m", "front tyre diameter"},
            {"tyres.FRONT.RIM_RADIUS", "m", "front wheel rim size"},
            {"tyres.REAR.WIDTH", "m", "rear tyre width"},
            {"tyres.REAR.RADIUS", "m", "rear tyre diameter"},
            {"tyres.REAR.RIM_RADIUS", "m", "rear wheel rim size"},
            {"fuel.MAX_FUEL", "litres", "fuel tank capacity litres"}};
        const std::string name = data.contains("name") ? data["name"].toString() : std::string();
        std::vector<Claim> claims;
        for (const Lookup& lookup : kLookups) {
            const Json* value = findPath(data, lookup.path);
            if (!value) continue;
            Claim claim;
            claim.path = lookup.path;
            claim.value = numberText(*value);
            claim.unit = lookup.unit;
            claim.query = name + " " + lookup.topic;
            claims.push_back(claim);
        }
        addCurvePeak(data, "powerCurve", "bhp", name, claims);
        addCurvePeak(data, "torqueCurve", "Nm", name, claims);
        return claims;
    }

    // Writes the checklist for a converted car, keeping every outcome already
    // recorded for a claim that is still there.
    static Result verify(const fs::path& carDir) {
        Result result;
        const fs::path dataPath = documentPath(carDir, kDataDocument);
        std::ifstream in(dataPath, std::ios::binary);
        if (!in) {
            result.error = "no data document in " + carDir.string();
            return result;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        bool ok = false;
        std::string error;
        Json data = Json::parse(buffer.str(), &ok, &error);
        if (!ok || !data.isObject()) {
            result.error = "invalid data document: " + error;
            return result;
        }

        Json previous = Json::array();
        const fs::path out = verifyPath(carDir);
        if (fs::exists(out)) {
            std::ifstream prev(out, std::ios::binary);
            std::ostringstream prevBuffer;
            prevBuffer << prev.rdbuf();
            bool prevOk = false;
            std::string prevError;
            Json previousDoc = Json::parse(prevBuffer.str(), &prevOk, &prevError);
            if (prevOk && previousDoc.isObject() && previousDoc.contains("claims") &&
                previousDoc["claims"].isArray()) {
                previous = previousDoc["claims"];
            }
        }

        Json claims = Json::array();
        for (const Claim& claim : verifyClaims(data)) {
            Json entry = Json::object();
            entry.insert("path", Json(claim.path));
            entry.insert("value", Json(claim.value));
            entry.insert("unit", Json(claim.unit));
            entry.insert("query", Json(claim.query));
            entry.insert("status", Json(std::string("unchecked")));
            entry.insert("source", Json(std::string()));
            entry.insert("note", Json(std::string()));
            for (int i = 0; i < previous.size(); ++i) {
                const Json& old = previous.at(i);
                if (old.contains("path") && old["path"].toString() == claim.path) {
                    for (const char* field : {"status", "source", "note"}) {
                        if (old.contains(field)) entry.insert(field, old[field]);
                    }
                    break;
                }
            }
            const std::string status = entry["status"].toString();
            if (!status.empty() && status != "unchecked") ++result.checked;
            if (status == "mismatch") ++result.mismatches;
            claims.append(entry);
            ++result.claims;
        }

        Json coverage = Json::object();
        coverage.insert("total", Json(result.claims));
        coverage.insert("checked", Json(result.checked));
        coverage.insert("mismatch", Json(result.mismatches));
        const char* status = "unchecked";
        if (result.checked > 0) status = result.checked == result.claims ? "complete" : "partial";
        coverage.insert("status", Json(std::string(status)));

        Json doc = Json::object();
        doc.insert("car", Json(data.contains("name") ? data["name"].toString() : std::string()));
        doc.insert("brand", Json(data.contains("brand") ? data["brand"].toString() : std::string()));
        doc.insert("coverage", coverage);
        doc.insert("claims", claims);

        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        if (!file) {
            result.error = "cannot write " + out.string();
            return result;
        }
        file << doc.dump(true);
        file.put(static_cast<char>(10));
        file.close();
        if (!file) {
            result.error = "write failed for " + out.string();
            return result;
        }
        result.verified = true;
        return result;
    }

    static bool setPath(Json& root, const std::string& path, const Json& value) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            const std::size_t dot = path.find('.', start);
            parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        if (parts.empty()) return false;
        Json* node = &root;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!node->isObject()) return false;
            if (!node->contains(parts[i])) node->insert(parts[i], Json::object());
            node = &(*node)[parts[i]];
        }
        if (!node->isObject()) return false;
        if (node->contains(parts.back())) node->remove(parts.back());
        node->insert(parts.back(), value);
        return true;
    }

    // A recorded mismatch is a proposal, never an edit: the data are rewritten
    // only for the claims whose proposal was accepted (accepted = true in
    // verify.json). Everything else keeps the value the content shipped with.
    static Result applyProposals(const fs::path& carDir) {
        Result result;
        const fs::path checklistPath = verifyPath(carDir);
        std::ifstream in(checklistPath, std::ios::binary);
        if (!in) {
            result.error = "no verification checklist in " + carDir.string();
            return result;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        bool ok = false;
        std::string error;
        Json checklist = Json::parse(buffer.str(), &ok, &error);
        if (!ok || !checklist.isObject() || !checklist.contains("claims")) {
            result.error = "invalid verification checklist: " + error;
            return result;
        }

        const fs::path dataPath = documentPath(carDir, kDataDocument);
        std::ifstream dataIn(dataPath, std::ios::binary);
        if (!dataIn) {
            result.error = "no data document in " + carDir.string();
            return result;
        }
        std::ostringstream dataBuffer;
        dataBuffer << dataIn.rdbuf();
        Json data = Json::parse(dataBuffer.str(), &ok, &error);
        if (!ok || !data.isObject()) {
            result.error = "invalid data document: " + error;
            return result;
        }

        const Json& claims = checklist["claims"];
        for (int i = 0; i < claims.size(); ++i) {
            const Json& claim = claims.at(i);
            const std::string path = claim.contains("path") ? claim["path"].toString() : std::string();
            const bool accepted = claim.contains("accepted") && claim["accepted"].toBool(false);
            // A curve peak is not one value: it cannot be patched mechanically.
            if (!accepted || !claim.contains("proposal") || path.find(" (peak)") != std::string::npos) {
                ++result.skipped;
                continue;
            }
            if (!setPath(data, path, claim["proposal"])) {
                ++result.skipped;
                continue;
            }
            ++result.applied;
        }

        if (result.applied == 0) {
            // Nothing to write, but the caller still wants the coverage.
            const Result audit = verify(carDir);
            result.claims = audit.claims;
            result.checked = audit.checked;
            result.mismatches = audit.mismatches;
            result.verified = audit.verified;
            return result;
        }

        std::ofstream file(dataPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            result.error = "cannot write " + dataPath.string();
            return result;
        }
        file << data.dump(true);
        file.put(static_cast<char>(10));
        file.close();
        if (!file) {
            result.error = "write failed for " + dataPath.string();
            return result;
        }
        const Result audit = verify(carDir);
        result.claims = audit.claims;
        result.checked = audit.checked;
        result.mismatches = audit.mismatches;
        result.verified = audit.verified;
        return result;
    }

    // Final step of the flow: the archive has to say the same thing as the
    // folder it stands for, so it is rebuilt from whatever data/ holds now
    // (documents, curves, presets) and encrypted with the car's own key.
    static Result packAcd(const fs::path& carDir) {
        Result result;
        if (!hasManifest(carDir)) {
            result.error = "car is not converted: the seven documents are not all there";
            return result;
        }
        // A trailing separator leaves the path without a name, and the name is
        // the key seed.
        fs::path named = carDir;
        while (named.has_relative_path() && named.filename().empty()) named = named.parent_path();
        const fs::path dataDir = fs::exists(carDir / "data") ? carDir / "data" : carDir;
        std::string error;
        result.packed = static_cast<int>(ks::engine::fileformat::writeAcdFromDirectory(
            dataDir, named.filename().string(), carDir / "data.acd", &error));
        if (!error.empty()) {
            result.error = error;
            return result;
        }
        result.packDone = true;
        return result;
    }

    // Third step of the flow: the .ini files are superseded by the documents.
    // It refuses while the required entries are not all there, because pruning
    // a car that failed the first step would leave it with nothing to read.
    static Result prune(const fs::path& carDir) {
        Result result;
        if (!hasManifest(carDir)) {
            result.error = "car is not converted: the seven documents are not all there";
            return result;
        }
        const std::vector<std::string> names = documents();
        std::vector<Json> docs;
        docs.reserve(names.size());
        for (const std::string& name : names) {
            std::ifstream in(documentPath(carDir, name), std::ios::binary);
            if (!in) {
                result.error = "cannot read " + name;
                return result;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            bool ok = false;
            std::string error;
            Json doc = Json::parse(buffer.str(), &ok, &error);
            if (!ok || !doc.isObject()) {
                result.error = "invalid " + name + ": " + error;
                return result;
            }
            docs.push_back(doc);
        }
        result.missing = missingRequired(docs, names);
        if (!result.missing.empty()) {
            result.error = "required entries missing, refusing to prune";
            return result;
        }

        const fs::path dataDir = fs::exists(carDir / "data") ? carDir / "data" : carDir;
        std::vector<fs::path> unwanted;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (!iequals(entry.path().extension().string(), ".ini")) continue;
            unwanted.push_back(entry.path());
        }
        for (const fs::path& path : unwanted) {
            std::error_code removeError;
            if (fs::remove(path, removeError)) ++result.pruned;
        }
        result.pruneDone = true;
        // The folder and the archive must agree: rebuild data.acd now that the
        // .ini files are gone, so AC and the documents tell the same story.
        const Result packed = packAcd(carDir);
        result.packed = packed.packed;
        result.packDone = packed.packDone;
        if (!packed.error.empty()) result.error = packed.error;
        return result;
    }

    static Result convert(const fs::path& carDir) {
        Result result;
        if (carDir.empty() || !fs::exists(carDir)) {
            result.error = "car folder not found";
            return result;
        }
        const fs::path dataDir = fs::exists(carDir / "data") ? carDir / "data" : carDir;
        std::vector<fs::path> inis;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (iequals(entry.path().extension().string(), ".ini")) inis.push_back(entry.path());
        }
        if (inis.empty()) {
            // Nothing to convert from, but the legacy data document may be
            // there: complete the folder by splitting it.
            return split(carDir);
        }
        std::sort(inis.begin(), inis.end());

        Json root = Json::object();
        mergeMetadata(carDir, root);
        carryOverMetadata(carDir, root);
        Json fuel = Json::object();
        for (const fs::path& path : inis) {
            config::IniFile ini;
            if (!ini.load(path.string())) continue;
            ++result.iniFiles;
            const std::string stem = path.stem().string();
            if (iequals(stem, "fuel_cons")) {
                for (const auto& group : ini.groups()) collectFuel(group, fuel);
                continue;
            }
            // Stems that carry nothing the car needs: escmode.ini only declares
            // the AC electronic-aid presets a road car cycles through,
            // ac_tyres.ini repeats the tyre geometry, width and radius already
            // held by tyres.ini, and blurred_objects.ini / ambient_shadows.ini
            // only tell the renderer which props spin-blur and where the baked
            // shadows sit. All four files stay in the folder, untouched.
            if (iequals(stem, "escmode") || iequals(stem, "ac_tyres") ||
                iequals(stem, "blurred_objects") || iequals(stem, "ambient_shadows")) {
                continue;
            }
            Json& target = objectAt(root, stem);
            for (const auto& group : ini.groups()) {
                const std::string section = upper(group.name);
                if (iequals(stem, "car") && (section == "FUEL" || section == "FUELTANK")) {
                    collectFuel(group, fuel);
                    continue;
                }
                mergeGroup(target, section, group, result);
            }
        }
        root.insert("fuel", fuel);
        normalize(root, carDir);

        if (!writeDocuments(carDir, root, result)) return result;
        const Result audit = verify(carDir);
        result.claims = audit.claims;
        result.checked = audit.checked;
        result.mismatches = audit.mismatches;
        result.verified = audit.verified;
        result.converted = true;
        return result;
    }

private:
    static bool iequals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    static std::string upper(const std::string& text) {
        std::string out = text;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return out;
    }

    static std::string lower(const std::string& text) {
        std::string out = text;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    static Json& objectAt(Json& parent, const std::string& key) {
        if (!parent.contains(key)) parent.insert(key, Json::object());
        return parent[key];
    }

    // AC .ini values are text: turn the numeric ones into JSON numbers (what
    // the reference manifest stores) and leave everything else as a string,
    // comments included ("50  ;kmh" stays verbatim).
    static Json coerce(const std::string& raw) {
        // AC writes inline comments after the value ("MAX_FUEL=200  ; max fuel").
        // Cut them first, otherwise every commented number stays a string and
        // the physics readers cannot use it. The original text stays in the .ini
        // files next to the manifest, so nothing is lost.
        std::string text = config::IniFile::trim(raw);
        const std::size_t comment = text.find(';');
        if (comment != std::string::npos) text = config::IniFile::trim(text.substr(0, comment));
        if (text.empty()) return Json(std::string());
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end && *end == 0) {
            const bool integral = text.find_first_of(".eE") == std::string::npos;
            if (integral) {
                char* iend = nullptr;
                const long long ivalue = std::strtoll(text.c_str(), &iend, 10);
                if (iend && *iend == 0) return Json(ivalue);
            }
            return Json(value);
        }
        return Json(text);
    }

    static void coerceDeep(Json& value) {
        if (value.isString()) {
            const std::string text = value.toString();
            if (text.empty() || text.find_first_of("0123456789") == std::string::npos) return;
            char* end = nullptr;
            std::strtod(text.c_str(), &end);
            if (end && *end == 0) value = coerce(text);
            return;
        }
        if (value.isArray()) {
            for (int i = 0; i < value.size(); ++i) {
                Json child = value.at(i);
                coerceDeep(child);
                value[i] = child;
            }
            return;
        }
        if (value.isObject()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                Json child = it.value();
                coerceDeep(child);
                value[it.key()] = child;
            }
        }
    }

    // ui_car.json is hand written: it may carry raw control characters inside
    // strings, which strict JSON rejects. Flatten them to spaces instead of
    // dropping the metadata.
    static std::string sanitizeControlChars(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        bool inString = false;
        bool escaped = false;
        for (const char c : text) {
            if (inString) {
                if (escaped) {
                    escaped = false;
                    out.push_back(c);
                    continue;
                }
                if (c == static_cast<char>(92)) {
                    escaped = true;
                    out.push_back(c);
                    continue;
                }
                if (c == '"') {
                    inString = false;
                    out.push_back(c);
                    continue;
                }
                out.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
                continue;
            }
            if (c == '"') inString = true;
            out.push_back(c);
        }
        return out;
    }

    // A document written by hand or by an older conversion can carry a comma
    // before its closing brace. The values are still readable, so the comma goes
    // instead of the whole document.
    static std::string withoutTrailingCommas(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == ',') {
                std::size_t j = i + 1;
                while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
                if (j < text.size() && (text[j] == '}' || text[j] == ']')) continue;
            }
            out.push_back(text[i]);
        }
        return out;
    }

    // The same repair for a document that was closed twice: everything after the
    // end of the first object is not part of it.
    static std::string firstJsonObject(const std::string& text) {
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        bool started = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') {
                inString = true;
                continue;
            }
            if (c == '{') {
                ++depth;
                started = true;
            } else if (c == '}') {
                --depth;
                if (started && depth == 0) return text.substr(0, i + 1);
            }
        }
        return text;
    }

    static void mergeMetadata(const fs::path& carDir, Json& root) {
        // AC keeps the menu metadata in ui/ui_car.json; some content keeps the
        // same file at the car root. Both are read, and ui/ wins when the two
        // disagree because it is the layout AC defines.
        for (const fs::path& path : {carDir / "ui_car.json", carDir / "ui" / "ui_car.json"}) {
            std::ifstream in(path, std::ios::binary);
            if (!in) continue;
            std::ostringstream buffer;
            buffer << in.rdbuf();
            bool ok = false;
            std::string error;
            Json doc = Json::parse(sanitizeControlChars(buffer.str()), &ok, &error);
            if (!ok || !doc.isObject()) continue;
            for (auto it = doc.begin(); it != doc.end(); ++it) {
                // "parent" is AC inheritance, meaningless once the manifest is one
                // self-contained document.
                if (iequals(it.key(), "parent")) continue;
                Json value = it.value();
                coerceDeep(value);
                root.insert(it.key(), value);
            }
        }
    }

    // The .ini set never says what the menu shows: name, brand, specs, tags, the
    // curves the dyno draws. Those come from ui_car.json and from the data
    // document an earlier conversion wrote. Nothing is invented here: what the
    // car already says is carried over, so converting from the .ini set again
    // cannot lose it. Only the metadata keys are taken, never a physics section:
    // those are what the .ini files are about.
    static void carryOverMetadata(const fs::path& carDir, Json& root) {
        std::ifstream in(documentPath(carDir, kDataDocument), std::ios::binary);
        if (!in) return;
        std::ostringstream buffer;
        buffer << in.rdbuf();
        bool ok = false;
        std::string error;
        Json doc = Json::parse(firstJsonObject(withoutTrailingCommas(
                                   sanitizeControlChars(buffer.str()))), &ok, &error);
        if (!ok || !doc.isObject()) return;
        static const char* kMetadata[] = {"name",  "brand",     "class",      "year",
                                          "tags",  "description", "author",   "email",
                                          "url",   "version",   "specs",      "powerCurve",
                                          "torqueCurve", "country", "colors"};
        for (const char* key : kMetadata) {
            if (!doc.contains(key)) continue;
            bool present = false;
            for (const std::string& existing : root.keys()) {
                if (iequals(existing, key)) {
                    present = true;
                    break;
                }
            }
            if (present) continue;
            Json value = doc[key];
            coerceDeep(value);
            root.insert(key, value);
        }
    }

    static void mergeGroup(Json& target, const std::string& section,
                           const config::IniFile::Group& group, Result& result) {
        Json& dst = objectAt(target, section);
        for (const auto& entry : group.entries) {
            if (dst.contains(entry.key)) continue;
            dst.insert(entry.key, coerce(entry.value));
        }
        ++result.sections;
    }

    // car.ini [FUEL] (CONSUMPTION/FUEL/MAX_FUEL), [FUELTANK] POSITION and
    // fuel_cons.ini [FUEL_EVAL] KM_PER_LITER end up in one "fuel" object, the
    // same way the reference manifest stores them.
    static void collectFuel(const config::IniFile::Group& group, Json& fuel) {
        for (const auto& entry : group.entries) {
            std::string key = upper(entry.key);
            if (key == "POSITION") key = "FUELTANK_POSITION";
            if (fuel.contains(key)) continue;
            fuel.insert(key, coerce(entry.value));
        }
    }

    // The brake temperature performance curve is a curve like every other one,
    // so it ships as a .lut file beside the documents instead of an inline
    // string: the same x|y lines power.lut and the aero curves use.
    static constexpr const char* kBrakeTempFrontLut = "brake_temp_front.lut";
    static constexpr const char* kBrakeTempRearLut = "brake_temp_rear.lut";

    static const char* brakeTempCurveText() {
        return "0|0.3409\r\n400|1\r\n900|0.9659\r\n1200|0.5909\r\n2000|0.3409\r\n";
    }

    // Brake discs and suspension legs are model nodes, and the documents name
    // them so the runtime can move them with the wheel. AC content only lists
    // them when brakes.ini [DISCS_GRAPHICS] happens to be there, so the nodes
    // the model really carries decide the mapping.
    static void collectNodeNames(const engine::fileformat::Kn5Node& node,
                                 std::vector<std::string>& out) {
        if (!node.name.empty()) out.push_back(node.name);
        for (const engine::fileformat::Kn5Node& child : node.children) collectNodeNames(child, out);
    }

    // The main model is the biggest .kn5 in the folder: collider.kn5 and the
    // LOD files are either tiny or a subset of it.
    static std::vector<std::string> collectModelNodeNames(const fs::path& carDir) {
        std::vector<std::string> names;
        std::error_code ec;
        fs::path model;
        std::uintmax_t modelSize = 0;
        for (const auto& entry : fs::directory_iterator(carDir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (!iequals(entry.path().extension().string(), ".kn5")) continue;
            if (iequals(entry.path().stem().string(), "collider")) continue;
            const std::uintmax_t size = entry.file_size(ec);
            if (size > modelSize) {
                modelSize = size;
                model = entry.path();
            }
        }
        if (model.empty()) return names;
        const engine::fileformat::Kn5ParseResult parsed =
            engine::fileformat::parseKn5File(model.string());
        if (!parsed.ok()) return names;
        for (const engine::fileformat::Kn5Node& root : parsed.file.nodes)
            collectNodeNames(root, names);
        return names;
    }

    // The corner must be a token of its own (LF, not the L of "LOW"), so a node
    // of another part never answers for a corner it does not belong to.
    static std::string findComponentNode(const std::vector<std::string>& nodes,
                                         const char* marker, const std::string& corner) {
        for (const std::string& name : nodes) {
            const std::string upperName = upper(name);
            if (upperName.find(marker) == std::string::npos) continue;
            std::size_t at = upperName.find(corner);
            while (at != std::string::npos) {
                const bool startOk =
                    at == 0 || !std::isalnum(static_cast<unsigned char>(upperName[at - 1]));
                const std::size_t end = at + corner.size();
                const bool endOk = end >= upperName.size() ||
                                   !std::isalnum(static_cast<unsigned char>(upperName[end]));
                if (startOk && endOk) return name;
                at = upperName.find(corner, at + 1);
            }
        }
        return std::string();
    }

    // One role per corner, the same shape the reference manifests carry:
    // DISC_LF -> the disc node, SUSP_LF -> the suspension leg node. A car whose
    // model has no such node gets no entry, never a guess.
    static void addComponentMaps(Json& root, const std::vector<std::string>& nodes) {
        if (nodes.empty()) return;
        // One scope per component family: inserting a section into brakes
        // reallocates its members, so a reference taken before that insertion
        // can dangle afterwards.
        {
            Json& discs = objectAt(objectAt(root, "brakes"), "DISCS_COMPONENTS");
            for (const char* corner : {"LF", "RF", "LR", "RR"}) {
                const std::string token = corner;
                const std::string role = "DISC_" + token;
                if (discs.contains(role)) continue;
                std::string node = findComponentNode(nodes, "DISC", token);
                if (node.empty()) node = findComponentNode(nodes, "DISK", token);
                if (!node.empty()) discs.insert(role, Json(node));
            }
        }
        {
            Json& suspensions = objectAt(objectAt(root, "brakes"), "SUSPENSIONS_COMPONENTS");
            for (const char* corner : {"LF", "RF", "LR", "RR"}) {
                const std::string token = corner;
                const std::string role = "SUSP_" + token;
                if (suspensions.contains(role)) continue;
                const std::string node = findComponentNode(nodes, "SUSP", token);
                if (!node.empty()) suspensions.insert(role, Json(node));
            }
        }
    }

    static bool writeBrakeTempLuts(const fs::path& carDir) {
        const fs::path dir = carDir / "data";
        std::error_code ec;
        fs::create_directories(dir, ec);
        for (const char* name : {kBrakeTempFrontLut, kBrakeTempRearLut}) {
            std::ofstream file(dir / name, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file << brakeTempCurveText();
        }
        return true;
    }

    // Brake temperature profile a car that writes no [TEMPS_FRONT]/[TEMPS_REAR]
    // falls back to. The values are the ones the reference manifests carry; only
    // the leading cooling factor differs between the axles.
    static Json brakeTempProfile(const char* lutName, double coolSpeedFactor0) {
        Json profile = Json::object();
        profile.insert("TRANSFER", Json(0.4));
        profile.insert("COOL_TRANSFER", Json(0.002));
        profile.insert("TORQUE_K", Json(0.4));
        profile.insert("PERF_CURVE", Json(lutName));
        profile.insert("COOL_SPEED_FACTOR", Json(0.06));
        profile.insert("COOL_SPEED_FACTOR_0", Json(coolSpeedFactor0));
        profile.insert("CORE_TRANSFER_IN", Json(0.16));
        profile.insert("CORE_TRANSFER_OUT", Json(0.5));
        profile.insert("CORE_TRANSFER_AIR", Json(0.0));
        return profile;
    }

    static void addTypeDefaults(Json& root, Json& fuel) {
        if (!fuel.contains("KG_PER_LITER") &&
            (fuel.contains("MAX_FUEL") || fuel.contains("FUEL")))
            fuel.insert("KG_PER_LITER", Json(kFuelDensityKgPerL));

        Json& car = objectAt(root, "car");
        Json& rules = objectAt(car, "RULES");
        if (!rules.contains("MIN_HEIGHT")) rules.insert("MIN_HEIGHT", Json(0));

        // Cooling and lubrication are engine systems, so their warning
        // thresholds sit in the engine block.
        Json& engine = objectAt(root, "engine");
        if (!engine.contains("WATER_LIGHT_MAX_C"))
            engine.insert("WATER_LIGHT_MAX_C", Json(kWaterLightMaxC));
        if (!engine.contains("OIL_LIGHT_MIN_BAR"))
            engine.insert("OIL_LIGHT_MIN_BAR", Json(kOilLightMinBar));

        if (!root.contains("SHORT_NAME")) {
            const std::string brand = root["brand"].toString();
            if (brand.size() >= 3) root.insert("SHORT_NAME", Json(upper(brand.substr(0, 3))));
        }

        // addImpliedDefaults: the entries every car must carry but older content
        // never writes, because AC keeps them inside its own physics code. Each
        // one is added here and only here, across vehicle types, never per car.
        //
        // One scope per section: objectAt(root, ...) may insert into root, and a
        // reference taken before that insertion can dangle afterwards.
        {
            // brakes.ini [TEMPS_FRONT]/[TEMPS_REAR] are optional overrides.
            Json& brakes = objectAt(root, "brakes");
            if (!brakes.contains("TEMPS_FRONT"))
                brakes.insert("TEMPS_FRONT", brakeTempProfile(kBrakeTempFrontLut, 0.05));
            if (!brakes.contains("TEMPS_REAR"))
                brakes.insert("TEMPS_REAR", brakeTempProfile(kBrakeTempRearLut, 0.04));
        }
        {
            // engine.ini [HEADER] and [ENGINE_DATA]: which power curve standard
            // the torque curve follows, the humidity the correction was measured
            // at, and whether ambient conditions change the output.
            Json& engine = objectAt(root, "engine");
            Json& header = objectAt(engine, "HEADER");
            if (!header.contains("POWER_CURVE_STANDARD")) header.insert("POWER_CURVE_STANDARD", Json(2));
            if (!header.contains("HUMIDITY_REF")) header.insert("HUMIDITY_REF", Json(0));
            Json& engineData = objectAt(engine, "ENGINE_DATA");
            if (!engineData.contains("AMBIENT_EFFECT"))
                engineData.insert("AMBIENT_EFFECT", Json(0));
        }
        {
            // ai.ini [TYRES]/[WING_VARIATION]: how much grip the AI is allowed to
            // assume, and whether it trades wing for speed. No car without them
            // moves either, so both stay at the values the references carry.
            Json& ai = objectAt(root, "ai");
            if (!ai.contains("TYRES")) {
                Json tyres = Json::object();
                tyres.insert("HINT", Json(1));
                ai.insert("TYRES", tyres);
            }
            if (!ai.contains("WING_VARIATION")) {
                Json wing = Json::object();
                wing.insert("VALUE", Json(0));
                ai.insert("WING_VARIATION", wing);
            }
        }
        {
            // electronics.ini: older cars carry [ABS]/[TRACTION_CONTROL] and no
            // ABS_V2/EDL block. The newer shapes mirror what the car declares,
            // so nothing is invented for a car that never had these systems.
            Json& electronics = objectAt(root, "electronics");
            if (!electronics.contains("ABS_V2")) {
                Json abs2 = Json::object();
                const Json* v1 = electronics.contains("ABS") && electronics["ABS"].isObject()
                                     ? &electronics["ABS"]
                                     : nullptr;
                static const char* kShared[] = {"SLIP_RATIO_LIMIT", "CURVE", "PRESENT",
                                                "ACTIVE", "RATE_HZ"};
                for (const char* key : kShared) {
                    if (v1 && v1->contains(key)) abs2.insert(key, (*v1)[key]);
                }
                if (!abs2.contains("SLIP_RATIO_LIMIT")) abs2.insert("SLIP_RATIO_LIMIT", Json(0.12));
                if (!abs2.contains("CURVE")) abs2.insert("CURVE", Json(""));
                if (!abs2.contains("PRESENT")) abs2.insert("PRESENT", Json(0));
                if (!abs2.contains("ACTIVE")) abs2.insert("ACTIVE", Json(0));
                if (!abs2.contains("RATE_HZ")) abs2.insert("RATE_HZ", Json(100));
                abs2.insert("CHANNELS", Json(4));
                electronics.insert("ABS_V2", abs2);
            }
            if (!electronics.contains("EDL")) {
                Json edl = Json::object();
                const Json* tc =
                    electronics.contains("TRACTION_CONTROL") && electronics["TRACTION_CONTROL"].isObject()
                        ? &electronics["TRACTION_CONTROL"]
                        : nullptr;
                edl.insert("PRESENT", tc && tc->contains("PRESENT") ? (*tc)["PRESENT"] : Json(0));
                edl.insert("ACTIVE", tc && tc->contains("ACTIVE") ? (*tc)["ACTIVE"] : Json(0));
                edl.insert("MAX_SPIN_POWER", Json(0.55));
                edl.insert("MAX_SPIN_COAST", Json(0.4));
                edl.insert("BRAKE_TORQUE_POWER", Json(140));
                edl.insert("BRAKE_TORQUE_COAST", Json(420));
                edl.insert("DEAD_ZONE_POWER", Json(0.12));
                edl.insert("DEAD_ZONE_COAST", Json(0.12));
                electronics.insert("EDL", edl);
            }
        }
        {
            // Camera angles car.ini [GRAPHICS] declares per car. A car that
            // writes none gets no tilt, which is exactly what AC does with a key
            // it does not find: the camera looks straight ahead.
            Json& car = objectAt(root, "car");
            Json& graphics = objectAt(car, "GRAPHICS");
            for (const char* key : {"BONNET_CAMERA_PITCH", "BUMPER_CAMERA_PITCH",
                                    "CHASE_CAMERA_PITCH"}) {
                if (!graphics.contains(key)) graphics.insert(key, Json(0));
            }
        }
    }
};

}  // namespace assetto_corsa
}  // namespace ks
