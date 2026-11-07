# Parity 1.4 — determinism
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_determinism.cpp")
    add_executable(test_determinism test_determinism.cpp)
    target_link_libraries(test_determinism PRIVATE ksengine)
    add_test(NAME test_determinism COMMAND test_determinism)
endif()

# Engine start — the sequence comes from the car's manifest (data.json
# "start") and its type decides which hardware cranks it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engine_start_test.cpp")
    add_executable(engine_start_test engine_start_test.cpp)
    target_link_libraries(engine_start_test PRIVATE ksengine)
    add_test(NAME engine_start_test COMMAND engine_start_test)
endif()

# The car's own electronics slip limits: [TRACTION_CONTROL] and [ABS]
# SLIP_RATIO_LIMIT from data.json, where the aids start cutting.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/car_electronics_test.cpp")
    add_executable(car_electronics_test car_electronics_test.cpp)
    target_include_directories(car_electronics_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_link_libraries(car_electronics_test PRIVATE ksengine)
    add_test(NAME car_electronics_test COMMAND car_electronics_test)
endif()

# Content's brake-disc section: what [BRAKEDISC_FX] says about the glow the
# thermal model produces (ACTIVE, GLOW_MULT), read from model.json.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/brake_disc_fx_test.cpp")
    add_executable(brake_disc_fx_test brake_disc_fx_test.cpp)
    target_include_directories(brake_disc_fx_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_link_libraries(brake_disc_fx_test PRIVATE ksengine)
    add_test(NAME brake_disc_fx_test COMMAND brake_disc_fx_test)
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

# AI line — arc-length parameterisation: distance lookup, wrapping on a closed
# loop, nearest point and uniform sampling.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ai_spline_arc_test.cpp")
    add_executable(ai_spline_arc_test ai_spline_arc_test.cpp)
    target_include_directories(ai_spline_arc_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(ai_spline_arc_test PRIVATE ksengine)
    add_test(NAME ai_spline_arc_test COMMAND ai_spline_arc_test)
endif()

# Game menu — navigation end-to-end: every screen from MAIN, its ESC path,
# the callbacks behind the rows and the row/separator invariants.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game_menu_navigation_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(game_menu_navigation_test
        game_menu_navigation_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(game_menu_navigation_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(game_menu_navigation_test PRIVATE ksengine)
    add_test(NAME game_menu_navigation_test COMMAND game_menu_navigation_test)
endif()

# The startup screen: the tile row the game opens on — its geometry, the
# scrolling window, the hit-test, every tile's icon and the hub's key/mouse
# paths. Compiles GameMenuOverlay.cpp directly (same pattern as above).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main_menu_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    # The hub is constructed by this test, so the overlays it owns and the
    # font/text TUs come with it; everything else it uses is header-only.
    add_executable(main_menu_test
        main_menu_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/DashboardOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TelemetryOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/ConsoleOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(main_menu_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(main_menu_test PRIVATE ksengine)
    add_test(NAME main_menu_test COMMAND main_menu_test)
endif()

# The pad-to-menu mapping: one key per press, so a held stick is one step.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pad_menu_test.cpp")
    add_executable(pad_menu_test pad_menu_test.cpp)
    target_include_directories(pad_menu_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME pad_menu_test COMMAND pad_menu_test)
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

# Phase 3.3 multi-layout catalog
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_layout_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackLayout.cpp")
    add_executable(track_layout_test
        track_layout_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackLayout.cpp")
    target_include_directories(track_layout_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME track_layout_test COMMAND track_layout_test)
endif()

# Parity 1.1 — golden trajectory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_PhysicsGolden.cpp")
    add_executable(test_PhysicsGolden test_PhysicsGolden.cpp)
    target_link_libraries(test_PhysicsGolden PRIVATE ksengine)
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/physics_json_test.cpp")
    add_executable(physics_json_test physics_json_test.cpp)
    target_link_libraries(physics_json_test PRIVATE ksengine)
    add_test(NAME physics_json_test COMMAND physics_json_test)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/brake_fade_test.cpp")
    add_executable(brake_fade_test brake_fade_test.cpp)
    target_link_libraries(brake_fade_test PRIVATE ksengine)
    add_test(NAME brake_fade_test COMMAND brake_fade_test)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/brake_stop_test.cpp")
    add_executable(brake_stop_test brake_stop_test.cpp)
    target_link_libraries(brake_stop_test PRIVATE ksengine)
    add_test(NAME brake_stop_test COMMAND brake_stop_test)
endif()

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

if(TARGET ksnet AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_multiplayer.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NetworkManager.cpp")
    add_executable(test_multiplayer
        test_multiplayer.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NetworkManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/NetworkLowLevel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(test_multiplayer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(test_multiplayer PRIVATE ksengine ksnet)
    add_test(NAME test_multiplayer COMMAND test_multiplayer)
    set_tests_properties(test_multiplayer PROPERTIES TIMEOUT 60)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui_svg_icon_test.cpp")
    # Header-only on purpose: the icon set and the parser live in the
    # simulator's ui/ headers, so the test needs no renderer or window.
    add_executable(ui_svg_icon_test ui_svg_icon_test.cpp)
    target_include_directories(ui_svg_icon_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME ui_svg_icon_test COMMAND ui_svg_icon_test)
    set_tests_properties(ui_svg_icon_test PROPERTIES TIMEOUT 60)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/showroom_test.cpp")
    # CameraController lives in the simulator (not the engine library), so the
    # turntable is tested by compiling it into the test.
    add_executable(showroom_test showroom_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/CameraController.cpp")
    target_include_directories(showroom_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(showroom_test PRIVATE ksengine)
    add_test(NAME showroom_test COMMAND showroom_test)
    set_tests_properties(showroom_test PROPERTIES TIMEOUT 60)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ppfilter_preset_test.cpp")
    # Post-processing preset loader (engine/Config/PPFilterPreset.h): needs the
    # shipped system/ppfilters/*.json, hence KS_SOURCE_DIR.
    add_executable(ppfilter_preset_test ppfilter_preset_test.cpp)
    target_include_directories(ppfilter_preset_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_compile_definitions(ppfilter_preset_test PRIVATE
        KS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ppfilter_preset_test PRIVATE ksengine)
    add_test(NAME ppfilter_preset_test COMMAND ppfilter_preset_test)
    set_tests_properties(ppfilter_preset_test PROPERTIES TIMEOUT 60)
endif()

# Content overrides (adapters/assetto_corsa/Overrides.h): hidden nodes, driver
# model and sections folded into the documents that own them. Needs the Json
# implementation, hence ksengine.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/overrides_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/adapters/assetto_corsa/Overrides.h")
    add_executable(overrides_test overrides_test.cpp)
    target_include_directories(overrides_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(overrides_test PRIVATE ksengine)
    add_test(NAME overrides_test COMMAND overrides_test)
    set_tests_properties(overrides_test PROPERTIES TIMEOUT 60)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/material_preset_test.cpp")
    # Material preset table (engine/Graphics/MaterialPresets.h): header only,
    # so the test needs nothing but the include path.
    add_executable(material_preset_test material_preset_test.cpp)
    target_include_directories(material_preset_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME material_preset_test COMMAND material_preset_test)
    set_tests_properties(material_preset_test PROPERTIES TIMEOUT 60)
endif()
