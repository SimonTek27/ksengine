#include "DSPLibrary.h"
#include <cmath>
#include <algorithm>

namespace ks {
namespace audio {
namespace dsp {

// ─── Biquad Implementation ──────────────────────────────────────────────────

void Biquad::setLowpass(float freq, float q, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw = cosf(w0);
    
    b0 = (1.0f - cosw) * 0.5f;
    b1 = 1.0f - cosw;
    b2 = b0;
    a0 = 1.0f + alpha;
    a1 = -2.0f * cosw;
    a2 = 1.0f - alpha;
    normalize();
}

void Biquad::setHighpass(float freq, float q, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw = cosf(w0);
    
    b0 = (1.0f + cosw) * 0.5f;
    b1 = -(1.0f + cosw);
    b2 = b0;
    a0 = 1.0f + alpha;
    a1 = -2.0f * cosw;
    a2 = 1.0f - alpha;
    normalize();
}

void Biquad::setBandpass(float freq, float q, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw = cosf(w0);
    
    b0 = alpha;
    b1 = 0.0f;
    b2 = -alpha;
    a0 = 1.0f + alpha;
    a1 = -2.0f * cosw;
    a2 = 1.0f - alpha;
    normalize();
}

void Biquad::setNotch(float freq, float q, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw = cosf(w0);
    
    b0 = 1.0f;
    b1 = -2.0f * cosw;
    b2 = 1.0f;
    a0 = 1.0f + alpha;
    a1 = -2.0f * cosw;
    a2 = 1.0f - alpha;
    normalize();
}

void Biquad::setPeak(float freq, float q, float gainDb, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float A = powf(10.0f, gainDb / 40.0f);
    float alpha = sinf(w0) / (2.0f * q);
    float cosw = cosf(w0);
    
    b0 = 1.0f + alpha * A;
    b1 = -2.0f * cosw;
    b2 = 1.0f - alpha * A;
    a0 = 1.0f + alpha / A;
    a1 = -2.0f * cosw;
    a2 = 1.0f - alpha / A;
    normalize();
}

void Biquad::setLowShelf(float freq, float q, float gainDb, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float A = powf(10.0f, gainDb / 40.0f);
    float alpha = sinf(w0) / (2.0f * q) * sqrtf((A + 1.0f/A) * (1.0f/2.0f - 1.0f) + 2.0f);
    float cosw = cosf(w0);
    float sqrtA = sqrtf(A);
    
    b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha);
    b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw);
    b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha);
    a0 = (A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;
    a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw);
    a2 = (A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha;
    normalize();
}

void Biquad::setHighShelf(float freq, float q, float gainDb, float sampleRate) {
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float A = powf(10.0f, gainDb / 40.0f);
    float alpha = sinf(w0) / (2.0f * q) * sqrtf((A + 1.0f/A) * (1.0f/2.0f - 1.0f) + 2.0f);
    float cosw = cosf(w0);
    float sqrtA = sqrtf(A);
    
    b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * sqrtA * alpha);
    b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * sqrtA * alpha);
    a0 = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * sqrtA * alpha;
    a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw);
    a2 = (A + 1.0f) - (A - 1.0f) * cosw - 2.0f * sqrtA * alpha;
    normalize();
}

void Biquad::normalize() {
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;
    a0 = 1.0f;
}

float Biquad::process(float x) {
    float y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;  // Wait, need temp
    float out = b0 * x + z1;
    z1 = b1 * x - a1 * out + z2;
    z2 = b2 * x - a2 * out;
    return out;
}

void Biquad::processBlock(const float* in, float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        out[i] = process(in[i]);
    }
}

// ─── OnePole Implementation ─────────────────────────────────────────────────

void OnePole::setLowpass(float freq) {
    float w = 2.0f * M_PI * freq / sampleRate;
    float g = tanf(w * 0.5f);
    a0 = g / (1.0f + g);
    b1 = (1.0f - g) / (1.0f + g);
}

void OnePole::setHighpass(float freq) {
    float w = 2.0f * M_PI * freq / sampleRate;
    float g = tanf(w * 0.5f);
    a0 = 1.0f / (1.0f + g);
    b1 = (1.0f - g) / (1.0f + g);
}

float OnePole::process(float x) {
    float out = a0 * x + b1 * z;
    z = out;
    return out;
}

void OnePole::processBlock(const float* in, float* out, int frames) {
    for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
}

// ─── Compressor Implementation ──────────────────────────────────────────────

// Configured in header

// ─── Limiter Implementation ─────────────────────────────────────────────────

// Configured in header

// ─── Gate Implementation ────────────────────────────────────────────────────

// Configured in header

// ─── DelayLine Implementation ───────────────────────────────────────────────

// Configured in header

// ─── Reverb Implementation ──────────────────────────────────────────────────

// Configured in header

// ─── Chorus Implementation ──────────────────────────────────────────────────

// Configured in header

// ─── SoftClipper Implementation ─────────────────────────────────────────────

// Process in header

// ─── WaveShaper Implementation ──────────────────────────────────────────────

// Process in header

} // namespace dsp
} // namespace audio
} // namespace ks