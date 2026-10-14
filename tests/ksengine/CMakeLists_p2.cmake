# Parity 1.4 — determinism
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_determinism.cpp")
    add_executable(test_determinism test_determinism.cpp)
    target_link_libraries(test_determinism PRIVATE ksengine)
    add_test(NAME test_determinism COMMAND test_determinism)
endif()

# Roadmap 2.6/2.7/2.8/2.9 gate tests and other special-link tests.
# (Recovered body part 2 of tests/ksengine/CMakeLists.txt)

# Fase 1 exit — session rows gated on car+track (GameMenuOverlay TU).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/menu_session_gate_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(menu_session_gate_test
        menu_session_gate_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(menu_session_gate_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(menu_session_gate_test PRIVATE ksengine)
    add_test(NAME menu_session_gate_test COMMAND menu_session_gate_test)
endif()

# Roadmap 2.5 — pit strategy screen (Sprint 8 / P1.8): GARAGE -> PIT
# STRATEGY rows, fuel steps, toggles, confirm tuple + PitStrategyBridge.
# Same TU pattern as menu_session_gate_test (PitStrategyBridge is header-only).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pit_strategy_menu_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(pit_strategy_menu_test
        pit_strategy_menu_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(pit_strategy_menu_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(pit_strategy_menu_test PRIVATE ksengine)
    add_test(NAME pit_strategy_menu_test COMMAND pit_strategy_menu_test)
endif()

# Gate 2.13 — team/upgrades bridge consolidation: team.ini parser +
# buildFieldFromTeam session field, upgrade catalog parser + appliedMods +
# applyUpgradesToVehicleSimulator on a real VehicleSimulator (header-only
# bridges + ksengine for the VehicleSimulator setters).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/team_upgrade_bridge_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TeamInfo.h" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/vehicle/VehicleUpgradeSystem.h")
    add_executable(team_upgrade_bridge_test team_upgrade_bridge_test.cpp)
    target_include_directories(team_upgrade_bridge_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(team_upgrade_bridge_test PRIVATE ksengine)
    add_test(NAME team_upgrade_bridge_test COMMAND team_upgrade_bridge_test)
endif()

# Roadmap 2.7 - pit lane queue: order of box exits (release gap + spacing).
# PitLaneQueue.h is header-only, so this target compiles just the test TU.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pit_queue_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/PitLaneQueue.h")
    add_executable(pit_queue_test pit_queue_test.cpp)
    target_include_directories(pit_queue_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(pit_queue_test PRIVATE ksengine)
    add_test(NAME pit_queue_test COMMAND pit_queue_test)
endif()

# Roadmap 2.3 — materials.txt: parse/format/degradation + the texture path
# resolution that mirrors the baker's sanitizeFileName. Header-only
# MaterialCache.h, so this target compiles just the test TU.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/material_cache_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/MaterialCache.h")
    add_executable(material_cache_test material_cache_test.cpp)
    target_include_directories(material_cache_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(material_cache_test PRIVATE ksengine)
    add_test(NAME material_cache_test COMMAND material_cache_test)
endif()

# renders a known quad and reads back the swapchain pixels. Compiles the
# simulator renderer TUs directly (track_loader_test pattern) + the ui TUs
# NativeRenderer::drawUi() links against. Skipped when no Vulkan SDK.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_renderer.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NativeRenderer.cpp")
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        # Roadmap 2.4 — the committed reference bakes the test drives end to
        # end (tools/make_reference_content.ps1 regenerates them). Resolved
        # against this file's directory, so both the top-level and the
        # standalone tests/ksengine configure land on the same content.
        get_filename_component(_ks_ref_bake
            "${CMAKE_CURRENT_SOURCE_DIR}/../../content/baked" ABSOLUTE)
        get_filename_component(_ks_ref_car
            "${CMAKE_CURRENT_SOURCE_DIR}/../../content/cars/refcar/baked" ABSOLUTE)
        file(GLOB _rtest_ui_sources "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/*.cpp")
        add_executable(test_renderer
            test_renderer.cpp
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NativeRenderer.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TextureRuntime.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ShadowSystem.cpp"
            ${_rtest_ui_sources})
        target_include_directories(test_renderer PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
            ${Vulkan_INCLUDE_DIRS})
        target_compile_definitions(test_renderer PRIVATE
            KSENGINE_QT_FREE=1
            KS_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
            # Roadmap 2.4/2.5: committed reference content (absolute, so the
            # test does not depend on the ctest working directory).
            KS_TEST_REFERENCE_BAKE="${_ks_ref_bake}"
            KS_TEST_REFERENCE_CAR="${_ks_ref_car}")
        target_link_libraries(test_renderer PRIVATE ksengine ${Vulkan_LIBRARIES})
        if(WIN32)
            target_link_libraries(test_renderer PRIVATE user32 gdi32)
        endif()
        if(TARGET ks_shaders)
            add_dependencies(test_renderer ks_shaders)
        endif()
        add_test(NAME test_renderer COMMAND test_renderer)
        set_tests_properties(test_renderer PROPERTIES TIMEOUT 120)
    else()
        message(STATUS "test_renderer: Vulkan SDK not found — test not built")
    endif()
endif()

# (Recovered body: loader / golden / parity blocks dropped by the p1/p2/p3
#  split — restored from 82172929:tests/ksengine/CMakeLists.txt.)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_loader_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackLoader.cpp")
    add_executable(track_loader_test
        track_loader_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackLoader.cpp")
    target_include_directories(track_loader_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(track_loader_test PRIVATE ksengine)
    add_test(NAME track_loader_test COMMAND track_loader_test)
endif()

# Parity 1.1 — golden trajectory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_PhysicsGolden.cpp")
    add_executable(test_PhysicsGolden test_PhysicsGolden.cpp)
    target_link_libraries(test_PhysicsGolden PRIVATE ksengine)
    # Working directory = repo root so tests/data/golden_lap.csv resolves
    add_test(NAME test_PhysicsGolden COMMAND test_PhysicsGolden
             WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

# Parity 1.2 — golden export round-trip (corr_speed > 0.95)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_GoldenExport.cpp")
    add_executable(test_GoldenExport test_GoldenExport.cpp)
    target_link_libraries(test_GoldenExport PRIVATE ksengine)
    add_test(NAME test_GoldenExport COMMAND test_GoldenExport)
endif()

# Parity 2.2 — physics data file loaders (power.lut / engine / tyres / gears)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_physics_ini.cpp")
    add_executable(test_physics_ini test_physics_ini.cpp)
    target_link_libraries(test_physics_ini PRIVATE ksengine)
    add_test(NAME test_physics_ini COMMAND test_physics_ini)
endif()

# Unified car physics file (data/physics.json): one file replacing the
# engine/tyres/drivetrain/aero/suspension .ini set, with .ini fallback.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/physics_json_test.cpp")
    add_executable(physics_json_test physics_json_test.cpp)
    target_link_libraries(physics_json_test PRIVATE ksengine)
    add_test(NAME physics_json_test COMMAND physics_json_test)
endif()

# Parity 4 — race flags: yellow/SC -> AI speed limiter + AC SM flag channel.
# Compiles the simulator TUs directly (track_loader_test pattern).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_race_flags.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RaceSessionManager.cpp")
    add_executable(test_race_flags
        test_race_flags.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RaceSessionManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/MultiCarManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AIController.cpp")
    target_include_directories(test_race_flags PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(test_race_flags PRIVATE ksengine)
    add_test(NAME test_race_flags COMMAND test_race_flags)
endif()

# Parity 3.5 — AI racing: grid spawn on fast_lane.ai, AI driving + lap wrap
# detection, flag speed limiter, race standings feed. Same simulator TUs as
# test_race_flags.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ai_race_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/MultiCarManager.cpp")
    add_executable(ai_race_test
        ai_race_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RaceSessionManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/MultiCarManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AIController.cpp")
    target_include_directories(ai_race_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(ai_race_test PRIVATE ksengine)
    add_test(NAME ai_race_test COMMAND ai_race_test)
    set_tests_properties(ai_race_test PROPERTIES TIMEOUT 120)
endif()

# Parity 3.1 — multiplayer: real ksnet transport over loopback UDP with two
# NetworkManager instances (host + remote guest). Compiles the simulator
# network TUs directly and stubs the two SimulationLoop symbols they pull in
# (see the comments at the top of test_multiplayer.cpp).
if(TARGET ksnet AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_multiplayer.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NetworkManager.cpp")
    add_executable(test_multiplayer
        test_multiplayer.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NetworkManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NetworkLowLevel.cpp")
    target_include_directories(test_multiplayer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(test_multiplayer PRIVATE ksengine ksnet)
    add_test(NAME test_multiplayer COMMAND test_multiplayer)
    set_tests_properties(test_multiplayer PROPERTIES TIMEOUT 60)
endif()
