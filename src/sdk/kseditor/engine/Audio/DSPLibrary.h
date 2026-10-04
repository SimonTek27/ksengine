#pragma once

#include <QObject>
#include <QVector>
#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>

namespace ks {
namespace audio {
namespace dsp {

// ─── Biquad Filter (Direct Form II Transposed) ──────────────────────────────

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0;
    float a0 = 1, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void setLowpass(float freq, float q, float sampleRate);
    void setHighpass(float freq, float q, float sampleRate);
    void setBandpass(float freq, float q, float sampleRate);
    void setNotch(float freq, float q, float sampleRate);
    void setPeak(float freq, float q, float gainDb, float sampleRate);
    void setLowShelf(float freq, float q, float gainDb, float sampleRate);
    void setHighShelf(float freq, float q, float gainDb, float sampleRate);
    void setAllpass(float freq, float q, float sampleRate);

    void normalize();
    float process(float x);
    void processBlock(const float* in, float* out, int frames);
    void reset() { z1 = z2 = 0; }
};

// ─── OnePole Filter ──────────────────────────────────────────────────────────

struct OnePole {
    float a0 = 1, b1 = 0;
    float z = 0;
    float sampleRate = 44100;

    void setLowpass(float freq);
    void setHighpass(float freq);
    float process(float x);
    void processBlock(const float* in, float* out, int frames);
    void reset() { z = 0; }
};

// ─── Compressor ──────────────────────────────────────────────────────────────

struct Compressor {
    float thresholdDb = -12.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float kneeDb = 6.0f;
    float makeupGainDb = 0.0f;
    float sampleRate = 44100;

    float env = 0;
    float gain = 1;

    float dbToLinear(float db) const { return powf(10.0f, db / 20.0f); }
    float linearToDb(float lin) const { return 20.0f * log10f(fmaxf(lin, 1e-10f)); }

    void prepare(float sr) { sampleRate = sr; }
    float process(float x);
    void processBlock(const float* in, float* out, int frames);
    void reset() { env = 0; gain = 1; }
};

// ─── Limiter ─────────────────────────────────────────────────────────────────

struct Limiter {
    float ceilingDb = -0.5f;
    float releaseMs = 50.0f;
    float sampleRate = 44100;

    float env = 0;
    float gain = 1;

    void prepare(float sr) { sampleRate = sr; }
    float process(float x);
    void processBlock(const float* in, float* out, int frames);
    void reset() { env = 0; gain = 1; }
};

// ─── Gate / Expander ─────────────────────────────────────────────────────────

struct Gate {
    float thresholdDb = -40.0f;
    float ratio = 10.0f;
    float attackMs = 1.0f;
    float releaseMs = 100.0f;
    float holdMs = 10.0f;
    float sampleRate = 44100;

    float env = 0;
    float gain = 1;
    int holdSamples = 0;
    int holdCounter = 0;

    void prepare(float sr) { sampleRate = sr; holdSamples = int(holdMs * sr / 1000.0f); }
    float process(float x);
    void processBlock(const float* in, float* out, int frames);
    void reset() { env = 0; gain = 1; holdCounter = 0; }
};

// ─── Delay Line ──────────────────────────────────────────────────────────────

struct DelayLine {
    QVector<float> buffer;
    int writePos = 0;
    float feedback = 0.5f;
    float mix = 0.5f;
    float sampleRate = 44100;

    void prepare(float sr) { sampleRate = sr; }

    void setDelayMs(float ms) {
        int len = int(ms * sampleRate / 1000.0f);
        if (len != buffer.size()) {
            buffer.resize(fmax(len, 1));
            buffer.fill(0);
            writePos = 0;
        }
    }

    float process(float x) {
        int readPos = writePos - buffer.size();
        if (readPos < 0) readPos += buffer.size();
        
        float y = buffer[readPos];
        buffer[writePos] = x + y * feedback;
        writePos = (writePos + 1) % buffer.size();
        
        return x + y * mix;
    }

    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }

