# Qt-free engine unit tests — CTest only (no Qt, no QTest, no kseditor_lib).
# Split body for tooling limits; order matters.
cmake_minimum_required(VERSION 3.16)
# SPIR-V shaders for test_renderer: it reads ${CMAKE_BINARY_DIR}/shaders through
# KS_TEST_SHADER_DIR (set in CMakeLists_p2.cmake), and KsShaders.cmake is the one
# place that produces that set. The root qt-free build only creates ks_shaders for
# SimulatorApp, so a tests-only configure left the directory empty and the
# renderer test died on the missing native_forward.{vert,frag}.spv. Creating it
# here also makes the if(TARGET ks_shaders) in CMakeLists_p2.cmake mean something.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/KsShaders.cmake")
# The simulator fragment creates the same target when KSIMULATOR_QT_FREE is
# on, and add_custom_target refuses a duplicate name.
if(NOT TARGET ks_shaders)
    ks_add_shader_target(ks_shaders "${CMAKE_SOURCE_DIR}/src")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_p1.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_p2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_p3.cmake")
