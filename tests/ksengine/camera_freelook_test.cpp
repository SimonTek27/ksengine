/**
 * controls.json -> camera: freelook (mouse e tastiera) e range HMD.
 *
 * Prima di questo test i parametri freelook e hmd venivano solo letti e
 * riscritti: nessuno li consumava. Qui si verifica che il mouse produca
 * l'offset dichiarato (radianti per pixel), che la tastiera lo riporti al
 * centro al rilascio, e che una testa tracciata resti dentro i range HMD con
 * la curva dell'esponente.
 */
#include "KsTest.h"
#include "simulator/CameraController.h"
#include "simulator/ControlsConfig.h"

#include <cmath>

using ks::sim::CameraController;
using ks::sim::ControlsConfig;

int main() {
    CameraController cam;
    cam.setMode(CameraController::Mode::Cockpit);
    // Un corpo fermo e dritto: qui interessa solo dove punta lo sguardo.
    using Mat4 = decltype(cam.viewMatrix());
    const Mat4 body{}; // mat4 e' identita' di default: car ferma e dritta

    ControlsConfig controls;
    cam.setControls(controls);

    // --- mouse: radianti per pixel, tenuto finché non si recentra ----------
    cam.addMouseLook(100.0f, 0.0f);
    KS_CHECK_NEAR(cam.lookYaw(), 100.0f * 0.004f, 1e-6f);
    cam.addMouseLook(0.0f, -100.0f); // mouse in alto: sguardo in alto
    KS_CHECK(cam.lookPitch() > 0.0f);
    cam.addMouseLook(0.0f, -100000.0f); // il collo si ferma, non si avvita
    KS_CHECK(cam.lookPitch() <= 1.3963f + 1e-6f);
    cam.recentreLook();
    KS_CHECK_NEAR(cam.lookYaw(), 0.0f, 1e-6f);
    KS_CHECK_NEAR(cam.lookPitch(), 0.0f, 1e-6f);

    // --- tastiera: accelera tenendo, torna al centro rilasciando -----------
    for (int i = 0; i < 30; ++i) {
        cam.setLookKeys(false, false, false, true, false); // guarda a destra
        cam.update(1.0f / 60.0f, body, 0.0f);
    }
    KS_CHECK(cam.lookYaw() > 0.0f);
    for (int i = 0; i < 120; ++i) {
        cam.setLookKeys(false, false, false, false, false); // rilasciato
        cam.update(1.0f / 60.0f, body, 0.0f);
    }
    KS_CHECK_NEAR(cam.lookYaw(), 0.0f, 1e-4f);

    // --- range HMD: dentro il range invariata, oltre fermata al limite -----
    const auto inside = cam.mapHeadTranslation({0.05f, 0.0f, 0.0f});
    KS_CHECK_NEAR(inside.x, 0.05f, 1e-6f); // 0.05 m su 0.15 m di range
    const auto clipped = cam.mapHeadTranslation({1.0f, 0.0f, 0.0f});
    KS_CHECK_NEAR(clipped.x, 0.15f, 1e-6f);
    const auto clippedDown = cam.mapHeadTranslation({0.0f, -1.0f, 0.0f});
    KS_CHECK_NEAR(clippedDown.y, -0.15f, 1e-6f);

    // --- esponente HMD: a metà corsa un esponente 2 vale un quarto ---------
    ControlsConfig curved = controls;
    curved.hmdYawExponent = 2.0;
    cam.setControls(curved);
    const auto half = cam.mapHeadTranslation({0.075f, 0.0f, 0.0f});
    KS_CHECK_NEAR(half.x, 0.0375f, 1e-6f); // (0.5)^2 * 0.15

    return 0;
}
