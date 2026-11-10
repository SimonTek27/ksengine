/**
 * Roadmap 2.7 — wizard di calibrazione guidato (min -> centro -> max) per asse.
 */
#include "KsTest.h"
#include "simulator/InputManager.h"

#include <cstdio>
#include <filesystem>
#include <string>

using ks::sim::InputManager;

int main() {
    InputManager im;
    // simula campioni grezzi: sterzo asse 0
    double axes[8] = {};
    axes[0] = 0.0;
    unsigned buttons = 0;
    im.injectAxes(axes, buttons);
    im.update();

    // inizio wizard
    im.calibrationWizardBegin(0);
    KS_CHECK(im.calibrating());
    KS_CHECK(im.calibrationStep() == InputManager::CalibrationStep::CaptureMin);

    // muovi il volante a sinistra (campione -0.9)
    axes[0] = -0.9; im.injectAxes(axes, buttons); im.update();
    im.calibrationWizardAdvance(); // -> CaptureCenter
    KS_CHECK(im.calibrationStep() == InputManager::CalibrationStep::CaptureCenter);

    // centro (0.05)
    axes[0] = 0.05; im.injectAxes(axes, buttons); im.update();
    im.calibrationWizardAdvance(); // -> CaptureMax
    KS_CHECK(im.calibrationStep() == InputManager::CalibrationStep::CaptureMax);

    // estremo destro
    axes[0] = 0.95; im.injectAxes(axes, buttons); im.update();
    im.calibrationWizardAdvance(); // -> Done
    KS_CHECK(im.calibrationStep() == InputManager::CalibrationStep::Done);
    KS_CHECK(!im.calibrating());

    const ks::sim::AxisCalibration& c = im.axisCalibration(0);
    KS_CHECK(c.valid());
    KS_CHECK(c.centerValid());
    KS_CHECK(c.min <= -0.89);
    KS_CHECK(c.max >= 0.94);
    KS_CHECK_NEAR(c.center, 0.05, 1e-9);

    // La calibrazione catturata deve anche essere quella che il poll applica:
    // al centro dichiarato lo sterzo resta fermo, agli estremi va a fondo scala
    // (prima la corsa calibrata era solo immagazzinata).
    axes[0] = 0.05; im.injectAxes(axes, buttons); im.update();
    KS_CHECK_NEAR(im.steer(), 0.0, 1e-6);
    axes[0] = 0.95; im.injectAxes(axes, buttons); im.update();
    KS_CHECK_NEAR(im.steer(), 1.0, 1e-6);
    axes[0] = -0.90; im.injectAxes(axes, buttons); im.update();
    KS_CHECK_NEAR(im.steer(), -1.0, 1e-6);

    // cancel ripristina lo stato idle
    im.calibrationWizardBegin(0);
    im.calibrationWizardCancel();
    KS_CHECK(!im.calibrating());
    KS_CHECK(im.calibrationStep() == InputManager::CalibrationStep::Idle);

    return 0;
}