    void reset() { buffer.fill(0); writePos = 0; }
};

// ─── Reverb (Schroeder/Moorer) ──────────────────────────────────────────────

struct Reverb {
    struct Comb {
        DelayLine delay;
        float damp = 0.5f;
        float lowpassZ = 0;
        void setDelayMs(float ms, float sr) { delay.sampleRate = sr; delay.setDelayMs(ms); }
        float process(float x) {
            float y = delay.process(x);
            lowpassZ = y * (1.0f - damp) + lowpassZ * damp;
            return lowpassZ;
        }
    };
    
    struct Allpass {
        DelayLine delay;
        float gain = 0.5f;
        void setDelayMs(float ms, float sr) { delay.sampleRate = sr; delay.setDelayMs(ms); }
        float process(float x) {
            float buf = delay.buffer[(delay.writePos - delay.buffer.size()) % delay.buffer.size()];
            float y = -x + buf + gain * delay.process(x);
            return y;
        }
    };

    QVector<Comb> combs;
    QVector<Allpass> allpasses;
    float wet = 0.3f;
    float dry = 0.5f;
    float sampleRate = 44100;
    float roomSize = 0.5f;
    float damping = 0.5f;

    void prepare(float sr) {
        sampleRate = sr;
        combs.resize(4);
        allpasses.resize(2);
        
        float combDelays[4] = {1116, 1188, 1277, 1356};
        for (int i = 0; i < 4; ++i) {
            combs[i].setDelayMs(combDelays[i] * roomSize * 0.001f, sr);
            combs[i].damp = damping;
        }
        float apDelays[2] = {225, 556};
        for (int i = 0; i < 2; ++i) {
            allpasses[i].setDelayMs(apDelays[i] * roomSize * 0.001f, sr);
            allpasses[i].gain = 0.5f;
        }
    }

    float process(float x) {
        float y = 0;
        for (auto& c : combs) y += c.process(x);
        for (auto& a : allpasses) y = a.process(y);
        return x * dry + y * wet;
    }

    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }

    void reset() {
        for (auto& c : combs) c.delay.reset();
        for (auto& a : allpasses) a.delay.reset();
    }
};

// ─── Chorus ──────────────────────────────────────────────────────────────────

struct Chorus {
    DelayLine delay;
    float depth = 0.003f;   // seconds
    float rate = 1.5f;      // Hz
    float mix = 0.5f;
    float phase = 0;
    float sampleRate = 44100;

    void prepare(float sr) { 
        sampleRate = sr; 
        delay.sampleRate = sr; 
        delay.setDelayMs(20.0f); 
    }

    float process(float x) {
        phase += 2.0f * M_PI * rate / sampleRate;
        if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        
        float modDelay = depth * (0.5f + 0.5f * sinf(phase)) * sampleRate;
        int baseDelay = delay.buffer.size();
        int readPos = delay.writePos - int(baseDelay + modDelay);
        if (readPos < 0) readPos += baseDelay;
        
        float y = delay.buffer[readPos];
        delay.buffer[delay.writePos] = x;
        delay.writePos = (delay.writePos + 1) % baseDelay;
        
        return x + y * mix;
    }

    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }

    void reset() { delay.reset(); phase = 0; }
};

// ─── Flanger ─────────────────────────────────────────────────────────────────

struct Flanger {
    DelayLine delay;
    float depth = 0.005f;
    float rate = 0.5f;
    float feedback = 0.7f;
    float mix = 0.5f;
    float phase = 0;
    float sampleRate = 44100;

    void prepare(float sr) { sampleRate = sr; delay.sampleRate = sr; delay.setDelayMs(5.0f); }

    float process(float x) {
        phase += 2.0f * M_PI * rate / sampleRate;
        if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        
        float modDelay = depth * (0.5f + 0.5f * sinf(phase)) * sampleRate;
        int readPos = delay.writePos - int(modDelay);
        if (readPos < 0) readPos += delay.buffer.size();
        
        float y = delay.buffer[readPos];
        float out = x + y * mix;
        delay.buffer[delay.writePos] = x + y * feedback;
        delay.writePos = (delay.writePos + 1) % delay.buffer.size();
        
        return out;
    }

    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
    void reset() { delay.reset(); phase = 0; }
};

