// Phase 3.3 — TrackLayoutCatalog discovery + selection.
#include "TrackLayout.h"
#include "KsTest.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "ks_track_layout_test";
    fs::remove_all(root);
    fs::create_directories(root / "data");
    fs::create_directories(root / "ai");
    writeText(root / "data" / "data.json",
              R"({"name":"Root Circuit","length_m":5000.0})");

    // Sub-layout without manifest entry (scan path).
    writeText(root / "national" / "data" / "data.json",
              R"({"name":"National Layout","length_m":3200.5})");
    fs::create_directories(root / "national" / "ai");

    // Non-layout noise dir.
    fs::create_directories(root / "textures");
    writeText(root / "textures" / "dummy.txt", "x");

    ks::sim::TrackLayoutCatalog cat;
    KS_CHECK(cat.discover(root.string()) >= 2);

    bool hasDefault = false;
    bool hasNational = false;
    for (const auto& l : cat.layouts()) {
        if (l.id == "default") {
            hasDefault = true;
            KS_CHECK(l.lengthM >= 4999.f);
        }
        if (l.id == "national") {
            hasNational = true;
            KS_CHECK(l.lengthM > 3200.f && l.lengthM < 3201.f);
            KS_CHECK(l.name.find("National") != std::string::npos);
        }
    }
    KS_CHECK(hasDefault);
    KS_CHECK(hasNational);

    KS_CHECK(cat.selectById("national"));
    KS_CHECK(cat.active() && cat.active()->id == "national");
    KS_CHECK(!cat.activeFolder().empty());

    // Manifest overrides scan when present.
    {
        const fs::path root2 = fs::temp_directory_path() / "ks_track_layout_manifest";
        fs::remove_all(root2);
        fs::create_directories(root2 / "data");
        writeText(root2 / "data" / "data.json", R"({"name":"Base","length_m":1000})");
        writeText(root2 / "layouts.json",
                  R"({
  "layouts": [
    {"id":"default","name":"Base GP","folder":".","length_m":1000,"default":true},
    {"id":"gp","name":"GP","folder":"gp","length_m":4500.0}
  ]
})");
        fs::create_directories(root2 / "gp" / "ai");
        writeText(root2 / "gp" / "data" / "data.json", R"({"name":"GP","length_m":4500})");

        ks::sim::TrackLayoutCatalog m;
        KS_CHECK(m.discover(root2.string()) == 2);
        KS_CHECK(m.selectById("gp"));
        KS_CHECK(m.active() && m.active()->lengthM == 4500.f);
        KS_CHECK(m.selectById("default"));
        KS_CHECK(m.active() && m.active()->isDefault);
    }

    // Binding rules: a layout's own data.json wins over its catalog entry, a
    // layout that ships a .kn5 and an ai/ folder keeps them, and one that has
    // neither inherits the track root's line and geometry.
    {
        const fs::path root3 = fs::temp_directory_path() / "ks_track_layout_binding";
        fs::remove_all(root3);
        fs::create_directories(root3 / "data");
        fs::create_directories(root3 / "ai");
        writeText(root3 / "data" / "data.json", R"({"name":"Root","length_m":1000})");
        writeText(root3 / "track.kn5", "rootmesh");

        fs::create_directories(root3 / "national" / "ai");
        writeText(root3 / "national" / "data" / "data.json",
                  R"({"name":"National","length_m":3200.5})");
        writeText(root3 / "national" / "national.kn5", "nationalmesh");

        // Only surfaces.ini: enough to be a layout, no geometry, no own line.
        fs::create_directories(root3 / "short" / "data");
        writeText(root3 / "short" / "data" / "surfaces.ini",
                  "[SURFACE_0]\nKEY=ASPHALT\n");

        ks::sim::TrackLayoutCatalog c;
        KS_CHECK(c.discover(root3.string()) >= 3);

        KS_CHECK(c.selectById("national"));
        const ks::sim::TrackLayoutBinding nat =
            ks::sim::resolveLayoutBinding(*c.active(), root3.string());
        KS_CHECK(nat.folder.find("national") != std::string::npos);
        KS_CHECK(nat.name == "National");
        KS_CHECK(nat.lengthM > 3200.f && nat.lengthM < 3201.f);
        KS_CHECK(nat.kn5Path.find("national.kn5") != std::string::npos);
        KS_CHECK(nat.aiFolder.find("national") != std::string::npos);

        KS_CHECK(c.selectById("short"));
        const ks::sim::TrackLayoutBinding shortL =
            ks::sim::resolveLayoutBinding(*c.active(), root3.string());
        KS_CHECK(shortL.kn5Path.empty());
        KS_CHECK(shortL.aiFolder == root3.string());
        // No data.json of its own: the catalog's name for the layout stands,
        // so the track does not keep presenting itself as the old layout.
        KS_CHECK(shortL.name == "short");

        KS_CHECK(c.selectById("default"));
        const ks::sim::TrackLayoutBinding rootL =
            ks::sim::resolveLayoutBinding(*c.active(), root3.string());
        KS_CHECK(rootL.folder == root3.string());
        KS_CHECK(rootL.name == "Root");
        KS_CHECK(rootL.lengthM > 999.f && rootL.lengthM < 1001.f);
        KS_CHECK(rootL.aiFolder == root3.string());
        KS_CHECK(rootL.kn5Path.empty());

        fs::remove_all(root3);
    }

    fs::remove_all(root);
    // A failing KS_CHECK has to fail the run: returning 0 made CTest pass the
    // suite while the log carried FAIL lines.
    return KS_TEST_RESULT("track_layout_test");
}
