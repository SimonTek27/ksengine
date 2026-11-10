/**
 * Roadmap 2.7 — profili per giocatore (mapping + calibrazione + tastiera
 * in un unico file user/<player>/profiles/<name>.json).
 */
#include "KsTest.h"
#include "simulator/InputManager.h"

#include <cstdio>
#include <filesystem>
#include <string>

using ks::sim::AxisCalibration;
using ks::sim::JoystickMapping;
using ks::sim::KeyboardMapping;
using ks::sim::saveInputProfileJson;
using ks::sim::loadInputProfileJson;

int main() {
    const std::string dir = "ksengine_profiles_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    JoystickMapping m = JoystickMapping::wheelDefault();
    m.steerAxis = 1; m.throttleAxis = 2; m.brakeAxis = 3; m.clutchAxis = 4;
    m.shiftUpButton = 9; m.shiftDownButton = 8;
    m.handbrakeButton = 10;
    AxisCalibration cal[8] = {};
    cal[1].min = -1.0; cal[1].max = 1.0; cal[1].center = 0.05;
    KeyboardMapping kb;
    kb.throttle = 'R'; kb.brake = 'F'; kb.steerLeft = 'Z';

    const std::string path = dir + "/wheel_profile.json";
    KS_CHECK(saveInputProfileJson(path, m, cal, kb));
    KS_CHECK(std::filesystem::exists(path));

    // load su strutture vergini
    JoystickMapping m2;
    AxisCalibration cal2[8] = {};
    KeyboardMapping kb2;
    KS_CHECK(loadInputProfileJson(path, m2, cal2, kb2));
    KS_CHECK(m2.steerAxis == 1);
    KS_CHECK(m2.throttleAxis == 2);
    KS_CHECK(m2.brakeAxis == 3);
    KS_CHECK(m2.clutchAxis == 4);
    KS_CHECK(m2.shiftUpButton == 9);
    KS_CHECK(m2.shiftDownButton == 8);
    KS_CHECK(m2.handbrakeButton == 10);
    KS_CHECK(cal2[1].valid());
    KS_CHECK_NEAR(cal2[1].center, 0.05, 1e-9);
    KS_CHECK(kb2.throttle == 'R');
    KS_CHECK(kb2.brake == 'F');
    KS_CHECK(kb2.steerLeft == 'Z');

    // nome con caratteri illegali sanificato a livello di saveProfile (qui
    // testiamo il percorso diretto: la sanificazione è nel metodo, il file
    // va comunque scritto in un percorso già pulito).
    const std::string p2 = dir + "/a_b_c.json";
    KS_CHECK(saveInputProfileJson(p2, m, cal, kb));
    KS_CHECK(std::filesystem::exists(p2));

    // file assente
    JoystickMapping m3;
    AxisCalibration cal3[8] = {};
    KeyboardMapping kb3;
    KS_CHECK(!loadInputProfileJson(dir + "/nope.json", m3, cal3, kb3));

    std::filesystem::remove_all(dir, ec);
    return 0;
}