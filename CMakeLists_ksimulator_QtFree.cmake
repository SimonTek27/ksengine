# Optional Qt-free simulator + headless server
if(NOT KSIMULATOR_QT_FREE AND NOT KSENGINE_QT_FREE)
  return()
endif()

find_package(Vulkan QUIET)

set(KSIM_SOURCES
  ${CMAKE_SOURCE_DIR}/src/simulator/SimulatorApp.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop_FeatureMethods.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop_Telemetry.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/InputManager.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/CameraController.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/DashboardOverlay.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/GameMenuOverlay.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/NetworkManager.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/SetupGarage.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/TelemetryOverlay.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/MultiCarManager.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/AIController.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/UdpTelemetryBridge.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/TcpTelemetryBridge.cpp
  ${CMAKE_SOURCE_DIR}/src/simulator/RaceSessionManager.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/physics/VehicleSimulator.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/physics/DamageSystem.cpp
)

set(_ksim_existing "")
foreach(f ${KSIM_SOURCES})
  if(EXISTS "${f}")
    list(APPEND _ksim_existing ${f})
  else()
    message(STATUS "ksimulator: skip missing ${f}")
  endif()
endforeach()

if(_ksim_existing)
  add_executable(ksimulator WIN32 ${_ksim_existing})
  target_compile_definitions(ksimulator PRIVATE KSENGINE_QT_FREE=1 HAS_VEHICLE_SIM=1 HAS_FFB=1)
  target_include_directories(ksimulator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/engine/physics
    ${CMAKE_SOURCE_DIR}/src/simulator
    ${CMAKE_SOURCE_DIR}/src/adapters
  )
  if(Vulkan_FOUND)
    target_include_directories(ksimulator PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(ksimulator PRIVATE ${Vulkan_LIBRARIES})
  endif()
  if(WIN32)
    target_link_libraries(ksimulator PRIVATE dinput8 dxguid xinput ole32 user32 gdi32 ws2_32)
  endif()
  message(STATUS "ksimulator Qt-free target configured")
endif()

include("${CMAKE_SOURCE_DIR}/CMakeLists_SimulatorServer.cmake" OPTIONAL)
include("${CMAKE_SOURCE_DIR}/cmake/CMakeLists_SimulatorServer.cmake" OPTIONAL)
