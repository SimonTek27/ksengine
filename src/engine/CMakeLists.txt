# KSEngine — generic open-source sim core (Qt-free)
cmake_minimum_required(VERSION 3.16)
project(ksengine VERSION 0.90 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(KSENGINE_QT_FREE "Build without Qt" ON)

# Explicitly refuse Qt for this target when Qt-free mode is on.
if(KSENGINE_QT_FREE)
  if(TARGET Qt6::Core OR TARGET Qt5::Core)
    message(WARNING "ksengine: Qt targets exist in the project but ksengine will not link them")
  endif()
endif()

set(KSENGINE_QT_FREE_DIRS
  Math physics devices FileFormat archive Config network material sim Graphics
  Audio assets sys AI animation hwril mesh Tools Scripting Video terrain
)
set(KSENGINE_SOURCES "")
foreach(_dir ${KSENGINE_QT_FREE_DIRS})
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_dir}")
    file(GLOB_RECURSE _dir_sources
      "${_dir}/*.h" "${_dir}/*.cpp" "${_dir}/*.c"
      "${_dir}/**/*.h" "${_dir}/**/*.cpp")
    list(APPEND KSENGINE_SOURCES ${_dir_sources})
  endif()
endforeach()

foreach(_pat
    "physics/weather/"
    "VehiclePhysics"
    "VehiclePhysicsModels"
    "TireCurveEditor"
    "PhysicsSimulations"
    "TrackPhysics"
    "PhysicsValidator"
    "devices/3dprint/"
    "devices/scanners/"
    "SimulatorApp"
    "GameMenuOverlay"
    "README.md"
    "StreamlineFunctions"
    "ksAssettocorsasndeventdefs"
    "CspConfigParser"
    "ACGuidsParser"
    "FSPROImporter"
    "FSPROExporter"
    "P3DModelLoader"
    "PAATextureConverter"
    "SevenZip"
    "VideoModesFunctions"
    "Blueprint/"
    "ScriptDebugger"
    "ShapeKeyModifier"
    "AssetPreviewWidget"
    # Qt/QML bridges + Qt audio helpers: editor-only, now live in
    # src/sdk/kseditor/qmlbridges/ — these patterns stay as a safety net so
    # nothing Qt-flavoured can ever re-enter the Qt-free engine.
    "[Qq][Mm][Ll]"
    "AssetsLibraryQmlBridge"
    "AudioQMLBridge"
    "AudioWaveformBridge"
    "MeshLoaderQML"
    "ACEContentQMLBridge"
    "ContentQMLBridge"
    "CspConfigQmlBridge"
    "Audio/NoiseReducer"
    "Audio/PeakMeter"
    "Audio/Studio"
    # Stale .cpp left behind by the qt-free header rewrites (headers are fully
    # inline / nothing references these classes): cannot compile as-is.
    "physics/EngineSimulator\\."
    "physics/DriverSimulator\\."
    "physics/StrategySimulator\\."
    "physics/ACModelManager\\."
    "physics/ErsDrsController\\."
    "physics/ChassisSimulator\\."
    # Dead code that never made it into the qt-free rewrite: nothing includes
    # BrakeWearSystem.h, so nothing can ever call it. It is kept out of the
    # build on purpose because it redefines ks::physics::BrakeThermalConfig
    # and ks::physics::BrakeThermalState with a DIFFERENT layout than
    # BrakeThermalModel.h. Both are inline/COMDAT types, so the two default
    # constructors share one mangled name (?..BrakeThermalConfig..) and the
    # linker silently picks one of them for the whole module: with the file
    # linked in, every BrakeThermalModel got the other definition's defaults
    # (std::clamp "invalid bounds" assert in the physics tests). A static
    # ksengine.lib never showed it because an unreferenced object file is not
    # pulled in; a shared library links every listed object.
    "physics/BrakeWearSystem\\."
    # Orphan source with no header of its own.
    "assets/KsPathDetector\\."
    "\\.ui$"
    "\\.qrc$"
)
  list(FILTER KSENGINE_SOURCES EXCLUDE REGEX "${_pat}")
endforeach()

