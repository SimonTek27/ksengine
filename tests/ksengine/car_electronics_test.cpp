/**
 * The car's electronics slip limits, from data.json.
 *
 * The conversion keeps electronics.ini under "electronics"; [TRACTION_CONTROL]
 * and [ABS] both carry SLIP_RATIO_LIMIT, the |slip ratio| at which the aid may
 * start cutting. The simulator now cuts drive and brake torque at the car's own
 * number instead of one value for every car, so this pins the reading: which
 * block wins, what a nonsense value does, and that a car declaring nothing keeps
 * the previous behaviour.
 */
#include "KsTest.h"

#include "engine/physics/CarElectronics.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

ks::Json parseDocument(const std::string& text) {
    bool ok = false;
    return ks::Json::parse(text, &ok);
}

} // namespace

int main() {
    using ks::physics::readElectronicsLimits;

    // --- A car that declares nothing keeps the model's own limits ----------
    {
        const auto limits = readElectronicsLimits(parseDocument("{\n  \"name\": \"Car\"\n}"));
        KS_CHECK(!limits.declaredTractionControl);
        KS_CHECK(!limits.declaredAbs);
        KS_CHECK_NEAR(limits.tcSlipRatioLimit, 0.10, 1e-6);
        KS_CHECK_NEAR(limits.absSlipRatioLimit, 0.12, 1e-6);
    }

    // --- The car's own blocks are read, as the conversion writes them ------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": {\n"
            "    \"TRACTION_CONTROL\": { \"SLIP_RATIO_LIMIT\": \"0.06\", \"PRESENT\": \"1\" },\n"
            "    \"ABS\": { \"SLIP_RATIO_LIMIT\": \"0.08\", \"PRESENT\": \"1\" }\n"
            "  }\n}\n"));
        KS_CHECK(limits.declaredTractionControl);
        KS_CHECK(limits.declaredAbs);
        KS_CHECK_NEAR(limits.tcSlipRatioLimit, 0.06, 1e-6);
        KS_CHECK_NEAR(limits.absSlipRatioLimit, 0.08, 1e-6);
    }

    // --- ABS_V2 stands in when the older [ABS] block is not there ----------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": {\n"
            "    \"ABS_V2\": { \"SLIP_RATIO_LIMIT\": \"0.12\", \"CHANNELS\": 4 },\n"
            "    \"EDL\": { \"MAX_SPIN_POWER\": \"0.55\" }\n"
            "  }\n}\n"));
        KS_CHECK(limits.declaredAbs);
        KS_CHECK_NEAR(limits.absSlipRatioLimit, 0.12, 1e-6);
        KS_CHECK(!limits.declaredTractionControl); // EDL carries no slip limit
    }

    // --- Section names are not case-sensitive across content ---------------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": {\n"
            "    \"traction_control\": { \"slip_ratio_limit\": \"0.09\" }\n"
            "  }\n}\n"));
        KS_CHECK(limits.declaredTractionControl);
        KS_CHECK_NEAR(limits.tcSlipRatioLimit, 0.09, 1e-6);
    }

    // --- A number, not a string, is read the same way ----------------------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS\": { \"SLIP_RATIO_LIMIT\": 0.11 } }\n}\n"));
        KS_CHECK(limits.declaredAbs);
        KS_CHECK_NEAR(limits.absSlipRatioLimit, 0.11, 1e-6);
    }

    // --- A limit outside any sane slip ratio keeps the default ------------
    {
        const auto high = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS\": { \"SLIP_RATIO_LIMIT\": \"2\" } }\n}\n"));
        KS_CHECK(!high.declaredAbs);
        KS_CHECK_NEAR(high.absSlipRatioLimit, 0.12, 1e-6);

        const auto zero = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"TRACTION_CONTROL\": { \"SLIP_RATIO_LIMIT\": \"0\" } }\n}\n"));
        KS_CHECK(!zero.declaredTractionControl);
        KS_CHECK_NEAR(zero.tcSlipRatioLimit, 0.10, 1e-6);

        const auto text = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS\": { \"SLIP_RATIO_LIMIT\": \"gentle\" } }\n}\n"));
        KS_CHECK(!text.declaredAbs);
    }

    // --- The folder reader finds the manifest the conversion writes -------
    {
        namespace fs = std::filesystem;
        const fs::path car = "car_electronics_test_car";
        std::error_code ec;
        fs::remove_all(car, ec);
        fs::create_directories(car / "data", ec);
        {
            std::ofstream out(car / "data" / "data.json", std::ios::binary | std::ios::trunc);
            out << "{\n  \"electronics\": {\n"
                   "    \"TRACTION_CONTROL\": { \"SLIP_RATIO_LIMIT\": \"0.07\" },\n"
                   "    \"ABS_V2\": { \"SLIP_RATIO_LIMIT\": \"0.12\" }\n"
                   "  }\n}\n";
        }
        const auto limits = ks::physics::readElectronicsLimitsForCar(car.string());
        KS_CHECK(limits.declaredTractionControl && limits.declaredAbs);
        KS_CHECK_NEAR(limits.tcSlipRatioLimit, 0.07, 1e-6);
        KS_CHECK_NEAR(limits.absSlipRatioLimit, 0.12, 1e-6);

        // No manifest at all: nothing declared, defaults untouched.
        fs::remove(car / "data" / "data.json", ec);
        const auto missing = ks::physics::readElectronicsLimitsForCar(car.string());
        KS_CHECK(!missing.declaredAbs && !missing.declaredTractionControl);
        KS_CHECK_NEAR(missing.tcSlipRatioLimit, 0.10, 1e-6);
        fs::remove_all(car, ec);
    }

    // --- Blocks that are not objects are ignored, never dereferenced ------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS\": 1, \"TRACTION_CONTROL\": [] }\n}\n"));
        KS_CHECK(!limits.declaredAbs);
        KS_CHECK(!limits.declaredTractionControl);
    }

    return ks::test::finish("car_electronics_test");
}
