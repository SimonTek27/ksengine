#ifndef KS_MATERIAL_RESPONSE_CONSTANTS_H
#define KS_MATERIAL_RESPONSE_CONSTANTS_H

/* Shared by src/engine/Graphics/MaterialResponse.h and the simulator shaders
 * (gbuffer.frag, deferred_lighting.frag, native_forward.frag) through glslc's
 * GL_GOOGLE_include_directive, so the loader and the GPU cannot drift apart.
 *
 * Plain decimal literals on purpose: the same file has to be valid C++ and
 * GLSL, so no f suffix and no casts. C++ callers cast to float.
 *
 * ksSpecular is a multiplier around AC's 0.5 neutral, which is the legacy
 * dielectric F0 of 0.04; 1.0 doubles it, 0 halves it. */

#define KS_MATERIAL_SPECULAR_NEUTRAL 0.5
#define KS_MATERIAL_SPECULAR_F0_SCALE 2.0
#define KS_MATERIAL_SPECULAR_F0_MIN 0.4
#define KS_MATERIAL_SPECULAR_F0_MAX 2.0

/* The GBuffer has no free channel for ksSpecular, so it rides in the coverage
 * MAGNITUDE as 1 + (spec - neutral) * step, keeping the sign for the brake
 * disc and staying in [0.75, 1.25] (the abs(w) < 0.5 "no geometry" test). */
#define KS_MATERIAL_COVERAGE_STEP 0.5

/* Fresnel: the AC default exponent the loader substitutes when the file has
 * none, the clamp the shaders apply to an authored one, and the Schlick power
 * used when no authored fresnel exists. */
#define KS_MATERIAL_FRESNEL_DEFAULT_EXP 5.0
#define KS_MATERIAL_FRESNEL_MIN_EXP 0.1
#define KS_MATERIAL_FRESNEL_MAX_EXP 64.0
#define KS_MATERIAL_FRESNEL_POWER 5.0

/* Pane classification keywords. C++ only: GLSL never matches shader names,
 * it receives the resulting glassiness flag. */
#define KS_MATERIAL_GLASS_SHADER_REFLECTION "reflection"
#define KS_MATERIAL_GLASS_SHADER_WINDSCREEN "windscreen"

#endif /* KS_MATERIAL_RESPONSE_CONSTANTS_H */
