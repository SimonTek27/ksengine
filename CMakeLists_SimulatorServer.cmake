# Headless server (Qt-free)
if(NOT TARGET SimulatorServer)
  set(_srv_src
    ${CMAKE_SOURCE_DIR}/src/simulator/SimulatorServerApp.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop_FeatureMethods.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/SimulationLoop_Telemetry.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/MultiCarManager.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/AIController.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/RaceSessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/InputManager.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/UdpTelemetryBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/simulator/TcpTelemetryBridge.cpp
  )
  set(_srv_exist "")
  foreach(f ${_srv_src})
    if(EXISTS "${f}")
      list(APPEND _srv_exist ${f})
    endif()
  endforeach()
  if(_srv_exist)
    add_executable(SimulatorServer ${_srv_exist})
    target_compile_definitions(SimulatorServer PRIVATE KSENGINE_QT_FREE=1 HAS_VEHICLE_SIM=1)
    target_include_directories(SimulatorServer PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/src/engine
      ${CMAKE_SOURCE_DIR}/src/engine/physics
      ${CMAKE_SOURCE_DIR}/src/simulator
      ${CMAKE_SOURCE_DIR}/src/adapters
    )
    if(WIN32)
      target_link_libraries(SimulatorServer PRIVATE ws2_32)
    endif()
    message(STATUS "SimulatorServer (headless) configured")
  endif()
endif()
