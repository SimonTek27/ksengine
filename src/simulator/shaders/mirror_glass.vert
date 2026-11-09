#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

layout(location = 0) out vec2 vUV;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vUV = inUV;
}
