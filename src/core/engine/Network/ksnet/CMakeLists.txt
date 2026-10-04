# ksnet — minimal reliable-UDP multiplayer transport (roadmap 3.1).
# Provides the yojimbo-compatible API surface used by src/simulator
# (NetworkConfig.h / NetworkLowLevel.cpp) via the public header ksnet.h.
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
