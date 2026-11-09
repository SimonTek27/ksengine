#version 450

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform sampler2D mirrorTex;

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(mirrorTex, vUV).rgb;
    outColor = vec4(c, 1.0);
}
