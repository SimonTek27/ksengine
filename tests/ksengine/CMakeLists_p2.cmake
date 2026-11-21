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

    # Track recovery: header-only rule, so the test compiles it by including it.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_recovery_test.cpp")
        add_executable(track_recovery_test track_recovery_test.cpp)
        target_include_directories(track_recovery_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator")
        target_link_libraries(track_recovery_test PRIVATE ksengine)
        add_test(NAME track_recovery_test COMMAND track_recovery_test)
    endif()
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

# Balance lever: with load sensitivity costing the transfer, the front share of
# roll stiffness (LLTD) must change the cornering radius at a fixed steer input.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lltd_balance_test.cpp")
    add_executable(lltd_balance_test lltd_balance_test.cpp)
    target_include_directories(lltd_balance_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_link_libraries(lltd_balance_test PRIVATE ksengine)
    add_test(NAME lltd_balance_test COMMAND lltd_balance_test)
endif()

# The garage's aid rows: a car whose own electronics say PRESENT = 0 offers no
# TC or ABS level, so the row is not there to pick. Compiles SetupGarage.cpp
# directly (main_menu_test pattern) with the text TUs it draws through.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/setup_garage_aids_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SetupGarage.cpp")
    add_executable(setup_garage_aids_test
        setup_garage_aids_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SetupGarage.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(setup_garage_aids_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(setup_garage_aids_test PRIVATE ksengine)
    add_test(NAME setup_garage_aids_test COMMAND setup_garage_aids_test)
endif()

# The setups a car folder ships (<car>/setups): the list, the [ / ] cycle and
# what choosing one does to the rows. Compiles SetupGarage.cpp directly.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/setup_catalog_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SetupGarage.cpp")
    add_executable(setup_catalog_test
        setup_catalog_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SetupGarage.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(setup_catalog_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(setup_catalog_test PRIVATE ksengine)
    add_test(NAME setup_catalog_test COMMAND setup_catalog_test)
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

# Roadmap 2.7 — wizard di calibrazione guidato (InputManager TU).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/input_calibration_wizard_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/InputManager.cpp")
    add_executable(input_calibration_wizard_test
        input_calibration_wizard_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/InputManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ControlsConfig.cpp")
    target_include_directories(input_calibration_wizard_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(input_calibration_wizard_test PRIVATE ksengine)
    add_test(NAME input_calibration_wizard_test COMMAND input_calibration_wizard_test)
endif()

# loop, nearest point and uniform sampling.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ai_spline_arc_test.cpp")
    add_executable(ai_spline_arc_test ai_spline_arc_test.cpp)
    target_include_directories(ai_spline_arc_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(ai_spline_arc_test PRIVATE ksengine)
    add_test(NAME ai_spline_arc_test COMMAND ai_spline_arc_test)
endif()

# CONTENT -> INSTALL: a package's entries land in their library, bad ones are
# refused before anything is written.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/content_install_test.cpp")
    add_executable(content_install_test content_install_test.cpp)
    target_include_directories(content_install_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(content_install_test PRIVATE ksengine)
    add_test(NAME content_install_test COMMAND content_install_test)
endif()

# VIDEO page: the launcher's options and what they write back to settings.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/video_settings_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(video_settings_test
        video_settings_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(video_settings_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(video_settings_test PRIVATE ksengine)
    add_test(NAME video_settings_test COMMAND video_settings_test)
endif()

# AUDIO section: the seven bus levels and the audio.json pointer.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_settings_test.cpp")
    add_executable(audio_settings_test audio_settings_test.cpp)
    target_include_directories(audio_settings_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(audio_settings_test PRIVATE ksengine)
    add_test(NAME audio_settings_test COMMAND audio_settings_test)
endif()

# The .kn5's load parameters as the car's record (model.json's kn5 block):
# written on a first load, read on every one after it, never over an existing
# block, and never over a document that does not parse.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kn5_model_parameters_test.cpp")
    add_executable(kn5_model_parameters_test kn5_model_parameters_test.cpp)
    target_include_directories(kn5_model_parameters_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(kn5_model_parameters_test PRIVATE ksengine)
    add_test(NAME kn5_model_parameters_test COMMAND kn5_model_parameters_test)
endif()

# CONTENT page rows for the loaded .kn5's own record (model.json's kn5 block):
# the model file and the skin, shown from the document and cycled back into it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/car_import_menu_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(car_import_menu_test
        car_import_menu_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(car_import_menu_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(car_import_menu_test PRIVATE ksengine)
    add_test(NAME car_import_menu_test COMMAND car_import_menu_test)
endif()

# CONTENT -> SKINS: the car's texture sets, the one model.json records, and
# the choice that writes it back and rebuilds the car.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/skin_select_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(skin_select_test
        skin_select_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(skin_select_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(skin_select_test PRIVATE ksengine)
    add_test(NAME skin_select_test COMMAND skin_select_test)
endif()

# CONTENT -> SUITS / HELMETS: the driver-gear libraries, the player's choice and
# the driver.json that keeps it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/driver_gear_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(driver_gear_test
        driver_gear_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(driver_gear_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(driver_gear_test PRIVATE ksengine)
    add_test(NAME driver_gear_test COMMAND driver_gear_test)
endif()

# CONTENT -> SHOWROOMS: the stored default room and the startup loader.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/showroom_default_test.cpp")
    add_executable(showroom_default_test showroom_default_test.cpp)
    target_include_directories(showroom_default_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(showroom_default_test PRIVATE ksengine)
    add_test(NAME showroom_default_test COMMAND showroom_default_test)
endif()

# Content gaps: the colour spellings content ships (oemColorsVintage) and the
# per-track tweaks document (trackTweaks).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/content_gaps_test.cpp")
    add_executable(content_gaps_test content_gaps_test.cpp)
    target_include_directories(content_gaps_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(content_gaps_test PRIVATE ksengine)
    add_test(NAME content_gaps_test COMMAND content_gaps_test)
endif()

# The shape every menu row is drawn with (ui/MenuRowShape.h): the "(" left
# edge, the top rule, the "/" bevel and the dark tab right of it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/menu_row_shape_test.cpp")
    add_executable(menu_row_shape_test menu_row_shape_test.cpp)
    target_include_directories(menu_row_shape_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME menu_row_shape_test COMMAND menu_row_shape_test)
endif()

# The built-in face: condensed glyphs and proportional advances (FontAtlas.h).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/font_metrics_test.cpp")
    add_executable(font_metrics_test font_metrics_test.cpp)
    target_include_directories(font_metrics_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME font_metrics_test COMMAND font_metrics_test)
endif()

# Cyrillic in the atlas: UTF-8 decoding and the U+0400..U+04FF block.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/font_cyrillic_test.cpp")
    add_executable(font_cyrillic_test font_cyrillic_test.cpp)
    target_include_directories(font_cyrillic_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME font_cyrillic_test COMMAND font_cyrillic_test)
endif()

# Dev-mode scripting: the Lua bridge behind the console's LUA verb.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dev_scripting_test.cpp")
    add_executable(dev_scripting_test dev_scripting_test.cpp
        "${CMAKE_SOURCE_DIR}/src/simulator/DevScripting.cpp")
    target_include_directories(dev_scripting_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(dev_scripting_test PRIVATE ksengine)
    # The example script that ships with the content: the test drives it with
    # synthetic frames, so "one ask per gear" is checked and not just claimed.
    target_compile_definitions(dev_scripting_test PRIVATE
        KS_EXAMPLE_SCRIPT="${CMAKE_SOURCE_DIR}/content/scripts/shift_alert.lua")
    add_test(NAME dev_scripting_test COMMAND dev_scripting_test)
endif()

# The painted menu backdrop: the crimson ridge behind menus, tiles and bar.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/menu_backdrop_test.cpp")
    add_executable(menu_backdrop_test menu_backdrop_test.cpp)
    target_include_directories(menu_backdrop_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME menu_backdrop_test COMMAND menu_backdrop_test)
endif()

# Dev mode: pause on focus loss (AppPausePolicy.h), virtual tyres and the
# session clock in the engineer HUD.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dev_mode_policy_test.cpp")
    add_executable(dev_mode_policy_test
        dev_mode_policy_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(dev_mode_policy_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(dev_mode_policy_test PRIVATE ksengine)
    add_test(NAME dev_mode_policy_test COMMAND dev_mode_policy_test)
endif()

# Lap delta against the best lap (ui/LapDelta.h over LapSectorTimer) and the
# HUD bar that shows it. Header-only maths, so the test is the timer and the
# HUD's own draw list - no window and no content.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lap_delta_test.cpp")
    add_executable(lap_delta_test
        lap_delta_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(lap_delta_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(lap_delta_test PRIVATE ksengine)
    add_test(NAME lap_delta_test COMMAND lap_delta_test)
endif()

# CARS -> DATA: the car's declared numbers (CarSpecSheet.h) and the page.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/car_data_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(car_data_test
        car_data_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(car_data_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(car_data_test PRIVATE ksengine)
    add_test(NAME car_data_test COMMAND car_data_test)
endif()

# CREDITS: the authors the installed content declares, and the page.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/content_credits_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(content_credits_test
        content_credits_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(content_credits_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(content_credits_test PRIVATE ksengine)
    add_test(NAME content_credits_test COMMAND content_credits_test)
endif()

# The content manifest: the libraries and their declared versions written to
# <content>/manifest.json, read back, diffed, and the CONTENT page row.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/content_manifest_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(content_manifest_test
        content_manifest_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(content_manifest_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(content_manifest_test PRIVATE ksengine)
    add_test(NAME content_manifest_test COMMAND content_manifest_test)
endif()

# RESULTS: the classification exported on the page's own row (ResultsExport.h).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/results_export_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(results_export_test
        results_export_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(results_export_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(results_export_test PRIVATE ksengine)
    add_test(NAME results_export_test COMMAND results_export_test)
endif()

# Surface effects: dust, stones and the rolling sound the ground under a wheel
# asks for (garage.surfaceEffects).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/surface_effects_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SurfaceEffects.h")
    add_executable(surface_effects_test surface_effects_test.cpp)
    target_include_directories(surface_effects_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(surface_effects_test PRIVATE ksengine)
    add_test(NAME surface_effects_test COMMAND surface_effects_test)
endif()

# Wet track shading: the rule in WetLook.h and the KS_WET_* constants the two
# scene shaders carry, read from the source tree so the mirror is checked.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wet_look_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/WetLook.h")
    add_executable(wet_look_test wet_look_test.cpp)
    target_include_directories(wet_look_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_compile_definitions(wet_look_test PRIVATE
        KS_SHADER_SRC="${CMAKE_SOURCE_DIR}/src/simulator/shaders")
    target_link_libraries(wet_look_test PRIVATE ksengine)
    add_test(NAME wet_look_test COMMAND wet_look_test)
endif()

# Frame generation / super resolution input seam: the contract in
# FrameGenInputs.h (motion convention, the jitter read back from the
# projections, readiness) and the wiring NativeRenderer.cpp publishes for it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frame_gen_inputs_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/FrameGenInputs.h")
    add_executable(frame_gen_inputs_test frame_gen_inputs_test.cpp)
    target_include_directories(frame_gen_inputs_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_compile_definitions(frame_gen_inputs_test PRIVATE
        KS_SIM_SRC="${CMAKE_SOURCE_DIR}/src/simulator"
        KS_SHADER_SRC="${CMAKE_SOURCE_DIR}/src/simulator/shaders")
    target_link_libraries(frame_gen_inputs_test PRIVATE ksengine)
    add_test(NAME frame_gen_inputs_test COMMAND frame_gen_inputs_test)
endif()

# CONTENT page: library counts, the install row and the packages page.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/content_manager_page_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(content_manager_page_test
        content_manager_page_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(content_manager_page_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(content_manager_page_test PRIVATE ksengine)
    add_test(NAME content_manager_page_test COMMAND content_manager_page_test)
endif()

# SETTINGS -> GAMEPLAY: one page and one shared table; the old VIDEO section
# and its duplicate ladder must stay gone.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gameplay_settings_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(gameplay_settings_test
        gameplay_settings_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(gameplay_settings_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(gameplay_settings_test PRIVATE ksengine)
    add_test(NAME gameplay_settings_test COMMAND gameplay_settings_test)
endif()

# Track limits -> penalties: the monitor's ladder is the only place a cut is
# charged (the session used to book the same 3rd report a second time), and the
# monitor only detects while the session races the track (a standing start and
# a car in its box are not cuts). Also covers gameplay.racingRulesStrict.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_limits_rules_test.cpp")
    add_executable(track_limits_rules_test
        track_limits_rules_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RaceSessionManager.cpp")
    target_include_directories(track_limits_rules_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(track_limits_rules_test PRIVATE ksengine)
    add_test(NAME track_limits_rules_test COMMAND track_limits_rules_test)
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

# Driver profile menu page — compile + smoke (Profile screen rows + callbacks).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/profile_menu_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(profile_menu_test
        profile_menu_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    target_include_directories(profile_menu_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(profile_menu_test PRIVATE ksengine)
    add_test(NAME profile_menu_test COMMAND profile_menu_test)
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
        # The hub calls SetupGarage::build() from renderFrame, so the setup
        # screen comes with the hub (this used to link only by luck, on a
        # stale object).
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SetupGarage.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/ConsoleOverlay.cpp"
        # The hub's constructor reads the theme, and that reader is a static
        # member defined here: without it the test links the ctor but not it.
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/NativeUiHub.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/KinetikaAppsOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(main_menu_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(main_menu_test PRIVATE ksengine)
    add_test(NAME main_menu_test COMMAND main_menu_test)
endif()

# The in-race panels are draggable with the mouse, the way AC's app windows are.
# Constructing the hub pulls in the overlays it owns, like main_menu_test.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/overlay_drag_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp")
    add_executable(overlay_drag_test
        overlay_drag_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GameMenuOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/DashboardOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TelemetryOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SetupGarage.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/ConsoleOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/NativeUiHub.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/KinetikaAppsOverlay.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/FontTtfLoader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/TextRenderer.cpp")
    target_include_directories(overlay_drag_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(overlay_drag_test PRIVATE ksengine)
    add_test(NAME overlay_drag_test COMMAND overlay_drag_test)
endif()

# Kinetika apps registry + manager geometry. The overlay's drawing code is
# compiled with the test so a new app cannot be declared in a header only.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kinetika_apps_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/KinetikaAppsOverlay.cpp")
    add_executable(kinetika_apps_test
        kinetika_apps_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/KinetikaAppsOverlay.cpp")
    target_include_directories(kinetika_apps_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(kinetika_apps_test PRIVATE ksengine)
    add_test(NAME kinetika_apps_test COMMAND kinetika_apps_test)
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

# Shader settings a model document carries (Graphics/ShaderSettings.h): the
# explicit shaderSettings block, the older [SHADER_REPLACEMENT_*] sections, the
# material matching and the properties applied to a parsed material.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shader_settings_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/Graphics/ShaderSettings.h")
    add_executable(shader_settings_test shader_settings_test.cpp)
    target_include_directories(shader_settings_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(shader_settings_test PRIVATE ksengine)
    add_test(NAME shader_settings_test COMMAND shader_settings_test)
endif()

# renders a known quad and reads back the swapchain pixels. Compiles the
# simulator renderer TUs directly (track_loader_test pattern) + the ui TUs
# NativeRenderer::drawUi() links against. Skipped when no Vulkan SDK.
#
# KS_REQUIRE_REFERENCE_CAR turns the reference-car section from "skipped when
# the fixture is absent" into a failure. That fixture lives in content/cars,
# which is gitignored and was dropped from the repo on purpose (40a07be), so a
# clean checkout cannot have it: OFF keeps a clean run green, ON is how a
# machine expected to hold the fixture makes the pixel pins non-optional.
# Generate it with: tools/make_reference_content.ps1 -CarOnly
option(KS_REQUIRE_REFERENCE_CAR
    "Fail test_renderer when the reference car bake is absent" OFF)
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
        if(KS_REQUIRE_REFERENCE_CAR)
            target_compile_definitions(test_renderer PRIVATE
                KS_TEST_REQUIRE_REFERENCE_CAR=1)
        endif()
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/differential_test.cpp")
    add_executable(differential_test differential_test.cpp)
    target_link_libraries(differential_test PRIVATE ksengine)
    add_test(NAME differential_test COMMAND differential_test)
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
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackGroundModel.cpp"
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
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackGroundModel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AIController.cpp")
    target_include_directories(ai_race_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(ai_race_test PRIVATE ksengine)
    add_test(NAME ai_race_test COMMAND ai_race_test)
    set_tests_properties(ai_race_test PROPERTIES TIMEOUT 120)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ai_tuning_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AIController.cpp")
    # AiTuning.h is header-only, so the test only compiles the controller it
    # drives (ai_race_test pattern) and links the engine for the file readers.
    add_executable(ai_tuning_test
        ai_tuning_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/MultiCarManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackGroundModel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AIController.cpp")
    target_include_directories(ai_tuning_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(ai_tuning_test PRIVATE ksengine)
    add_test(NAME ai_tuning_test COMMAND ai_tuning_test)
    set_tests_properties(ai_tuning_test PROPERTIES TIMEOUT 60)
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/camera_freelook_test.cpp")
    # controls.json -> camera: freelook e mappatura dei range HMD, compilati come
    # il test del turntable (CameraController vive nel simulatore).
    add_executable(camera_freelook_test camera_freelook_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/CameraController.cpp")
    target_include_directories(camera_freelook_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME camera_freelook_test COMMAND camera_freelook_test)
    set_tests_properties(camera_freelook_test PROPERTIES TIMEOUT 60)
endif()

# Materiali di pista: la mappa categoria (locationmaterial.json) -> preset.
add_executable(location_material_map_test location_material_map_test.cpp)
target_include_directories(location_material_map_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/Graphics"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_test(NAME location_material_map_test COMMAND location_material_map_test)

# Materiali di pista: l'interruttore e l'applicazione dei preset al loader.
add_executable(location_material_apply_test location_material_apply_test.cpp)
target_include_directories(location_material_apply_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/Graphics"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_test(NAME location_material_apply_test COMMAND location_material_apply_test)

# Materiali di pista: il report testuale che un run headless scrive su KS_MATERIALS_REPORT.
add_executable(location_material_report_test location_material_report_test.cpp)
target_include_directories(location_material_report_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/Graphics"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(location_material_report_test PRIVATE ksengine)
add_test(NAME location_material_report_test COMMAND location_material_report_test)

# Tema UI: l'ordine di risoluzione dei theme.json deve essere deterministico.
add_executable(theme_resolution_test theme_resolution_test.cpp)
target_include_directories(theme_resolution_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_test(NAME theme_resolution_test COMMAND theme_resolution_test)

# Materiali di pista: dove si trova system/cfg/locationmaterial.json (cwd, exe, content root).
add_executable(location_document_lookup_test location_document_lookup_test.cpp)
target_include_directories(location_document_lookup_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_test(NAME location_document_lookup_test COMMAND location_document_lookup_test)

# Moduli nativi: tre DLL vere (buona, ABI sbagliata, senza initialize) e il
# loader che le carica, le rifiuta e le scarica.
add_library(ks_test_module SHARED ks_test_module.cpp)
target_include_directories(ks_test_module PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator")
set_target_properties(ks_test_module PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(ks_test_module_stale SHARED ks_test_module_stale.cpp)
target_include_directories(ks_test_module_stale PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator")
set_target_properties(ks_test_module_stale PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(ks_test_module_broken SHARED ks_test_module_broken.cpp)
target_include_directories(ks_test_module_broken PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator")
set_target_properties(ks_test_module_broken PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(module_loader_test module_loader_test.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ModuleLoader.cpp")
target_include_directories(module_loader_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_dependencies(module_loader_test ks_test_module ks_test_module_stale ks_test_module_broken)
add_test(NAME module_loader_test COMMAND module_loader_test)

# Visore: recentre, range in metri e il delta che muove davvero la camera.
add_executable(vr_head_recentre_test vr_head_recentre_test.cpp
    # CameraController lives in the app target, not the library; the camera half
    # of this test compiles it in.
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/CameraController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ControlsConfig.cpp")
target_include_directories(vr_head_recentre_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(vr_head_recentre_test PRIVATE ksengine)
add_test(NAME vr_head_recentre_test COMMAND vr_head_recentre_test)

# Linter della spec: status, test citati che esistono, note senza doppi apici.
add_executable(spec_lint_test spec_lint_test.cpp)
target_include_directories(spec_lint_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(spec_lint_test PRIVATE ksengine)
add_test(NAME spec_lint_test COMMAND spec_lint_test)

# Baseline visiva: firma del frame + confronto con un riferimento
# (tools/visual_baseline.ps1 guida le catture).
add_executable(visual_baseline_test visual_baseline_test.cpp)
target_include_directories(visual_baseline_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(visual_baseline_test PRIVATE ksengine)
add_test(NAME visual_baseline_test COMMAND visual_baseline_test)

# Aderenza per tipo di suolo: il modello che rende la query fisica.
add_executable(surface_grip_test surface_grip_test.cpp)
target_include_directories(surface_grip_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(surface_grip_test PRIVATE ksengine)
add_test(NAME surface_grip_test COMMAND surface_grip_test)

# Il campo d'erba: posizione delle lame dalla query condivisa + mesh.
add_executable(grass_field_test grass_field_test.cpp)
target_include_directories(grass_field_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(grass_field_test PRIVATE ksengine)
add_test(NAME grass_field_test COMMAND grass_field_test)

# Controller VR -> InputManager: il mapping, senza visore.
add_executable(vr_input_mapping_test vr_input_mapping_test.cpp)
target_include_directories(vr_input_mapping_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(vr_input_mapping_test PRIVATE ksengine)
add_test(NAME vr_input_mapping_test COMMAND vr_input_mapping_test)

# Posa testa OpenXR -> frame della camera cockpit (convenzione +Z).
add_executable(vr_head_pose_test vr_head_pose_test.cpp)
target_include_directories(vr_head_pose_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(vr_head_pose_test PRIVATE ksengine)
add_test(NAME vr_head_pose_test COMMAND vr_head_pose_test)

# La query superficie/pavimento condivisa: track limits, ombre ruote ed erba
# leggono lo stesso dato (header puri).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_surface_query_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackSurfaceQuery.h")
    add_executable(track_surface_query_test
        track_surface_query_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RaceSessionManager.cpp")
    target_include_directories(track_surface_query_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(track_surface_query_test PRIVATE ksengine)
    add_test(NAME track_surface_query_test COMMAND track_surface_query_test)
    set_tests_properties(track_surface_query_test PROPERTIES TIMEOUT 60)
endif()

# Spectator view: lo stato su cui la camera costruisce il corpo dell'auto
# seguita (SpectatorMode + LogManager, quindi contro ksengine).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/spectator_view_test.cpp")
    add_executable(spectator_view_test spectator_view_test.cpp)
    target_include_directories(spectator_view_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_link_libraries(spectator_view_test PRIVATE ksengine)
    add_test(NAME spectator_view_test COMMAND spectator_view_test)
    set_tests_properties(spectator_view_test PROPERTIES TIMEOUT 60)
endif()

# Per-node transforms: nome nodo -> ruota, spin dallo slip ratio, posa del
# pneumatico e matrice del nodo (header puri, nessun renderer).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wheel_node_transform_test.cpp")
    add_executable(wheel_node_transform_test wheel_node_transform_test.cpp)
    target_include_directories(wheel_node_transform_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    add_test(NAME wheel_node_transform_test COMMAND wheel_node_transform_test)
    set_tests_properties(wheel_node_transform_test PROPERTIES TIMEOUT 60)
endif()

# Photo mode's camera: l'orbita di camera/FreeCamera.h dentro CameraController
# (seed dove sta la camera, mouse, rotella, pan key).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/photo_camera_test.cpp")
    add_executable(photo_camera_test photo_camera_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/CameraController.cpp")
    target_include_directories(photo_camera_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME photo_camera_test COMMAND photo_camera_test)
    set_tests_properties(photo_camera_test PROPERTIES TIMEOUT 60)
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

# Staged layout guard: the layout commented at the top of
# cmake/KsInstallLayout.cmake promises directories the runtime reads relative to
# the executable (user/, system/ppfilters, system/shaders, ...). A folder the
# staging forgot used to surface only as a warning at start-up (system/ppfilters
# did exactly that), so the promise is checked against the real staging here.
if(TARGET SimulatorApp)
    add_test(NAME staged_layout_test
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=$<TARGET_FILE_DIR:SimulatorApp>
                     -DSRC=${CMAKE_SOURCE_DIR}
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CheckStagedLayout.cmake")
    set_tests_properties(staged_layout_test PROPERTIES TIMEOUT 30)
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

# The KN5 response rules the loader applies (engine/Graphics/MaterialResponse.h):
# ksSpecular -> F0, detailUVMultiplier -> tiling, fresnel -> pane. Header only.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/material_response_test.cpp")
    add_executable(material_response_test material_response_test.cpp)
    target_include_directories(material_response_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(material_response_test PRIVATE ksengine)
    add_test(NAME material_response_test COMMAND material_response_test)
    set_tests_properties(material_response_test PROPERTIES TIMEOUT 60)
endif()

# A room's [AUTO_EXPOSURE] metering (simulator/AutoExposure.h): the metered
# rectangle, the luminance of a readback and the step the loop takes next.
# Header only, so the test needs nothing but the include path.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/auto_exposure_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AutoExposure.h")
    add_executable(auto_exposure_test auto_exposure_test.cpp)
    target_include_directories(auto_exposure_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME auto_exposure_test COMMAND auto_exposure_test)
    set_tests_properties(auto_exposure_test PROPERTIES TIMEOUT 60)
endif()

  # Season detection (simulator/SeasonControl.h): mapping a calendar month to a
  # meteorological season for both hemispheres. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/season_control_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SeasonControl.h")
    add_executable(season_control_test season_control_test.cpp)
    target_include_directories(season_control_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME season_control_test COMMAND season_control_test)
    set_tests_properties(season_control_test PROPERTIES TIMEOUT 60)
  endif()

  # Track-side dynamic lights parser (simulator/TrackDynamicLights.h).
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/track_dynamic_lights_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TrackDynamicLights.h")
    add_executable(track_dynamic_lights_test track_dynamic_lights_test.cpp)
    target_include_directories(track_dynamic_lights_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME track_dynamic_lights_test COMMAND track_dynamic_lights_test)
    set_tests_properties(track_dynamic_lights_test PROPERTIES TIMEOUT 60)
  endif()

  # Grass FX fondazione (simulator/GrassFx.h): parse del blocco [GRASS_FX]
  # dichiarato dal tracciato in data/grass.json + modello vento per lo sway.
  # Header only, come season_control_test sopra.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/grass_fx_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GrassFx.h")
    add_executable(grass_fx_test grass_fx_test.cpp)
    target_include_directories(grass_fx_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME grass_fx_test COMMAND grass_fx_test)
    set_tests_properties(grass_fx_test PROPERTIES TIMEOUT 60)
  endif()

  # Convolution reverb fondazione (simulator/ConvolutionReverb.h): config
  # [CSP_REVERB], loader IR WAV e convoluzione overlap-save verificata contro
  # la convoluzione diretta. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/convolution_reverb_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ConvolutionReverb.h")
    add_executable(convolution_reverb_test convolution_reverb_test.cpp)
    target_include_directories(convolution_reverb_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME convolution_reverb_test COMMAND convolution_reverb_test)
    set_tests_properties(convolution_reverb_test PROPERTIES TIMEOUT 60)
  endif()

  # Occlusione acustica fondazione (simulator/AudioOcclusion.h): raycast
  # ray-AABB, pannello di raggi con bordi morbidi, gain e cutoff del low-pass.
  # Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_occlusion_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AudioOcclusion.h")
    add_executable(audio_occlusion_test audio_occlusion_test.cpp)
    target_include_directories(audio_occlusion_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME audio_occlusion_test COMMAND audio_occlusion_test)
    set_tests_properties(audio_occlusion_test PROPERTIES TIMEOUT 60)
  endif()

  # Digital dashboards fondazione (simulator/DigitalDash.h): parse di
  # digital_instruments.ini + modello puro (LED strip, cifre, barre, spie)
  # alimentato dai canali di telemetria. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/digital_dash_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/DigitalDash.h")
    add_executable(digital_dash_test digital_dash_test.cpp)
    target_include_directories(digital_dash_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME digital_dash_test COMMAND digital_dash_test)
    set_tests_properties(digital_dash_test PROPERTIES TIMEOUT 60)
  endif()

  # Schedulazione luci per ora del giorno (simulator/LightSchedule.h): parse
  # di data/lights.json (sezione "schedule") + fade alba/tramonto. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/light_schedule_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/LightSchedule.h")
    add_executable(light_schedule_test light_schedule_test.cpp)
    target_include_directories(light_schedule_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME light_schedule_test COMMAND light_schedule_test)
    set_tests_properties(light_schedule_test PROPERTIES TIMEOUT 60)
  endif()

  # Render grass FX (simulator/GrassRenderFx.h): tinta/densita'/sway per lama
  # dall'erba dichiarata in data/grass.json. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/grass_render_fx_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GrassRenderFx.h")
    add_executable(grass_render_fx_test grass_render_fx_test.cpp)
    target_include_directories(grass_render_fx_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME grass_render_fx_test COMMAND grass_render_fx_test)
    set_tests_properties(grass_render_fx_test PROPERTIES TIMEOUT 60)
  endif()

  # Seasons texture swap (simulator/SeasonTextureSet.h): risoluzione
  # percorso texture per stagione da data/seasons.json. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/season_texture_set_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SeasonTextureSet.h")
    add_executable(season_texture_set_test season_texture_set_test.cpp)
    target_include_directories(season_texture_set_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME season_texture_set_test COMMAND season_texture_set_test)
    set_tests_properties(season_texture_set_test PROPERTIES TIMEOUT 60)
  endif()

  # AudioSourceFx (simulator/AudioSourceFx.h): catena reverb + occlusione
  # per sorgente audio. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio_source_fx_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AudioSourceFx.h")
    add_executable(audio_source_fx_test audio_source_fx_test.cpp)
    target_include_directories(audio_source_fx_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME audio_source_fx_test COMMAND audio_source_fx_test)
    set_tests_properties(audio_source_fx_test PROPERTIES TIMEOUT 60)
  endif()

  # DigitalDashRenderer (simulator/DigitalDashRenderer.h): lista di
  # primitive di disegno dal cruscotto digitale. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/digital_dash_renderer_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/DigitalDashRenderer.h")
    add_executable(digital_dash_renderer_test digital_dash_renderer_test.cpp)
    target_include_directories(digital_dash_renderer_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME digital_dash_renderer_test COMMAND digital_dash_renderer_test)
    set_tests_properties(digital_dash_renderer_test PROPERTIES TIMEOUT 60)
  endif()

  # Cruscotto digitale dell'auto disegnato (ui/DigitalDashPanel.h): lettura del
  # display che l'auto dichiara in data/data.json, alimentazione dal frame del
  # HUD e collocazione delle primitive nel pannello. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/digital_dash_panel_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/DigitalDashPanel.h")
    add_executable(digital_dash_panel_test digital_dash_panel_test.cpp)
    target_include_directories(digital_dash_panel_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME digital_dash_panel_test COMMAND digital_dash_panel_test)
    set_tests_properties(digital_dash_panel_test PROPERTIES TIMEOUT 60)
  endif()

  # Race control UDP protocol (simulator/RaceControlProtocol.h): serializzazione
  # messaggi di controllo gara. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/race_control_protocol_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RaceControlProtocol.h")
    add_executable(race_control_protocol_test race_control_protocol_test.cpp)
    target_include_directories(race_control_protocol_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME race_control_protocol_test COMMAND race_control_protocol_test)
    set_tests_properties(race_control_protocol_test PROPERTIES TIMEOUT 60)
  endif()

  # Script host (simulator/ScriptHost.h): interfaccia + backend nullo.
  # Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/script_host_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ScriptHost.h")
    add_executable(script_host_test script_host_test.cpp)
    target_include_directories(script_host_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME script_host_test COMMAND script_host_test)
    set_tests_properties(script_host_test PROPERTIES TIMEOUT 60)
  endif()

  # ACP protocol (simulator/AcpProtocol.h): broadcasting su UDP.
  # Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/acp_protocol_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AcpProtocol.h")
    add_executable(acp_protocol_test acp_protocol_test.cpp)
    target_include_directories(acp_protocol_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME acp_protocol_test COMMAND acp_protocol_test)
    set_tests_properties(acp_protocol_test PROPERTIES TIMEOUT 60)
  endif()

  # Driver swap endurance (simulator/DriverSwap.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/driver_swap_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/DriverSwap.h")
    add_executable(driver_swap_test driver_swap_test.cpp)
    target_include_directories(driver_swap_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME driver_swap_test COMMAND driver_swap_test)
    set_tests_properties(driver_swap_test PROPERTIES TIMEOUT 60)
  endif()

  # Skill rating (simulator/SkillRating.h): Elo per piloti. Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/skill_rating_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SkillRating.h")
    add_executable(skill_rating_test skill_rating_test.cpp)
    target_include_directories(skill_rating_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME skill_rating_test COMMAND skill_rating_test)
    set_tests_properties(skill_rating_test PROPERTIES TIMEOUT 60)
  endif()

  # VR stereo (simulator/VrStereo.h): pose HMD, offset oculari, proiezione.
  # Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vr_stereo_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/VrStereo.h")
    add_executable(vr_stereo_test vr_stereo_test.cpp)
    target_include_directories(vr_stereo_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME vr_stereo_test COMMAND vr_stereo_test)
    set_tests_properties(vr_stereo_test PROPERTIES TIMEOUT 60)
  endif()

  # Laser scan track pipeline (simulator/LaserScanTrack.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/laser_scan_track_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/LaserScanTrack.h")
    add_executable(laser_scan_track_test laser_scan_track_test.cpp)
    target_include_directories(laser_scan_track_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME laser_scan_track_test COMMAND laser_scan_track_test)
    set_tests_properties(laser_scan_track_test PROPERTIES TIMEOUT 60)
  endif()

  # Triple monitor (simulator/TripleMonitor.h): FOV split, viewport.
  # Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/triple_monitor_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TripleMonitor.h")
    add_executable(triple_monitor_test triple_monitor_test.cpp)
    target_include_directories(triple_monitor_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME triple_monitor_test COMMAND triple_monitor_test)
    set_tests_properties(triple_monitor_test PROPERTIES TIMEOUT 60)
  endif()

  # Wheel shadowing (simulator/WheelShadow.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wheel_shadow_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/WheelShadow.h")
    add_executable(wheel_shadow_test wheel_shadow_test.cpp)
    target_include_directories(wheel_shadow_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME wheel_shadow_test COMMAND wheel_shadow_test)
    set_tests_properties(wheel_shadow_test PROPERTIES TIMEOUT 60)
  endif()

  # Colored shadows (simulator/ColoredShadows.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/colored_shadows_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ColoredShadows.h")
    add_executable(colored_shadows_test colored_shadows_test.cpp)
    target_include_directories(colored_shadows_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME colored_shadows_test COMMAND colored_shadows_test)
    set_tests_properties(colored_shadows_test PROPERTIES TIMEOUT 60)
  endif()

  # Tyre visuals (simulator/TyreVisuals.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tyre_visuals_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/TyreVisuals.h")
    add_executable(tyre_visuals_test tyre_visuals_test.cpp)
    target_include_directories(tyre_visuals_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME tyre_visuals_test COMMAND tyre_visuals_test)
    set_tests_properties(tyre_visuals_test PROPERTIES TIMEOUT 60)
  endif()

  # Head physics (simulator/HeadPhysics.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/head_physics_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/HeadPhysics.h")
    add_executable(head_physics_test head_physics_test.cpp)
    target_include_directories(head_physics_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME head_physics_test COMMAND head_physics_test)
    set_tests_properties(head_physics_test PROPERTIES TIMEOUT 60)
  endif()

  # Render modes (simulator/RenderModes.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/render_modes_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/RenderModes.h")
    add_executable(render_modes_test render_modes_test.cpp)
    target_include_directories(render_modes_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME render_modes_test COMMAND render_modes_test)
    set_tests_properties(render_modes_test PROPERTIES TIMEOUT 60)
  endif()

  # G-buffer layout (simulator/GBufferLayout.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gbuffer_layout_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/GBufferLayout.h")
    add_executable(gbuffer_layout_test gbuffer_layout_test.cpp)
    target_include_directories(gbuffer_layout_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME gbuffer_layout_test COMMAND gbuffer_layout_test)
    set_tests_properties(gbuffer_layout_test PROPERTIES TIMEOUT 60)
  endif()

  # Camera bookmarks (simulator/CameraBookmarks.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/camera_bookmarks_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/CameraBookmarks.h")
    add_executable(camera_bookmarks_test camera_bookmarks_test.cpp)
    target_include_directories(camera_bookmarks_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME camera_bookmarks_test COMMAND camera_bookmarks_test)
    set_tests_properties(camera_bookmarks_test PROPERTIES TIMEOUT 60)
  endif()

  # Cockpit camera (simulator/CockpitCamera.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cockpit_camera_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/CockpitCamera.h")
    add_executable(cockpit_camera_test cockpit_camera_test.cpp)
    target_include_directories(cockpit_camera_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME cockpit_camera_test COMMAND cockpit_camera_test)
    set_tests_properties(cockpit_camera_test PROPERTIES TIMEOUT 60)
  endif()

  # Weather shaders (simulator/WeatherShaders.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/weather_shaders_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/WeatherShaders.h")
    add_executable(weather_shaders_test weather_shaders_test.cpp)
    target_include_directories(weather_shaders_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME weather_shaders_test COMMAND weather_shaders_test)
    set_tests_properties(weather_shaders_test PROPERTIES TIMEOUT 60)
  endif()

  # Windscreen effects (simulator/WindscreenEffects.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/windscreen_effects_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/WindscreenEffects.h")
    add_executable(windscreen_effects_test windscreen_effects_test.cpp)
    target_include_directories(windscreen_effects_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME windscreen_effects_test COMMAND windscreen_effects_test)
    set_tests_properties(windscreen_effects_test PROPERTIES TIMEOUT 60)
  endif()

  # Post chain (simulator/PostChain.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/post_chain_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/PostChain.h")
    add_executable(post_chain_test post_chain_test.cpp)
    target_include_directories(post_chain_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME post_chain_test COMMAND post_chain_test)
    set_tests_properties(post_chain_test PROPERTIES TIMEOUT 60)
  endif()

  # Material mapping (simulator/MaterialMapping.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/material_mapping_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/MaterialMapping.h")
    add_executable(material_mapping_test material_mapping_test.cpp)
    target_include_directories(material_mapping_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME material_mapping_test COMMAND material_mapping_test)
    set_tests_properties(material_mapping_test PROPERTIES TIMEOUT 60)
  endif()

  # Free camera (simulator/FreeCamera.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/free_camera_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/FreeCamera.h")
    add_executable(free_camera_test free_camera_test.cpp)
    target_include_directories(free_camera_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME free_camera_test COMMAND free_camera_test)
    set_tests_properties(free_camera_test PROPERTIES TIMEOUT 60)
  endif()

  # Chase camera (simulator/ChaseCamera.h). Header only.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/chase_camera_test.cpp" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ChaseCamera.h")
    add_executable(chase_camera_test chase_camera_test.cpp)
    target_include_directories(chase_camera_test PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME chase_camera_test COMMAND chase_camera_test)
    set_tests_properties(chase_camera_test PROPERTIES TIMEOUT 60)
  endif()
# deferred lighting pass reads, and the falloff/cone curves it mirrors.
# Header only, so the test needs nothing but the include paths.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local_lights_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/LocalLights.h")
    add_executable(local_lights_test local_lights_test.cpp)
    target_include_directories(local_lights_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME local_lights_test COMMAND local_lights_test)
    set_tests_properties(local_lights_test PROPERTIES TIMEOUT 60)
endif()

# Local light shadows (simulator/LocalLightShadows.h): the cube-face basis and
# the face rule the lighting shader mirrors, the spot/cube light-space matrices,
# and the atlas layer assignment. Header only, so nothing but include paths.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local_shadows_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/LocalLightShadows.h")
    add_executable(local_shadows_test local_shadows_test.cpp)
    target_include_directories(local_shadows_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME local_shadows_test COMMAND local_shadows_test)
    set_tests_properties(local_shadows_test PROPERTIES TIMEOUT 60)
endif()

# A room's [COLOR] block (simulator/ShowroomColor.h): the black-body pair, the
# white-balance gain that keeps a grey grey, and the filter it multiplies with.
# Header only, so the test needs nothing but the include path.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/showroom_color_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ShowroomColor.h")
    add_executable(showroom_color_test showroom_color_test.cpp)
    target_include_directories(showroom_color_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME showroom_color_test COMMAND showroom_color_test)
    set_tests_properties(showroom_color_test PROPERTIES TIMEOUT 60)
endif()

# The console's calibration strip (simulator/ui/ConsoleOverlay.h): where a drag
# lands inside a row, including the ends and the step snapping. Header only, so
# the test needs no renderer, window or mouse.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/console_slider_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/ConsoleOverlay.h")
    add_executable(console_slider_test console_slider_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ui/ConsoleOverlay.cpp")
    target_include_directories(console_slider_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME console_slider_test COMMAND console_slider_test)
    set_tests_properties(console_slider_test PROPERTIES TIMEOUT 60)
endif()

# The car's own setup document (simulator/AcSetup.h): the rows setup.ini
# declares, the grid the screen draws them on, the values the physics files seed
# them with, and the .setup they round-trip through. Header only.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ac_setup_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AcSetup.h")
    add_executable(ac_setup_test ac_setup_test.cpp)
    target_include_directories(ac_setup_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME ac_setup_test COMMAND ac_setup_test)
    set_tests_properties(ac_setup_test PROPERTIES TIMEOUT 60)
endif()

# The showroom's part inspector (simulator/ShowroomInspector.h): the
# screen-space pick on a mesh's triangles and the parameter sheet it fills.
# Header only, so nothing but the include paths is needed.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/showroom_inspector_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ShowroomInspector.h")
    add_executable(showroom_inspector_test showroom_inspector_test.cpp)
    target_include_directories(showroom_inspector_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME showroom_inspector_test COMMAND showroom_inspector_test)
    set_tests_properties(showroom_inspector_test PROPERTIES TIMEOUT 60)
endif()

# The car's material entries in its own model.json (simulator/ModelMaterials.h):
# reading the shaderSettings block, editing one entry and writing the document
# back with every other key intact. Header only plus the Json implementation.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/model_materials_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ModelMaterials.h")
    add_executable(model_materials_test model_materials_test.cpp)
    target_include_directories(model_materials_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(model_materials_test PRIVATE ksengine)
    add_test(NAME model_materials_test COMMAND model_materials_test)
    set_tests_properties(model_materials_test PROPERTIES TIMEOUT 60)
endif()

# The car material database (engine/Graphics/MaterialDatabase.h): the types it
# carries, the look each one states and the variant a material name selects.
# Needs the shipped system/cfg/carmaterial.json, hence KS_SOURCE_DIR.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/material_database_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine/Graphics/MaterialDatabase.h")
    add_executable(material_database_test material_database_test.cpp)
    target_include_directories(material_database_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_compile_definitions(material_database_test PRIVATE
        KS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(material_database_test PRIVATE ksengine)
    add_test(NAME material_database_test COMMAND material_database_test
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(material_database_test PROPERTIES TIMEOUT 60)
endif()

# Vulkan validation: a bounded simulator run must log no VUID. The check is a
# CMake script, like staged_layout_test, so no shell is involved.
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CheckNoValidationErrors.cmake")
    add_test(NAME validation_errors_test
        COMMAND ${CMAKE_COMMAND}
            -DROOT=$<TARGET_FILE_DIR:SimulatorApp>
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckNoValidationErrors.cmake")
    set_tests_properties(validation_errors_test PROPERTIES TIMEOUT 120)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/replay_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ReplayRecorder.cpp")
    add_executable(replay_test
        replay_test.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ReplayRecorder.cpp")
    target_include_directories(replay_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    target_link_libraries(replay_test PRIVATE ksengine)
    add_test(NAME replay_test COMMAND replay_test)
endif()

# The showroom's display state while a session keeps the hall on screen
# (simulator/ShowroomSession.h): sun, lamps, haze, stop, IBL and [COLOR] grade
# applied only while the hall is the scene and handed back to a track. Header
# only, so the test needs nothing but the include path — no Vulkan, no content.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/showroom_session_test.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/ShowroomSession.h")
    add_executable(showroom_session_test showroom_session_test.cpp)
    target_include_directories(showroom_session_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    add_test(NAME showroom_session_test COMMAND showroom_session_test)
    set_tests_properties(showroom_session_test PROPERTIES TIMEOUT 60)
endif()
