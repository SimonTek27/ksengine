cmake_minimum_required(VERSION 3.16)

project(kseditor
	VERSION 0.90
	LANGUAGES C CXX
)

cmake_policy(VERSION 3.16)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add MSVC specific flags required by Qt6
if(MSVC)
	add_compile_options(/permissive-)
	# Prevent Windows.h from defining min/max macros
	add_compile_definitions(NOMINMAX)
	# Force synchronous PDB writes for parallel compilation
	add_compile_options(/FS)
	# Allow large object files
	add_compile_options(/bigobj)
endif()

# Qt-free build switch (KSENGINE_QT_FREE / KSIMULATOR_QT_FREE) - must come
# first so no Qt lookup or moc/uic/rcc happens in Qt-free configurations.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeLists_root_qtfree_hook.cmake")

# Auto-detect Qt6 path from environment or cache
if(NOT KS_QT_FREE_BUILD AND NOT QT6_PATH)
	if(DEFINED ENV{QTDIR})
		set(QT6_PATH "$ENV{QTDIR}")
	elseif(DEFINED ENV{QT6_DIR})
		set(QT6_PATH "$ENV{QT6_DIR}")
	elseif(MSVC)
		set(QT6_PATH "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt6 path")
	else()
		set(QT6_PATH "C:/Qt/6.11.1/mingw_64" CACHE PATH "Qt6 path")
	endif()
endif()
if(QT6_PATH)
	list(PREPEND CMAKE_PREFIX_PATH "${QT6_PATH}")
endif()

# Auto-detect vcpkg path (append after Qt)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/installed/x64-windows")
	list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/vcpkg/installed/x64-windows")
endif()

set(QML_IMPORT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/qml CACHE PATH "QML import paths")

if(NOT KS_QT_FREE_BUILD)
	find_package(Qt6 COMPONENTS Core Gui Widgets Network Qml Quick Quick3D QuickControls2 QuickWidgets Multimedia OpenGL Sql Charts WebSockets Svg DataVisualization QUIET)
	if(NOT Qt6Svg_FOUND)
		message(STATUS "Qt6 Svg not found, PaintVector SVG features will be disabled")
	endif()
	if(NOT Qt6DataVisualization_FOUND)
		message(STATUS "Qt6 DataVisualization not found, CfdWidget will be disabled")
	endif()
endif()
find_package(Vulkan QUIET)

# ============================================================================
# Eigen 3.4+ (header-only linear algebra)
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/eigen/Eigen/Core")
	set(HAS_EIGEN TRUE)
	message(STATUS "Eigen found: ${CMAKE_CURRENT_SOURCE_DIR}/external/eigen")
	add_compile_definitions(HAS_EIGEN=1)
else()
	set(HAS_EIGEN FALSE)
	message(STATUS "Eigen not found in external/eigen")
	add_compile_definitions(HAS_EIGEN=0)
endif()

# ============================================================================
# mikktspace 1.0+ (tangent space computation)
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/mikktspace/mikktspace.c")
	set(HAS_MIKKTSPACE TRUE)
	message(STATUS "mikktspace found: ${CMAKE_CURRENT_SOURCE_DIR}/external/mikktspace")
	add_library(mikktspace STATIC
		"${CMAKE_CURRENT_SOURCE_DIR}/external/mikktspace/mikktspace.c"
	)
	target_include_directories(mikktspace PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/external/mikktspace"
	)
	set_target_properties(mikktspace PROPERTIES
		C_STANDARD 11
		AUTOMOC OFF
	)
	add_compile_definitions(HAS_MIKKTSPACE=1)
else()
	set(HAS_MIKKTSPACE FALSE)
	message(STATUS "mikktspace not found in external/mikktspace")
	add_compile_definitions(HAS_MIKKTSPACE=0)
endif()

# ============================================================================
# Bullet Physics 3.25+ - ENABLED for driving simulator
# ============================================================================
find_package(Bullet QUIET)
if(BULLET_FOUND OR TARGET BulletDynamics)
	set(HAS_BULLET TRUE)
	message(STATUS "Bullet Physics enabled for driving simulator")
	add_compile_definitions(HAS_BULLET=1)
