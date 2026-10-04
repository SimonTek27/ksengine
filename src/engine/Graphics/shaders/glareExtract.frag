#version 450

// Bright-pass extraction for the bloom chain. Samples the HDR scene color
// and keeps only the portion above `threshold`, with a soft knee so the
// transition isn't a hard clip (matches how most bloom-capable engines,
// CryEngine included, avoid a harsh cutoff in the extract pass). Driven by
// NativeRenderer's deferred path: set 0 binding 0 is the TAA-resolved frame,
// the result is blurred by bloomBlur.frag and folded back in by tonemap.frag.

layout(location = 0) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform PushConstants {
    float threshold;
    float knee;
    float pad0;
    float pad1;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(sceneColor, fragTexCoord).rgb;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft-knee threshold curve (same shape as the Unreal/CryEngine-style
    // bloom extract: quadratic ramp inside the knee, hard pass above it).
    float knee = max(pc.knee, 1e-4);
    float soft = luma - pc.threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, luma - pc.threshold);
    contribution = max(contribution, 0.0);

    float weight = contribution / max(luma, 1e-5);
    outColor = vec4(color * weight, 1.0);
}
