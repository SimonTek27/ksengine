// Qt-free test for the ext_config.ini split (src/adapters/assetto_corsa/ExtConfig.h).
//
// The 499P's skin ships the file this is written against: a driver model, two
// hidden driver nodes and two MODEL_REPLACEMENT blocks with HIDE lists. A skin's
// file belongs to the skin, so it becomes skins/<id>/skin.json; a car-root file
// belongs to the car and is folded into model.json and driver.json. The test
// builds both shapes in a temp directory and checks where each part lands, that
// the car documents never carry skin data, and that the matcher the renderer
// uses (Graphics/HiddenNodes.h) reads both.

#include "adapters/assetto_corsa/ExtConfig.h"
#include "Graphics/HiddenNodes.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// The Json type lives in ks; the merge helpers live in assetto_corsa.
using Json = ks::Json;

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what);
        ++g_failures;
    }
}

int nodeCount(const Json& block) {
    if (!block.isObject() || !block.contains("HIDDEN_NODES")) return 0;
    int count = 0;
    for (const auto& value : block["HIDDEN_NODES"]) {
        (void)value;
        ++count;
    }
    return count;
}

bool hasNode(const Json& block, const std::string& node) {
    if (!block.isObject() || !block.contains("HIDDEN_NODES")) return false;
    for (const auto& value : block["HIDDEN_NODES"])
        if (value.toString() == node) return true;
    return false;
}

bool hasSection(const Json& block, const std::string& section) {
    return block.isObject() && block.contains("SECTIONS") && block["SECTIONS"].contains(section);
}

