#version 450

// One axis of the bloom blur: a 9-tap separable gaussian run twice by
// NativeRenderer (horizontal over the bright-pass result, then vertical back
// over it) at half resolution. `dir` is one texel step along the blur axis,
// so the same shader handles both passes.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcColor;

layout(push_constant) uniform BlurPC {
    vec2 dir;     // texel step along the blur axis
    float pad0;
    float pad1;
} pc;

void main() {
    const float w[5] = float[](
        0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);

    vec3 c = texture(srcColor, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 o = pc.dir * float(i);
        c += texture(srcColor, vUV + o).rgb * w[i];
        c += texture(srcColor, vUV - o).rgb * w[i];
    }
    outColor = vec4(c, 1.0);
}
