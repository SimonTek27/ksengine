# kslobby + kslobby-cli — hosted matchmaking lobby (Qt-free, sockets only)
# Include from root CMakeLists.txt

function(_ks_add_lobby_tool _name _file)
  if(TARGET ${_name})
    return()
  endif()
  set(_src "")
  foreach(_cand
      "${CMAKE_SOURCE_DIR}/src/simulator/${_file}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${_file}"
      "${CMAKE_SOURCE_DIR}/tools/${_file}"
      "${CMAKE_SOURCE_DIR}/src/tools/${_file}")
    if(EXISTS "${_cand}")
      set(_src "${_cand}")
      break()
    endif()
  endforeach()
  if(NOT _src)
    message(STATUS "${_name}: ${_file} not found — skip")
    return()
  endif()
  add_executable(${_name} "${_src}")
  target_compile_features(${_name} PRIVATE cxx_std_17)
  target_compile_definitions(${_name} PRIVATE KSENGINE_QT_FREE=1)
  # LobbyWebEmbedded.h sits next to LobbyServerApp.cpp / LobbyWebClient.html
  get_filename_component(_src_dir "${_src}" DIRECTORY)
  target_include_directories(${_name} PRIVATE
    "${_src_dir}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/simulator"
    "${CMAKE_SOURCE_DIR}/tools")
  if(WIN32)
    target_link_libraries(${_name} PRIVATE ws2_32)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(${_name} PRIVATE Threads::Threads)
  endif()
  set_target_properties(${_name} PROPERTIES
    OUTPUT_NAME "${_name}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
  message(STATUS "${_name} configured: ${_src}")
endfunction()

_ks_add_lobby_tool(kslobby LobbyServerApp.cpp)
_ks_add_lobby_tool(kslobby-cli LobbyClientApp.cpp)
