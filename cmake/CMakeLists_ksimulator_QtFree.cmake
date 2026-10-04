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
	${CMAKE_SOURCE_DIR}/src/simulator/AudioMixer.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/AudioBankManager.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/SoundsIniParser.cpp
	${CMAKE_SOURCE_DIR}/src/simulator/NativeRenderer.cpp
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
