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

# ksengine.dll must sit next to every executable that loads it: ctest starts
# a test from its own directory, and the Windows loader only searches the
# directory of the exe (plus PATH). Point the test binaries at bin/<config>,
# where ksengine.dll and ksim.exe already are. Static builds are left alone —
# nothing extra has to be found there.
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
