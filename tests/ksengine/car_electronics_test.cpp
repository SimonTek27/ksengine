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
#include "engine/physics/TrackSurface.h"
#include "engine/physics/VehicleSimulator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

    // --- PRESENT and ACTIVE, read as AC reads them ------------------------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": {\n"
            "    \"TRACTION_CONTROL\": { \"PRESENT\": \"0\", \"ACTIVE\": \"0\" },\n"
            "    \"ABS\": { \"PRESENT\": \"1\", \"ACTIVE\": \"0\" }\n"
            "  }\n}\n"));
        KS_CHECK(limits.tcPresent == 0);
        KS_CHECK(limits.tcActive == 0);
        KS_CHECK(limits.absPresent == 1);
        KS_CHECK(limits.absActive == 0);
    }

    // --- A block without the keys says nothing, it does not say "absent" ---
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"TRACTION_CONTROL\": { \"SLIP_RATIO_LIMIT\": \"0.08\" } }\n}\n"));
        KS_CHECK(limits.tcPresent == -1);
        KS_CHECK(limits.tcActive == -1);
        KS_CHECK(limits.absPresent == -1);
    }

    // --- ABS_V2 stands in for presence when [ABS] is not there ------------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS_V2\": { \"PRESENT\": 0, \"SLIP_RATIO_LIMIT\": \"0.12\" } }\n}\n"));
        KS_CHECK(limits.absPresent == 0);
        KS_CHECK(limits.declaredAbs);
    }

    // --- The EDL block: the brake-based aid and the spin it allows --------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": {\n"
            "    \"EDL\": { \"PRESENT\": \"1\", \"ACTIVE\": \"1\", "
            "\"MAX_SPIN_POWER\": \"0.45\", \"MAX_SPIN_COAST\": \"0.30\", "
            "\"BRAKE_TORQUE_POWER\": \"200\", \"BRAKE_TORQUE_COAST\": \"380\", "
            "\"DEAD_ZONE_POWER\": \"0.05\", \"DEAD_ZONE_COAST\": \"0.08\" }\n"
            "  }\n}\n"));
        KS_CHECK(limits.declaredEdl);
        KS_CHECK(limits.edlPresent == 1);
        KS_CHECK_NEAR(limits.edlMaxSpinPower, 0.45, 1e-6);
        KS_CHECK_NEAR(limits.edlMaxSpinCoast, 0.30, 1e-6);
        KS_CHECK_NEAR(limits.edlBrakeTorquePower, 200.0, 1e-3);
        KS_CHECK_NEAR(limits.edlBrakeTorqueCoast, 380.0, 1e-3);
        KS_CHECK_NEAR(limits.edlDeadZonePower, 0.05, 1e-6);
        KS_CHECK_NEAR(limits.edlDeadZoneCoast, 0.08, 1e-6);
    }

    // --- Nonsense EDL values keep AC's reference numbers -------------------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": {\n"
            "    \"EDL\": { \"MAX_SPIN_POWER\": \"3\", \"BRAKE_TORQUE_POWER\": \"-5\", "
            "\"DEAD_ZONE_COAST\": \"0.9\" }\n  }\n}\n"));
        KS_CHECK_NEAR(limits.edlMaxSpinPower, 0.55, 1e-6);
        KS_CHECK_NEAR(limits.edlBrakeTorquePower, 140.0, 1e-3);
        KS_CHECK_NEAR(limits.edlDeadZoneCoast, 0.12, 1e-6);
    }

    // --- No EDL block: the aid is not declared at all ---------------------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS\": { \"SLIP_RATIO_LIMIT\": \"0.1\" } }\n}\n"));
        KS_CHECK(!limits.declaredEdl);
        KS_CHECK(limits.edlPresent == -1);
    }

    // --- An EDL block with numbers but no PRESENT says nothing about hardware
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"EDL\": { \"MAX_SPIN_POWER\": \"0.45\" } }\n}\n"));
        KS_CHECK(limits.declaredEdl);
        KS_CHECK(limits.edlPresent == -1);
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

    // --- The simulator applies what the car declares ----------------------
    // Reading the file is only half of it: the values have to reach the model that
    // cuts torque, on the path a session actually takes.
    {
        namespace fs = std::filesystem;
        const fs::path car = "car_electronics_test_session_car";
        std::error_code ec;
        fs::remove_all(car, ec);
        fs::create_directories(car / "data", ec);
        {
            std::ofstream out(car / "data" / "data.json", std::ios::binary | std::ios::trunc);
            out << "{\n  \"electronics\": {\n"
                   "    \"TRACTION_CONTROL\": { \"SLIP_RATIO_LIMIT\": \"0.07\", "
                   "\"PRESENT\": \"0\" },\n"
                   "    \"ABS_V2\": { \"SLIP_RATIO_LIMIT\": \"0.14\", \"PRESENT\": \"1\" },\n"
                   "    \"EDL\": { \"PRESENT\": \"0\", \"MAX_SPIN_POWER\": \"0.40\" }\n"
                   "  }\n}\n";
        }

        ks::physics::VehicleSimulator sim;
        // The setup asks both aids up front, as a driver who likes them would.
        ks::physics::VehicleSimulator::SetupParams setup;
        setup.tcLevel = 8;
        setup.absLevel = 6;
        sim.applySetup(setup);
        // Nothing loaded yet: the setup alone decides, as it always did.
        KS_CHECK(sim.tractionControlPresent() == -1);
        KS_CHECK(sim.absPresent() == -1);
        KS_CHECK(sim.tcEnabled());
        KS_CHECK(sim.absEnabled());

        sim.loadElectronicsForCar(car.string());
        KS_CHECK_NEAR(sim.tractionControlSlipLimit(), 0.07, 1e-6);
        KS_CHECK_NEAR(sim.absSlipLimit(), 0.14, 1e-6);
        KS_CHECK(sim.tractionControlPresent() == 0);
        KS_CHECK(sim.absPresent() == 1);
        // The car has no traction control: no level can switch one on.
        KS_CHECK(!sim.tcEnabled());
        KS_CHECK(sim.absEnabled());

        // A car with no manifest keeps answering to the level alone.
        fs::remove(car / "data" / "data.json", ec);
        ks::physics::VehicleSimulator plain;
        plain.applySetup(setup);
        plain.loadElectronicsForCar(car.string());
        KS_CHECK(plain.tractionControlPresent() == -1);
        KS_CHECK(plain.absPresent() == -1);
        KS_CHECK(plain.tcEnabled());
        KS_CHECK(plain.absEnabled());
        KS_CHECK_NEAR(plain.tractionControlSlipLimit(), 0.10, 1e-6);
        KS_CHECK_NEAR(plain.absSlipLimit(), 0.12, 1e-6);

        // Traction control fitted, EDL not: the document's EDL numbers are not a
        // system, so the brake-based aid stays off even with TC up.
        {
            std::ofstream out(car / "data" / "data.json", std::ios::binary | std::ios::trunc);
            out << "{\n  \"electronics\": {\n"
                   "    \"TRACTION_CONTROL\": { \"PRESENT\": \"1\" },\n"
                   "    \"EDL\": { \"PRESENT\": \"0\", \"MAX_SPIN_POWER\": \"0.45\" }\n"
                   "  }\n}\n";
        }
        ks::physics::VehicleSimulator noEdl;
        noEdl.applySetup(setup);
        noEdl.loadElectronicsForCar(car.string());
        KS_CHECK(noEdl.tcEnabled());
        KS_CHECK(noEdl.edlPresent() == 0);
        KS_CHECK(!noEdl.edlFitted());

        // Without PRESENT the numbers are not a "no": the aid applies as before.
        {
            std::ofstream out(car / "data" / "data.json", std::ios::binary | std::ios::trunc);
            out << "{\n  \"electronics\": { \"EDL\": { \"MAX_SPIN_POWER\": \"0.45\" } }\n}\n";
        }
        ks::physics::VehicleSimulator unknownEdl;
        unknownEdl.loadElectronicsForCar(car.string());
        KS_CHECK(unknownEdl.edlPresent() == -1);
        KS_CHECK(unknownEdl.edlFitted());
        fs::remove_all(car, ec);
    }

    // --- The gate changes what the car does, not just what it reports -----
    // Two identical runs from a standstill at full throttle with a high TC level:
    // the car that declares the system cuts drive at its slip limit, the car that
    // declares none does not. Same setup, same steps, one difference.
    {
        struct Pull {
            float speed = 0.f;      // m/s after the run
            float peakSlip = 0.f;
            float peakOmega = 0.f;
            float cutSteps = 0.f;   // steps with the drive cut on the rear axle
        };
        auto pull = [](int tcPresent, int tcLevel) {
            // The surface keeps the rubber a run deposits, and the grip it adds is
            // enough to move the next run's speed. Two runs are only comparable
            // from the same clean track.
            ks::physics::TrackSurface::instance().configure();
            ks::physics::VehicleSimulator sim;
            sim.setFuelL(50.f);
            sim.setElectronicsPresence(tcPresent, -1);
            ks::physics::VehicleSimulator::SetupParams setup;
            setup.tcLevel = tcLevel;
            sim.applySetup(setup);
            sim.startSimulation();
            sim.setThrottle(1.0);
            Pull out;
            for (int i = 0; i < 300; ++i) {
                sim.updatePhysics(0.01);
                const float slip = std::max(std::fabs(sim.slipRatio(2)),
                                            std::fabs(sim.slipRatio(3)));
                out.peakSlip = std::max(out.peakSlip, slip);
                out.peakOmega = std::max(out.peakOmega,
                                         std::max(std::fabs(sim.wheelOmega(2)),
                                                  std::fabs(sim.wheelOmega(3))));
                if (slip > 0.2f) out.cutSteps += 1.f;
            }
            out.speed = sim.getState().speed;
            std::fprintf(stderr,
                         "[tc-behaviour] present=%d level=%d speed=%0.3f "
                         "peakSlip=%0.3f peakOmega=%0.2f stepsOver02=%0.0f\n",
                         tcPresent, tcLevel, out.speed, out.peakSlip, out.peakOmega,
                         out.cutSteps);
            return out;
        };
        const Pull fitted = pull(1, 10);
        const Pull absent = pull(0, 10);
        const Pull off = pull(-1, 0);
        // The invariant the gate exists for: a car that declares no traction
        // control during the whole run behaves exactly like one driven with the
        // level at zero — not like one with a level selected and ignored. Same
        // start, same steps, so the comparison is bit for bit.
        KS_CHECK_NEAR(absent.speed, off.speed, 1e-6);
        // And the aid is not a no-op: with the system fitted the run differs.
        KS_CHECK(std::fabs(fitted.speed - off.speed) > 1e-3);
    }

    // --- Blocks that are not objects are ignored, never dereferenced ------
    {
        const auto limits = readElectronicsLimits(parseDocument(
            "{\n  \"electronics\": { \"ABS\": 1, \"TRACTION_CONTROL\": [] }\n}\n"));
        KS_CHECK(!limits.declaredAbs);
        KS_CHECK(!limits.declaredTractionControl);
    }

    // --- The step the caller passes must be a stable one -------------------
    // A wheel and its tyre are stiff: at 10 ms the rear axle changed the sign of
    // its slip on almost every step and the car stopped accelerating (1.4 m/s
    // after three seconds). updatePhysics splits the caller's step, so a coarse
    // caller now gets the fine-step answer instead of a different physics.
    {
        struct Launch {
            float speed = 0.f;
            int flips = 0;
        };
        auto launch = [](float dt) {
            ks::physics::TrackSurface::instance().configure(); // clean rubber
            ks::physics::VehicleSimulator sim;
            sim.setFuelL(50.f);
            sim.startSimulation();
            sim.setThrottle(1.0);
            const int steps = static_cast<int>(3.0f / dt);
            float prev = 0.f;
            Launch out;
            for (int i = 0; i < steps; ++i) {
                sim.updatePhysics(dt);
                const float k = sim.slipRatio(2);
                if (sim.getState().speed > 0.5f) {
                    if (i > 0 && ((k > 0.02f && prev < -0.02f) ||
                                  (k < -0.02f && prev > 0.02f)))
                        ++out.flips;
                }
                prev = k;
            }
            out.speed = sim.getState().speed;
            return out;
        };
        const Launch coarse = launch(0.01f);
        const Launch fine = launch(0.002f);
        std::fprintf(stderr,
                     "[step] coarse(10ms) speed=%0.3f flips=%d | fine(2ms) speed=%0.3f "
                     "flips=%d\n",
                     coarse.speed, coarse.flips, fine.speed, fine.flips);
        // No sign chatter at either step, and the same answer from both.
        KS_CHECK(coarse.flips == 0);
        KS_CHECK(fine.flips == 0);
        KS_CHECK_NEAR(coarse.speed, fine.speed, 0.05 * fine.speed);
    }

    return ks::test::finish("car_electronics_test");
}
