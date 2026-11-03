// Qt-free test for the ext_config.ini merge (src/adapters/assetto_corsa/ExtConfig.h).
//
// The 499P's skin ships the file this is written against: a driver model, two
// hidden driver nodes and two MODEL_REPLACEMENT blocks with HIDE lists. The test
// builds the same folder shape in a temp directory and checks that each subject
// reaches the document that owns it — driver to driver.json, model to model.json —
// with wildcards intact, and that merging twice does not duplicate anything.

#include "adapters/assetto_corsa/ExtConfig.h"
#include "Graphics/HiddenNodes.h"

#include <cstdio>
#include <filesystem>
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

bool hasNode(const Json& document, const std::string& node) {
    if (!document.contains("EXT_CONFIG")) return false;
    const Json& block = document["EXT_CONFIG"];
    if (!block.contains("HIDDEN_NODES")) return false;
    for (const auto& value : block["HIDDEN_NODES"])
        if (value.toString() == node) return true;
    return false;
}

int nodeCount(const Json& document) {
    if (!document.contains("EXT_CONFIG")) return 0;
    const Json& block = document["EXT_CONFIG"];
    if (!block.contains("HIDDEN_NODES")) return 0;
    int count = 0;
    for (const auto& value : block["HIDDEN_NODES"]) {
        (void)value;
        ++count;
    }
    return count;
}

