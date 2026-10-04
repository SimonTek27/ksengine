#version 450

// Billboarded particle vertex stage (roadmap ksengine-vs-cryengine P1).
// One vertex shader drives both pipelines the sprite is drawn with: the
// forward pass (alpha blended, particle.frag) and the deferred GBuffer pass
// (particle_gbuffer.frag). Inputs are identical, so the two pipelines share
// one vertex-input state.
//
// Vertex data is the flat float[12] record ParticleSystem::buildQuads packs,
// read back-to-back from a single binding:
//
//   offset  0  vec3  world position of the particle centre
//   offset 12  vec2  sprite corner in 0..1 (the quad this vertex belongs to)
//   offset 20  vec4  tint (rgb) + base alpha (a)
//   offset 36  float size (metres, half-extent along each billboard axis)
//   offset 40  float life remaining
//   offset 44  float max life
//
// Push constants (112 bytes, vertex stage): the frame's view-projection, the
// camera basis the quad is built in (so the sprite never shears when the
// camera rolls), and a params vector — x is the pass-wide alpha multiplier,
// yzw are reserved.

layout(push_constant) uniform ParticlePC {
    mat4 viewProj;     // 0
    vec4 cameraRight;  // 64  camera right axis in world space
    vec4 cameraUp;     // 80  camera up axis in world space
    vec4 params;       // 96  x = global alpha, yzw reserved
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inSize;
layout(location = 4) in float inLife;
layout(location = 5) in float inMaxLife;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec3 fragNormal;

void main() {
    // Fraction of the life still left: 1 the moment it spawns (life ==
    // maxLife), 0 the moment it dies (life is decremented by the sim, so the
    // sprite fades out over its life) and a particle reaped mid-frame can
    // never flash a full-alpha quad.
    float remaining = clamp(inLife / max(inMaxLife, 0.001), 0.0, 1.0);
    float alpha = inColor.a * pc.params.x * remaining;
    alpha *= step(0.001, inLife);

    // UV (0..1) -> quad space (-1..1): the four corners of the sprite, then
    // scaled by the particle's size along the camera basis.
    vec2 corner = inUV * 2.0 - 1.0;
    vec3 offset = pc.cameraRight.xyz * (corner.x * inSize) +
                  pc.cameraUp.xyz * (corner.y * inSize);
    vec3 worldPos = inPosition + offset;

    fragUV = inUV;
    fragColor = vec4(inColor.rgb, alpha);
    fragWorldPos = worldPos;
    // right x up points back down the view axis, i.e. at the eye, so the
    // deferred pass lights the sprite as a surface facing the camera.
    fragNormal = normalize(cross(pc.cameraRight.xyz, pc.cameraUp.xyz));

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
}