const std::string kSkinIni =
    "[DRIVER3D_MODEL]\n"
    "NAME=rh_bell_helmet_driver_19\n"
    "POSITION=0.0,0.0,0.0\n"
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

    // `ext_config_test --print-content <carDir>` prints what the converter would
    // write for that car — the car documents' blocks and every skin's skin.json —
    // so an installed car is refreshed with the same code that produces it.
    if (argc == 3 && std::string(argv[1]) == "--print-content") {
        const fs::path carDir(argv[2]);
        Json printed = Json::object();
        Json model = Json::object();
        Json driver = Json::object();
        mergeExtConfig(carDir, model, driver);
        Json documents = Json::object();
        if (model.contains("EXT_CONFIG")) documents.insert("model", model["EXT_CONFIG"]);
        if (driver.contains("EXT_CONFIG")) documents.insert("driver", driver["EXT_CONFIG"]);
        printed.insert("documents", documents);
        Json skins = Json::object();
        for (const std::string& id : skinsWithExtConfig(carDir))
            skins.insert(id, skinDocument(carDir / "skins" / id, id));
        printed.insert("skins", skins);
        std::printf("%s\n", printed.dump(true).c_str());
        return 0;
    }

    const fs::path root = fs::temp_directory_path() / "ksim_ext_config_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeText(root / "skins" / "2024_AFCorse_LeMans_50" / "ext_config.ini", kSkinIni);

    // A skin's file becomes that skin's skin.json, split by subject.
    check(writeSkinDocuments(root) == 1, "the skin's ext_config.ini is written");
    const fs::path skinPath = root / "skins" / "2024_AFCorse_LeMans_50" / "skin.json";
    check(fs::is_regular_file(skinPath, ec), "skins/<id>/skin.json exists");
    {
        std::ifstream in(skinPath, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        bool ok = false;
        const Json skin = Json::parse(text, &ok);
        check(ok, "skin.json parses");
        check(skin.contains("SKIN") && skin["SKIN"].toString() == "2024_AFCorse_LeMans_50",
              "skin.json names its skin");
        check(skin.contains("MODEL") && skin.contains("DRIVER"), "skin.json holds both subjects");
        check(nodeCount(skin["MODEL"]) == 6, "the model's nodes are in the skin's MODEL block");
        check(hasNode(skin["MODEL"], "KERS_LEVEL_?"), "a wildcard survives into skin.json");
        check(hasSection(skin["MODEL"], "MODEL_REPLACEMENT_..."), "model sections stay model");
        check(nodeCount(skin["DRIVER"]) == 2, "the driver's nodes are in the skin's DRIVER block");
        check(hasSection(skin["DRIVER"], "DRIVER3D_MODEL"), "the driver model stays with the driver");
        check(skin["DRIVER"].contains("MODEL_NAME") &&
                  skin["DRIVER"]["MODEL_NAME"].toString() == "rh_bell_helmet_driver_19",
              "the driver model name is the one the file names");
        check(!hasSection(skin["MODEL"], "DRIVER3D_MODEL"), "no driver section in the MODEL block");
    }

    // Writing again is idempotent, and the car documents carry no skin data.
    check(writeSkinDocuments(root) == 1, "rewriting the skin document counts one file");
    Json model = Json::object();
    Json driver = Json::object();
    check(mergeExtConfig(root, model, driver) == 0, "a car without ext_config.ini reports zero");
    check(!model.contains("EXT_CONFIG") && !driver.contains("EXT_CONFIG"),
          "no skin data leaks into model.json or driver.json");

    // A block left by an older run is rebuilt away, not kept.
    {
        Json stale = Json::object();
        stale.insert("EXT_CONFIG", Json::object());
        mergeExtConfig(root, stale, driver);
        check(!stale.contains("EXT_CONFIG"), "a stale EXT_CONFIG is removed");
    }

    // The car-root file is the car's own: it lands in the documents, split by
    // subject, and no SKINS table is written anywhere.
    const fs::path carRoot = root / "carroot";
    writeText(carRoot / "ext_config.ini",
              "[DRIVER3D_HIDE_OBJECT_0]\nNAME=DRIVER:HELMET\n"
              "[MODEL_REPLACEMENT_0]\nHIDE=DISPLAY_HIGHBEAM\n");
    Json carModel = Json::object();
    Json carDriver = Json::object();
    check(mergeExtConfig(carRoot, carModel, carDriver) == 1, "the car-root file is read");
    check(hasNode(carModel["EXT_CONFIG"], "DISPLAY_HIGHBEAM"), "a model HIDE goes to model.json");
    check(hasNode(carDriver["EXT_CONFIG"], "DRIVER:HELMET"), "a driver hide goes to driver.json");
    check(!hasSection(carModel["EXT_CONFIG"], "DRIVER3D_HIDE_OBJECT_0"),
          "the driver's sections stay out of model.json");
    check(!carModel["EXT_CONFIG"].contains("SKINS") && !carDriver["EXT_CONFIG"].contains("SKINS"),
          "no SKINS table in the car documents");

    // The renderer's matcher reads both sources and hides by AC's rules.
    {
        using ks::graphics::HiddenNodes;
        using ks::graphics::nodeNameMatches;
        check(nodeNameMatches("KERS_LEVEL_?", "KERS_LEVEL_1"), "'?' matches one character");
        check(!nodeNameMatches("KERS_LEVEL_?", "KERS_LEVEL_12"), "'?' does not match two");
        check(nodeNameMatches("DRIVER:HELMET", "driver:helmet"), "names match case-insensitively");
        check(nodeNameMatches("DISPLAY_*", "DISPLAY_MONITOR_001"), "'*' matches a run");
        check(!nodeNameMatches("DRIVER:HELMET", "DRIVER:GEO_Driver_FACE"),
              "a plain name matches only itself");

        std::ifstream in(skinPath, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        bool skinParsed = false;
        const Json skin = Json::parse(text, &skinParsed);
        check(skinParsed, "skin.json parses for the matcher");
        HiddenNodes hidden;
        hidden.addSkinDocument(skin);
        hidden.addDocument(carDriver);
        // The car-root file hides DRIVER:HELMET too, so the union is still eight.
        check(hidden.size() == 8, "the skin and the car contribute one pattern set");
        check(hidden.hides("DRIVER:GEO_Driver_FACE"), "a driver node is hidden");
        check(hidden.hides("KERS_LEVEL_3"), "a wildcard reaches the level digits");
        check(hidden.hides("GEO_BD_Disc_Hub_LR_Decal"), "a hub decal is hidden");
        check(!hidden.hides("GEO_BD_Wheel_RR"), "a node nobody hides stays");
    }

    fs::remove_all(root, ec);
    if (g_failures == 0) std::printf("ext_config_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
