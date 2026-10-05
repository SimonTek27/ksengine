# Headless server (Qt-free) - merged from 74dd224 and rewired for this tree.
#
# The incoming file listed the translation units of the *other* SimulationLoop
# split (SimulationLoop_FeatureMethods.cpp / SimulationLoop_Telemetry.cpp,
# which implement a different SimulationLoop API and cannot compile against
# our full SimulationLoop.cpp) and it omitted the UI / renderer / loader TUs
# that our SimulationLoop.cpp pulls in. The source set is therefore derived
# from the SimulatorApp list assembled just above: SimulatorServerApp.cpp
# supplies the entry point, everything else stays identical.
if(NOT KSIMULATOR_QT_FREE)
	return()
endif()
if(TARGET SimulatorServer)
	return()
endif()
if(NOT TARGET SimulatorApp)
	return()
endif()

set(_ksrv_entry "${CMAKE_SOURCE_DIR}/src/simulator/SimulatorServerApp.cpp")
if(NOT EXISTS "${_ksrv_entry}")
	message(STATUS "SimulatorServer: skip - ${_ksrv_entry} not found")
	return()
endif()

set(_ksrv_sources "")
foreach(f ${_ksim_existing})
	if(f STREQUAL "${CMAKE_SOURCE_DIR}/src/simulator/SimulatorApp.cpp")
		list(APPEND _ksrv_sources "${_ksrv_entry}")
	else()
		list(APPEND _ksrv_sources "${f}")
	endif()
endforeach()

add_executable(SimulatorServer ${_ksrv_sources})
set_target_properties(SimulatorServer PROPERTIES
	AUTOMOC OFF
	AUTOUIC OFF
	AUTORCC OFF
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_compile_definitions(SimulatorServer PRIVATE
	KSENGINE_QT_FREE=1
	HAS_VEHICLE_SIM=1
	HAS_FFB=1
	KSASSETTOCORSA_EXPORTS
)
target_include_directories(SimulatorServer PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/src/simulator
)
target_link_libraries(SimulatorServer PRIVATE ksengine ${Vulkan_LIBRARIES})
if(TARGET ksnet)
	target_link_libraries(SimulatorServer PRIVATE ksnet)
endif()
if(HAS_MIKKTSPACE)
	target_link_libraries(SimulatorServer PRIVATE mikktspace)
endif()
if(WIN32)
	target_link_libraries(SimulatorServer PRIVATE dinput8 dxguid xinput ole32 user32 gdi32 ws2_32 winmm)
endif()

message(STATUS "SimulatorServer (headless) configured - disc :20779, ctrl :20780")
