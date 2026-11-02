// PhysicsSimulations — verifica d'esecuzione del port Qt-free.
//
// Il modulo (soft body, cloth, particelle, fluido SPH, hair) era fra i 17 file
// che nel build Qt-free compilavano a vuoto: le dichiarazioni esistevano nell'
// header, i corpi no. Questo test esegue il soft body, che e' il pezzo con piu'
// stato interno, e controlla le tre cose che un port sbagliato romperebbe:
// la mesh resta della stessa dimensione, i vertici pinnati non si muovono, la
// gravita' muove quelli liberi e i vincoli non esplodono.

#include "KsTest.h"

#include "engine/physics/PhysicsSimulations.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace ph = ks::physics;

namespace {

float distance(const ph::PhysVec3& a, const ph::PhysVec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

int main() {
    ph::SoftBodySimulator soft;
    const std::vector<ph::PhysVec3> cube{
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    const std::vector<int> faces{0, 1, 2, 0, 2, 3};

    soft.setMesh(cube, faces);
    KS_CHECK(soft.getPositions().size() == cube.size());

    soft.pinVertex(0);
    soft.setGravity(ph::PhysVec3(0.0f, -9.81f, 0.0f));
    for (int i = 0; i < 10; ++i) soft.simulate(1);

    const std::vector<ph::PhysVec3> after = soft.getPositions();
    KS_CHECK(after.size() == cube.size());

    if (after.size() == cube.size()) {
        // Il pin tiene fermo il vertice 0 (entro il rumore numerico).
        KS_CHECK_NEAR(after[0].x, 0.0, 1e-4);
        KS_CHECK_NEAR(after[0].y, 0.0, 1e-4);
        KS_CHECK_NEAR(after[0].z, 0.0, 1e-4);

        // La gravita' ha spostato verso il basso un vertice libero.
        KS_CHECK(after[7].y < cube[7].y - 1e-3f);

        // I vincoli tengono: lo spigolo 0-1 non collassa ne' si allunga senza limite.
        const float edge = distance(after[0], after[1]);
        KS_CHECK(edge > 0.5f);
        KS_CHECK(edge < 1.5f);
    }

    // reset() azzera le velocita', non la geometria. Il port e' fedele
    // all'originale: le posizioni restano quelle raggiunte, cambia lo stato
    // dinamico. Documentarlo qui vale piu' che assumere il contrario.
    const std::vector<ph::PhysVec3> beforeReset = soft.getPositions();
    soft.reset();
    const std::vector<ph::PhysVec3> afterReset = soft.getPositions();
    KS_CHECK(afterReset.size() == beforeReset.size());
    if (afterReset.size() == beforeReset.size()) {
        for (std::size_t i = 0; i < afterReset.size(); ++i) {
            KS_CHECK_NEAR(afterReset[i].x, beforeReset[i].x, 1e-6);
            KS_CHECK_NEAR(afterReset[i].y, beforeReset[i].y, 1e-6);
            KS_CHECK_NEAR(afterReset[i].z, beforeReset[i].z, 1e-6);
        }
    }

    // Una mesh senza facce non deve far esplodere il simulatore.
    ph::SoftBodySimulator empty;
    empty.setMesh({}, {});
    empty.simulate(1);
    KS_CHECK(empty.getPositions().empty());

    return KS_TEST_RESULT("physics_simulations_test");
}
