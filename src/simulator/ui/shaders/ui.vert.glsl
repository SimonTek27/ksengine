#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform Push {
    vec2 screenSize; // width, height in pixels
} pc;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    // Pixel coords → NDC. Both spaces have Y pointing down (UI origin is the
    // top-left corner), so no flip is involved: a "1.0 - y" here — written for
    // an OpenGL NDC — drew the whole overlay upside down in Vulkan.
    vec2 ndc = vec2(
        (inPos.x / pc.screenSize.x) * 2.0 - 1.0,
        (inPos.y / pc.screenSize.y) * 2.0 - 1.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = inColor;
    vUV = inUV;
}
