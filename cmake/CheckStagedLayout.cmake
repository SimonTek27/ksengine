# Verifies that a tree (build-tree staging or an installed prefix) holds
# everything the layout commented at the top of KsInstallLayout.cmake promises.
#
# Runs as a CTest next to the other binaries and again at the end of
# `cmake --install`, so a folder the runtime needs but the layout forgot
# (system/ppfilters, once) fails a check instead of showing up as a warning at
# start-up.
#
#   ROOT  directory that should hold the layout (next to ksim.exe)
#   SRC   repository root; when given, engine/assets/Paths.h is read to make sure
#         every system/ directory the runtime names is covered by the list below
#
# The list is the documentation, but Paths.h is the code: a new path there that
# is not listed here fails the check, which is the direction that actually
# catches mistakes.

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

# --- the runtime's own list ------------------------------------------------
if(DEFINED SRC AND EXISTS "${SRC}/engine/assets/Paths.h")
    file(READ "${SRC}/engine/assets/Paths.h" _paths_header)
    # Literal directories only: Paths.h builds file names by concatenation, so
    # the directories are the "system/..." strings it returns.
    string(REGEX MATCHALL "\"system/[A-Za-z0-9_]+\"" _named "${_paths_header}")
    set(_unlisted "")
    foreach(_lit ${_named})
        string(REGEX REPLACE "^\"|\"$" "" _dir "${_lit}")
        set(_covered FALSE)
        foreach(_rel ${_required})
            if(_rel STREQUAL _dir OR _rel MATCHES "^${_dir}/")
                set(_covered TRUE)
            endif()
        endforeach()
        if(NOT _covered)
            list(APPEND _unlisted "${_dir}")
        endif()
    endforeach()
    if(_unlisted)
        list(REMOVE_DUPLICATES _unlisted)
        string(REPLACE ";" ", " _list "${_unlisted}")
        message(FATAL_ERROR
            "engine/assets/Paths.h resolves ${_list}, but the layout list in "
            "cmake/CheckStagedLayout.cmake does not cover it")
    endif()
endif()

# --- what the tree actually holds ------------------------------------------
set(_missing "")
foreach(_rel ${_required})
    if(NOT EXISTS "${ROOT}/${_rel}")
        list(APPEND _missing "${_rel}")
    endif()
endforeach()

# A preset is a colour transform plus its .lut strip: when the source ships a
# strip, the tree must carry both halves. Names are matched, not contents, so a
# preset that loses its cube (or a strip that loses its preset) is caught here
# rather than showing up as a different image.
if(DEFINED SRC AND EXISTS "${SRC}/system/ppfilters")
    file(GLOB _luts RELATIVE "${SRC}/system/ppfilters" "${SRC}/system/ppfilters/*.lut")
    foreach(_lut ${_luts})
        get_filename_component(_stem "${_lut}" NAME_WE)
        foreach(_need "${_stem}.json" "${_lut}")
            if(NOT EXISTS "${ROOT}/system/ppfilters/${_need}")
                list(APPEND _missing "system/ppfilters/${_need}")
            endif()
        endforeach()
    endforeach()
endif()

if(_missing)
    string(REPLACE ";" ", " _list "${_missing}")
    message(FATAL_ERROR "layout incomplete in ${ROOT}: missing ${_list}")
endif()

message(STATUS "layout complete in ${ROOT}")
