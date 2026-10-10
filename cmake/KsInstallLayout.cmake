# ============================================================================
# Installed layout of ksim — single source of truth for both the build-tree
# staging (running the binaries straight out of the build) and
# `cmake --install` (a real installed copy).
#
#   ksim.exe           the simulator          (CMake target SimulatorApp)
#   ksengine.dll       the engine, shared     (target ksengine, when SHARED)
#   kssimserver.exe    the headless host      (target SimulatorServer)
#   content/           reference content read at runtime
#   user/              per-user data written at runtime (created empty; the
#                      runtime fills in user/<player>/ on the first start)
#   server/            kssimserver startup configuration
#   system/cfg/        engine settings as JSON (shipped empty, see
#                      src/engine/Config/EngineSettings.h)
#   system/ppfilters/  post-processing filter presets as JSON
#   system/i18n/       game strings per language (system/i18n/<lang>.json,
#                      src/engine/Config/Locale.h)
#   system/shaders/    precompiled SPIR-V
#
# Entry points:
#   ks_stage_install_layout(<target>)  POST_BUILD copy of the layout next to
#                                      <target> in the build tree
#   ks_install_layout()                install() rules producing the same tree
#                                      under CMAKE_INSTALL_PREFIX
#
# Everything the runtime reads is resolved relative to the working directory
# (src/engine/assets/Paths.h, src/simulator/*), which for a normally launched
# executable is the directory holding it — hence DESTINATION ".".
# ============================================================================

# Reference content shipped with the product. Deliberately *not* the whole
# content/ tree: a developer's local content/cars must never end up in an
# install (same rule as the historical POST_BUILD copies, roadmap 2.4).
set(KS_LAYOUT_CONTENT_REFS content/baked content/cars/refcar content/examples)

# kssimserver startup configuration shipped with the install (a plain
# directory-scope variable: it is read from both staging and install rules,
# which are evaluated in the including CMakeLists.txt's scope).
set(KS_LAYOUT_SERVER_CONFIG "${CMAKE_SOURCE_DIR}/server/kssimserver.ini")

# Engine settings shipped with the install: system/cfg/ksengine.json, an
# empty JSON object. The runtime merges it with user/<player>/settings.json
# (src/engine/Config/EngineSettings.h) — the install ships no keys, the
# user's file gains them when edited.
set(KS_LAYOUT_ENGINE_CONFIG "${CMAKE_SOURCE_DIR}/system/cfg/ksengine.json")

# Game strings shipped with the install: system/i18n/en.json (the English
# table every key falls back to) and one file per translation on top of it.
# The Qt-free runtime reads them through src/engine/Config/Locale.h.
set(KS_LAYOUT_I18N "${CMAKE_SOURCE_DIR}/system/i18n")

