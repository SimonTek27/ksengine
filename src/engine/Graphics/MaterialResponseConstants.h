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

/* The GBuffer has no free channel for the authored surface response, so
 * ksSpecular and fresnelMaxLevel both ride in the coverage MAGNITUDE as two
 * integer levels: magnitude = 1 + (specLevel * 17 + fresnelLevel) * step.
 * The channel is a float, so the pair survives exactly; the sign still marks a
 * brake disc and the magnitude stays in [1, 1.06], nowhere near the
 * abs(w) < 0.5 "no geometry" test. KS_MATERIAL_COVERAGE_STEP is 1 / (16 * 17):
 * 16 is the highest level either value reaches (so a level decodes to 0..1 by
 * dividing by 16, and AC's 0.5 neutral lands on level 8, i.e. 0.5 again), and
 * 17 is the base the two levels are packed in. */
#define KS_MATERIAL_COVERAGE_LEVEL_MAX 16.0
#define KS_MATERIAL_COVERAGE_FRESNEL_BASE 17.0
#define KS_MATERIAL_COVERAGE_STEP 0.0036764705882352941

/* Fresnel: the AC default exponent the loader substitutes when the file has
 * none, the clamp the shaders apply to an authored one, and the Schlick power
 * used when no authored fresnel exists. */
#define KS_MATERIAL_FRESNEL_DEFAULT_EXP 5.0
#define KS_MATERIAL_FRESNEL_MIN_EXP 0.1
#define KS_MATERIAL_FRESNEL_MAX_EXP 64.0
#define KS_MATERIAL_FRESNEL_POWER 5.0

/* A pane's coverage is the AVERAGE of its texture's alpha, not the per-texel
 * value. A pane that is not alpha-tested has no holes to cut: the alpha such a
 * file carries is a dirt and scratch layer (the Countach's INTERNAL_glass.dds
 * holds 0..73, mean 16), and read per texel it multiplied the pane's own
 * reflection, painting a bright speck through every scratch instead of a
 * smooth sheen. The mip chain is a box filter, so sampling this LOD is that
 * average, while an authored tint gradient survives as a blurred version. */
#define KS_MATERIAL_PANE_ALPHA_LOD 5.0

/* The detail gain the loader writes when the detail slot carries the file's OWN
 * diffuse mapping: an occlusion bake moved there because it carries no colour
 * (see SimulationLoop). A real detail grain keeps 2.0, so the two values are
 * what tells the shaders which map the file's alpha test samples — the bake,
 * never the wear layer that took its place in the albedo. */
#define KS_MATERIAL_DETAIL_GAIN_BAKE 1.0
#define KS_MATERIAL_DETAIL_GAIN_NEUTRAL 2.0

/* Pane classification keywords. C++ only: GLSL never matches shader names,
 * it receives the resulting glassiness flag. */
#define KS_MATERIAL_GLASS_SHADER_REFLECTION "reflection"
#define KS_MATERIAL_GLASS_SHADER_WINDSCREEN "windscreen"

#endif /* KS_MATERIAL_RESPONSE_CONSTANTS_H */
