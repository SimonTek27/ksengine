#include "KsTest.h"
#include "engine/Config/ControllerSettings.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main() {
    // The default document is the Controller.ini shape, with no values of its
    // own: the sections are objects and the only content is the _meta note.
    {
        ks::engine::config::ControllerSettings s;
        KS_CHECK(s.root.isObject());
        const ks::engine::json::Value* meta = s.root.find("_meta");
        KS_CHECK(meta && meta->isObject());
        KS_CHECK(meta->stringAt("source") == "builtin");
        const ks::engine::json::Value* steer = s.root.find("STEER");
        KS_CHECK(steer && steer->isObject() && steer->size() == 0);
    }

    // Round trip through a file.
    const fs::path dir = fs::temp_directory_path() / "ks_controller_settings_test";
    fs::create_directories(dir);
    const std::string path = (dir / "controller.json").string();

    ks::engine::config::ControllerSettings a;
    KS_CHECK(a.save(path));
    ks::engine::config::ControllerSettings b;
    KS_CHECK(b.load(path));
    // Re-dump is stable.
    KS_CHECK(ks::engine::json::dump(b.root) == ks::engine::json::dump(a.root));

    // Missing file leaves defaults intact.
    ks::engine::config::ControllerSettings c;
    const std::string orig = ks::engine::json::dump(c.root);
    KS_CHECK(!c.load((dir / "nope.json").string()));
    KS_CHECK(ks::engine::json::dump(c.root) == orig);

    // Malformed file leaves defaults intact.
    {
        std::FILE* f = std::fopen((dir / "bad.json").string().c_str(), "w");
        if (f) { std::fputs("{ oops", f); std::fclose(f); }
    }
    ks::engine::config::ControllerSettings d;
    const std::string orig2 = ks::engine::json::dump(d.root);
    KS_CHECK(!d.load((dir / "bad.json").string()));
    KS_CHECK(ks::engine::json::dump(d.root) == orig2);

    // Alias names compile.
    ks::sim::ControllerSettings s1;
    ks::device::ControllerSettings s2;
    ks::engine::config::ControlsSettings s3;
    (void)s1; (void)s2; (void)s3;

    fs::remove_all(dir);
    return KS_TEST_RESULT("controller_settings_test");
}
