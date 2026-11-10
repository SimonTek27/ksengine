# ksenginehw — hardware backends for ksengine (controls, audio endpoints, video
# runtime). The general engine (ksengine) does not talk to devices, WASAPI or
# the Vulkan loader itself: it calls the facade in HardwareBackend.h, which is
# implemented here. Keeping the backends in their own target is what makes that
# direction enforceable — a stray #include of a device header in the engine
# would have to link ksenginehw to build.
#
# The sources still live in their historical folders (src/engine/devices,
# src/engine/Audio, src/engine/Video) so every existing include keeps working;
# ksengine excludes exactly these files from its own glob and links this
# library, so each one is compiled once, here.

set(KSENGINEHW_SOURCES "")

# --- controls -------------------------------------------------------------
# DirectInput, XInput, force feedback and the VR/OpenXR device layer.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../engine/devices")
  file(GLOB_RECURSE _kshw_controls
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/devices/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/devices/*.cpp")
  list(APPEND KSENGINEHW_SOURCES ${_kshw_controls})
endif()

# --- audio endpoints ------------------------------------------------------
# Device enumeration and capture are Windows APIs (WASAPI); the rest of the
# audio module (DSP, banks, mixing) stays in the engine.
foreach(_kshw_audio AudioDevices AudioUtil WASAPIInput)
  foreach(_ext .cpp .h)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../engine/Audio/${_kshw_audio}${_ext}")
      list(APPEND KSENGINEHW_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/Audio/${_kshw_audio}${_ext}")
    endif()
  endforeach()
endforeach()
if(NOT WIN32)
  # WASAPI is the only capture backend; a POSIX host drops it, like the engine
  # glob does for the same three files.
  list(FILTER KSENGINEHW_SOURCES EXCLUDE REGEX "Audio/(AudioDevices|AudioUtil|WASAPIInput)\\.")
endif()

# --- video ----------------------------------------------------------------
# The encoder (offline frames -> file) is hardware-facing I/O, not scene code.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../engine/Video")
  file(GLOB _kshw_video "${CMAKE_CURRENT_SOURCE_DIR}/../engine/Video/*.cpp")
  list(APPEND KSENGINEHW_SOURCES ${_kshw_video})
endif()

# --- facade ---------------------------------------------------------------
list(APPEND KSENGINEHW_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/HardwareBackend.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/HardwareBackend.h")

add_library(ksenginehw STATIC ${KSENGINEHW_SOURCES})
set_target_properties(ksenginehw PROPERTIES FOLDER "engine/hw" POSITION_INDEPENDENT_CODE ON)

# The archive is linked *into* ksengine.dll, so its objects must see the same
# export macro the engine itself compiles with: a KSENGINE_API class defined
# here has to leave the DLL as dllexport, exactly as before the split.
target_compile_features(ksenginehw PUBLIC cxx_std_17)
target_include_directories(ksenginehw PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/../engine"
  "${CMAKE_CURRENT_SOURCE_DIR}/../engine/external"
  "${CMAKE_CURRENT_SOURCE_DIR}/../engine/external/stb")
if(KSENGINE_SHARED)
  # The archive lands inside ksengine.dll, so its objects compile with the same
  # export macro the engine uses: a KSENGINE_API class defined here still leaves
  # the DLL as dllexport, exactly as before the split. KSENGINE_USE_DLL is not
  # set here - that is for consumers, and the two must not fight over a header.
  target_compile_definitions(ksenginehw PRIVATE KSENGINE_BUILDING_DLL)
endif()
if(MSVC)
  target_compile_definitions(ksenginehw PUBLIC NOMINMAX)
  target_compile_options(ksenginehw PRIVATE /bigobj /permissive- /FS)
endif()
if(KSENGINE_QT_FREE)
  target_compile_definitions(ksenginehw PRIVATE KSENGINE_QT_FREE=1)
endif()

# Platform libraries the backends call directly. dinput8/xinput/hid/setupapi are
# the control backends, ole32 the WASAPI COM entry points, ws2_32 and winmm
# come from the device and encoder code.
if(WIN32)
  target_link_libraries(ksenginehw PUBLIC
    dinput8 dxguid xinput hid setupapi ole32 ws2_32 winmm
    mfplat mfreadwrite mfuuid)
endif()

if(Vulkan_FOUND AND Vulkan_LIBRARIES)
  target_include_directories(ksenginehw PUBLIC ${Vulkan_INCLUDE_DIRS})
  target_link_libraries(ksenginehw PUBLIC ${Vulkan_LIBRARIES})
endif()

message(STATUS "ksenginehw: hardware backends (controls, audio endpoints, video)")
