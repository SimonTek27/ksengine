/**
 * The content's [BRAKEDISC_FX] sections, read from the car's model document.
 *
 * The conversion folds them into model.json under "sections"; the two keys that
 * land on this engine's disc glow are ACTIVE (off for a car whose disc shader
 * the content replaced) and GLOW_MULT (how bright). Everything else in the
 * section describes a shader this engine does not have and must not be turned
 * into numbers here, so the test also pins that unknown keys are ignored.
 */
#include "KsTest.h"

#include "engine/Graphics/BrakeDiscFx.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

int main() {
    const fs::path car = "brake_disc_fx_test_car";
    std::error_code ec;
    fs::remove_all(car, ec);
    const fs::path model = car / "data" / "model.json";

    // --- A car that declares nothing keeps the glow as computed -------------
    writeText(model, "{\n  \"name\": \"Test Car\"\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(!fx.declared);
        KS_CHECK(fx.active);
        KS_CHECK_NEAR(fx.glowMult, 1.0, 1e-9);
    }

    // --- The section, next to sections this engine does not interpret -------
    writeText(model,
              "{\n  \"sections\": {\n"
              "    \"VAO\": { \"OPACITY\": \"0.9\" },\n"
              "    \"BRAKEDISC_FX\": { \"ACTIVE\": \"1\", \"CERAMIC\": \"0\", \"GLOW_MULT\": \"2\" }\n"
              "  }\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(fx.declared);
        KS_CHECK(fx.active);
        KS_CHECK_NEAR(fx.glowMult, 2.0, 1e-9);
    }

    // --- ACTIVE = 0: the content replaced the disc shader, nothing glows ----
    writeText(model,
              "{\n  \"sections\": {\n"
              "    \"BRAKEDISC_FX\": { \"ACTIVE\": \"0\", \"GLOW_MULT\": \"2\" }\n"
              "  }\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(fx.declared);
        KS_CHECK(!fx.active);
    }

    // --- One axis declaring is enough, and it is read ----------------------
    writeText(model,
              "{\n  \"sections\": {\n"
              "    \"BRAKEDISC_FX_FRONT\": { \"GLOW_MULT\": \"0.5\" }\n"
              "  }\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(fx.declared);
        KS_CHECK(fx.active);
        KS_CHECK_NEAR(fx.glowMult, 0.5, 1e-9);
    }

    // --- Both axes: the brighter one is what the discs can reach -----------
    writeText(model,
              "{\n  \"sections\": {\n"
              "    \"BRAKEDISC_FX_FRONT\": { \"GLOW_MULT\": \"0.5\" },\n"
              "    \"BRAKEDISC_FX_REAR\": { \"GLOW_MULT\": \"1.5\" }\n"
              "  }\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK_NEAR(fx.glowMult, 1.5, 1e-9);
    }

    // --- An axis that turns itself off leaves the other one glowing --------
    writeText(model,
              "{\n  \"sections\": {\n"
              "    \"BRAKEDISC_FX_FRONT\": { \"ACTIVE\": \"0\" },\n"
              "    \"BRAKEDISC_FX_REAR\": { \"GLOW_MULT\": \"1\" }\n"
              "  }\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(fx.active);
    }

    // --- A value that is not a number keeps the default, never 0 -----------
    writeText(model,
              "{\n  \"sections\": {\n"
              "    \"BRAKEDISC_FX\": { \"GLOW_MULT\": \"bright\" }\n"
              "  }\n}\n");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(fx.declared);
        KS_CHECK_NEAR(fx.glowMult, 1.0, 1e-9);
    }

    // --- A malformed document never fails a car load -----------------------
    writeText(model, "{ not json");
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(!fx.declared);
        KS_CHECK(fx.active);
    }

    // --- A car folder with no document at all ------------------------------
    fs::remove(model, ec);
    {
        const ks::graphics::BrakeDiscFx fx = ks::graphics::readBrakeDiscFx(car.string());
        KS_CHECK(!fx.declared);
    }

    fs::remove_all(car, ec);
    return ks::test::finish("brake_disc_fx_test");
}