else()
	set(HAS_BULLET FALSE)
	message(STATUS "Bullet Physics NOT found - some physics features disabled")
	add_compile_definitions(HAS_BULLET=0)
endif()

# ============================================================================
# libigl 2.5+ (header-only geometry processing)
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/libigl/include/igl/boundary_loop.h")
	set(HAS_LIBIGL TRUE)
	message(STATUS "libigl found: ${CMAKE_CURRENT_SOURCE_DIR}/external/libigl")
	add_compile_definitions(HAS_LIBIGL=1)
else()
	set(HAS_LIBIGL FALSE)
	message(STATUS "libigl not found in external/libigl")
	add_compile_definitions(HAS_LIBIGL=0)
endif()

# ============================================================================
# Optional Scripting Languages
# ============================================================================
# The vendored copy in src/engine/external/lua wins over a system install:
# it means a fresh clone builds scripting with no extra package, and it keeps
# every target compiling against exactly one lua.h. A system Lua remains the
# fallback when the vendored tree is absent.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/lua/lua.h")
	set(HAS_LUA TRUE)
	message(STATUS "Lua found: vendored copy in src/engine/external/lua")
	add_compile_definitions(HAS_LUA=1)
else()
	find_package(Lua QUIET)
	if(Lua_FOUND)
		set(HAS_LUA TRUE)
		message(STATUS "Lua found: ${LUA_VERSION}")
		add_compile_definitions(HAS_LUA=1)
	else()
		set(HAS_LUA FALSE)
		message(STATUS "Lua not found")
		add_compile_definitions(HAS_LUA=0)
	endif()
endif()

find_package(Python3 COMPONENTS Development QUIET)
if(Python3_FOUND)
	set(HAS_PYTHON TRUE)
	message(STATUS "Python found: ${Python3_VERSION}")
	add_compile_definitions(HAS_PYTHON=1)
else()
	set(HAS_PYTHON FALSE)
	message(STATUS "Python not found")
	add_compile_definitions(HAS_PYTHON=0)
endif()

# ============================================================================
# zlib (for FBX compressed data)
# ============================================================================
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
	set(HAS_ZLIB TRUE)
	message(STATUS "zlib found: ${ZLIB_INCLUDE_DIRS}")
	add_compile_definitions(HAS_ZLIB=1)
else()
	set(HAS_ZLIB FALSE)
	message(STATUS "zlib not found")
	add_compile_definitions(HAS_ZLIB=0)
endif()

if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    add_compile_definitions(HAS_VULKAN=1)
else()
    message(STATUS "Vulkan SDK not found")
    add_compile_definitions(HAS_VULKAN=0)
endif()

