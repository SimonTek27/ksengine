#version 450

// Minimal forward-lit shader for the Qt-free NativeRenderer path used by
// SimulatorApp. Deliberately does not reuse pbr.vert/pbr.frag: those assume
// a material/IBL descriptor layout (albedo, normal, MR, IBL cube maps, ...)
// authored for the Qt-based editor's material system, which NativeRenderer
// has no equivalent for yet. This is a smaller, self-contained pipeline:
// one directional light + cascaded shadows, nothing else.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 mvp;
} pc;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragColor;
// Roadmap 2.3: mesh UV drives the albedo/normal sampling of descriptor set 1.
layout(location = 3) out vec2 fragUV;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(pc.model) * inNormal;
    fragColor = inColor;
    fragUV = inUV;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
