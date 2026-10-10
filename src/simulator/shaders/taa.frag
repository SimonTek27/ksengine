#version 450

// Temporal resolve. Sits between the deferred lighting pass (which renders
// into an offscreen HDR target) and the tonemap display pass, and writes its
// result to the history target — still *scene-linear HDR*, never gamma- or
// PQ-encoded: temporal accumulation has to happen before the display
// transform, and the display pass (tonemap.frag) is what turns this buffer
// into swapchain pixels (sRGB for the SDR path, PQ/Rec.2020 for HDR10).
//
// Reprojection uses the *unjittered* view/projection pair from the UBO, so
// the motion vector is jitter-free; the actual sample point is then the
// jittered pixel UV plus that motion. A 3x3 neighbourhood clamp on the
// history kills most of the ghosting that reprojection alone would leave
// behind (moving objects, camera cuts), and taaParams.x drops to 0 on the
// first frame after a resize so undefined history never shows.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outHistory;

layout(set = 0, binding = 0) uniform FrameData {
    vec4 sunDirection;
    vec4 sunColor;
    mat4 cascadeViewProj[3];
    vec4 cascadeSplits;
    vec4 cameraPos;
    mat4 viewProj;         // unjittered, current frame
    mat4 prevViewProj;     // unjittered, previous frame
    vec4 taaParams;        // x = history feedback (0 disables TAA)
    vec4 fogColor;         // declared to reach motionBlurParams below
    vec4 fogParams;        // (std140 offsets must be walked member by member)
    vec4 aoParams;
    vec4 ssrParams;
    vec4 motionBlurParams; // x = strength, y = sample count, z = max length, w = 1 when enabled
} frame;

layout(set = 0, binding = 1) uniform sampler2D gbufWorldPos;
layout(set = 0, binding = 2) uniform sampler2D currentHDR;
layout(set = 0, binding = 3) uniform sampler2D history;

// Motion blur (roadmap ksengine-vs-cryengine P0, KS_MOTIONBLUR). `motionblur.
// frag` on disk wants a velocity buffer this GBuffer has no free channel for,
// so the motion vector is reconstructed here from the world position and the
// unjittered previous view-projection - exactly what the reprojection below
// already does - and the gather runs on the *current* frame before the
// temporal blend, so history is never smeared twice.
vec3 motionBlur(vec3 cur, vec2 uv) {
    if (frame.motionBlurParams.w < 0.5) return cur;
    vec4 coverage = texture(gbufWorldPos, uv);
    // abs(): brief P6 signs coverage to -1 on brake-disc pixels; the
    // "drawn?" test must decode the magnitude (legacy pixels are +1).
    if (abs(coverage.w) < 0.5) return cur;           // sky does not smear

    vec4 curClip = frame.viewProj * vec4(coverage.xyz, 1.0);
    vec4 prevClip = frame.prevViewProj * vec4(coverage.xyz, 1.0);
    if (curClip.w <= 0.0 || prevClip.w <= 0.0) return cur;
    vec2 curUV = curClip.xy / curClip.w * 0.5 + 0.5;
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;

    vec2 vel = (curUV - prevUV) * frame.motionBlurParams.x;
    float speed = length(vel);
    float maxLen = frame.motionBlurParams.z;
    if (speed <= 1e-6) return cur;
    if (speed > maxLen) vel *= maxLen / speed;

    int n = int(frame.motionBlurParams.y);
    vec3 sum = cur;
    float wsum = 1.0;
    for (int i = 1; i <= n; ++i) {
        float t = float(i) / float(n);
        float w = 1.0 - t * 0.5;
        sum += (texture(currentHDR, uv + vel * t).rgb +
                texture(currentHDR, uv - vel * t).rgb) * w;
        wsum += 2.0 * w;
    }
    return sum / wsum;
}

void main() {
    vec3 cur = texture(currentHDR, vUV).rgb;
    cur = motionBlur(cur, vUV);
    float feedback = frame.taaParams.x;

    vec4 coverage = texture(gbufWorldPos, vUV);
    vec2 histUV = vUV;
    if (abs(coverage.w) > 0.5 && feedback > 0.0) {   // abs(): brief P6 disc flag in the sign
        vec4 curClip = frame.viewProj * vec4(coverage.xyz, 1.0);
        vec4 prevClip = frame.prevViewProj * vec4(coverage.xyz, 1.0);
        vec2 curUV = curClip.xy / curClip.w * 0.5 + 0.5;
        vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
        histUV = vUV + (prevUV - curUV);
    } else {
        feedback = 0.0;
    }

    if (histUV.x < 0.0 || histUV.x > 1.0 || histUV.y < 0.0 || histUV.y > 1.0) feedback = 0.0;

    vec3 result = cur;
    if (feedback > 0.0) {
        vec2 texel = 1.0 / vec2(textureSize(currentHDR, 0));
        vec3 nMin = vec3(1e20);
        vec3 nMax = vec3(-1e20);
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 s = texture(currentHDR, vUV + vec2(float(x), float(y)) * texel).rgb;
                nMin = min(nMin, s);
                nMax = max(nMax, s);
            }
        }
        vec3 hist = clamp(texture(history, histUV).rgb, nMin, nMax);
        result = mix(cur, hist, feedback);
    }

    outHistory = vec4(result, 1.0);
}