list(APPEND KSENGINE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/EngineModule.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/Engine.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/KsQtFreeGuard.h")

# ============================================================================
# Vendored Lua 5.4 (external/lua), preferred over a system install so a fresh
# clone gets working scripting without the machine needing Lua present.
#
# It gets its own target rather than being folded into ksengine: the engine
# puts src/engine/sys on the include path, and on a case-insensitive
# filesystem lstate.h's `#include <signal.h>` would resolve to the engine's
# Signal.h (a C++ header) instead of the CRT's.  Keeping Lua's include path
# down to just its own directory keeps that collision out of the picture.
# The two interpreter entry points are filtered out so this stays a library.
# ============================================================================
set(KSENGINE_LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/lua")
set(_ksengine_lua_sources "")
set(KSENGINE_HAS_VENDORED_LUA FALSE)
if(EXISTS "${KSENGINE_LUA_DIR}/lua.h")
	file(GLOB _ksengine_lua_sources "${KSENGINE_LUA_DIR}/*.c")
	list(FILTER _ksengine_lua_sources EXCLUDE REGEX "[/\\\\](lua|luac)\\.c$")
	if(_ksengine_lua_sources)
		add_library(ksengine_lua STATIC ${_ksengine_lua_sources})
		target_include_directories(ksengine_lua PUBLIC "${KSENGINE_LUA_DIR}")
		set_target_properties(ksengine_lua PROPERTIES FOLDER "engine/external")
		if(MSVC)
			set_source_files_properties(${_ksengine_lua_sources} PROPERTIES
				COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
		endif()
		set(KSENGINE_HAS_VENDORED_LUA TRUE)
	endif()
endif()

# ============================================================================
# Shared or static. The shipped layout runs ksim.exe / kssimserver.exe
# against ksengine.dll (see cmake/KsInstallLayout.cmake), so shared is the
# default; -DKSENGINE_SHARED=OFF restores the historical static archive when
# some consumer wants every engine object linked in directly.
#
# Sharing only works together with the explicit export API: KsExport.h turns
# KSENGINE_BUILDING_DLL (here, PRIVATE) into dllexport and KSENGINE_USE_DLL
# (here, PUBLIC, so every consumer inherits it) into dllimport. Without
# KSENGINE_SHARED the macros expand to nothing and the same headers build a
# plain static library.
# ============================================================================
option(KSENGINE_SHARED "Build ksengine as ksengine.dll (shared)" ON)

if(KSENGINE_SHARED)
	add_library(ksengine SHARED ${KSENGINE_SOURCES})
else()
	add_library(ksengine STATIC ${KSENGINE_SOURCES})
endif()

if(KSENGINE_SHARED)
	# dllexport while compiling the DLL, dllimport for everything linking it.
	target_compile_definitions(ksengine
		PRIVATE KSENGINE_BUILDING_DLL
		PUBLIC   KSENGINE_USE_DLL)
	# ksengine.dll is a runtime dependency of every executable in this build:
	# keep it beside them (bin/<config>, where ksim.exe and kssimserver.exe
	# land too), so the loader finds it without PATH tricks.
	set_target_properties(ksengine PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# ============================================================================
# Self-sufficiency — everything ksengine needs to build standalone
# (cmake -S src/engine -B out) is declared here, not inherited from the
# parent project.
# ============================================================================

# C++17 is part of the public interface: engine headers use it.
target_compile_features(ksengine PUBLIC cxx_std_17)

# 1. windows.h injects min/max macros; engine headers use std::min/std::max in
#    inline functions, so every consumer of ksengine needs NOMINMAX too.
if(MSVC)
	target_compile_definitions(ksengine PUBLIC NOMINMAX)
	target_compile_options(ksengine PRIVATE /bigobj /permissive- /FS)
endif()

# 2. Vendored third-party headers.
#    "external" itself must be on the path so <openxr/openxr.h> resolves
#    (the files live flat in external/openxr/, not external/openxr/openxr/).
target_include_directories(ksengine PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/external"
	"${CMAKE_CURRENT_SOURCE_DIR}/external/stb"
	"${CMAKE_CURRENT_SOURCE_DIR}/external/mikktspace"
)

# 2b. Lua. The vendored copy wins over a system install (both would otherwise
#     race to define HAS_LUA, and mixing two lua.h would be worse than either).
#     PUBLIC, so ksengine's consumers see lua.h and resolve the symbols out of
#     the transitive static library.
#     HAS_LUA is also a CMake variable the parent project sets before
#     add_subdirectory(), so the find_package fallback only runs for a
#     standalone `cmake -S src/engine` build.
if(KSENGINE_HAS_VENDORED_LUA)
	set(HAS_LUA TRUE)
	target_link_libraries(ksengine PUBLIC ksengine_lua)
endif()
if(NOT DEFINED HAS_LUA)
	find_package(Lua QUIET)
	if(Lua_FOUND)
		set(HAS_LUA TRUE)
		target_include_directories(ksengine PUBLIC ${LUA_INCLUDE_DIR})
		if(LUA_LIBRARIES)
			target_link_libraries(ksengine PUBLIC ${LUA_LIBRARIES})
		endif()
	else()
		set(HAS_LUA FALSE)
	endif()
endif()
if(HAS_LUA)
	target_compile_definitions(ksengine PUBLIC HAS_LUA=1)
else()
	target_compile_definitions(ksengine PUBLIC HAS_LUA=0)
endif()

# 3. Vulkan headers only. The loader is dlopen'ed at runtime by
#    Graphics/VulkanFunctions.h (LoadLibraryA("vulkan-1.dll")), so there is no
#    link-time dependency on vulkan-1.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
	target_include_directories(ksengine PUBLIC ${Vulkan_INCLUDE_DIRS})
elseif(DEFINED ENV{VULKAN_SDK})
	target_include_directories(ksengine PUBLIC "$ENV{VULKAN_SDK}/Include")
endif()
target_compile_definitions(ksengine PUBLIC HAS_VULKAN=$<BOOL:${Vulkan_FOUND}>)

# 3b. A shared ksengine has to resolve its own imports. Nearly all engine
#     code reaches Vulkan through the table loaded above, but two TUs
#     (sim/GpuProfiler.cpp, devices/XrViewportRenderer.cpp) call vk*
#     directly; with a static archive those references were satisfied by the
#     *final executable's* link against ${Vulkan_LIBRARIES}, but ksengine.dll
#     now links on its own. Take the import library and delay-bind it: the
#     DLL keeps loading on a machine without Vulkan and vulkan-1.dll arrives
#     on the first vk* call, i.e. when the renderer is actually used. A plain
#     (non-delayed) import would put vulkan-1.dll into the DLL's import
#     table and every test executable would stop starting without it.
if(KSENGINE_SHARED AND Vulkan_FOUND AND Vulkan_LIBRARIES)
	target_link_libraries(ksengine PRIVATE ${Vulkan_LIBRARIES})
	if(MSVC)
		target_link_options(ksengine PRIVATE "/DELAYLOAD:vulkan-1.dll")
		target_link_libraries(ksengine PRIVATE delayimp)
	endif()
endif()

# 4. Platform libraries pulled in by engine sources. Each source also carries
#    a #pragma comment(lib, ...), but declaring them on the target means a
#    consumer gets them even when those TUs are not linked in.
if(WIN32)
	target_link_libraries(ksengine PUBLIC
		ws2_32      # devices/simracing/SimucubeFFB.cpp
		dinput8     # devices/{DirectInputJoystick, simracing/*}
		dxguid
		xinput      # devices/xinput/XInputDevice
		ole32       # Audio/{AudioDevices,WASAPIInput}, COM
		setupapi    # devices/simracing/MozaFFB.cpp
		hid         # devices/simracing/MozaFFB.cpp
	)
endif()

target_include_directories(ksengine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/physics
  ${CMAKE_CURRENT_SOURCE_DIR}/devices
  ${CMAKE_CURRENT_SOURCE_DIR}/sim
  ${CMAKE_CURRENT_SOURCE_DIR}/Graphics
  ${CMAKE_CURRENT_SOURCE_DIR}/Audio
  ${CMAKE_CURRENT_SOURCE_DIR}/assets
  ${CMAKE_CURRENT_SOURCE_DIR}/sys
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh
  ${CMAKE_CURRENT_SOURCE_DIR}/Tools
  ${CMAKE_CURRENT_SOURCE_DIR}/Math)

if(KSENGINE_QT_FREE)
  target_compile_definitions(ksengine PUBLIC KSENGINE_QT_FREE=1)
  # Do not link Qt under any name.
  set_property(TARGET ksengine PROPERTY AUTOMOC OFF)
  set_property(TARGET ksengine PROPERTY AUTOUIC OFF)
  set_property(TARGET ksengine PROPERTY AUTORCC OFF)
endif()

if(UNIX AND NOT APPLE)
	target_link_libraries(ksengine PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()

message(STATUS "ksengine: generic sim core (KSENGINE_QT_FREE=${KSENGINE_QT_FREE})")