# ---------------------------------------------------------------------------
# Build-tree staging.
# ---------------------------------------------------------------------------
function(ks_stage_install_layout _target)
	if(NOT TARGET ${_target})
		return()
	endif()

	# user/ is written at runtime (user/pb/, and the per-player folder
	# user/<player>/ that the runtime creates on the first start): pre-create
	# it so a staged run never has to race the first write.
	add_custom_command(TARGET ${_target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_target}>/user"
		COMMENT "Staging user/ next to $<TARGET_FILE_NAME:${_target}>"
		VERBATIM)

	# system/cfg/ksengine.json — engine settings, shipped as {}.
	if(EXISTS "${KS_LAYOUT_ENGINE_CONFIG}")
		add_custom_command(TARGET ${_target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_target}>/system/cfg"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${KS_LAYOUT_ENGINE_CONFIG}" "$<TARGET_FILE_DIR:${_target}>/system/cfg/"
			COMMENT "Staging system/cfg/ksengine.json next to $<TARGET_FILE_NAME:${_target}>"
			VERBATIM)
	endif()

	# system/i18n/ — game strings per language (en.json + one file per
	# translation). copy_directory keeps the directory itself in the target.
	if(EXISTS "${KS_LAYOUT_I18N}")
		add_custom_command(TARGET ${_target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${KS_LAYOUT_I18N}" "$<TARGET_FILE_DIR:${_target}>/system/i18n"
			COMMENT "Staging system/i18n/ next to $<TARGET_FILE_NAME:${_target}>"
			VERBATIM)
	endif()

	# server/kssimserver.ini — kssimserver.exe reads it on start-up.
	if(EXISTS "${KS_LAYOUT_SERVER_CONFIG}")
		add_custom_command(TARGET ${_target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_target}>/server"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${KS_LAYOUT_SERVER_CONFIG}" "$<TARGET_FILE_DIR:${_target}>/server/"
			COMMENT "Staging server/kssimserver.ini next to $<TARGET_FILE_NAME:${_target}>"
			VERBATIM)
	endif()

	# Reference content (see KS_LAYOUT_CONTENT_REFS).
	foreach(_ref ${KS_LAYOUT_CONTENT_REFS})
		if(EXISTS "${CMAKE_SOURCE_DIR}/${_ref}")
			add_custom_command(TARGET ${_target} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_directory
					"${CMAKE_SOURCE_DIR}/${_ref}" "$<TARGET_FILE_DIR:${_target}>/${_ref}"
				COMMENT "Staging ${_ref} next to $<TARGET_FILE_NAME:${_target}>"
				VERBATIM)
		endif()
	endforeach()

	# SPIR-V: system/shaders. Guarded on the shader target because glslc may be
	# absent, in which case ${CMAKE_BINARY_DIR}/shaders never gets created.
	if(TARGET ks_shaders)
		add_custom_command(TARGET ${_target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_target}>/system/shaders"
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:${_target}>/system/shaders"
			COMMENT "Staging system/shaders next to $<TARGET_FILE_NAME:${_target}>"
			VERBATIM)
	endif()
endfunction()

# ---------------------------------------------------------------------------
# Install rules: the layout above under CMAKE_INSTALL_PREFIX.
# ---------------------------------------------------------------------------
function(ks_install_layout)
	if(KS_LAYOUT_INSTALLED)
		return()
	endif()
	set(KS_LAYOUT_INSTALLED TRUE PARENT_SCOPE)

	# --- executables (+ ksengine.dll once ksengine is built shared) --------
	foreach(_exe SimulatorApp SimulatorServer)
		if(TARGET ${_exe})
			install(TARGETS ${_exe} RUNTIME DESTINATION .)
		endif()
	endforeach()
	if(TARGET ksengine)
		get_target_property(_ksengine_type ksengine TYPE)
		if(_ksengine_type STREQUAL "SHARED_LIBRARY" OR _ksengine_type STREQUAL "MODULE_LIBRARY")
			install(TARGETS ksengine RUNTIME DESTINATION . LIBRARY DESTINATION .)
		endif()
		# ARCHIVE deliberately omitted: the import library is a build
		# artefact, not part of the installed tree.
	endif()

	# --- content/ ----------------------------------------------------------
	foreach(_ref ${KS_LAYOUT_CONTENT_REFS})
		if(EXISTS "${CMAKE_SOURCE_DIR}/${_ref}")
			install(DIRECTORY "${CMAKE_SOURCE_DIR}/${_ref}" DESTINATION content)
		endif()
	endforeach()

	# --- server/ -----------------------------------------------------------
	if(EXISTS "${KS_LAYOUT_SERVER_CONFIG}")
		install(FILES "${KS_LAYOUT_SERVER_CONFIG}" DESTINATION server)
	endif()

	# --- system/cfg/ -------------------------------------------------------
	if(EXISTS "${KS_LAYOUT_ENGINE_CONFIG}")
		install(FILES "${KS_LAYOUT_ENGINE_CONFIG}" DESTINATION system/cfg)
	endif()

	# --- system/i18n/ ------------------------------------------------------
	if(EXISTS "${KS_LAYOUT_I18N}")
		install(DIRECTORY "${KS_LAYOUT_I18N}" DESTINATION system)
	endif()

	# --- system/shaders/ ---------------------------------------------------
	# OPTIONAL: glslc may be missing, and `cmake --install` runs long after
	# configure, so the directory's existence is only known here.
	install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION system/shaders
		OPTIONAL FILES_MATCHING PATTERN "*.spv")

	# --- user/ -------------------------------------------------------------
	# Holds only data the installed copy writes itself, so ship it empty.
	install(CODE "file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/user\")")
endfunction()

# ---------------------------------------------------------------------------
# Convenience target: `cmake --build <build> --target ks_dist` drops the whole
# layout in <source>/dist/ksim. Assumes the binaries were built first (the
# default target pulls in everything ks_dist needs, the custom one does not).
# ---------------------------------------------------------------------------
function(ks_add_dist_target)
	if(TARGET ks_dist)
		return()
	endif()
	add_custom_target(ks_dist
		COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
			--prefix "${CMAKE_SOURCE_DIR}/dist/ksim" --config "$<CONFIG>"
		COMMENT "Installing the ksim layout into dist/ksim"
		VERBATIM)
	foreach(_dep SimulatorApp SimulatorServer ks_shaders ksengine)
		if(TARGET ${_dep})
			add_dependencies(ks_dist ${_dep})
		endif()
	endforeach()
endfunction()
