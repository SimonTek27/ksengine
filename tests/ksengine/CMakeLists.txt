# Qt-free engine unit tests — CTest only (no Qt, no QTest, no kseditor_lib).
#
# Two ways to build:
#   from the repo root qt-free configure (enable_testing + add_subdirectory)
#   standalone: cmake -S tests/ksengine -B build_ksengine_tests
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ksengine_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # The tests include the engine as "engine/...", so the `src` root must be
    # on the path. The root configure puts it there for every target; a
    # standalone one only gets ksengine's own PUBLIC directories (`src/engine`
    # and below), which cannot resolve that prefix.
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    # test_renderer loads ${CMAKE_BINARY_DIR}/shaders; the root qt-free
    # configure gets that set from SimulatorApp, so build it here too.
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/KsShaders.cmake")
    ks_add_shader_target(ks_shaders "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    if(NOT TARGET ksengine)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine" ksengine)
    endif()
endif()

enable_testing()

set(KS_QT_FREE_TESTS
    ecs_test
    engine_tick_test
    math_test
    frustum_test
    occlusion_test
    ai_line_test
    navmesh_test
    kn5_test
    dds_test
    ksanim_test
    acd_test
    obj_test
    deflate_test
    fbx_test
    ttf_test
    bank_test
    lua_test
    script_module_test
    mod_sdk_test
    particle_test
    terrain_test
    png_test
    damage_telemetry_test
    keyboard_mapping_test
    content_library_test
    scene_assets_test
)

foreach(_t ${KS_QT_FREE_TESTS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_t}.cpp")
        add_executable(${_t} "${_t}.cpp")
        target_link_libraries(${_t} PRIVATE ksengine)
        add_test(NAME ${_t} COMMAND ${_t})
    endif()
endforeach()

# Parity 2.5 — real FMOD bank data (template SDK sample banks).
if(TARGET bank_test)
    target_compile_definitions(bank_test PRIVATE
        KS_BANK_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../template/ksAudioEditor/ac_ksaudiostudio_sdk/Build/Desktop")
endif()

# Parity 2.1 — renderer validation: real Vulkan device on a hidden window,
# renders a known quad and reads back the swapchain pixels. Compiles the
# simulator renderer TUs directly (track_loader_test pattern) + the ui TUs
# NativeRenderer::drawUi() links against. Skipped when no Vulkan SDK.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_renderer.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NativeRenderer.cpp")
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        file(GLOB _rtest_ui_sources "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/*.cpp")
        add_executable(test_renderer
            test_renderer.cpp
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NativeRenderer.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ShadowSystem.cpp"
            ${_rtest_ui_sources})
        target_include_directories(test_renderer PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
            ${Vulkan_INCLUDE_DIRS})
        target_compile_definitions(test_renderer PRIVATE
            KSENGINE_QT_FREE=1
            KS_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
        target_link_libraries(test_renderer PRIVATE ksengine ${Vulkan_LIBRARIES})
        if(WIN32)
            target_link_libraries(test_renderer PRIVATE user32 gdi32)
        endif()
        if(TARGET ks_shaders)
            add_dependencies(test_renderer ks_shaders)
        endif()
        add_test(NAME test_renderer COMMAND test_renderer)
    else()
        message(STATUS "test_renderer: Vulkan SDK not found — test not built")
    endif()
endif()

# Parity 1.4 — determinism
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_determinism.cpp")
    add_executable(test_determinism test_determinism.cpp)
    target_link_libraries(test_determinism PRIVATE ksengine)
    add_test(NAME test_determinism COMMAND test_determinism)
endif()

# TrackLoader (src/simulator) — real-KN5 header validation regression test.
# Compiles the simulator TU directly: TrackLoader is part of SimulatorApp,
# not ksengine, but only depends on engine headers/libs.
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

# Parity 3.2 — dedicated server smoke (C API): 1 lap on a short loop,
# fast mode, must reach checkered within --duration sim seconds.
if(TARGET ks_server)
    add_test(NAME ks_server_smoke
             COMMAND ks_server --laps 1 --track-length 150 --countdown 1
                     --fast --duration 60 --status 0)
endif()

# Parity 4.2 — headless no-Vulkan: SimulatorApp must boot and run the session
# without creating a window or touching Vulkan, then exit on its own timer.
if(TARGET SimulatorApp)
    add_test(NAME sim_headless_smoke COMMAND SimulatorApp)
    set_tests_properties(sim_headless_smoke PROPERTIES
        ENVIRONMENT "KS_HEADLESS=1;KS_HEADLESS_SECONDS=2"
        TIMEOUT 60)
endif()

# Parity 4.3 — scene bridge: ECS scene snapshot API for the Godot
# GDExtension host (handles, transform/mesh round-trip, snapshot sizing,
# revision counter, vehicle pose binding).
if(TARGET ksengine_c AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scene_bridge_test.cpp")
    add_executable(scene_bridge_test scene_bridge_test.cpp)
    target_link_libraries(scene_bridge_test PRIVATE ksengine_c)
    add_test(NAME scene_bridge_test COMMAND scene_bridge_test)
endif()

# Parity 3.3 — track tooling: unit test (scaffold/validate + AiFileWriter
# roundtrip) plus ks_tracktool CLI end-to-end (scaffold a fixture, validate
# it, fail on a missing dir, usage error).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_tool_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tracktool/TrackToolCore.cpp")
    add_executable(track_tool_test
        track_tool_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tracktool/TrackToolCore.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackLoader.cpp")
    target_include_directories(track_tool_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/tracktool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(track_tool_test PRIVATE ksengine)
    add_test(NAME track_tool_test COMMAND track_tool_test)
endif()

if(TARGET ks_tracktool)
    set(_tt_fixture "${CMAKE_CURRENT_BINARY_DIR}/tracktool_fixture")
    add_test(NAME track_tool_scaffold
             COMMAND ks_tracktool scaffold "${_tt_fixture}")
    add_test(NAME track_tool_validate
             COMMAND ks_tracktool validate "${_tt_fixture}")
    set_tests_properties(track_tool_validate PROPERTIES
        DEPENDS track_tool_scaffold)
    add_test(NAME track_tool_spline_info
             COMMAND ks_tracktool spline-info "${_tt_fixture}/ai/fast_lane.ai")
    set_tests_properties(track_tool_spline_info PROPERTIES
        DEPENDS track_tool_scaffold)
    add_test(NAME track_tool_validate_missing
             COMMAND ks_tracktool validate
                     "${CMAKE_CURRENT_BINARY_DIR}/no_such_track_dir")
    set_tests_properties(track_tool_validate_missing PROPERTIES WILL_FAIL TRUE)
    add_test(NAME track_tool_usage COMMAND ks_tracktool)
    set_tests_properties(track_tool_usage PROPERTIES WILL_FAIL TRUE)
endif()

# Roadmap 1.1 "audio base": offline render test for SimulatorAudio. Runs
# headless through initialize(false) + renderOffline(), so no WASAPI device
# is needed; the synth fallback covers a missing car audio pack.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_audio_render.cpp")
    add_executable(test_audio_render
        test_audio_render.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SimulatorAudio.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/WASAPIOutput.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AudioMixer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SoundsIniParser.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AudioBankManager.cpp")
    target_include_directories(test_audio_render PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_link_libraries(test_audio_render PRIVATE ksengine)
    add_test(NAME test_audio_render COMMAND test_audio_render)
endif()
