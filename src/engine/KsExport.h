#pragma once
/**
 * KsExport — the explicit export API of ksengine.dll.
 *
 * ksengine is built as a shared library (see KSENGINE_SHARED in
 * CMakeLists.txt of this directory) and every entity an executable touches
 * from outside the DLL is annotated with KSENGINE_API:
 *
 *     class KSENGINE_API SimulationLoop { ... };
 *     KSENGINE_API bool loadTrack(const std::string&);
 *
 * Three states, selected by compile definitions the build sets:
 *
 *   KSENGINE_BUILDING_DLL  compiling the DLL itself       -> dllexport
 *   KSENGINE_USE_DLL       compiling a target that links
 *                          ksengine.dll (PUBLIC, so it is
 *                          inherited by every consumer)   -> dllimport
 *   neither                a static build of the engine, or a TU that only
 *                          includes engine headers without linking the
 *                          library                        -> nothing
 *
 * Header-only entities (inline functions, constexpr values) do not need the
 * macro: they are compiled into whichever module uses them. Anything with a
 * definition in a .cpp does, otherwise the linker reports LNK2019 once the
 * consumer stops getting it from the static archive.
 *
 * On non-Windows the macro is empty: without -fvisibility=hidden the default
 * ELF/Mach-O visibility already exports everything.
 */
#if defined(_WIN32)
#  if defined(KSENGINE_BUILDING_DLL)
#    define KSENGINE_API __declspec(dllexport)
#  elif defined(KSENGINE_USE_DLL)
#    define KSENGINE_API __declspec(dllimport)
#  else
#    define KSENGINE_API
#  endif
#else
#  define KSENGINE_API
#endif
