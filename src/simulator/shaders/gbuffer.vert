#version 450

// Vertex stage of the deferred GBuffer pass. Byte-identical inputs and push
// constants to native_forward.vert so a single vertex-input state and a single
// pipeline layout can drive both the forward and the deferred geometry pass.

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
