/**
 * The content's overrides, folded into the documents that own each part.
 *
 * Covers what the reader has to get right for real content: a car-wide
 * ext_config.ini that some mods keep in extension/ instead of the car root, the
 * repeated section headers a paint override is written with (one block per skin
 * set), the inline comments whose bytes are single-byte code page and cannot go
 * into a JSON document, the driver's sections going to the driver document, and
 * a car whose source files are gone keeping what its documents already carry.
 */
#include "KsTest.h"

#include "adapters/assetto_corsa/Overrides.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using ks::Json;
using namespace ks::assetto_corsa;

namespace {

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

/** A byte that is not valid UTF-8 cannot go into a document. */
bool validUtf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        if (lead < 0x80) length = 1;
        else if ((lead & 0xE0) == 0xC0) length = 2;
        else if ((lead & 0xF0) == 0xE0) length = 3;
        else if ((lead & 0xF8) == 0xF0) length = 4;
        if (length == 0 || i + length > text.size()) return false;
        for (std::size_t k = 1; k < length; ++k)
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        i += length;
    }
    return true;
}

} // namespace

int main() {
    const fs::path car = "overrides_test_car";
    std::error_code ec;
    fs::remove_all(car, ec);

    // --- The root file and the one kept in extension/ are one set ----------
    // Content puts the car-wide overrides in either place; they describe the
    // same car, and where both repeat an entry the root file is the one kept.
    writeText(car / "ext_config.ini",
              "[ABOUT]\n"
              "AUTHOR=Somebody\n"
              "[VAO]\n"
              "OPACITY=0.5\n");
    writeText(car / "extension" / "ext_config.ini",
              ";VRC config\n"
              "[VAO]\n"
              "OPACITY=0.9\n"
              "MULTIPLIER=1.0\n"
              "[INCLUDE: common/materials_carpaint.ini]\n"
              "CarPaintVersionAware=2\n");
    {
        Json model = Json::object();
        Json driver = Json::object();
        KS_CHECK(mergeOverrides(car, model, driver) == 2);
        KS_CHECK(model.contains("sections"));
        KS_CHECK(model["sections"]["ABOUT"]["AUTHOR"].toString() == "Somebody");
        KS_CHECK(model["sections"]["VAO"]["OPACITY"].toString() == "0.5");
        KS_CHECK(model["sections"]["VAO"]["MULTIPLIER"].toString() == "1.0");
        KS_CHECK(model["sections"]["INCLUDE: common/materials_carpaint.ini"]
                     ["CarPaintVersionAware"].toString() == "2");
        KS_CHECK(!driver.contains("sections")); // nothing belongs to the driver
    }

    // --- Repeated headers are numbered, each block keeping its values ------
    fs::remove(car / "extension" / "ext_config.ini", ec);
    writeText(car / "ext_config.ini",
              "[Material_CarPaint_Metallic]\n"
              "Skins=01, 25\n"
              "ClearCoatThickness=0.4\n"
              "[Material_CarPaint_Metallic]\n"
              "Skins=03, 33\n"
              "ClearCoatThickness=0.05\n"
              "[Material_CarPaint_Metallic]\n"
              "Skins=17\n"
              "ClearCoatThickness=0.1\n");
    {
        Json model = Json::object();
        Json driver = Json::object();
        KS_CHECK(mergeOverrides(car, model, driver) == 1);
        KS_CHECK(model["sections"].contains("Material_CarPaint_Metallic"));
        KS_CHECK(model["sections"].contains("Material_CarPaint_Metallic_2"));
        KS_CHECK(model["sections"].contains("Material_CarPaint_Metallic_3"));
        KS_CHECK(model["sections"]["Material_CarPaint_Metallic"]["Skins"].toString() == "01, 25");
        KS_CHECK(model["sections"]["Material_CarPaint_Metallic_2"]["ClearCoatThickness"]
                     .toString() == "0.05");
        KS_CHECK(model["sections"]["Material_CarPaint_Metallic_3"]["Skins"].toString() == "17");
    }

    // --- A comment is not the value, and its bytes stay out of the JSON ----
    std::string codePage = "[TYRES_FX]\nFADE=0.1  ; how much, in 0";
    codePage.push_back(static_cast<char>(0x85)); // single-byte code page
    codePage += "1 range\nNAME=Caf";
    codePage.push_back(static_cast<char>(0x85));
    codePage += "\n";
    writeText(car / "ext_config.ini", codePage);
    {
        Json model = Json::object();
        Json driver = Json::object();
        KS_CHECK(mergeOverrides(car, model, driver) == 1);
        KS_CHECK(model["sections"]["TYRES_FX"]["FADE"].toString() == "0.1");
        KS_CHECK(model["sections"]["TYRES_FX"]["NAME"].toString() == "Caf?");
        KS_CHECK(validUtf8(model.dump(true)));
    }

    // --- The driver's sections go to the driver document -------------------
    writeText(car / "ext_config.ini",
              "[DRIVER3D_HIDE_OBJECT_0]\n"
              "NAME=rh_helmet\n"
              "[DRIVER3D_MODEL]\n"
              "NAME=driver_x\n"
              "POSITION=0,0.1,0\n");
    {
        Json model = Json::object();
        Json driver = Json::object();
        KS_CHECK(mergeOverrides(car, model, driver) == 1);
        KS_CHECK(!model.contains("sections"));
        KS_CHECK(driver["hidden_nodes"].size() == 1);
        KS_CHECK(driver["hidden_nodes"].at(0).toString() == "rh_helmet");
        KS_CHECK(driver["sections"].contains("DRIVER3D_MODEL"));
        KS_CHECK(driver["driver_model"]["name"].toString() == "driver_x");
        KS_CHECK(driver["driver_model"]["position"].at(1).toDouble() == 0.1);
    }

    // --- No source at all: the documents keep what they already hold -------
    fs::remove(car / "ext_config.ini", ec);
    {
        Json model = Json::object();
        model.insert("sections", Json::object());
        model["sections"].insert("KEPT", Json::object());
        Json driver = Json::object();
        KS_CHECK(mergeOverrides(car, model, driver) == 0);
        KS_CHECK(model["sections"].contains("KEPT"));
        KS_CHECK(!driver.contains("sections"));
    }

    fs::remove_all(car, ec);
    return ks::test::finish("overrides_test");
}
