# Verifies that the build-tree staging holds everything the layout commented at
# the top of KsInstallLayout.cmake promises.
#
# Runs as a CTest next to the other binaries: a folder the runtime needs but the
# staging forgot (system/ppfilters, once) then fails the suite instead of showing
# up as a warning at start-up. ROOT is the directory holding ksim.exe.

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "CheckStagedLayout.cmake: pass -DROOT=<dir next to ksim.exe>")
endif()

# Exactly the entries in the layout's header comment, minus the executables and
# the DLL (those are CMake targets, not copies).
set(_required
    "user"
    "content/baked"
    "content/examples"
    "content/showrooms"
    "server/kssimserver.ini"
    "system/cfg/ksengine.json"
    "system/i18n"
    "system/ppfilters"
    "system/shaders")

set(_missing "")
foreach(_rel ${_required})
    if(NOT EXISTS "${ROOT}/${_rel}")
        list(APPEND _missing "${_rel}")
    endif()
endforeach()

if(_missing)
    string(REPLACE ";" ", " _list "${_missing}")
    message(FATAL_ERROR "staging layout incomplete in ${ROOT}: missing ${_list}")
endif()

message(STATUS "layout complete in ${ROOT}")