// ─── Phaser ──────────────────────────────────────────────────────────────────

struct Phaser {
    struct Stage {
        float freq = 1000, q = 1.0;
        float a0, a1, a2, b0, b1, b2;
        float z1 = 0, z2 = 0;
        void setParams(float f, float q_, float sr) {
            freq = f; q = q_;
            float w = 2 * M_PI * freq / sr;
            float alpha = sinf(w) / (2 * q);
            float cosw = cosf(w);
            b0 = 1 + alpha; b1 = -2*cosw; b2 = 1 - alpha;
            a0 = 1 + alpha; a1 = -2*cosw; a2 = 1 - alpha;
            // Normalize
            b0 /= a0; b1 /= a0; b2 /= a0;
            a1 /= a0; a2 /= a0;
        }
        float process(float x) {
            float y = b0*x + b1*z1 + b2*z2 - a1*z1 - a2*z2;
            z2 = z1;
            z1 = y;
            return y;
        }
    };

    QVector<Stage> stages;
    float lfoRate = 0.5f, lfoDepth = 0.5f, feedback = 0, mix = 0.5f;
    float sampleRate = 44100, phase = 0;

    void prepare(float sr, int numStages = 6) {
        sampleRate = sr;
        stages.resize(numStages);
        for (auto& s : stages) s.setParams(1000, 1.0, sr);
    }

    float process(float x) {
        phase += 2.0f * M_PI * lfoRate / sampleRate;
        if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
        
        float lfo = 0.5f + 0.5f * sinf(phase);
        float y = x + feedback * y; // Will use last output
        
        for (auto& s : stages) {
            s.freq = 200 + lfo * lfoDepth * 4000;
            s.setParams(s.freq, s.q, sampleRate);
            y = s.process(y);
        }
        
        return x * (1.0f - mix) + y * mix;
    }

    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
    void reset() { for (auto& s : stages) s.z1 = s.z2 = 0; }
};

// ─── Distortion / Waveshaping ────────────────────────────────────────────────

struct SoftClipper {
    float drive = 1.0f;
    float mix = 1.0f;
    float process(float x) {
        float y = tanhf(x * drive);
        return x * (1.0f - mix) + y * mix;
    }
    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
};

struct HardClipper {
    float threshold = 0.5f;
    float mix = 1.0f;
    float process(float x) {
        float y = fmaxf(-threshold, fminf(threshold, x));
        return x * (1.0f - mix) + y * mix;
    }
    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
};

struct WaveShaper {
    QVector<float> table;
    float drive = 1.0f;
    float mix = 1.0f;
    
    void setTable(const QVector<float>& t) { table = t; }
    
    // Chebyshev polynomials for harmonic distortion
    void generateChebyshev(int order, float amount) {
        table.resize(1025);
        for (int i = 0; i <= 1024; ++i) {
            float x = (i - 512) / 512.0f;
            float y = x;
            float Tn_1 = 1, Tn = x;
            for (int n = 2; n <= order; ++n) {
                float Tn1 = 2 * x * Tn - Tn_1;
                Tn_1 = Tn; Tn = Tn1;
            }
            y += Tn * amount;
            table[i] = fmaxf(-1.0f, fminf(1.0f, y));
        }
    }
    
    float process(float x) {
        if (table.empty()) return x;
        float idx = (x * drive + 1.0f) * 512.0f;
        int i = int(floorf(idx));
        float frac = idx - i;
        i = fmaxf(0, fminf(1023, i));
        float y = table[i] + frac * (table[i+1] - table[i]);
        return x * (1.0f - mix) + y * mix;
    }
    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
};

// ─── EQ (Multi-band) ─────────────────────────────────────────────────────────

