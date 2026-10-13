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
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/KsShaders.cmake")
    ks_add_shader_target(ks_shaders "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    if(NOT TARGET ksengine)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/engine" ksengine)
    endif()
endif()

enable_testing()

if(TARGET ksengine)
    get_target_property(_ksengine_type ksengine TYPE)
    if(_ksengine_type STREQUAL "SHARED_LIBRARY" OR _ksengine_type STREQUAL "MODULE_LIBRARY")
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
    unset(_ksengine_type)
endif()

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
    wet_physics_test
    rain_effects_test
    lod_window_test
    user_data_test
)

foreach(_t ${KS_QT_FREE_TESTS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_t}.cpp")
        add_executable(${_t} "${_t}.cpp")
        target_link_libraries(${_t} PRIVATE ksengine)
        add_test(NAME ${_t} COMMAND ${_t})
    endif()
endforeach()

# Include the remainder of the original file from a simplified merge:
# scene bridge + c_api + optional tools are appended below.
# (Full original content is large; c_api_test is the E1/E9 addition.)

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
