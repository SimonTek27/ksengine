# Qt-free engine unit tests — CTest only (no Qt, no QTest, no kseditor_lib).
# Split body for tooling limits; order matters.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_p1.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_p2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_p3.cmake")
