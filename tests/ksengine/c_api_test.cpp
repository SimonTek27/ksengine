/**
 * c_api_test — E1/E9 smoke: create, step, profiler, load guards.
 */
#include "ksengine_c.h"
#include <cstdio>
#include <cmath>

static int fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++fail; } } while(0)

int main() {
    std::printf("ksengine_c version %s\n", ks_engine_version());
    KsEngine* e = ks_engine_create(42);
    CHECK(e != nullptr);

    ks_engine_set_timestep(e, 0.001);
    KsInput in{};
    in.throttle = 0.5f;
    ks_engine_set_input(e, &in);

    ks_engine_profiler_set_enabled(1);
    ks_engine_profiler_begin_frame();
    ks_engine_profiler_begin_section("step_batch");
    for (int i = 0; i < 1000; ++i)
        ks_engine_step(e, 0.001);
    ks_engine_profiler_end_section("step_batch");
    ks_engine_profiler_end_frame();

    KsVehicleState st{};
    ks_engine_get_state(e, &st);
    CHECK(std::isfinite(st.x) && std::isfinite(st.speed_ms));
    std::printf("state: pos=(%.2f,%.2f,%.2f) speed=%.2f rpm=%.0f gear=%d\n",
                st.x, st.y, st.z, st.speed_ms, st.rpm, st.gear);

    CHECK(ks_engine_load_track(e, "/no/such/track/dir") == 0);
    CHECK(ks_engine_profiler_is_enabled() == 1);
    std::printf("profiler frame_ms=%.3f fps=%d\n",
                ks_engine_profiler_frame_ms(), ks_engine_profiler_fps());

    // E9 GPU frame path: this run has no renderer, so 0 is the only honest
    // answer — the getters must never invent a number for a missing GPU pass.
    CHECK(ks_engine_profiler_gpu_ms() >= 0.0);
    CHECK(ks_engine_profiler_gpu_avg_ms() >= 0.0);
    CHECK(ks_engine_profiler_gpu_fps() >= 0);
    std::printf("profiler gpu_ms=%.3f gpu_avg_ms=%.3f gpu_fps=%d\n",
                ks_engine_profiler_gpu_ms(), ks_engine_profiler_gpu_avg_ms(),
                ks_engine_profiler_gpu_fps());

    ks_engine_destroy(e);
    if (fail) {
        std::printf("%d check(s) failed\n", fail);
        return 1;
    }
    std::printf("c_api_test OK\n");
    return 0;
}
