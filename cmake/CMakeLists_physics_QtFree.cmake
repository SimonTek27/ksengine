# Qt-free physics sources for ksengine (include from engine CMake or SimulatorApp)
# Paths relative to src/engine/physics or flat physics/ depending on layout.

set(KSENGINE_PHYSICS_QT_FREE_SOURCES
    PhysicsCoreTypes.h
    PhysicsTypes.h
    PhysicsEngine.h
    PhysicsEngine.cpp
    PhysicsLogger.h
    PhysicsLogger.cpp
    PhysicsProfiler.h
    PhysicsProfiler.cpp
    GpuProfiler.h
    GpuProfiler.cpp
    AeroDraft.h
    AeroModel.h
    AeroModel.cpp
    AeroSimulator.h
    AeroSimulator.cpp
    KsTireModel.h
    KsTireModel.cpp
    TireFlatSpot.h
    TireSimulator.h
    TireSimulator.cpp
    EngineModel.h
    EngineModel.cpp
    WeatherPhysics.h
    WeatherPhysics.cpp
    SuspensionModel.h
    SuspensionModel.cpp
    SuspensionKinematics.h
    DifferentialModel.h
    DifferentialModel.cpp
    DamageSystem.h
    DamageSystem.cpp
    BrakeThermalModel.h
    BrakeThermalModel.cpp
    TrackSurface.h
    phys_Simulator.h
    phys_Simulator.cpp
)

# Optional note for SimulatorApp:
# target_link_libraries(SimulatorApp PRIVATE ksengine)
# target_include_directories(SimulatorApp PRIVATE ${KSENGINE_ROOT}/physics)

# ksphprofile — physics hot-path microbenchmark (Qt-free)
if(NOT TARGET ksphprofile)
  set(_ksph_src "")
  foreach(_cand
      "${CMAKE_SOURCE_DIR}/src/engine/physics/PhysicsHotPathProfile.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/PhysicsHotPathProfile.cpp"
      "${CMAKE_SOURCE_DIR}/tools/PhysicsHotPathProfile.cpp")
    if(EXISTS "${_cand}")
      set(_ksph_src "${_cand}")
      break()
    endif()
  endforeach()
  if(_ksph_src)
    set(_ksph_deps
      "${CMAKE_CURRENT_SOURCE_DIR}/KsTireModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/TireSimulator.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/PhysicsProfiler.cpp")
    # resolve alternate layouts
    foreach(_d KsTireModel.cpp TireSimulator.cpp PhysicsProfiler.cpp)
      if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_d}")
        if(EXISTS "${CMAKE_SOURCE_DIR}/src/engine/physics/${_d}")
          list(APPEND _ksph_deps "${CMAKE_SOURCE_DIR}/src/engine/physics/${_d}")
        endif()
      endif()
    endforeach()
    add_executable(ksphprofile "${_ksph_src}"
      "${CMAKE_CURRENT_SOURCE_DIR}/KsTireModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/TireSimulator.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/PhysicsProfiler.cpp")
    target_include_directories(ksphprofile PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_SOURCE_DIR}/src/engine/physics")
    target_compile_features(ksphprofile PRIVATE cxx_std_17)
    message(STATUS "ksphprofile (physics hot-path) configured")
  endif()
endif()

# ksvsprofile — full VehicleSimulator step benchmark
if(NOT TARGET ksvsprofile)
  set(_ksvs_main "")
  foreach(_cand
      "${CMAKE_CURRENT_SOURCE_DIR}/VehicleSimProfile.cpp"
      "${CMAKE_SOURCE_DIR}/src/engine/physics/VehicleSimProfile.cpp"
      "${CMAKE_SOURCE_DIR}/tools/VehicleSimProfile.cpp")
    if(EXISTS "${_cand}")
      set(_ksvs_main "${_cand}")
      break()
    endif()
  endforeach()
  if(_ksvs_main AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VehicleSimulator.cpp")
    add_executable(ksvsprofile
      "${_ksvs_main}"
      "${CMAKE_CURRENT_SOURCE_DIR}/VehicleSimulator.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/KsTireModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/AeroModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/EngineModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/DifferentialModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/SuspensionModel.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/DamageSystem.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/PhysicsProfiler.cpp")
    target_include_directories(ksvsprofile PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_SOURCE_DIR}/src/engine/physics")
    target_compile_features(ksvsprofile PRIVATE cxx_std_17)
    message(STATUS "ksvsprofile (VehicleSimulator full step) configured")
  endif()
endif()

# ksmmm — offline constrained Moment Method (Milliken Ch.8), not hot-path
if(NOT TARGET ksmmm)
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/ksmmm.cpp"
     AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/MomentMethodTool.cpp")
    add_executable(ksmmm
      "${CMAKE_CURRENT_LIST_DIR}/ksmmm.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/MomentMethodTool.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/KsTireModel.cpp"
    )
    target_include_directories(ksmmm PRIVATE
      "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_compile_features(ksmmm PRIVATE cxx_std_17)
    message(STATUS "ksmmm (Moment Method offline tool) configured")
  endif()
endif()

# kstirenorm — offline tire normalize/validate (Milliken Ch.14)
if(NOT TARGET kstirenorm)
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/kstirenorm.cpp"
     AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/TireNormalizeTool.cpp")
    add_executable(kstirenorm
      "${CMAKE_CURRENT_LIST_DIR}/kstirenorm.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/TireNormalizeTool.cpp"
      "${CMAKE_CURRENT_LIST_DIR}/KsTireModel.cpp"
    )
    target_include_directories(kstirenorm PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_features(kstirenorm PRIVATE cxx_std_17)
    message(STATUS "kstirenorm (tire normalize) configured")
  endif()
endif()