bool hasSection(const Json& document, const std::string& section) {
    if (!document.contains("EXT_CONFIG")) return false;
    const Json& block = document["EXT_CONFIG"];
    return block.contains("SECTIONS") && block["SECTIONS"].contains(section);
}

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace ks::assetto_corsa;

    // `ext_config_test --print-block <carDir>` prints the EXT_CONFIG blocks the
    // converter would fold into the car's documents, so an installed car is
    // refreshed with the same code that produced it instead of a hand-written copy.
    if (argc == 3 && std::string(argv[1]) == "--print-block") {
        Json model = Json::object();
        Json driver = Json::object();
        if (mergeExtConfig(fs::path(argv[2]), model, driver) == 0) {
            std::printf("no ext_config.ini under %s\n", argv[2]);
            return 1;
        }
        Json printed = Json::object();
        if (model.contains("EXT_CONFIG")) printed.insert("model", model["EXT_CONFIG"]);
        if (driver.contains("EXT_CONFIG")) printed.insert("driver", driver["EXT_CONFIG"]);
        std::printf("%s\n", printed.dump(true).c_str());
        return 0;
    }

    const fs::path root = fs::temp_directory_path() / "ksim_ext_config_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "skins" / "2024_AFCorse_LeMans_50", ec);

    const std::string skinIni =
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
    {
        std::ofstream out(root / "skins" / "2024_AFCorse_LeMans_50" / "ext_config.ini",
                          std::ios::binary);
        out << skinIni;
    }

    Json model = Json::object();
    Json driver = Json::object();
    model.insert("GRAPHICS", Json::object());

    const int files = mergeExtConfig(root, model, driver);
    check(files == 1, "the skin file is read");

    // The driver's part belongs to the driver document.
    check(driver.contains("EXT_CONFIG"), "driver.json gains EXT_CONFIG");
    check(hasNode(driver, "DRIVER:HELMET"), "a DRIVER3D_HIDE_OBJECT name is hidden");
    check(hasNode(driver, "DRIVER:GEO_Driver_FACE"), "the second hidden driver node is there");
    check(nodeCount(driver) == 2, "the driver document holds only the driver's nodes");
    check(hasSection(driver, "DRIVER3D_MODEL"), "the driver model section stays with the driver");
    check(hasSection(driver, "DRIVER3D_HIDE_OBJECT_0"), "its hide sections go there too");
    check(driver["EXT_CONFIG"].contains("DRIVER3D_MODEL_NAME") &&
              driver["EXT_CONFIG"]["DRIVER3D_MODEL_NAME"].toString() ==
                  "rh_bell_helmet_driver_19",
          "the driver model name is the one the file names");

    // The model's part belongs to the model document.
    check(model.contains("EXT_CONFIG"), "model.json gains EXT_CONFIG");
    check(hasNode(model, "KERS_LEVEL_?"), "a wildcard HIDE entry is kept as a wildcard");
    check(hasNode(model, "GEO_BD_Disc_Hub_LF_Decal"), "a comma-separated HIDE list is split");
    check(nodeCount(model) == 6, "the model document holds only the model's nodes");
    check(hasSection(model, "MODEL_REPLACEMENT_..."), "a MODEL_REPLACEMENT section is model content");
    check(!hasSection(model, "DRIVER3D_MODEL"), "the driver model does not leak into the model");
    check(!model["EXT_CONFIG"].contains("DRIVER3D_MODEL_NAME"),
          "no driver fields in the model block");

    // Each skin keeps its own sections, split the same way.
    check(model["EXT_CONFIG"].contains("SKINS") &&
              model["EXT_CONFIG"]["SKINS"].contains("2024_AFCorse_LeMans_50"),
          "the skin keeps its model sections");
    check(driver["EXT_CONFIG"].contains("SKINS") &&
              driver["EXT_CONFIG"]["SKINS"].contains("2024_AFCorse_LeMans_50"),
          "the skin keeps its driver sections");

    // Idempotent: a second merge rebuilds the same blocks instead of appending.
    mergeExtConfig(root, model, driver);
    check(nodeCount(model) == 6 && nodeCount(driver) == 2,
          "merging twice does not duplicate nodes");

    // The matcher the renderer uses on every node name (Graphics/HiddenNodes.h):
    // AC's '?' is one character, '*' any run, names compare case-insensitively.
    {
        using ks::graphics::HiddenNodes;
        using ks::graphics::nodeNameMatches;
        check(nodeNameMatches("KERS_LEVEL_?", "KERS_LEVEL_1"), "'?' matches one character");
        check(!nodeNameMatches("KERS_LEVEL_?", "KERS_LEVEL_12"), "'?' does not match two");
        check(nodeNameMatches("DRIVER:HELMET", "driver:helmet"), "names match case-insensitively");
        check(nodeNameMatches("DISPLAY_*", "DISPLAY_MONITOR_001"), "'*' matches a run");
        check(!nodeNameMatches("DRIVER:HELMET", "DRIVER:GEO_Driver_FACE"),
              "a plain name matches only itself");

        HiddenNodes hidden;
        hidden.addDocument(model);
        hidden.addDocument(driver);
        check(hidden.size() == 8, "both documents contribute their patterns");
        check(hidden.hides("DRIVER:GEO_Driver_FACE"), "a driver node is hidden");
        check(hidden.hides("KERS_LEVEL_3"), "a wildcard pattern reaches the level digits");
        check(hidden.hides("GEO_BD_Disc_Hub_LR_Decal"), "a hub decal is hidden");
        check(!hidden.hides("GEO_BD_Wheel_RR"), "a node nobody hides stays");
    }

    // A car without the file is left alone, in both documents.
    Json untouchedModel = Json::object();
    Json untouchedDriver = Json::object();
    check(mergeExtConfig(root / "skins", untouchedModel, untouchedDriver) == 0,
          "a folder without ext_config.ini reports zero files");
    check(!untouchedModel.contains("EXT_CONFIG") && !untouchedDriver.contains("EXT_CONFIG"),
          "and neither document is touched");

    // A file with only model sections writes only the model document.
    const fs::path modelOnly = root / "modelonly";
    fs::create_directories(modelOnly, ec);
    {
        std::ofstream out(modelOnly / "ext_config.ini", std::ios::binary);
        out << "[MODEL_REPLACEMENT_0]\nHIDE = SOME_DECAL\n";
    }
    Json onlyModel = Json::object();
    Json onlyDriver = Json::object();
    mergeExtConfig(modelOnly, onlyModel, onlyDriver);
    check(onlyModel.contains("EXT_CONFIG") && !onlyDriver.contains("EXT_CONFIG"),
          "a model-only file leaves the driver document untouched");

    fs::remove_all(root, ec);
    if (g_failures == 0) std::printf("ext_config_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