# ============================================================================
# Qt-free build: ksengine (+ SimulatorApp). No Qt package, no editor, no moc.
# ============================================================================
if(KS_QT_FREE_BUILD)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/stb")
		add_compile_definitions(HAS_STB=1)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/openxr")
		add_compile_definitions(HAS_OPENXR_HEADERS=1)
	endif()
	add_compile_definitions(KSASSETTOCORSA_EXPORTS)
	include_directories(
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/simulator"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/stb"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/openxr"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/mikktspace"
	)
	if(Vulkan_FOUND)
		include_directories(${Vulkan_INCLUDE_DIRS})
	endif()
	# ksnet - multiplayer transport (roadmap 3.1). The full build has this
	# gate after the qt-free return() above, so qt-free needs its own copy.
	# Everything after this point (SimulatorApp, tests) compiles with
	# HAS_KSNET=1 and links the ksnet target.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/network/ksnet/CMakeLists.txt")
		add_subdirectory(src/engine/network/ksnet EXCLUDE_FROM_ALL)
		add_compile_definitions(HAS_KSNET=1)
	else()
		add_compile_definitions(HAS_KSNET=0)
	endif()
	add_subdirectory(src/engine)
	# Offline KN5 -> .nmsh baker: the Qt-free replacement for the editor's
	# modules/kn5baker CLI, built on the real "sc6969" format reader.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/kn5baker/main.cpp")
		add_executable(kn5baker "${CMAKE_CURRENT_SOURCE_DIR}/tools/kn5baker/main.cpp")
		target_link_libraries(kn5baker PRIVATE ksengine)
		set_target_properties(kn5baker PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
	endif()
	# Dedicated server binary on the headless C API (roadmap 3.2)
	include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KsEngineC.cmake")
	if(TARGET ksengine_c AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ks_server/main.cpp")
		add_executable(ks_server "${CMAKE_CURRENT_SOURCE_DIR}/tools/ks_server/main.cpp")
		target_link_libraries(ks_server PRIVATE ksengine_c)
		set_target_properties(ks_server PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
	endif()
	# Track folder tooling (roadmap 3.3): scaffold/validate track folders.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/tracktool/main.cpp")
		add_executable(ks_tracktool
			tools/tracktool/main.cpp
			tools/tracktool/TrackToolCore.cpp
			src/simulator/TrackLoader.cpp)
		target_include_directories(ks_tracktool PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/tracktool"
			"${CMAKE_CURRENT_SOURCE_DIR}/src/simulator")
		target_link_libraries(ks_tracktool PRIVATE ksengine)
		set_target_properties(ks_tracktool PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
	endif()
	# Milliken offline analysis tools (G-work): constrained moment-method
	# chart (ksmmm), tire-coefficient normalizer (kstirenorm), tire-model
	# hot-path profiler (ksphprofile), vehicle-sim step profiler (ksvsprofile).
	foreach(_kstool ksmmm kstirenorm ksphprofile ksvsprofile)
		file(GLOB _kstool_src "${CMAKE_CURRENT_SOURCE_DIR}/tools/${_kstool}/*.cpp")
		if(_kstool_src)
			add_executable(${_kstool} ${_kstool_src})
			target_link_libraries(${_kstool} PRIVATE ksengine)
			set_target_properties(${_kstool} PROPERTIES
				RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
		endif()
	endforeach()
	# Hosted matchmaking lobby (PARITY_STATUS "Still open"): kslobby server +
	# kslobby-cli client, Qt-free sockets only. The fragment defines
	# _ks_add_lobby_tool() and creates both targets (skips gracefully if the
	# sources are absent).
	include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeLists_LobbyServer.cmake")
	if(KSIMULATOR_QT_FREE)
		include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeLists_ksimulator_QtFree.cmake")
	endif()
	enable_testing()
	add_subdirectory(tests/ksengine)
	return()
endif()


# ============================================================================
# ksnet - Multiplayer networking library (yojimbo-based) for client/server games
# ============================================================================
# Reliable UDP networking with encryption, authentication, and message serialization
# Built on netcode + reliable + serialize (all vendored)
# ============================================================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/network/ksnet/CMakeLists.txt")
    add_subdirectory(src/engine/network/ksnet EXCLUDE_FROM_ALL)
    set(HAS_KSNET TRUE)
    message(STATUS "ksnet multiplayer networking enabled")
    add_compile_definitions(HAS_KSNET=1)
else()
    set(HAS_KSNET FALSE)
    message(STATUS "ksnet not found - multiplayer disabled")
    add_compile_definitions(HAS_KSNET=0)
endif()

# ============================================================================
# KSEngine - Independent Engine Framework
# ============================================================================
add_subdirectory(src/engine)
set(HAS_KSENGINE TRUE)
message(STATUS "KSEngine included as subdirectory")

# ============================================================================
# Examples
# ============================================================================
add_subdirectory(examples/MinimalSimulator EXCLUDE_FROM_ALL)

# ============================================================================
# Dear ImGui - Qt6 backend for Driving Simulator UI
# ============================================================================
# ksEditor will use ImGui for driving simulator UI panels (dashboard, telemetry, setup)
# The Qt6 ImGui backend allows embedding ImGui windows inside Qt widgets/ dock widgets
# ============================================================================

find_package(imgui CONFIG QUIET)
if(imgui_FOUND)
    set(HAS_IMGUI TRUE)
    message(STATUS "ImGui found: ${imgui_VERSION}")
    add_compile_definitions(HAS_IMGUI=1)
else()
    set(HAS_IMGUI FALSE)
    message(STATUS "ImGui not found - driving simulator UI panels will be disabled")
    add_compile_definitions(HAS_IMGUI=0)
endif()

# ============================================================================
# Driving Simulator Engine Modules
# ============================================================================
# Note: Source files are collected via GLOB_RECURSE in the main library target.
# The subdirectories src/renderer, src/physics, src/audio, src/ui are included
# automatically by the recursive file collection. No separate CMakeLists.txt
# is required in each directory for the basic build.
# ============================================================================

# ============================================================================
# Main Library and Application
# ============================================================================

# Collect source files
# Editor code now lives under src/sdk/kseditor (modules/, plugins/,
# qmlbridges/, EditorApp, ...); physics moved to src/engine and arrives
# transitively through ksengine.
file(GLOB_RECURSE KSEDITOR_CORE_SOURCES
	"src/sdk/kseditor/*.h"
	"src/sdk/kseditor/*.cpp"
)

# Legacy duplicate whose header was replaced by a redirect to the engine
# version; the .cpp implements a class that no longer exists anywhere.
# MainWindow/main.cpp are the kslib-side duplicate of the exe's
# src/MainWindow.{h,cpp}: keeping them here would moc the same
# MainWindow metaobject twice and fail with LNK2005 at exe link.
list(REMOVE_ITEM KSEDITOR_CORE_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor/modules/PhysicsEditor/simulator/phys_LapTimer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor/MainWindow.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor/MainWindow.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor/main.cpp"
)

set(KSEDITOR_MODULES_SOURCES "")
set(KSEDITOR_PLUGINS_SOURCES "")

file(GLOB_RECURSE KSEDITOR_RESOURCES_UI_SOURCES
	"resources/ui/*.h"
	"resources/ui/*.cpp"
)

file(GLOB KSEDITOR_MAIN_SOURCES
	"src/main.cpp"
	"src/MainWindow.cpp"
	resources.qrc
)

# Enforce: src/engine/ui must stay in resources/ui (single source of truth)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ui")
	file(GLOB _core_ui_headers "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ui/*.h")
	foreach(_hdr IN LISTS _core_ui_headers)
		get_filename_component(_name "${_hdr}" NAME)
		if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/${_name}")
message(FATAL_ERROR "src/engine/ui/ has no counterpart in resources/ui - src/engine/ui must stay in resources/ui")
		endif()
	endforeach()
endif()

# Create main kseditor library
add_library(kslib STATIC
	${KSEDITOR_CORE_SOURCES}
	${KSEDITOR_MODULES_SOURCES}
	${KSEDITOR_PLUGINS_SOURCES}
	${KSEDITOR_RESOURCES_UI_SOURCES}
	"src/sdk/kseditor/EditorApp.cpp"
)

target_include_directories(kslib PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor/engine"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor/modules"
	"${CMAKE_CURRENT_SOURCE_DIR}/external/eigen"
)

# Add Qt6 private headers (for qzipreader_p.h etc.)
if(QT6_PATH)
	target_include_directories(kslib PUBLIC
		"${QT6_PATH}/include/QtCore/6.11.1"
	)
endif()

target_link_libraries(kslib PUBLIC
	ksengine
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
	Qt6::Network
	Qt6::Qml
	Qt6::Quick
	Qt6::Quick3D
	Qt6::QuickControls2
	Qt6::QuickWidgets
	Qt6::Multimedia
	Qt6::OpenGL
	Qt6::Sql
	Qt6::Charts
	Qt6::WebSockets
	dwmapi
	opengl32
)

if(HAS_MIKKTSPACE)
	target_link_libraries(kslib PUBLIC mikktspace)
endif()

target_compile_definitions(kslib PUBLIC
	KSASSETTOCORSA_EXPORTS
	KSASSETTOCORSAEVO_EXPORTS
	KS_HAS_QT=1
)

if(Qt6Svg_FOUND)
	target_link_libraries(kslib PUBLIC Qt6::Svg)
endif()

if(ZLIB_FOUND)
	target_link_libraries(kslib PUBLIC ZLIB::ZLIB)
endif()

if(HAS_BULLET)
	target_link_libraries(kslib PUBLIC Bullet3Common BulletDynamics BulletCollision LinearMath)
endif()

if(Vulkan_FOUND)
	target_include_directories(kslib PUBLIC ${Vulkan_INCLUDE_DIRS})
	target_link_libraries(kslib PUBLIC ${Vulkan_LIBRARIES})
endif()

if(HAS_LUA AND LUA_INCLUDE_DIR)
	target_link_libraries(kslib PUBLIC ${LUA_LIBRARIES})
	target_include_directories(kslib PUBLIC ${LUA_INCLUDE_DIR})
endif()

if(HAS_PYTHON)
	target_link_libraries(kslib PUBLIC ${Python3_LIBRARIES})
	target_include_directories(kslib PUBLIC ${Python3_INCLUDE_DIRS})
endif()

if(HAS_IMGUI)
	target_link_libraries(kslib PUBLIC imgui::imgui)
	target_compile_definitions(kslib PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
endif()

if(HAS_KSNET)
	target_link_libraries(kslib PUBLIC ksnet)
endif()

# Create main executable
add_executable(kseditor
	${KSEDITOR_MAIN_SOURCES}
)

target_link_libraries(kseditor PRIVATE
	kslib
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
)

target_include_directories(kseditor PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/kseditor"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(kseditor PRIVATE
	KSASSETTOCORSA_EXPORTS
	KSASSETTOCORSAEVO_EXPORTS
)


# Simulator application - standalone driving simulator
add_executable(SimulatorApp
	"src/simulator/SimulatorApp.cpp"
	"src/simulator/InputManager.cpp"
	"src/simulator/CameraController.cpp"
	"src/simulator/NativeRenderer.cpp"
	"src/simulator/TextureRuntime.cpp"
	"src/simulator/SimulationLoop.cpp"
	"src/simulator/SimulatorAudio.cpp"
	"src/simulator/AIController.cpp"
	"src/simulator/RaceSessionManager.cpp"
	"src/simulator/DashboardOverlay.cpp"
	"src/simulator/SetupGarage.cpp"
	"src/simulator/TelemetryOverlay.cpp"
	"src/simulator/TrackLoader.cpp"
	"src/simulator/MultiCarManager.cpp"
	"src/simulator/PostProcessing.cpp"
	"src/simulator/ReplayRecorder.cpp"
	"src/simulator/GameMenuOverlay.cpp"
	"src/simulator/ShadowSystem.cpp"
	"src/simulator/WASAPIOutput.cpp"
	"src/simulator/AudioMixer.cpp"
	"src/simulator/AudioBankManager.cpp"
	"src/simulator/SoundsIniParser.cpp"
	"src/simulator/NetworkManager.cpp"
	"src/simulator/NetworkLowLevel.cpp"
	"src/simulator/ui/FontTtfLoader.cpp"
)

set_target_properties(SimulatorApp PROPERTIES AUTOMOC ON)

target_link_libraries(SimulatorApp PRIVATE
	kslib
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
	Qt6::OpenGL
	opengl32
)

# Directory includi per i sottosistemi del motore core
target_include_directories(SimulatorApp PRIVATE
	"src/engine/Network"
	"src/engine/Audio"
	"src/engine/Graphics"
	"src/engine/Physics"
	"src/engine/Devices"
	"src/simulator"
)

if(HAS_KSNET)
	target_link_libraries(SimulatorApp PRIVATE ksnet)
endif()

# Copy Qt DLLs to output directory and create platforms/ subfolder
add_custom_command(TARGET SimulatorApp POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SimulatorApp>/platforms"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PROPERTY:Qt6::Core,LOCATION>" "$<TARGET_FILE_DIR:SimulatorApp>/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PROPERTY:Qt6::Gui,LOCATION>" "$<TARGET_FILE_DIR:SimulatorApp>/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PROPERTY:Qt6::Widgets,LOCATION>" "$<TARGET_FILE_DIR:SimulatorApp>/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PROPERTY:Qt6::OpenGL,LOCATION>" "$<TARGET_FILE_DIR:SimulatorApp>/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${Qt6_DIR}/../../../plugins/platforms/qwindows.dll" "$<TARGET_FILE_DIR:SimulatorApp>/platforms/"
)

target_include_directories(SimulatorApp PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/engine"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/plugins"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/simulator"
)

set_target_properties(SimulatorApp PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


# Set output directory
set_target_properties(kseditor PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "ksEditor v${PROJECT_VERSION} configured successfully - Driving Simulator Engine")
