# SPIR-V shaders: glslc -> ${CMAKE_BINARY_DIR}/shaders
#
# Shared by the qt-free root build (cmake/CMakeLists_ksimulator_QtFree.cmake)
# and the standalone tests build (tests/ksengine/CMakeLists.txt): test_renderer
# reads exactly this .spv set through KS_TEST_SHADER_DIR.
#
#   ks_add_shader_target(<target> <src_root>)
#
# <src_root> is the repository `src/` directory, passed in because the caller
# is not always the root CMakeLists (the standalone tests configure sets
# CMAKE_SOURCE_DIR to tests/ksengine). Creates <target> only when glslc is
# available, so callers must guard target-dependent work with `if(TARGET ...)`.
function(ks_add_shader_target _target _src_root)
	find_program(KS_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
	if(NOT KS_GLSLC)
		message(STATUS "${_target}: glslc not found, shaders will not be built")
		return()
	endif()

	set(_ksim_shader_sources
		${_src_root}/engine/Graphics/shaders/ksShadow.vert
		${_src_root}/engine/Graphics/shaders/ksShadow.frag
		${_src_root}/simulator/shaders/native_forward.vert
		${_src_root}/simulator/shaders/native_forward.frag
		${_src_root}/simulator/shaders/gbuffer.vert
		${_src_root}/simulator/shaders/gbuffer.frag
		${_src_root}/simulator/shaders/deferred_lighting.vert
		${_src_root}/simulator/shaders/deferred_lighting.frag
		${_src_root}/simulator/shaders/taa.frag
		${_src_root}/simulator/shaders/hiz_downsample.frag
		# Display pass + bloom chain of the deferred path (ksengine shaders).
		${_src_root}/engine/Graphics/shaders/tonemap.frag
		${_src_root}/engine/Graphics/shaders/glareExtract.frag
		${_src_root}/engine/Graphics/shaders/bloomBlur.frag
		${_src_root}/simulator/ui/shaders/ui.vert.glsl
		${_src_root}/simulator/ui/shaders/ui.frag.glsl
		# Roadmap ksengine-vs-cryengine P0: shaders that existed in
		# src/engine/Graphics/shaders but were never built by any target, so
		# nobody noticed when they rotted. Building them keeps them honest.
		${_src_root}/engine/Graphics/shaders/ssao.comp
		${_src_root}/engine/Graphics/shaders/ssr.comp
		${_src_root}/engine/Graphics/shaders/motionblur.frag
		${_src_root}/engine/Graphics/shaders/fxaa.frag
		${_src_root}/engine/Graphics/shaders/particle.vert
		${_src_root}/engine/Graphics/shaders/particle.frag
		${_src_root}/engine/Graphics/shaders/particle_gbuffer.frag
		${_src_root}/engine/Graphics/shaders/terrain.vert
		${_src_root}/engine/Graphics/shaders/terrain.frag
		${_src_root}/engine/Graphics/shaders/water.vert
		${_src_root}/engine/Graphics/shaders/water.frag
		${_src_root}/engine/Graphics/shaders/vegetation.vert
		${_src_root}/engine/Graphics/shaders/vegetation.frag
	)
	set(_ksim_spv_files "")
	foreach(_shader ${_ksim_shader_sources})
		if(EXISTS "${_shader}")
			get_filename_component(_name "${_shader}" NAME)
			string(REGEX REPLACE "\\.glsl$" "" _name "${_name}")
			# .glsl files carry no stage in the extension: pass it explicitly.
			set(_stage "")
			if(_shader MATCHES "\\.glsl$")
				string(REGEX REPLACE "^(.*)([./])([a-z]+)\\.glsl$" "\\3" _stage "${_shader}")
				set(_stage "-fshader-stage=${_stage}")
			endif()
			set(_spv "${CMAKE_BINARY_DIR}/shaders/${_name}.spv")
			add_custom_command(OUTPUT "${_spv}"
				COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
				COMMAND ${KS_GLSLC} -O ${_stage} -o "${_spv}" "${_shader}"
				DEPENDS "${_shader}"
				COMMENT "glslc ${_name}.spv"
				VERBATIM)
			list(APPEND _ksim_spv_files "${_spv}")
		endif()
	endforeach()
	add_custom_target(${_target} DEPENDS ${_ksim_spv_files})
endfunction()