struct ParametricEQ {
    struct Band {
        Biquad filter;
        float freq = 1000, q = 1.0f, gainDb = 0;
        bool enabled = true;
        int type = 0; // 0=peak, 1=lowshelf, 2=highshelf, 3=lowpass, 4=highpass
    };
    
    QVector<Band> bands;
    float sampleRate = 44100;
    
    void prepare(float sr, int numBands = 6) {
        sampleRate = sr;
        bands.resize(numBands);
        float freqs[] = {60, 250, 1000, 4000, 8000, 16000};
        for (int i = 0; i < numBands; ++i) {
            bands[i].freq = freqs[i];
            bands[i].filter.setPeak(freqs[i], 1.0f, 0, sr);
        }
    }
    
    void setBand(int i, float freq, float q, float gainDb, int type = 0) {
        if (i < 0 || i >= bands.size()) return;
        auto& b = bands[i];
        b.freq = freq; b.q = q; b.gainDb = gainDb; b.type = type;
        
        switch (type) {
            case 0: b.filter.setPeak(freq, q, gainDb, sampleRate); break;
            case 1: b.filter.setLowShelf(freq, q, gainDb, sampleRate); break;
            case 2: b.filter.setHighShelf(freq, q, gainDb, sampleRate); break;
            case 3: b.filter.setLowpass(freq, q, sampleRate); break;
            case 4: b.filter.setHighpass(freq, q, sampleRate); break;
        }
    }
    
    float process(float x) {
        for (auto& b : bands) if (b.enabled) x = b.filter.process(x);
        return x;
    }
    
    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
    
    void reset() { for (auto& b : bands) b.filter.reset(); }
};

// ─── Pitch Shifter (Simple) ─────────────────────────────────────────────────

struct PitchShifter {
    DelayLine delay;
    float semitones = 0;
    float mix = 1.0f;
    float sampleRate = 44100;
    float phase = 0;
    int windowSize = 1024;
    
    void prepare(float sr) { sampleRate = sr; delay.sampleRate = sr; delay.setDelayMs(20.0f); }
    
    float process(float x) {
        float ratio = powf(2.0f, semitones / 12.0f);
        phase += ratio - 1.0f;
        if (phase > 1.0f) phase -= 1.0f;
        if (phase < 0.0f) phase += 1.0f;
        
        // Simple delay-based pitch shift (low quality)
        float modDelay = phase * windowSize;
        int readPos = delay.writePos - int(modDelay);
        if (readPos < 0) readPos += delay.buffer.size();
        
        float y = delay.buffer[readPos];
        delay.buffer[delay.writePos] = x;
        delay.writePos = (delay.writePos + 1) % delay.buffer.size();
        
        return x * (1.0f - mix) + y * mix;
    }
    
    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
    void reset() { delay.reset(); phase = 0; }
};

// ─── Spectrum Analyzer ──────────────────────────────────────────────────────

struct SpectrumAnalyzer {
    int fftSize = 1024;
    int hopSize = 256;
    QVector<float> window;
    QVector<std::complex<float>> fftBuffer;
    QVector<float> magnitudes;
    QVector<float> peakHold;
    int writePos = 0;
    float sampleRate = 44100;
    
    void prepare(int size, float sr) {
        fftSize = size; hopSize = size / 4; sampleRate = sr;
        window.resize(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fftSize - 1))); // Hann
        }
        fftBuffer.resize(fftSize);
        magnitudes.resize(fftSize / 2);
        peakHold.resize(fftSize / 2);
        peakHold.fill(-120.0f);
    }
    
    void process(float x) {
        fftBuffer[writePos] = std::complex<float>(x * window[writePos], 0);
        writePos = (writePos + 1) % fftSize;
        
        if (writePos % hopSize == 0) computeFFT();
    }
    
    void processBlock(const float* in, int frames) {
        for (int i = 0; i < frames; ++i) process(in[i]);
    }
    
    void computeFFT() {
        // Simple DFT (replace with FFTW/kissfft in production)
        for (int k = 0; k < fftSize / 2; ++k) {
            std::complex<float> sum(0, 0);
            for (int n = 0; n < fftSize; ++n) {
                float angle = -2.0f * M_PI * k * n / fftSize;
                sum += fftBuffer[n] * std::polar(1.0f, angle);
            }
            float mag = std::abs(sum);
            magnitudes[k] = 20.0f * log10f(fmaxf(mag / fftSize, 1e-10f));
            peakHold[k] = fmaxf(peakHold[k] * 0.99f, magnitudes[k]);
        }
    }
    
    const QVector<float>& getMagnitudes() const { return magnitudes; }
    const QVector<float>& getPeakHold() const { return peakHold; }
    float getBinFrequency(int bin) const { return bin * sampleRate / fftSize; }
};

