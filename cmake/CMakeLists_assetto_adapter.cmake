# Optional Kinetika / CSP content adapter (not part of ksengine core)
if(NOT TARGET ks_adapter_ac)
  set(KS_AC_ADAPTER_DIR ${CMAKE_SOURCE_DIR}/src/adapters/assetto_corsa)
  if(EXISTS "${KS_AC_ADAPTER_DIR}/CspConfigParser.cpp")
    add_library(ks_adapter_ac STATIC
      ${KS_AC_ADAPTER_DIR}/CspConfigParser.cpp
      ${KS_AC_ADAPTER_DIR}/ACGuidsParser.cpp
      ${KS_AC_ADAPTER_DIR}/FSPROImporter.cpp
      ${KS_AC_ADAPTER_DIR}/FSPROExporter.cpp
    )
    target_include_directories(ks_adapter_ac PUBLIC
      ${CMAKE_SOURCE_DIR}/src
      ${KS_AC_ADAPTER_DIR})
    target_compile_features(ks_adapter_ac PUBLIC cxx_std_17)
    message(STATUS "ks_adapter_ac: Kinetika / CSP content adapter")
  endif()
endif()
