# 3D Printing Module
# This module is included automatically via the main CMakeLists.txt GLOB_RECURSE

# Module-specific compile definitions
target_compile_definitions(kslib PRIVATE HAS_3DPRINT=1)

# Include directory for this module
target_include_directories(kslib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)