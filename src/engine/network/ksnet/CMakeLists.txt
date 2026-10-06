# ksnet — KS Engine reliable-UDP multiplayer transport.
# Public header ksnet.h is consumed by src/simulator (NetworkConfig / NetworkLowLevel).
cmake_minimum_required(VERSION 3.16)

add_library(ksnet STATIC
    ksnet.cpp
    ksnet.h
)

target_include_directories(ksnet PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(ksnet PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    FOLDER "engine/network"
)

if(WIN32)
    target_link_libraries(ksnet PUBLIC ws2_32)
endif()

set_target_properties(ksnet PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Compatibility alias for older CMake that still link Yojimbo::yojimbo
if(NOT TARGET Yojimbo::yojimbo)
    add_library(Yojimbo::yojimbo ALIAS ksnet)
endif()
