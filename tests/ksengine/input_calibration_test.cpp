/**
 * Roadmap 2.7 — calibrazione guidata degli assi (min/max/centro) e
 * persistenza joystick.json (mapping + calibrazione) per giocatore.
 */
#include "KsTest.h"
#include "simulator/InputManager.h"

#include <cstdio>
#include <filesystem>
#include <string>

using ks::sim::AxisCalibration;
using ks::sim::InputManager;
using ks::sim::JoystickMapping;
using ks::sim::loadJoystickMappingJson;
using ks::sim::saveJoystickMappingJson;

int main() {
    // --- AxisCalibration: validità della corsa osservata ---------------
    AxisCalibration c;
    KS_CHECK(!c.valid()); // min == max default: nessuna corsa
    c.min = 0.0; c.max = 1.0; c.center = 0.5;
    KS_CHECK(c.valid());
    KS_CHECK(c.centerValid());

    AxisCalibration empty;
    empty.min = 0.5; empty.max = 0.5;
    KS_CHECK(!empty.valid()); // estremi coincidenti -> ignorata

    AxisCalibration off;
    off.min = 0.0; off.max = 1.0; off.center = -1.0;
    KS_CHECK(off.valid());
    KS_CHECK(!off.centerValid()); // non calibrato: pass-through

    // --- round trip mapping + calibrazione ----------------------------
    const std::string path = "ksengine_input_calibration_test.json";
    {
        JoystickMapping m;
        m.steerAxis = 2;
        m.throttleAxis = 3;
        m.brakeAxis = 4;
        m.clutchAxis = 5;
        m.shiftUpButton = 7;
        m.shiftDownButton = 6;
        m.clutchButton = 1;
        m.handbrakeButton = 8;
        AxisCalibration cal[8] = {};
        cal[0].min = -0.95; cal[0].max = 0.97; cal[0].center = 0.02; // sterzo
        cal[3].min = 0.05; cal[3].max = 0.98; cal[3].center = 0.05;   // pedale
        KS_CHECK(saveJoystickMappingJson(path, m, cal, 8));
    }
    {
        JoystickMapping m;
        AxisCalibration cal[8] = {};
        KS_CHECK(loadJoystickMappingJson(path, m, cal, 8) != 0);
        KS_CHECK(m.steerAxis == 2 && m.throttleAxis == 3 && m.brakeAxis == 4);
        KS_CHECK(m.clutchAxis == 5 && m.shiftUpButton == 7 && m.shiftDownButton == 6);
        KS_CHECK(m.clutchButton == 1 && m.handbrakeButton == 8);
        KS_CHECK(cal[0].valid() && cal[0].centerValid());
        KS_CHECK_NEAR(cal[0].min, -0.95, 1e-9);
        KS_CHECK_NEAR(cal[0].max, 0.97, 1e-9);
        KS_CHECK_NEAR(cal[0].center, 0.02, 1e-9);
        KS_CHECK(cal[3].valid());
        KS_CHECK_NEAR(cal[3].min, 0.05, 1e-9);
        KS_CHECK_NEAR(cal[3].max, 0.98, 1e-9);
        // asse 1 mai calibrato: resta invalido
        KS_CHECK(!cal[1].valid());
    }
    std::remove(path.c_str());

    // --- file assente: niente tocco, nessun crash -------------------
    {
        JoystickMapping m;
        m.steerAxis = 9;
        AxisCalibration cal[8] = {};
        const int r = loadJoystickMappingJson("ksengine_no_such_joystick.json", m, cal, 8);
        KS_CHECK(r == 0);
        KS_CHECK(m.steerAxis == 9); // untouched
    }
    return 0;
}