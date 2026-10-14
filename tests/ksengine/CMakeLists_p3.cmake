# Parity / roadmap special tests (part 3) + E1/E9 c_api_test.

if(TARGET ks_server)
    add_test(NAME ks_server_smoke
             COMMAND ks_server --laps 1 --track-length 150 --countdown 1
                     --fast --duration 60 --status 0)
endif()

if(TARGET SimulatorApp)
    add_test(NAME sim_headless_smoke COMMAND SimulatorApp)
    set_tests_properties(sim_headless_smoke PROPERTIES
        ENVIRONMENT "KS_HEADLESS=1;KS_HEADLESS_SECONDS=2"
        TIMEOUT 60)
endif()

if(TARGET ksengine_c AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scene_bridge_test.cpp")
    add_executable(scene_bridge_test scene_bridge_test.cpp)
    target_link_libraries(scene_bridge_test PRIVATE ksengine_c)
    add_test(NAME scene_bridge_test COMMAND scene_bridge_test)
endif()

# Roadmap E1/E9 — C API smoke (create/step/profiler/load guards).
if(TARGET ksengine_c AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/c_api_test.cpp")
    add_executable(c_api_test c_api_test.cpp)
    target_link_libraries(c_api_test PRIVATE ksengine_c)
    add_test(NAME c_api_test COMMAND c_api_test)
endif()

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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_audio_3d.cpp")
    add_executable(test_audio_3d
        test_audio_3d.cpp
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SimulatorAudio.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/WASAPIOutput.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AudioMixer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/SoundsIniParser.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator/AudioBankManager.cpp")
    target_include_directories(test_audio_3d PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/simulator"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine")
    target_link_libraries(test_audio_3d PRIVATE ksengine)
    add_test(NAME test_audio_3d COMMAND test_audio_3d)
endif()
