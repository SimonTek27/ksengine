/**
 * Roadmap 2.4 / P2.1 — authored KN5 distance windows (lodIn/lodOut) made
 * testable:
 *  - LodWindow.h rules: the default pair is "no window" (drawn at any sane
 *    distance, renderer skips the test), a sane pair gates strictly on
 *    [in, out] with inclusive edges, anything malformed fails OPEN so a bad
 *    mod file can never blank the scene;
 *  - distanceToBounds(): nearest-point distance, so a mesh the camera stands
 *    inside never reads as "far" however large its bounds are;
 *  - the queued-decision replay of NativeRenderer::drawMesh()'s gate: window
 *    test only when one is authored, camera distance in metres.
 */
#include "KsTest.h"
#include "engine/scene/LodWindow.h"

#include <cmath>
#include <cstdio>

using ks::math::vec3;
using ks::scene::distanceToBounds;
using ks::scene::hasAuthoredWindow;
using ks::scene::inLodWindow;
using ks::scene::LodWindow;
using ks::scene::lodWindowUsable;

int main() {
    // --- Default window: no authored pair, always queued -------------------
    const LodWindow none;
    KS_CHECK(none.in == 0.0f && none.out == ks::scene::kLodNoLimit);
    KS_CHECK(lodWindowUsable(none));
    KS_CHECK(!hasAuthoredWindow(none)); // renderer keeps the pre-2.4 path
    KS_CHECK(inLodWindow(0.0f, none));
    KS_CHECK(inLodWindow(1.0e6f, none)); // any sane camera distance

    // --- Authored far limit ------------------------------------------------
    const LodWindow farLimit{0.0f, 500.0f};
    KS_CHECK(hasAuthoredWindow(farLimit));
    KS_CHECK(inLodWindow(0.0f, farLimit));
    KS_CHECK(inLodWindow(499.99f, farLimit));
    KS_CHECK(inLodWindow(500.0f, farLimit)); // edge is inclusive
    KS_CHECK(!inLodWindow(500.01f, farLimit));

    // --- Near-only detail: both edges bite ---------------------------------
    const LodWindow nearOnly{10.0f, 50.0f};
    KS_CHECK(hasAuthoredWindow(nearOnly));
    KS_CHECK(!inLodWindow(9.99f, nearOnly));
    KS_CHECK(inLodWindow(10.0f, nearOnly));
    KS_CHECK(inLodWindow(50.0f, nearOnly));
    KS_CHECK(!inLodWindow(50.01f, nearOnly));

    // --- Malformed windows fail OPEN ---------------------------------------
    const LodWindow inverted{5.0f, 3.0f};   // out < in
    const LodWindow emptyRange{0.0f, 0.0f}; // zero-length
    const LodWindow negativeIn{-1.0f, 100.0f};
    const LodWindow negativeOut{0.0f, -5.0f};
    for (const LodWindow& w : {inverted, emptyRange, negativeIn, negativeOut}) {
        KS_CHECK(!lodWindowUsable(w));
        KS_CHECK(inLodWindow(0.0f, w));
        KS_CHECK(inLodWindow(1.0e6f, w));
    }

    // --- distanceToBounds: gap to the nearest face/corner ------------------
    const vec3 bmin{0.f, 0.f, 0.f};
    const vec3 bmax{4.f, 4.f, 4.f};
    KS_CHECK_NEAR(distanceToBounds({2.f, 2.f, 2.f}, bmin, bmax), 0.0f, 1e-6f);
    KS_CHECK_NEAR(distanceToBounds({4.f, 1.f, 2.f}, bmin, bmax), 0.0f, 1e-6f);
    KS_CHECK_NEAR(distanceToBounds({10.f, 4.f, 2.f}, bmin, bmax), 6.0f, 1e-6f);
    KS_CHECK_NEAR(distanceToBounds({2.f, -3.f, 2.f}, bmin, bmax), 3.0f, 1e-6f);
    KS_CHECK_NEAR(distanceToBounds({7.f, 7.f, 7.f}, bmin, bmax),
                  std::sqrt(27.0f), 1e-5f); // corner

    // --- drawMesh()'s decision, replayed -----------------------------------
    auto queued = [](float dist, const LodWindow& w) {
        return !hasAuthoredWindow(w) || inLodWindow(dist, w);
    };
    // 100 m out: default and far-limit meshes stay, near-only detail drops.
    KS_CHECK(queued(100.0f, none));
    KS_CHECK(queued(100.0f, farLimit));
    KS_CHECK(!queued(100.0f, nearOnly));
    // camera closes to 40 m: the near-detail mesh comes back.
    KS_CHECK(queued(40.0f, none));
    KS_CHECK(queued(40.0f, farLimit));
    KS_CHECK(queued(40.0f, nearOnly));
    // malformed windows are never dropped, at any distance.
    KS_CHECK(queued(100.0f, inverted));
    KS_CHECK(queued(1.0e6f, inverted));

    std::printf("lod_window: default/far/near windows + bounds distance OK\n");
    return 0;
}
