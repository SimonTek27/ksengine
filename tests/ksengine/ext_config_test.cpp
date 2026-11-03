// Qt-free test for the ext_config.ini merge (src/adapters/assetto_corsa/ExtConfig.h).
//
// The 499P's skin ships the file this is written against: a driver model, two
// hidden driver nodes and two MODEL_REPLACEMENT blocks with HIDE lists. The test
// builds the same folder shape in a temp directory and checks that everything
// lands in the model document, wildcards and all, and that merging twice does
// not duplicate anything.

#include "adapters/assetto_corsa/ExtConfig.h"

#include <cstdio>
#include <filesystem>
#include <string>
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

bool containsNode(const Json& model, const std::string& node) {
    if (!model.contains("EXT_CONFIG")) return false;
    const Json& block = model["EXT_CONFIG"];
    if (!block.contains("HIDDEN_NODES")) return false;
    for (const auto& value : block["HIDDEN_NODES"]) {
        if (value.toString() == node) return true;
    }
    return false;
}

int hiddenCount(const Json& model) {
    if (!model.contains("EXT_CONFIG")) return 0;
    const Json& block = model["EXT_CONFIG"];
    if (!block.contains("HIDDEN_NODES")) return 0;
    int count = 0;
    for (const auto& value : block["HIDDEN_NODES"]) {
        (void)value;
        ++count;
    }
    return count;
}

} // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace ks::assetto_corsa;

    // `ext_config_test --print-block <carDir>` prints the EXT_CONFIG block the
    // converter would fold into model.json, so an installed car is refreshed with
    // the same code that produced it instead of a hand-written copy.
    if (argc == 3 && std::string(argv[1]) == "--print-block") {
        Json model = Json::object();
        if (mergeExtConfigIntoModel(fs::path(argv[2]), model) == 0 ||
            !model.contains("EXT_CONFIG")) {
            std::printf("no ext_config.ini under %s\n", argv[2]);
            return 1;
        }
        std::printf("%s\n", model["EXT_CONFIG"].dump(true).c_str());
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
    model.insert("GRAPHICS", Json::object());

    const int files = mergeExtConfigIntoModel(root, model);
    check(files == 1, "the skin file is read");
    check(model.contains("EXT_CONFIG"), "the model document gains EXT_CONFIG");
    check(containsNode(model, "DRIVER:HELMET"), "a DRIVER3D_HIDE_OBJECT name is hidden");
    check(containsNode(model, "DRIVER:GEO_Driver_FACE"), "the second hidden node is there");
    check(containsNode(model, "KERS_LEVEL_?"), "a wildcard HIDE entry is kept as a wildcard");
    check(containsNode(model, "GEO_BD_Disc_Hub_LF_Decal"), "a comma-separated HIDE list is split");
    check(hiddenCount(model) == 8, "every distinct hidden node is kept, once");

    const Json& block = model["EXT_CONFIG"];
    check(block.contains("DRIVER3D_MODEL_NAME"), "the driver model name is folded in");
    if (block.contains("DRIVER3D_MODEL_NAME"))
        check(block["DRIVER3D_MODEL_NAME"].toString() == "rh_bell_helmet_driver_19",
              "the driver model name is the one the file names");
    check(block.contains("SKINS") && block["SKINS"].contains("2024_AFCorse_LeMans_50"),
          "the skin keeps its own sections");

    // Idempotent: a second merge rebuilds the same block instead of appending.
    mergeExtConfigIntoModel(root, model);
    check(hiddenCount(model) == 8, "merging twice does not duplicate nodes");

    // A car without the file is left alone.
    Json untouched = Json::object();
    check(mergeExtConfigIntoModel(root / "skins", untouched) == 0,
          "a folder without ext_config.ini reports zero files");
    check(!untouched.contains("EXT_CONFIG"), "and the document is untouched");

    fs::remove_all(root, ec);
    if (g_failures == 0) std::printf("ext_config_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
