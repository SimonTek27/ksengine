# Fails when a bounded simulator run logs a Vulkan validation error.
#
# The validation layer only speaks when it is loaded, so the run asks the
# loader for it by name. The simulator opens a window and never returns on its
# own, so the run is bounded: the process result is reported (a crash there is
# worth seeing) but the verdict of this check is the VUID list, which is what
# the layer emits when the API is used in a way the spec forbids.
#
#   cmake -DROOT=<dir holding ksim.exe> -P CheckNoValidationErrors.cmake
#
# A missing layer is not a failure: the loader ignores unknown names, and a
# machine without the Vulkan SDK would otherwise turn a content check red.

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "CheckNoValidationErrors: pass -DROOT=<dir holding ksim.exe>")
endif()
set(KSIM "${ROOT}/ksim.exe")
if(NOT EXISTS "${KSIM}")
    message(FATAL_ERROR "CheckNoValidationErrors: no ksim.exe in ${ROOT}")
endif()

set(errLog "${ROOT}/validation_stderr.log")
set(outLog "${ROOT}/validation_stdout.log")
file(REMOVE "${errLog}" "${outLog}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
            "${KSIM}"
    WORKING_DIRECTORY "${ROOT}"
    OUTPUT_FILE "${outLog}"
    ERROR_FILE "${errLog}"
    TIMEOUT 8
    RESULT_VARIABLE runResult)

# Both streams: the renderer writes validation-adjacent lines to stdout and
# stderr depending on the call site.
set(messages "")
foreach(f "${errLog}" "${outLog}")
    if(EXISTS "${f}")
        file(READ "${f}" text)
        string(APPEND messages "${text}")
    endif()
endforeach()

string(REGEX MATCHALL "VUID-[A-Za-z0-9-]+" vuidList "${messages}")
list(REMOVE_DUPLICATES vuidList)
list(LENGTH vuidList vuidCount)
string(REGEX MATCHALL "\\[render\\] [0-9.]+ fps" frameReports "${messages}")
list(LENGTH frameReports frameCount)

if(vuidCount GREATER 0)
    list(JOIN vuidList "\n  " printed)
    message(FATAL_ERROR
        "the simulator logged ${vuidCount} distinct Vulkan validation error(s):\n  ${printed}")
endif()

message(STATUS "no Vulkan validation errors (${frameCount} frame report(s), run result: ${runResult})")
