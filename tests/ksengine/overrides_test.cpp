// Qt-free test for the content overrides (src/adapters/assetto_corsa/Overrides.h).
//
// The 499P's skin ships the file this is written against: a driver model, two
// hidden driver nodes and two model blocks with node lists. A skin's file belongs
// to the skin, so it becomes skins/<id>/skin.json; a car-root file belongs to the
// car and is folded into model.json and driver.json. The test builds both shapes
// in a temp directory and checks where each part lands, that the car documents
// never carry skin data, and that the matcher the renderer uses
// (Graphics/HiddenNodes.h) reads both.

#include "adapters/assetto_corsa/Overrides.h"
#include "Graphics/HiddenNodes.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using Json = ks::Json;

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what);
        ++g_failures;
    }
}

int nodeCount(const Json& block) {
    if (!block.isObject() || !block.contains("hidden_nodes")) return 0;
    int count = 0;
    for (const auto& value : block["hidden_nodes"]) {
        (void)value;
        ++count;
    }
    return count;
}

bool hasNode(const Json& block, const std::string& node) {
    if (!block.isObject() || !block.contains("hidden_nodes")) return false;
    for (const auto& value : block["hidden_nodes"])
        if (value.toString() == node) return true;
    return false;
}

bool hasSection(const Json& block, const std::string& section) {
    return block.isObject() && block.contains("sections") && block["sections"].contains(section);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const std::string kSkinFile =
    "[DRIVER3D_MODEL]\n"
    "NAME=rh_bell_helmet_driver_19\n"
    "POSITION=0.0,0.5,-1.0\n"
    "\n"
    "[DRIVER3D_HIDE_OBJECT_0]\n"
    "NAME=DRIVER:HELMET\n"
    "\n"
    "[DRIVER3D_HIDE_OBJECT_1]\n"
    "NAME=DRIVER:GEO_Driver_FACE\n"
    "\n"
    "[MODEL_REPLACEMENT_...]\n"
    "FILE =  ferrari_499p_LODA.kn5\n"
    "HIDE =  KERS_LEVEL_?, DISPLAY_FUEL_WARNING, DISPLAY_HIGHBEAM, DISPLAY_MONITOR_001\n"
    "\n"
    "[MODEL_REPLACEMENT_0]\n"
    "FILE = ferrari_499p_LODA.kn5\n"
    "HIDE = GEO_BD_Disc_Hub_LF_Decal,GEO_BD_Disc_Hub_LR_Decal\n";

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace ks::assetto_corsa;

    // `overrides_test --print-content <carDir>` prints what the converter would
    // write for that car — the car documents' keys and every skin's skin.json —
    // so an installed car is refreshed with the same code that produces it.
    if (argc == 3 && std::string(argv[1]) == "--print-content") {
        const fs::path carDir(argv[2]);
        Json printed = Json::object();
        Json model = Json::object();
        Json driver = Json::object();
        mergeOverrides(carDir, model, driver);
        Json documents = Json::object();
        for (const char* key : {"hidden_nodes", "sections", "driver_model"}) {
            if (model.contains(key)) documents.insert(std::string("model.") + key, model[key]);
            if (driver.contains(key)) documents.insert(std::string("driver.") + key, driver[key]);
        }
        printed.insert("documents", documents);
        Json skins = Json::object();
        for (const std::string& id : skinsWithOverrides(carDir)) {
            Json document;
            if (skinOverridesDocument(carDir / "skins" / id, id, document))
                skins.insert(id, document);
        }
        printed.insert("skins", skins);
        std::printf("%s\n", printed.dump(true).c_str());
        return 0;
    }

    const fs::path root = fs::temp_directory_path() / "ksim_overrides_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeText(root / "skins" / "2024_AFCorse_LeMans_50" / "ext_config.ini", kSkinFile);

    // A skin's file becomes that skin's skin.json, split by subject.
    check(writeSkinOverrides(root) == 1, "the skin's overrides are written");
    const fs::path skinPath = root / "skins" / "2024_AFCorse_LeMans_50" / "skin.json";
    check(fs::is_regular_file(skinPath, ec), "skins/<id>/skin.json exists");
    {
        bool ok = false;
        const Json skin = Json::parse(readText(skinPath), &ok);
        check(ok, "skin.json parses");
        check(skin.contains("skin") && skin["skin"].toString() == "2024_AFCorse_LeMans_50",
              "skin.json names its skin");
        check(skin.contains("model") && skin.contains("driver"), "skin.json holds both subjects");
        check(nodeCount(skin["model"]) == 6, "the model's nodes are in the skin's model");
        check(hasNode(skin["model"], "KERS_LEVEL_?"), "a wildcard survives into skin.json");
        check(hasSection(skin["model"], "MODEL_REPLACEMENT_..."), "model sections stay model");
        check(nodeCount(skin["driver"]) == 2, "the driver's nodes are in the skin's driver");
        check(hasSection(skin["driver"], "DRIVER3D_MODEL"), "the driver sections stay with the driver");
        check(skin["driver"].contains("driver_model"), "the driver model is a field of its own");
        check(skin["driver"]["driver_model"]["name"].toString() == "rh_bell_helmet_driver_19",
              "the driver model name is the one the file names");
        const Json& position = skin["driver"]["driver_model"]["position"];
        check(position.isArray() && position.size() == 3, "the position is three numbers");
        check(position[1].toDouble() == 0.5, "the position keeps its values");
        check(!hasSection(skin["model"], "DRIVER3D_MODEL"), "no driver section in the model");
        check(!skin.contains("IMPORTED_FROM"), "skin.json names no source file");
    }

    // Writing again is idempotent, and the car documents carry no skin data.
    check(writeSkinOverrides(root) == 1, "rewriting the skin document counts one file");
    Json model = Json::object();
    Json driver = Json::object();
    check(mergeOverrides(root, model, driver) == 0, "a car without overrides reports zero");
    check(!model.contains("hidden_nodes") && !model.contains("sections") &&
              !driver.contains("hidden_nodes") && !driver.contains("driver_model"),
          "no skin data leaks into model.json or driver.json");

    // Keys left by an earlier merge are rebuilt away, not kept.
    {
        Json stale = Json::object();
        stale.insert("hidden_nodes", Json::array());
        mergeOverrides(root, stale, driver);
        check(!stale.contains("hidden_nodes"), "a stale hidden node list is removed");
    }

    // The car-root file is the car's own: it lands in the documents, split by
    // subject, and a skin's file never does.
    const fs::path carRoot = root / "carroot";
    writeText(carRoot / "ext_config.ini",
              "[DRIVER3D_MODEL]\nNAME=driver_a\nPOSITION=1,2,3\n"
              "[DRIVER3D_HIDE_OBJECT_0]\nNAME=DRIVER:HELMET\n"
              "[MODEL_REPLACEMENT_0]\nHIDE=DISPLAY_HIGHBEAM\n");
    Json carModel = Json::object();
    Json carDriver = Json::object();
    check(mergeOverrides(carRoot, carModel, carDriver) == 1, "the car-root overrides are read");
    check(hasNode(carModel, "DISPLAY_HIGHBEAM"), "a model node goes to model.json");
    check(hasNode(carDriver, "DRIVER:HELMET"), "a driver node goes to driver.json");
    check(!hasSection(carModel, "DRIVER3D_HIDE_OBJECT_0"),
          "the driver's sections stay out of model.json");
    check(carDriver["driver_model"]["name"].toString() == "driver_a",
          "the car-level driver model lands in driver.json");
    check(!carModel.contains("driver_model"), "model.json knows nothing about the driver");
    check(!carModel.contains("skin") && !carDriver.contains("skin"),
          "car documents carry no skin");

    // The renderer's matcher reads both sources and hides by the content's rules.
    {
        using ks::graphics::HiddenNodes;
        using ks::graphics::nodeNameMatches;
        check(nodeNameMatches("KERS_LEVEL_?", "KERS_LEVEL_1"), "'?' matches one character");
        check(!nodeNameMatches("KERS_LEVEL_?", "KERS_LEVEL_12"), "'?' does not match two");
        check(nodeNameMatches("DRIVER:HELMET", "driver:helmet"), "names match case-insensitively");
        check(nodeNameMatches("DISPLAY_*", "DISPLAY_MONITOR_001"), "'*' matches a run");
        check(!nodeNameMatches("DRIVER:HELMET", "DRIVER:GEO_Driver_FACE"),
              "a plain name matches only itself");

        bool parsed = false;
        const Json skin = Json::parse(readText(skinPath), &parsed);
        check(parsed, "skin.json parses for the matcher");
        HiddenNodes hidden;
        hidden.addSkinDocument(skin);
        hidden.addDocument(carDriver);
        // The car-level file hides DRIVER:HELMET too, so the union is still eight.
        check(hidden.size() == 8, "the skin and the car contribute one pattern set");
        check(hidden.hides("DRIVER:GEO_Driver_FACE"), "a driver node is hidden");
        check(hidden.hides("KERS_LEVEL_3"), "a wildcard reaches the level digits");
        check(hidden.hides("GEO_BD_Disc_Hub_LR_Decal"), "a hub decal is hidden");
        check(!hidden.hides("GEO_BD_Wheel_RR"), "a node nobody hides stays");
    }

    fs::remove_all(root, ec);
    if (g_failures == 0) std::printf("overrides_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