// ─── Utility Functions ──────────────────────────────────────────────────────

inline float dbToGain(float db) { return powf(10.0f, db / 20.0f); }
inline float gainToDb(float gain) { return 20.0f * log10f(fmaxf(gain, 1e-10f)); }
inline float hzToMel(float hz) { return 2595.0f * log10f(1.0f + hz / 700.0f); }
inline float melToHz(float mel) { return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f); }

// Linear interpolation
inline float lerp(float a, float b, float t) { return a + t * (b - a); }
inline float smoothstep(float edge0, float edge1, float x) {
    float t = fmaxf(0.0f, fminf(1.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

// ─── Peak Meter ──────────────────────────────────────────────────────────────

struct PeakMeter {
    float sampleRate = 44100;
    float attackMs = 10.0f;
    float releaseMs = 300.0f;
    float env = 0;
    float peak = 0;
    float rmsAccum = 0;
    int rmsCount = 0;
    float attackCoeff = 0;
    float releaseCoeff = 0;

    void configure(float sr) {
        sampleRate = sr;
        attackCoeff = expf(-1.0f / (attackMs * 0.001f * sr));
        releaseCoeff = expf(-1.0f / (releaseMs * 0.001f * sr));
    }

    float process(float x) {
        float absX = fabsf(x);
        if (absX > env) env = attackCoeff * env + (1.0f - attackCoeff) * absX;
        else env = releaseCoeff * env + (1.0f - releaseCoeff) * absX;
        
        if (absX > peak) peak = absX;
        else peak *= 0.999f; // slow decay
        
        rmsAccum += x * x;
        rmsCount++;
        return env;
    }

    void processBlock(const float* in, int frames) {
        for (int i = 0; i < frames; ++i) process(in[i]);
    }

    float getPeakDb() const { return gainToDb(peak); }
    float getRmsDb() const { return rmsCount > 0 ? gainToDb(sqrtf(rmsAccum / rmsCount)) : -120.0f; }
    float getEnvDb() const { return gainToDb(env); }

    void reset() { 
        env = 0; 
        peak = 0; 
        rmsAccum = 0; 
        rmsCount = 0; 
    }

private:
    static float gainToDb(float gain) {
        return gain > 0 ? 20.0f * log10f(gain) : -120.0f;
    }
};

// ─── Envelope Follower ──────────────────────────────────────────────────────

struct EnvelopeFollower {
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float sampleRate = 44100;
    float env = 0;
    float attackCoeff = 0, releaseCoeff = 0;
    
    void prepare(float sr) {
        sampleRate = sr;
        attackCoeff = expf(-1.0f / (attackMs * 0.001f * sr));
        releaseCoeff = expf(-1.0f / (releaseMs * 0.001f * sr));
    }
    
    float process(float x) {
        float absX = fabsf(x);
        if (absX > env) env = attackCoeff * env + (1.0f - attackCoeff) * absX;
        else env = releaseCoeff * env + (1.0f - releaseCoeff) * absX;
        return env;
    }
    
    void processBlock(const float* in, float* out, int frames) {
        for (int i = 0; i < frames; ++i) out[i] = process(in[i]);
    }
    
    void reset() { env = 0; }
};

} // namespace dsp
} // namespace audio
} // namespace ks