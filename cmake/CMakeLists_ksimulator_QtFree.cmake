# Qt-free SimulatorApp executable — sources under src/simulator + engine natives.
# Included from the root CMakeLists.txt only when KSIMULATOR_QT_FREE is ON,
# after src/engine (ksengine) has been added.

if(NOT KSIMULATOR_QT_FREE)
	return()
endif()

find_package(Vulkan REQUIRED)

set(KSIM_SOURCES
	${CMAKE_SOURCE_DIR}/src/simulator/SimulatorApp.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop_NetSync.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/InputManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/CameraController.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/SimulatorAudio.cpp
	# race/upgrade sound packs (roadmap 2.9) — was never compiled before
	${CMAKE_SOURCE_DIR}/src/simulator/SimulatorAudio_SoundPack.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/AudioMixer.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/AudioBankManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/SoundsIniParser.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/NativeRenderer.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/TextureRuntime.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/ShadowSystem.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/DashboardOverlay.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/SetupGarage.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/TelemetryOverlay.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/TrackLoader.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/AIController.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/RaceSessionManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/MultiCarManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/PostProcessing.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/ReplayRecorder.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/GameMenuOverlay.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/NetworkManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/NetworkLowLevel.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/UdpTelemetryListener.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/TrackAudioManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/WASAPIOutput.cpp
)

file(GLOB _ksim_ui_sources "${CMAKE_SOURCE_DIR}/src/simulator/ui/*.cpp")
list(APPEND KSIM_SOURCES ${_ksim_ui_sources})

set(_ksim_existing "")
foreach(f ${KSIM_SOURCES})
	if(EXISTS "${f}")
		list(APPEND _ksim_existing ${f})
	else()
		message(STATUS "SimulatorApp: skip missing ${f}")
	endif()
endforeach()

add_executable(SimulatorApp WIN32 ${_ksim_existing})
# Installed name: the shipped binary is ksim.exe, the CMake target keeps its
# historical name (cmake/KsInstallLayout.cmake).
set_target_properties(SimulatorApp PROPERTIES
	AUTOMOC OFF
	AUTOUIC OFF
	AUTORCC OFF
	OUTPUT_NAME ksim
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_compile_definitions(SimulatorApp PRIVATE
	KSENGINE_QT_FREE=1
	HAS_VEHICLE_SIM=1
	HAS_FFB=1
	KSASSETTOCORSA_EXPORTS
)
target_include_directories(SimulatorApp PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/simulator
)
target_link_libraries(SimulatorApp PRIVATE ksengine ${Vulkan_LIBRARIES})
if(TARGET ksnet)
	target_link_libraries(SimulatorApp PRIVATE ksnet)
endif()
if(HAS_MIKKTSPACE)
	target_link_libraries(SimulatorApp PRIVATE mikktspace)
endif()
if(WIN32)
	target_link_libraries(SimulatorApp PRIVATE dinput8 dxguid xinput ole32 user32 gdi32 ws2_32 winmm)
endif()

# ---------------------------------------------------------------------------
# SPIR-V shaders: glslc -> ${CMAKE_BINARY_DIR}/shaders, copied next to the exe
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/KsShaders.cmake")
ks_add_shader_target(ks_shaders "${CMAKE_SOURCE_DIR}/src")
if(TARGET ks_shaders)
	add_dependencies(SimulatorApp ks_shaders)
endif()

# The installed layout (ksim.exe, content/, user/, server/, system/shaders/)
# is defined once in cmake/KsInstallLayout.cmake and staged here so a
# build-tree run behaves exactly like an installed copy. The shader stage
# above is part of it; content/user/server staging lives there too.
include("${CMAKE_CURRENT_LIST_DIR}/KsInstallLayout.cmake")
ks_stage_install_layout(SimulatorApp)

message(STATUS "SimulatorApp: Qt-free runtime (src/simulator, render=src/engine)")

# ---------------------------------------------------------------------------
# Headless host (merged from 74dd224): SimulatorServer reuses the runtime
# source list above with its own entry point.
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists_SimulatorServer.cmake" OPTIONAL)
