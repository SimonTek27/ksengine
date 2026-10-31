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

    fs::remove_all(root);
    return 0;
}
