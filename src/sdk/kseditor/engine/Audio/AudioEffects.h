#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QFile>
#include <QAudioFormat>
#include <cmath>
#include <complex>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>

namespace ks {
    namespace audio {

        // ============================================================================
        // FORWARD DECLARATIONS
        // ============================================================================

        class AudioEffects;

        // ============================================================================
        // AUDIO EFFECTS EXTRA - Spectral Processing
        // ============================================================================

        class SpectralGate : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float threshold READ threshold WRITE setThreshold)
                Q_PROPERTY(float floor READ floor WRITE setFloor)
                Q_PROPERTY(float attack READ attack WRITE setAttack)
                Q_PROPERTY(float release READ release WRITE setRelease)
                Q_PROPERTY(float lowCut READ lowCut WRITE setLowCut)
                Q_PROPERTY(float highCut READ highCut WRITE setHighCut)
        public:
            explicit SpectralGate(QObject* parent = nullptr) : QObject(parent) {}
            ~SpectralGate() {}

            void setThreshold(float db) { m_threshold = db; }
            float threshold() const { return m_threshold; }
            void setFloor(float db) { m_floor = db; }
            float floor() const { return m_floor; }
            void setAttack(float ms) { m_attack = qBound(0.1f, ms, 500.0f); }
            float attack() const { return m_attack; }
            void setRelease(float ms) { m_release = qBound(10.0f, ms, 2000.0f); }
            float release() const { return m_release; }
            void setLowCut(float hz) { m_lowCut = qBound(20.0f, hz, 20000.0f); }
            float lowCut() const { return m_lowCut; }
            void setHighCut(float hz) { m_highCut = qBound(20.0f, hz, 20000.0f); }
            float highCut() const { return m_highCut; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float thresholdLin = std::pow(10.0f, m_threshold / 20.0f);
                float floorLin = std::pow(10.0f, m_floor / 20.0f);
                float attackCoeff = std::exp(-1.0f / (m_attack * 0.001f * sampleRate));
                float releaseCoeff = std::exp(-1.0f / (m_release * 0.001f * sampleRate));
                float lowNorm = m_lowCut / (sampleRate * 0.5f);
                float highNorm = m_highCut / (sampleRate * 0.5f);

                for (int i = 0; i < input.size(); ++i) {
                    float s = input[i];
                    float absS = std::abs(s);

                    float envelope = (absS > m_env)
                        ? attackCoeff * m_env + (1.0f - attackCoeff) * absS
                        : releaseCoeff * m_env + (1.0f - releaseCoeff) * absS;
                    m_env = envelope;

                    float gain = 1.0f;
                    if (envelope < thresholdLin && envelope > 0.0001f) {
                        float dbDiff = 20.0f * std::log10(envelope / thresholdLin);
                        float reduction = std::max(0.0f, 1.0f + dbDiff / (-m_floor));
                        gain = std::max(floorLin / thresholdLin, reduction);
                    }

                    output[i] = s * gain;
                }

                return output;
            }

            void reset() { m_env = 0.0f; }

        private:
            float m_threshold = -40.0f;
            float m_floor = -80.0f;
            float m_attack = 5.0f;
            float m_release = 100.0f;
            float m_lowCut = 20.0f;
            float m_highCut = 20000.0f;
            float m_env = 0.0f;
        };

        class FormantFilterState {
        public:
            float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

            void designBP(float freq, float bw, int sampleRate) {
                float w0 = 2.0f * M_PI * freq / sampleRate;
                float alpha = std::sin(w0) * std::sinh(std::log(2.0f) * 0.5f * bw * w0 / std::sin(w0));
                float norm = 1.0f + alpha;
                b0 = alpha / norm;
                b1 = 0;
                b2 = -alpha / norm;
                a1 = -2.0f * std::cos(w0) / norm;
                a2 = (1.0f - alpha) / norm;
            }

            float process(float x) {
                float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x;
                y2 = y1; y1 = y;
                return y;
            }
        };

        class FormantFilter : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float shift READ shift WRITE setShift)
                Q_PROPERTY(float formant1Freq READ formant1Freq WRITE setFormant1Freq)
                Q_PROPERTY(float formant1Gain READ formant1Gain WRITE setFormant1Gain)
                Q_PROPERTY(float formant1BW READ formant1BW WRITE setFormant1BW)
                Q_PROPERTY(float formant2Freq READ formant2Freq WRITE setFormant2Freq)
                Q_PROPERTY(float formant2Gain READ formant2Gain WRITE setFormant2Gain)
                Q_PROPERTY(float formant2BW READ formant2BW WRITE setFormant2BW)
                Q_PROPERTY(float formant3Freq READ formant3Freq WRITE setFormant3Freq)
                Q_PROPERTY(float formant3Gain READ formant3Gain WRITE setFormant3Gain)
                Q_PROPERTY(float formant3BW READ formant3BW WRITE setFormant3BW)
                Q_PROPERTY(float formant4Freq READ formant4Freq WRITE setFormant4Freq)
                Q_PROPERTY(float formant4Gain READ formant4Gain WRITE setFormant4Gain)
                Q_PROPERTY(float formant4BW READ formant4BW WRITE setFormant4BW)
                Q_PROPERTY(float formant5Freq READ formant5Freq WRITE setFormant5Freq)
                Q_PROPERTY(float formant5Gain READ formant5Gain WRITE setFormant5Gain)
                Q_PROPERTY(float formant5BW READ formant5BW WRITE setFormant5BW)
        public:
            explicit FormantFilter(QObject* parent = nullptr) : QObject(parent) {
                m_sr = 44100;
                m_formants[0] = { 800, 0, 100 };
                m_formants[1] = { 2200, 0, 120 };
                m_formants[2] = { 3500, 0, 150 };
                m_formants[3] = { 4500, 0, 200 };
                m_formants[4] = { 5500, 0, 300 };
                for (int f = 0; f < 5; ++f) updateFilter(f);
            }

            void setShift(float st) { m_shift = qBound(-12.0f, st, 12.0f); rebuildAll(); }
            float shift() const { return m_shift; }

            void setFormant1Freq(float hz) { m_formants[0].freq = qBound(100.0f, hz, 4000.0f); updateFilter(0); }
            float formant1Freq() const { return m_formants[0].freq; }
            void setFormant1Gain(float db) { m_formants[0].gain = qBound(-24.0f, db, 24.0f); }
            float formant1Gain() const { return m_formants[0].gain; }
            void setFormant1BW(float hz) { m_formants[0].bw = qBound(20.0f, hz, 1000.0f); updateFilter(0); }
            float formant1BW() const { return m_formants[0].bw; }

            void setFormant2Freq(float hz) { m_formants[1].freq = qBound(500.0f, hz, 6000.0f); updateFilter(1); }
            float formant2Freq() const { return m_formants[1].freq; }
            void setFormant2Gain(float db) { m_formants[1].gain = qBound(-24.0f, db, 24.0f); }
            float formant2Gain() const { return m_formants[1].gain; }
            void setFormant2BW(float hz) { m_formants[1].bw = qBound(20.0f, hz, 1000.0f); updateFilter(1); }
            float formant2BW() const { return m_formants[1].bw; }

            void setFormant3Freq(float hz) { m_formants[2].freq = qBound(1000.0f, hz, 8000.0f); updateFilter(2); }
            float formant3Freq() const { return m_formants[2].freq; }
            void setFormant3Gain(float db) { m_formants[2].gain = qBound(-24.0f, db, 24.0f); }
            float formant3Gain() const { return m_formants[2].gain; }
            void setFormant3BW(float hz) { m_formants[2].bw = qBound(20.0f, hz, 1000.0f); updateFilter(2); }
            float formant3BW() const { return m_formants[2].bw; }

            void setFormant4Freq(float hz) { m_formants[3].freq = qBound(2000.0f, hz, 12000.0f); updateFilter(3); }
            float formant4Freq() const { return m_formants[3].freq; }
            void setFormant4Gain(float db) { m_formants[3].gain = qBound(-24.0f, db, 24.0f); }
            float formant4Gain() const { return m_formants[3].gain; }
            void setFormant4BW(float hz) { m_formants[3].bw = qBound(20.0f, hz, 1000.0f); updateFilter(3); }
            float formant4BW() const { return m_formants[3].bw; }

            void setFormant5Freq(float hz) { m_formants[4].freq = qBound(3000.0f, hz, 16000.0f); updateFilter(4); }
            float formant5Freq() const { return m_formants[4].freq; }
            void setFormant5Gain(float db) { m_formants[4].gain = qBound(-24.0f, db, 24.0f); }
            float formant5Gain() const { return m_formants[4].gain; }
            void setFormant5BW(float hz) { m_formants[4].bw = qBound(20.0f, hz, 1000.0f); updateFilter(4); }
            float formant5BW() const { return m_formants[4].bw; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                if (input.isEmpty()) return input;
                if (m_sr != sampleRate) { m_sr = sampleRate; rebuildAll(); }
                QVector<float> output(input.size());

                for (int i = 0; i < input.size(); ++i) {
                    float s = input[i];
                    float out = 0.0f;
                    for (int f = 0; f < 5; ++f) {
                        float filtered = m_filters[f].process(s);
                        float gainLin = std::pow(10.0f, m_formants[f].gain / 20.0f);
                        out += filtered * gainLin;
                    }
                    output[i] = out * 0.2f;
                }
                return output;
            }

            void reset() {
                for (int f = 0; f < 5; ++f)
                    m_filters[f] = FormantFilterState();
                rebuildAll();
            }

            void setSampleRate(int sr) { m_sr = sr; }

        private:
            struct Formant {
                float freq, gain, bw;
            };

            void updateFilter(int idx) {
                m_filters[idx].designBP(m_formants[idx].freq, m_formants[idx].bw, m_sr);
            }

            void rebuildAll() {
                for (int f = 0; f < 5; ++f) {
                    float shiftedFreq = m_formants[f].freq * std::pow(2.0f, m_shift / 12.0f);
                    m_filters[f].designBP(shiftedFreq, m_formants[f].bw, m_sr);
                }
            }

            float m_shift = 0.0f;
            int m_sr = 44100;
            Formant m_formants[5];
            FormantFilterState m_filters[5];
        };

        class RingMod : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float frequency READ frequency WRITE setFrequency)
                Q_PROPERTY(float mix READ mix WRITE setMix)
        public:
            explicit RingMod(QObject* parent = nullptr) : QObject(parent) {}
            ~RingMod() {}

            void setFrequency(float hz) { m_freq = qBound(1.0f, hz, 5000.0f); }
            float frequency() const { return m_freq; }
            void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
            float mix() const { return m_mix; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float inc = 2.0f * M_PI * m_freq / sampleRate;

                for (int i = 0; i < input.size(); ++i) {
                    float carrier = std::sin(m_phase);
                    m_phase += inc;
                    if (m_phase > 2.0f * M_PI) m_phase -= 2.0f * M_PI;

                    float wet = input[i] * carrier;
                    output[i] = input[i] * (1.0f - m_mix) + wet * m_mix;
                }
                return output;
            }

            void reset() { m_phase = 0.0f; }

        private:
            float m_freq = 440.0f;
            float m_mix = 0.5f;
            float m_phase = 0.0f;
        };

        class AutoWah : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(int filterType READ filterType WRITE setFilterType)
                Q_PROPERTY(float freqMin READ freqMin WRITE setFreqMin)
                Q_PROPERTY(float freqMax READ freqMax WRITE setFreqMax)
                Q_PROPERTY(float resonance READ resonance WRITE setResonance)
                Q_PROPERTY(float sensitivity READ sensitivity WRITE setSensitivity)
                Q_PROPERTY(float attack READ attack WRITE setAttack)
                Q_PROPERTY(float release READ release WRITE setRelease)
                Q_PROPERTY(float depth READ depth WRITE setDepth)
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(bool envelopeFollower READ envelopeFollower WRITE setEnvelopeFollower)
                Q_PROPERTY(float lfoRate READ lfoRate WRITE setLfoRate)
        public:
            explicit AutoWah(QObject* parent = nullptr) : QObject(parent) {}
            ~AutoWah() {}

            void setFilterType(int t) { m_filterType = qBound(0, t, 3); }
            int filterType() const { return m_filterType; }
            void setFreqMin(float hz) { m_freqMin = qBound(50.0f, hz, 5000.0f); }
            float freqMin() const { return m_freqMin; }
            void setFreqMax(float hz) { m_freqMax = qBound(100.0f, hz, 15000.0f); }
            float freqMax() const { return m_freqMax; }
            void setResonance(float r) { m_resonance = qBound(0.0f, r, 1.0f); }
            float resonance() const { return m_resonance; }
            void setSensitivity(float s) { m_sensitivity = qBound(0.0f, s, 1.0f); }
            float sensitivity() const { return m_sensitivity; }
            void setAttack(float ms) { m_attack = qBound(0.1f, ms, 200.0f); }
            float attack() const { return m_attack; }
            void setRelease(float ms) { m_release = qBound(5.0f, ms, 1000.0f); }
            float release() const { return m_release; }
            void setDepth(float d) { m_depth = qBound(0.0f, d, 1.0f); }
            float depth() const { return m_depth; }
            void setMix(float m) { m_mix = qBound(0.0f, m, 1.0f); }
            float mix() const { return m_mix; }
            void setEnvelopeFollower(bool e) { m_envFollow = e; }
            bool envelopeFollower() const { return m_envFollow; }
            void setLfoRate(float hz) { m_lfoRate = qBound(0.05f, hz, 20.0f); }
            float lfoRate() const { return m_lfoRate; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                double lfoInc = 2.0 * M_PI * m_lfoRate / sampleRate;
                double attCoeff = std::exp(-1.0 / (m_attack * 0.001 * sampleRate));
                double relCoeff = std::exp(-1.0 / (m_release * 0.001 * sampleRate));

                for (int i = 0; i < input.size(); ++i) {
                    double s = input[i];

                    double absS = std::abs(s);
                    m_env = (absS > m_env)
                        ? attCoeff * m_env + (1.0 - attCoeff) * absS
                        : relCoeff * m_env + (1.0 - relCoeff) * absS;

                    double envPos = std::min(1.0, m_env * (1.0 + m_sensitivity * 9.0));

                    double lfo = 0.5 + 0.5 * std::sin(m_lfoPhase);
                    m_lfoPhase += lfoInc;
                    if (m_lfoPhase > 2.0 * M_PI) m_lfoPhase -= 2.0 * M_PI;

                    double control = m_envFollow ? envPos : lfo;
                    control *= m_depth;

                    double freq = m_freqMin + (m_freqMax - m_freqMin) * control;
                    double Q = 1.0 + m_resonance * 15.0;
                    double w0 = 2.0 * M_PI * freq / sampleRate;
                    double alpha = std::sin(w0) / (2.0 * Q);

                    double b0, b1, b2, a1, a2;
                    if (m_filterType == 0) {
                        b0 = alpha; b1 = 0; b2 = -alpha;
                        a1 = -2.0 * std::cos(w0);
                        a2 = 1.0 - alpha;
                    }
                    else if (m_filterType == 1) {
                        double norm = 1.0 + alpha;
                        b0 = (1.0 - std::cos(w0)) / 2.0 / norm;
                        b1 = (1.0 - std::cos(w0)) / norm;
                        b2 = b0;
                        a1 = -2.0 * std::cos(w0) / norm;
                        a2 = (1.0 - alpha) / norm;
                    }
                    else if (m_filterType == 2) {
                        double norm = 1.0 + alpha;
                        b0 = (1.0 + std::cos(w0)) / 2.0 / norm;
                        b1 = -(1.0 + std::cos(w0)) / norm;
                        b2 = b0;
                        a1 = -2.0 * std::cos(w0) / norm;
                        a2 = (1.0 - alpha) / norm;
                    }
                    else {
                        double norm = 1.0 + alpha;
                        b0 = 1.0 + alpha * std::pow(10.0, 6.0 / 20.0);
                        b1 = -2.0 * std::cos(w0);
                        b2 = 1.0 - alpha;
                        a1 = b1 / norm;
                        a2 = (1.0 - alpha) / norm;
                    }

                    double norm = 1.0 + alpha;
                    b0 /= norm; b1 /= norm; b2 /= norm; a1 /= norm; a2 /= norm;

                    double y = b0 * s + b1 * m_x1 + b2 * m_x2 - a1 * m_y1 - a2 * m_y2;
                    m_x2 = m_x1; m_x1 = s;
                    m_y2 = m_y1; m_y1 = y;

                    output[i] = static_cast<float>(s * (1.0 - m_mix) + y * m_mix);
                }
                return output;
            }

            void reset() {
                m_env = 0.0;
                m_lfoPhase = 0.0;
                m_x1 = m_x2 = m_y1 = m_y2 = 0.0;
            }

        private:
            int m_filterType = 0;
            float m_freqMin = 400.0f;
            float m_freqMax = 4000.0f;
            float m_resonance = 0.5f;
            float m_sensitivity = 0.5f;
            float m_attack = 20.0f;
            float m_release = 50.0f;
            float m_depth = 0.8f;
            float m_mix = 0.7f;
            bool m_envFollow = true;
            float m_lfoRate = 0.5f;
            double m_env = 0.0;
            double m_lfoPhase = 0.0;
            double m_x1 = 0, m_x2 = 0, m_y1 = 0, m_y2 = 0;
        };

class Phaser : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float rate READ rate WRITE setRate)
                Q_PROPERTY(float depth READ depth WRITE setDepth)
                Q_PROPERTY(float feedback READ feedback WRITE setFeedback)
                Q_PROPERTY(float mix READ mix WRITE setMix)
            public:
                explicit Phaser(QObject* parent = nullptr) : QObject(parent) {}
                ~Phaser() {}

                void setRate(float hz) { m_rate = qBound(0.01f, hz, 20.0f); }
                float rate() const { return m_rate; }
                void setDepth(float d) { m_depth = qBound(0.0f, d, 1.0f); }
                float depth() const { return m_depth; }
                void setFeedback(float fb) { m_feedback = qBound(-0.99f, fb, 0.99f); }
                float feedback() const { return m_feedback; }
                void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
                float mix() const { return m_mix; }

                QVector<float> process(const QVector<float>& input, int sampleRate) {
                    QVector<float> output(input.size());
                    float phase = 0.0f;
                    float phaseInc = 2.0f * M_PI * m_rate / sampleRate;
                    float delayBuffer = 0.0f;

                    for (int i = 0; i < input.size(); ++i) {
                        float dry = input[i];
                        float lfo = sinf(phase);
                        float modDelay = m_depth * 0.002f * (1.0f + lfo); // 0-2ms delay
                        int delaySamples = static_cast<int>(modDelay * sampleRate);

                        // Simple all-pass phaser
                        float wet = dry * 0.5f + delayBuffer * 0.5f;
                        delayBuffer = wet * m_feedback + dry * 0.5f;

                        phase += 2.0f * M_PI * m_rate / sampleRate;
                        if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

                        output[i] = dry * (1.0f - m_mix) + wet * m_mix;
                    }
                    return output;
                }

                void reset() {}

private:
                float m_rate = 0.5f;
                float m_depth = 0.5f;
                float m_feedback = 0.0f;
                float m_mix = 0.5f;
            };

        class BitCrusher : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(int bitDepth READ bitDepth WRITE setBitDepth)
                Q_PROPERTY(int sampleRateReduction READ sampleRateReduction WRITE setSampleRateReduction)
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(float noiseShaping READ noiseShaping WRITE setNoiseShaping)
            public:
                explicit BitCrusher(QObject* parent = nullptr) : QObject(parent) {}
                ~BitCrusher() {}

                void setBitDepth(int bits) { m_bitDepth = qBound(1, bits, 24); }
                int bitDepth() const { return m_bitDepth; }
                void setSampleRateReduction(int factor) { m_srReduction = qBound(1, factor, 100); }
                int sampleRateReduction() const { return m_srReduction; }
                void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
                float mix() const { return m_mix; }
                void setNoiseShaping(float ns) { m_noiseShaping = qBound(0.0f, ns, 1.0f); }
                float noiseShaping() const { return m_noiseShaping; }

                QVector<float> process(const QVector<float>& input, int sampleRate) {
                    Q_UNUSED(sampleRate)
                    QVector<float> output(input.size());
                    float quantSteps = std::pow(2.0f, m_bitDepth - 1);
                    float invSteps = 1.0f / quantSteps;

                    for (int i = 0; i < input.size(); ++i) {
                        float s = input[i];

                        m_holdCounter++;
                        if (m_holdCounter >= m_srReduction) {
                            m_holdCounter = 0;
                            m_heldSample = s;
                        }

                        float sample = m_heldSample + m_error * m_noiseShaping;
                        sample = std::round(sample * quantSteps) * invSteps;
                        m_error = s - sample;

                        output[i] = s * (1.0f - m_mix) + sample * m_mix;
                    }
                    return output;
                }

                void reset() {
                    m_heldSample = 0.0f;
                    m_holdCounter = 0;
                    m_error = 0.0f;
                }

            private:
                int m_bitDepth = 8;
                int m_srReduction = 4;
                float m_mix = 0.5f;
                float m_noiseShaping = 0.5f;
                float m_heldSample = 0.0f;
                int m_holdCounter = 0;
                float m_error = 0.0f;
            };

        class Ducker : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float threshold READ threshold WRITE setThreshold)
                Q_PROPERTY(float depth READ depth WRITE setDepth)
                Q_PROPERTY(float attack READ attack WRITE setAttack)
                Q_PROPERTY(float release READ release WRITE setRelease)
                Q_PROPERTY(float sidechainGain READ sidechainGain WRITE setSidechainGain)
                Q_PROPERTY(float mix READ mix WRITE setMix)
        public:
            explicit Ducker(QObject* parent = nullptr) : QObject(parent) {}
            ~Ducker() {}

            void setThreshold(float db) { m_threshold = db; }
            float threshold() const { return m_threshold; }
            void setDepth(float db) { m_depth = qBound(0.0f, db, 60.0f); }
            float depth() const { return m_depth; }
            void setAttack(float ms) { m_attack = qBound(0.1f, ms, 500.0f); }
            float attack() const { return m_attack; }
            void setRelease(float ms) { m_release = qBound(10.0f, ms, 2000.0f); }
            float release() const { return m_release; }
            void setSidechainGain(float g) { m_sidechainGain = qBound(-12.0f, g, 12.0f); }
            float sidechainGain() const { return m_sidechainGain; }
            void setMix(float m) { m_mix = qBound(0.0f, m, 1.0f); }
            float mix() const { return m_mix; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float scGain = std::pow(10.0f, m_sidechainGain / 20.0f);
                float thresholdLin = std::pow(10.0f, m_threshold / 20.0f);
                float depthLin = std::pow(10.0f, -m_depth / 20.0f);
                float attackCoeff = std::exp(-1.0f / (m_attack * 0.001f * sampleRate));
                float releaseCoeff = std::exp(-1.0f / (m_release * 0.001f * sampleRate));

                for (int i = 0; i < input.size(); ++i) {
                    float s = input[i];
                    float sc = std::abs(s) * scGain;

                    m_env = (sc > m_env)
                        ? attackCoeff * m_env + (1.0f - attackCoeff) * sc
                        : releaseCoeff * m_env + (1.0f - releaseCoeff) * sc;

                    float gain = 1.0f;
                    if (m_env > thresholdLin) {
                        float ratio = thresholdLin / m_env;
                        float reduction = 1.0f - (1.0f - ratio) * (1.0f - depthLin);
                        gain = std::max(depthLin, reduction);
                    }

                    output[i] = s * (gain * (1.0f - m_mix) + 1.0f * m_mix);
                }
                return output;
            }

            void reset() { m_env = 0.0f; }

        private:
            float m_threshold = -20.0f;
            float m_depth = 12.0f;
            float m_attack = 1.0f;
            float m_release = 200.0f;
            float m_sidechainGain = 0.0f;
            float m_mix = 0.5f;
            float m_env = 0.0f;
        };

        class DeEsser : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float threshold READ threshold WRITE setThreshold)
                Q_PROPERTY(float frequency READ frequency WRITE setFrequency)
                Q_PROPERTY(float bandwidth READ bandwidth WRITE setBandwidth)
                Q_PROPERTY(float reduction READ reduction WRITE setReduction)
                Q_PROPERTY(float attack READ attack WRITE setAttack)
                Q_PROPERTY(float release READ release WRITE setRelease)
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(bool listenMode READ listenMode WRITE setListenMode)
        public:
            explicit DeEsser(QObject* parent = nullptr) : QObject(parent) {}
            ~DeEsser() {}

            void setThreshold(float db) { m_threshold = db; }
            float threshold() const { return m_threshold; }
            void setFrequency(float hz) { m_freq = qBound(1000.0f, hz, 12000.0f); }
            float frequency() const { return m_freq; }
            void setBandwidth(float bw) { m_bw = qBound(0.5f, bw, 4.0f); }
            float bandwidth() const { return m_bw; }
            void setReduction(float db) { m_reduction = qBound(0.0f, db, 40.0f); }
            float reduction() const { return m_reduction; }
            void setAttack(float ms) { m_attack = qBound(0.1f, ms, 50.0f); }
            float attack() const { return m_attack; }
            void setRelease(float ms) { m_release = qBound(10.0f, ms, 500.0f); }
            float release() const { return m_release; }
            void setMix(float m) { m_mix = qBound(0.0f, m, 1.0f); }
            float mix() const { return m_mix; }
            void setListenMode(bool l) { m_listenMode = l; }
            bool listenMode() const { return m_listenMode; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                double w0 = 2.0 * M_PI * m_freq / sampleRate;
                double Q = m_bw;
                double alpha = std::sin(w0) / (2.0 * Q);
                double norm = 1.0 + alpha;
                double b0 = alpha / norm;
                double b1 = 0;
                double b2 = -alpha / norm;
                double a1 = -2.0 * std::cos(w0) / norm;
                double a2 = (1.0 - alpha) / norm;

                double thresholdLin = std::pow(10.0, m_threshold / 20.0);
                double reductionLin = std::pow(10.0, -m_reduction / 20.0);
                double attCoeff = std::exp(-1.0 / (m_attack * 0.001 * sampleRate));
                double relCoeff = std::exp(-1.0 / (m_release * 0.001 * sampleRate));

                for (int i = 0; i < input.size(); ++i) {
                    double s = input[i];
                    double bp = b0 * s + b1 * m_bpX1 + b2 * m_bpX2 - a1 * m_bpY1 - a2 * m_bpY2;
                    m_bpX2 = m_bpX1; m_bpX1 = s;
                    m_bpY2 = m_bpY1; m_bpY1 = bp;

                    double absBP = std::abs(bp);
                    m_env = (absBP > m_env)
                        ? attCoeff * m_env + (1.0 - attCoeff) * absBP
                        : relCoeff * m_env + (1.0 - relCoeff) * absBP;

                    double gain = 1.0;
                    if (m_env > thresholdLin) {
                        double excess = m_env - thresholdLin;
                        double maxReduction = 1.0 - reductionLin;
                        double reduction = std::min(1.0, excess / (thresholdLin * 2.0)) * maxReduction;
                        gain = 1.0 - reduction;
                    }

                    double processed = s * gain;
                    if (m_listenMode) {
                        output[i] = static_cast<float>(bp * gain * 6.0);
                    }
                    else {
                        output[i] = static_cast<float>(s * (1.0 - m_mix) + processed * m_mix);
                    }
                }
                return output;
            }

            void reset() {
                m_env = 0.0;
                m_bpX1 = m_bpX2 = m_bpY1 = m_bpY2 = 0.0;
            }

        private:
            float m_threshold = -30.0f;
            float m_freq = 7000.0f;
            float m_bw = 2.0f;
            float m_reduction = 15.0f;
            float m_attack = 1.0f;
            float m_release = 50.0f;
            float m_mix = 0.5f;
            bool m_listenMode = false;
            double m_env = 0.0;
            double m_bpX1 = 0, m_bpX2 = 0, m_bpY1 = 0, m_bpY2 = 0;
        };

        class SaturationDistortion : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float drive READ drive WRITE setDrive)
                Q_PROPERTY(float bias READ bias WRITE setBias)
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(int type READ type WRITE setType)
                Q_PROPERTY(float outputGain READ outputGain WRITE setOutputGain)
        public:
            explicit SaturationDistortion(QObject* parent = nullptr) : QObject(parent) {}
            ~SaturationDistortion() {}

            void setDrive(float d) { m_drive = qBound(0.0f, d, 1.0f); }
            float drive() const { return m_drive; }
            void setBias(float b) { m_bias = qBound(-0.5f, b, 0.5f); }
            float bias() const { return m_bias; }
            void setMix(float m) { m_mix = qBound(0.0f, m, 1.0f); }
            float mix() const { return m_mix; }
            void setType(int t) { m_type = qBound(0, t, 4); }
            int type() const { return m_type; }
            void setOutputGain(float g) { m_outputGain = qBound(-12.0f, g, 12.0f); }
            float outputGain() const { return m_outputGain; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                Q_UNUSED(sampleRate)
                    QVector<float> output(input.size());
                float gain = std::pow(10.0f, m_outputGain / 20.0f);

                for (int i = 0; i < input.size(); ++i) {
                    float s = input[i] + m_bias;
                    float driveAmt = 1.0f + m_drive * 30.0f;
                    float wet = s * driveAmt;

                    switch (m_type) {
                    case 0: // Soft clip
                        wet = std::tanh(wet);
                        break;
                    case 1: // Hard clip
                        wet = std::max(-1.0f, std::min(1.0f, wet));
                        break;
                    case 2: // Tube
                        wet = wet / (1.0f + std::abs(wet));
                        break;
                    case 3: // Exponential
                        wet = (wet >= 0) ? 1.0f - std::exp(-wet) : -(1.0f - std::exp(wet));
                        break;
                    case 4: // Square
                        wet = (wet > 0) ? 1.0f : -1.0f;
                        break;
                    }

                    wet *= gain;
                    output[i] = s * (1.0f - m_mix) + wet * m_mix;
                }
                return output;
            }

            void reset() {}

        private:
            float m_drive = 0.3f;
            float m_bias = 0.0f;
            float m_mix = 0.5f;
            int m_type = 0;
            float m_outputGain = 0.0f;
        };

        class TremoloModulation : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float rate READ rate WRITE setRate)
                Q_PROPERTY(float depth READ depth WRITE setDepth)
                Q_PROPERTY(float shape READ shape WRITE setShape)
                Q_PROPERTY(int waveform READ waveform WRITE setWaveform)
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(bool stereoPhase READ stereoPhase WRITE setStereoPhase)
        public:
            explicit TremoloModulation(QObject* parent = nullptr) : QObject(parent) {}
            ~TremoloModulation() {}

            void setRate(float hz) { m_rate = qBound(0.05f, hz, 20.0f); }
            float rate() const { return m_rate; }
            void setDepth(float d) { m_depth = qBound(0.0f, d, 1.0f); }
            float depth() const { return m_depth; }
            void setShape(float s) { m_shape = qBound(0.0f, s, 1.0f); }
            float shape() const { return m_shape; }
            void setWaveform(int w) { m_waveform = qBound(0, w, 2); }
            int waveform() const { return m_waveform; }
            void setMix(float m) { m_mix = qBound(0.0f, m, 1.0f); }
            float mix() const { return m_mix; }
            void setStereoPhase(bool sp) { m_stereoPhase = sp; }
            bool stereoPhase() const { return m_stereoPhase; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float inc = 2.0f * M_PI * m_rate / sampleRate;
                bool stereo = m_stereoPhase && input.size() >= 2;

                for (int i = 0; i < input.size(); ++i) {
                    float lfo;
                    float phase = m_phase;
                    if (stereo && (i % 2 == 1)) phase += M_PI_2;

                    switch (m_waveform) {
                    case 0: lfo = 0.5f + 0.5f * std::sin(phase); break;
                    case 1: lfo = std::abs(std::sin(phase)); break;
                    case 2: lfo = phase / (2.0f * M_PI); break;
                    default: lfo = 0.5f + 0.5f * std::sin(phase); break;
                    }

                    lfo = std::pow(lfo, 1.0f + m_shape * 4.0f);
                    float mod = 1.0f - lfo * m_depth;
                    float wet = input[i] * mod;

                    output[i] = input[i] * (1.0f - m_mix) + wet * m_mix;

                    if (i % 2 == (stereo ? 0 : 0)) {
                        m_phase += inc;
                        if (m_phase > 2.0f * M_PI) m_phase -= 2.0f * M_PI;
                    }
                }
                return output;
            }

            void reset() { m_phase = 0.0f; }

        private:
            float m_rate = 4.0f;
            float m_depth = 0.5f;
            float m_shape = 0.0f;
            int m_waveform = 0;
            float m_mix = 0.5f;
            bool m_stereoPhase = false;
            float m_phase = 0.0f;
        };

        class MultiBandSplitter : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(int bandCount READ bandCount WRITE setBandCount)
                Q_PROPERTY(float crossover0 READ crossover0 WRITE setCrossover0)
                Q_PROPERTY(float crossover1 READ crossover1 WRITE setCrossover1)
                Q_PROPERTY(float crossover2 READ crossover2 WRITE setCrossover2)
                Q_PROPERTY(float band0Gain READ band0Gain WRITE setBand0Gain)
                Q_PROPERTY(float band1Gain READ band1Gain WRITE setBand1Gain)
                Q_PROPERTY(float band2Gain READ band2Gain WRITE setBand2Gain)
                Q_PROPERTY(float band3Gain READ band3Gain WRITE setBand3Gain)
                Q_PROPERTY(bool band0Mute READ band0Mute WRITE setBand0Mute)
                Q_PROPERTY(bool band1Mute READ band1Mute WRITE setBand1Mute)
                Q_PROPERTY(bool band2Mute READ band2Mute WRITE setBand2Mute)
                Q_PROPERTY(bool band3Mute READ band3Mute WRITE setBand3Mute)
                Q_PROPERTY(bool band0Solo READ band0Solo WRITE setBand0Solo)
                Q_PROPERTY(bool band1Solo READ band1Solo WRITE setBand1Solo)
                Q_PROPERTY(bool band2Solo READ band2Solo WRITE setBand2Solo)
                Q_PROPERTY(bool band3Solo READ band3Solo WRITE setBand3Solo)
        public:
            explicit MultiBandSplitter(QObject* parent = nullptr) : QObject(parent) {
                m_crossovers[0] = 250.0f;
                m_crossovers[1] = 2000.0f;
                m_crossovers[2] = 8000.0f;
                for (int i = 0; i < 4; ++i) m_bandGains[i] = 0.0f;
            }
            ~MultiBandSplitter() {}

            void setBandCount(int c) { m_bandCount = qBound(2, c, 4); }
            int bandCount() const { return m_bandCount; }

            void setCrossover0(float hz) { m_crossovers[0] = qBound(20.0f, hz, 20000.0f); }
            float crossover0() const { return m_crossovers[0]; }
            void setCrossover1(float hz) { m_crossovers[1] = qBound(20.0f, hz, 20000.0f); }
            float crossover1() const { return m_crossovers[1]; }
            void setCrossover2(float hz) { m_crossovers[2] = qBound(20.0f, hz, 20000.0f); }
            float crossover2() const { return m_crossovers[2]; }

            void setBand0Gain(float db) { m_bandGains[0] = qBound(-24.0f, db, 24.0f); }
            float band0Gain() const { return m_bandGains[0]; }
            void setBand1Gain(float db) { m_bandGains[1] = qBound(-24.0f, db, 24.0f); }
            float band1Gain() const { return m_bandGains[1]; }
            void setBand2Gain(float db) { m_bandGains[2] = qBound(-24.0f, db, 24.0f); }
            float band2Gain() const { return m_bandGains[2]; }
            void setBand3Gain(float db) { m_bandGains[3] = qBound(-24.0f, db, 24.0f); }
            float band3Gain() const { return m_bandGains[3]; }

            void setBand0Mute(bool m) { m_bandMutes[0] = m; }
            bool band0Mute() const { return m_bandMutes[0]; }
            void setBand1Mute(bool m) { m_bandMutes[1] = m; }
            bool band1Mute() const { return m_bandMutes[1]; }
            void setBand2Mute(bool m) { m_bandMutes[2] = m; }
            bool band2Mute() const { return m_bandMutes[2]; }
            void setBand3Mute(bool m) { m_bandMutes[3] = m; }
            bool band3Mute() const { return m_bandMutes[3]; }

            void setBand0Solo(bool s) { m_bandSolos[0] = s; }
            bool band0Solo() const { return m_bandSolos[0]; }
            void setBand1Solo(bool s) { m_bandSolos[1] = s; }
            bool band1Solo() const { return m_bandSolos[1]; }
            void setBand2Solo(bool s) { m_bandSolos[2] = s; }
            bool band2Solo() const { return m_bandSolos[2]; }
            void setBand3Solo(bool s) { m_bandSolos[3] = s; }
            bool band3Solo() const { return m_bandSolos[3]; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                int n = input.size();
                QVector<float> output(n, 0.0f);
                bool anySolo = false;
                for (int i = 0; i < 4; ++i) if (m_bandSolos[i]) anySolo = true;

                QVector<QVector<float>> bands(m_bandCount, QVector<float>(n, 0.0f));

                for (int b = 0; b < m_bandCount; ++b) {
                    float lo = (b == 0) ? 20.0f : m_crossovers[b - 1];
                    float hi = (b == m_bandCount - 1) ? 20000.0f : m_crossovers[b];

                    if (b == 0) {
                        for (int i = 0; i < n; ++i)
                            bands[b][i] = lpf(input[i], sampleRate, hi, m_lpStates[0]);
                    }
                    else if (b == m_bandCount - 1) {
                        for (int i = 0; i < n; ++i)
                            bands[b][i] = hpf(input[i], sampleRate, lo, m_hpStates[m_bandCount - 1]);
                    }
                    else {
                        for (int i = 0; i < n; ++i) {
                            float lp = lpf(input[i], sampleRate, hi, m_lpStates[b]);
                            float hp = hpf(input[i], sampleRate, lo, m_hpStates[b]);
                            bands[b][i] = lp + hp;
                        }
                    }

                    float gainLin = std::pow(10.0f, m_bandGains[b] / 20.0f);
                    bool pass = !m_bandMutes[b] && (!anySolo || m_bandSolos[b]);

                    for (int i = 0; i < n; ++i) {
                        if (pass) output[i] += bands[b][i] * gainLin;
                    }
                }

                return output;
            }

            void reset() {
                for (int i = 0; i < 4; ++i) {
                    m_lpStates[i] = 0.0f;
                    m_hpStates[i] = 0.0f;
                }
            }

        private:
            float lpf(float x, int sr, float freq, float& state) {
                float rc = 1.0f / (2.0f * M_PI * freq);
                float dt = 1.0f / sr;
                float alpha = dt / (rc + dt);
                state = state + alpha * (x - state);
                return state;
            }

            float hpf(float x, int sr, float freq, float& state) {
                float rc = 1.0f / (2.0f * M_PI * freq);
                float dt = 1.0f / sr;
                float alpha = rc / (rc + dt);
                state = alpha * state + alpha * (x - state);
                return x - state;
            }

            int m_bandCount = 3;
            float m_crossovers[3] = { 250.0f, 2000.0f, 8000.0f };
            float m_bandGains[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            bool m_bandMutes[4] = { false, false, false, false };
            bool m_bandSolos[4] = { false, false, false, false };
            float m_lpStates[4] = { 0.0f };
            float m_hpStates[4] = { 0.0f };
        };

        // ============================================================================
        // BASIC EFFECTS
        // ============================================================================

        class ChorusEffect : public QObject
        {
            Q_OBJECT
        public:
            explicit ChorusEffect(QObject* parent = nullptr) : QObject(parent), m_delayBuffer(MAX_DELAY_SAMPLES, 0.0f) {}
            ~ChorusEffect() {}

            void setRate(float hz) { m_rate = qBound(0.1f, hz, 20.0f); }
            float rate() const { return m_rate; }
            void setDepth(float depth) { m_depth = qBound(0.0f, depth, 1.0f); }
            float depth() const { return m_depth; }
            void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
            float mix() const { return m_mix; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float lfoIncrement = 2.0f * M_PI * m_rate / sampleRate;
                int maxDelaySamples = static_cast<int>(30.0f * sampleRate / 1000.0f);

                for (int i = 0; i < input.size(); ++i) {
                    float lfoValue = sinf(m_lfoPhase);
                    m_lfoPhase += lfoIncrement;
                    if (m_lfoPhase > 2.0f * M_PI) m_lfoPhase -= 2.0f * M_PI;

                    float delayMs = 20.0f + lfoValue * m_depth * 10.0f;
                    int delaySamples = static_cast<int>(delayMs * sampleRate / 1000.0f);
                    delaySamples = qMin(delaySamples, maxDelaySamples);

                    int readPos = m_writePos - delaySamples;
                    if (readPos < 0) readPos += MAX_DELAY_SAMPLES;

                    float delayed = m_delayBuffer[readPos];
                    m_delayBuffer[m_writePos] = input[i];
                    m_writePos = (m_writePos + 1) % MAX_DELAY_SAMPLES;

                    output[i] = input[i] * (1.0f - m_mix) + delayed * m_mix;
                }
                return output;
            }

        private:
            float m_rate = 1.5f;
            float m_depth = 0.5f;
            float m_mix = 0.5f;
            static const int MAX_DELAY_SAMPLES = 4096;
            QVector<float> m_delayBuffer;
            int m_writePos = 0;
            float m_lfoPhase = 0.0f;
        };

        class FlangerEffect : public QObject
        {
            Q_OBJECT
        public:
            explicit FlangerEffect(QObject* parent = nullptr) : QObject(parent), m_delayBuffer(MAX_DELAY, 0.0f) {}
            ~FlangerEffect() {}

            void setRate(float hz) { m_rate = qBound(0.1f, hz, 10.0f); }
            float rate() const { return m_rate; }
            void setDepth(float depth) { m_depth = qBound(0.0f, depth, 1.0f); }
            float depth() const { return m_depth; }
            void setFeedback(float feedback) { m_feedback = qBound(-0.95f, feedback, 0.95f); }
            float feedback() const { return m_feedback; }
            void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
            float mix() const { return m_mix; }
            void setDelay(float ms) { m_delay = qBound(0.0f, ms, 20.0f); }
            float delay() const { return m_delay; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float lfoIncrement = 2.0f * M_PI * m_rate / sampleRate;
                int baseDelaySamples = static_cast<int>(m_delay * sampleRate / 1000.0f);
                int maxDepthSamples = static_cast<int>(m_depth * 10.0f * sampleRate / 1000.0f);

                for (int i = 0; i < input.size(); ++i) {
                    float lfoValue = sinf(m_lfoPhase);
                    m_lfoPhase += lfoIncrement;
                    if (m_lfoPhase > 2.0f * M_PI) m_lfoPhase -= 2.0f * M_PI;

                    int delaySamples = baseDelaySamples + static_cast<int>(lfoValue * maxDepthSamples);
                    delaySamples = qBound(0, delaySamples, MAX_DELAY - 1);

                    int readPos = m_writePos - delaySamples;
                    if (readPos < 0) readPos += MAX_DELAY;

                    float delayed = m_delayBuffer[readPos];
                    float fb = delayed * m_feedback;
                    m_delayBuffer[m_writePos] = input[i] + fb;
                    m_writePos = (m_writePos + 1) % MAX_DELAY;

                    output[i] = input[i] * (1.0f - m_mix) + delayed * m_mix;
                }
                return output;
            }

        private:
            float m_rate = 0.5f;
            float m_depth = 0.5f;
            float m_feedback = 0.5f;
            float m_mix = 0.5f;
            float m_delay = 5.0f;
            static const int MAX_DELAY = 4096;
            QVector<float> m_delayBuffer;
            int m_writePos = 0;
            float m_lfoPhase = 0.0f;
        };

        class NoiseGate : public QObject
        {
            Q_OBJECT
        public:
            explicit NoiseGate(QObject* parent = nullptr) : QObject(parent) {}

            void setThreshold(float db) { m_threshold = db; }
            float threshold() const { return m_threshold; }
            void setRatio(float ratio) { m_ratio = qBound(1.0f, ratio, 20.0f); }
            float ratio() const { return m_ratio; }
            void setAttack(float ms) { m_attack = qBound(0.1f, ms, 100.0f); }
            float attack() const { return m_attack; }
            void setRelease(float ms) { m_release = qBound(10.0f, ms, 1000.0f); }
            float release() const { return m_release; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                QVector<float> output(input.size());
                float attackCoeff = expf(-1.0f / (m_attack * sampleRate / 1000.0f));
                float releaseCoeff = expf(-1.0f / (m_release * sampleRate / 1000.0f));

                for (int i = 0; i < input.size(); ++i) {
                    float db = 20.0f * log10f(qAbs(input[i]) + 1e-10f);
                    float targetGain = (db > m_threshold) ? 1.0f : 0.0f;
                    m_envelope = (m_envelope < targetGain) ? attackCoeff * m_envelope + (1.0f - attackCoeff) * targetGain
                        : releaseCoeff * m_envelope + (1.0f - releaseCoeff) * targetGain;
                    output[i] = input[i] * m_envelope;
                }
                return output;
            }

        private:
            float m_threshold = -40.0f;
            float m_ratio = 10.0f;
            float m_attack = 5.0f;
            float m_release = 100.0f;
            float m_envelope = 0.0f;
        };

        class NoiseReduction : public QObject
        {
            Q_OBJECT
        public:
            explicit NoiseReduction(QObject* parent = nullptr) : QObject(parent) {}

            void setNoiseFloor(float db) { m_noiseFloor = db; }
            float noiseFloor() const { return m_noiseFloor; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                Q_UNUSED(sampleRate)
                    QVector<float> output(input.size());
                float thresholdLinear = powf(10.0f, m_noiseFloor / 20.0f);

                for (int i = 0; i < input.size(); ++i) {
                    float absVal = qAbs(input[i]);
                    output[i] = (absVal > thresholdLinear) ? input[i] : input[i] * 0.1f;
                }
                return output;
            }

        private:
            float m_noiseFloor = -60.0f;
        };

        class PitchShifter : public QObject
        {
            Q_OBJECT
        public:
            explicit PitchShifter(QObject* parent = nullptr) : QObject(parent) {}

            void setPitch(float semitones) { m_pitch = qBound(-12.0f, semitones, 12.0f); }
            float pitch() const { return m_pitch; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                float ratio = powf(2.0f, m_pitch / 12.0f);
                int outputSize = static_cast<int>(input.size() / ratio);
                QVector<float> output(outputSize);

                for (int i = 0; i < outputSize; ++i) {
                    float srcPos = i * ratio;
                    int idx = static_cast<int>(srcPos);
                    float frac = srcPos - idx;
                    if (idx >= input.size()) {
                        output[i] = 0.0f;
                    }
                    else if (idx < input.size() - 1) {
                        output[i] = input[idx] * (1.0f - frac) + input[idx + 1] * frac;
                    }
                    else {
                        output[i] = input[idx];
                    }
                }
                return output;
            }

        private:
            float m_pitch = 0.0f;
        };

        class SFXReverb : public QObject
        {
            Q_OBJECT
        public:
            explicit SFXReverb(QObject* parent = nullptr) : QObject(parent) {
                m_combs.resize(8);
                m_alls.resize(4);
                for (int i = 0; i < 8; ++i) m_combs[i].resize(m_combDelays[i]);
                for (int i = 0; i < 4; ++i) m_alls[i].resize(m_allDelays[i]);
            }

            void setRoomSize(float size) { m_roomSize = qBound(0.0f, size, 1.0f); }
            float roomSize() const { return m_roomSize; }
            void setDamping(float damping) { m_damping = qBound(0.0f, damping, 1.0f); }
            float damping() const { return m_damping; }
            void setWetLevel(float wet) { m_wet = qBound(0.0f, wet, 1.0f); }
            float wetLevel() const { return m_wet; }
            void setDryLevel(float dry) { m_dry = qBound(0.0f, dry, 1.0f); }
            float dryLevel() const { return m_dry; }

            QVector<float> process(const QVector<float>& input, int sampleRate) {
                Q_UNUSED(sampleRate)
                    QVector<float> output(input.size(), 0.0f);

                for (int i = 0; i < input.size(); ++i) {
                    float sample = input[i];
                    float wet = 0.0f;

                    for (int c = 0; c < 8; ++c) {
                        float& buf = m_combs[c][m_combPos[c]];
                        float temp = buf;
                        buf = temp * m_damping + sample;
                        wet += temp;
                        m_combPos[c] = (m_combPos[c] + 1) % m_combs[c].size();
                    }

                    for (int a = 0; a < 4; ++a) {
                        float& buf = m_alls[a][m_allPos[a]];
                        float temp = buf;
                        buf = temp * 0.5f + sample;
                        wet += temp;
                        m_allPos[a] = (m_allPos[a] + 1) % m_alls[a].size();
                    }

                    output[i] = sample * m_dry + wet * m_wet * 0.125f;
                }
                return output;
            }

        private:
            float m_roomSize = 0.5f;
            float m_damping = 0.5f;
            float m_wet = 0.3f;
            float m_dry = 0.7f;

            int m_combPos[8] = { 0 };
            int m_allPos[4] = { 0 };

            const int m_combDelays[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            const int m_allDelays[4] = { 556, 441, 341, 225 };

            QVector<QVector<float>> m_combs;
            QVector<QVector<float>> m_alls;
        };

        // ============================================================================
        // ADVANCED EFFECTS
        // ============================================================================

        class ConvolutionReverb : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(float preDelay READ preDelay WRITE setPreDelay)
                Q_PROPERTY(float gain READ gain WRITE setGain)
        public:
            explicit ConvolutionReverb(QObject* parent = nullptr) : QObject(parent) {}
            ~ConvolutionReverb() {}

            void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
            float mix() const { return m_mix; }
            void setPreDelay(float ms) { m_preDelay = qBound(0.0f, ms, 500.0f); }
            float preDelay() const { return m_preDelay; }
            void setGain(float gain) { m_gain = qBound(0.0f, gain, 10.0f); }
            float gain() const { return m_gain; }

            bool loadImpulseResponse(const QString& wavPath, int targetSampleRate = 48000)
            {
                if (wavPath.isEmpty()) return false;
                QFile file(wavPath);
                if (!file.open(QIODevice::ReadOnly)) return false;

                m_ir.clear();
                QAudioFormat fmt;
                m_preDelaySamples = static_cast<int>(m_preDelay * targetSampleRate / 1000.0f);
                m_sampleRate = targetSampleRate;

                QByteArray data = file.readAll();
                file.close();

                if (data.size() < 44) return false;
                if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') return false;
                if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') return false;

                int channels = *reinterpret_cast<const quint16*>(data.data() + 22);
                int bitsPerSample = *reinterpret_cast<const quint16*>(data.data() + 34);
                int dataSize = *reinterpret_cast<const quint32*>(data.data() + 40);
                int sampleCount = dataSize / (channels * (bitsPerSample / 8));
                if (sampleCount == 0) return false;

                const char* pcm = data.data() + 44;
                int bytesPerSample = bitsPerSample / 8;
                int stride = channels * bytesPerSample;

                m_ir.reserve(sampleCount / channels);
                float scale = (bitsPerSample == 16) ? 32768.0f : 8388608.0f;
                for (int i = 0; i + stride <= dataSize; i += stride) {
                    qint32 val = 0;
                    if (bytesPerSample == 2)
                        val = *reinterpret_cast<const qint16*>(pcm + i);
                    else if (bytesPerSample == 3)
                        val = (static_cast<qint32>(static_cast<unsigned char>(pcm[i + 2])) << 16)
                        | (static_cast<qint32>(static_cast<unsigned char>(pcm[i + 1])) << 8)
                        | static_cast<qint32>(static_cast<unsigned char>(pcm[i]));
                    else if (bytesPerSample == 1)
                        val = (static_cast<qint32>(static_cast<unsigned char>(pcm[i])) - 128) << 8;
                    m_ir.append(static_cast<float>(val) / scale);
                }

                if (m_ir.isEmpty()) return false;

                int irSize = 1;
                while (irSize < m_ir.size() * 2) irSize <<= 1;
                if (irSize > m_ir.size()) {
                    m_ir.resize(irSize, 0.0f);
                }

                m_fftReady = true;
                return true;
            }

            bool isImpulseLoaded() const { return m_fftReady && !m_ir.isEmpty(); }
            int irLength() const { return m_ir.size(); }

            QVector<float> process(const QVector<float>& input, int sampleRate)
            {
                if (!m_fftReady || m_ir.isEmpty() || input.isEmpty())
                    return input;

                Q_UNUSED(sampleRate)
                    int n = input.size();
                QVector<float> output(n, 0.0f);
                int irLen = m_ir.size();

                for (int i = 0; i < n; ++i) {
                    float dry = input[i];
                    float wet = dry * m_gain;

                    if (i >= m_preDelaySamples) {
                        int irIdx = i - m_preDelaySamples;
                        if (irIdx < irLen)
                            wet += m_ir[irIdx] * dry * 0.1f;
                    }

                    for (int j = 0; j < irLen && i - j >= 0; ++j) {
                        wet += input[i - j] * m_ir[j];
                    }

                    output[i] = dry * (1.0f - m_mix) + wet * m_mix;
                }

                return output;
            }

            void reset()
            {
                m_ir.clear();
                m_fftReady = false;
                m_preDelaySamples = 0;
            }

        private:
            float m_mix = 0.3f;
            float m_preDelay = 20.0f;
            float m_gain = 1.0f;
            QVector<float> m_ir;
            int m_preDelaySamples = 0;
            int m_sampleRate = 48000;
            bool m_fftReady = false;
        };

        class MultibandCompressor : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(int bandCount READ bandCount WRITE setBandCount)
                Q_PROPERTY(float crossover0 READ crossover0 WRITE setCrossover0)
                Q_PROPERTY(float crossover1 READ crossover1 WRITE setCrossover1)
                Q_PROPERTY(float crossover2 READ crossover2 WRITE setCrossover2)
        public:
            explicit MultibandCompressor(QObject* parent = nullptr) : QObject(parent) {
                m_bands.resize(4);
                for (int i = 0; i < 4; ++i) {
                    m_bands[i].threshold = (i == 0) ? -20.0f : -24.0f;
                    m_bands[i].ratio = 4.0f;
                    m_bands[i].attack = 5.0f;
                    m_bands[i].release = 100.0f;
                    m_bands[i].makeupGain = 0.0f;
                    m_bands[i].envelope = 0.0f;
                }
            }
            ~MultibandCompressor() {}

            void setBandCount(int count) { m_bandCount = qBound(2, count, 4); }
            int bandCount() const { return m_bandCount; }

            void setCrossover0(float hz) { m_crossovers[0] = qBound(20.0f, hz, 20000.0f); }
            float crossover0() const { return m_crossovers[0]; }
            void setCrossover1(float hz) { m_crossovers[1] = qBound(20.0f, hz, 20000.0f); }
            float crossover1() const { return m_crossovers[1]; }
            void setCrossover2(float hz) { m_crossovers[2] = qBound(20.0f, hz, 20000.0f); }
            float crossover2() const { return m_crossovers[2]; }

            void setBandThreshold(int band, float db) { if (band >= 0 && band < m_bands.size()) m_bands[band].threshold = db; }
            float bandThreshold(int band) const { return (band >= 0 && band < m_bands.size()) ? m_bands[band].threshold : 0.0f; }
            void setBandRatio(int band, float ratio) { if (band >= 0 && band < m_bands.size()) m_bands[band].ratio = qMax(1.0f, ratio); }
            float bandRatio(int band) const { return (band >= 0 && band < m_bands.size()) ? m_bands[band].ratio : 1.0f; }
            void setBandAttack(int band, float ms) { if (band >= 0 && band < m_bands.size()) m_bands[band].attack = qBound(0.1f, ms, 500.0f); }
            float bandAttack(int band) const { return (band >= 0 && band < m_bands.size()) ? m_bands[band].attack : 5.0f; }
            void setBandRelease(int band, float ms) { if (band >= 0 && band < m_bands.size()) m_bands[band].release = qBound(10.0f, ms, 2000.0f); }
            float bandRelease(int band) const { return (band >= 0 && band < m_bands.size()) ? m_bands[band].release : 100.0f; }
            void setBandMakeupGain(int band, float db) { if (band >= 0 && band < m_bands.size()) m_bands[band].makeupGain = db; }
            float bandMakeupGain(int band) const { return (band >= 0 && band < m_bands.size()) ? m_bands[band].makeupGain : 0.0f; }

            QVector<float> process(const QVector<float>& input, int sampleRate)
            {
                if (input.isEmpty()) return input;
                QVector<float> output(input.size(), 0.0f);
                int n = input.size();

                for (int b = 0; b < m_bandCount; ++b) {
                    QVector<float> band(n, 0.0f);
                    float lo = (b == 0) ? 20.0f : m_crossovers[b - 1];
                    float hi = (b == m_bandCount - 1) ? 20000.0f : m_crossovers[b];
                    float center = sqrtf(lo * hi);
                    float Q = center / (hi - lo + 1.0f);
                    float w0 = 2.0f * M_PI * center / sampleRate;
                    float alpha = sinf(w0) / (2.0f * Q);
                    float a0 = 1.0f + alpha;
                    float b0 = (1.0f - cosf(w0)) / 2.0f / a0;
                    float b1 = (1.0f - cosf(w0)) / a0;
                    float b2 = b0;

                    float v0 = 0.0f, v1 = 0.0f;
                    for (int i = 0; i < n; ++i) {
                        float x = input[i];
                        float y = b0 * x + b1 * v0 + b2 * v1;
                        v1 = v0;
                        v0 = x;
                        band[i] = y;
                    }

                    BandState& bs = m_bands[b];
                    double threshold = std::pow(10.0, bs.threshold / 20.0);
                    double ratio = bs.ratio;
                    double attackCoeff = std::exp(-1.0 / (bs.attack * 0.001 * sampleRate));
                    double releaseCoeff = std::exp(-1.0 / (bs.release * 0.001 * sampleRate));
                    double makeup = std::pow(10.0, bs.makeupGain / 20.0);
                    double env = bs.envelope;

                    for (int i = 0; i < n; ++i) {
                        double absVal = std::abs(static_cast<double>(band[i]));
                        env = (absVal > env)
                            ? attackCoeff * env + (1.0 - attackCoeff) * absVal
                            : releaseCoeff * env + (1.0 - releaseCoeff) * absVal;
                        if (env > threshold) {
                            double gr = threshold + (env - threshold) / ratio;
                            double gain = (env > 1e-10) ? gr / env : 1.0;
                            output[i] += static_cast<float>(band[i] * gain * makeup);
                        }
                        else {
                            output[i] += static_cast<float>(band[i] * makeup);
                        }
                    }
                    bs.envelope = static_cast<float>(env);
                }

                return output;
            }

            void reset()
            {
                for (int i = 0; i < m_bands.size(); ++i)
                    m_bands[i].envelope = 0.0f;
            }

        private:
            struct BandState {
                float threshold;
                float ratio;
                float attack;
                float release;
                float makeupGain;
                float envelope;
            };

            int m_bandCount = 3;
            float m_crossovers[3] = { 250.0f, 2000.0f, 8000.0f };
            QVector<BandState> m_bands;
        };

        class TapeEmulator : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float drive READ drive WRITE setDrive)
                Q_PROPERTY(float mix READ mix WRITE setMix)
                Q_PROPERTY(float wowRate READ wowRate WRITE setWowRate)
                Q_PROPERTY(float wowDepth READ wowDepth WRITE setWowDepth)
                Q_PROPERTY(float hissLevel READ hissLevel WRITE setHissLevel)
                Q_PROPERTY(float bias READ bias WRITE setBias)
        public:
            explicit TapeEmulator(QObject* parent = nullptr) : QObject(parent) {}
            ~TapeEmulator() {}

            void setDrive(float drive) { m_drive = qBound(0.0f, drive, 1.0f); }
            float drive() const { return m_drive; }
            void setMix(float mix) { m_mix = qBound(0.0f, mix, 1.0f); }
            float mix() const { return m_mix; }
            void setWowRate(float hz) { m_wowRate = qBound(0.1f, hz, 20.0f); }
            float wowRate() const { return m_wowRate; }
            void setWowDepth(float depth) { m_wowDepth = qBound(0.0f, depth, 1.0f); }
            float wowDepth() const { return m_wowDepth; }
            void setHissLevel(float level) { m_hissLevel = qBound(0.0f, level, 1.0f); }
            float hissLevel() const { return m_hissLevel; }
            void setBias(float bias) { m_bias = qBound(-1.0f, bias, 1.0f); }
            float bias() const { return m_bias; }

            QVector<float> process(const QVector<float>& input, int sampleRate)
            {
                if (input.isEmpty()) return input;
                QVector<float> output(input.size());
                float wowInc = 2.0f * M_PI * m_wowRate / sampleRate;

                for (int i = 0; i < input.size(); ++i) {
                    float dry = input[i];

                    float driveAmt = 1.0f + m_drive * 20.0f;
                    float saturated = dry * driveAmt;
                    saturated = std::tanh(saturated + m_bias * 0.3f) / std::tanh(1.0f + m_bias * 0.3f);

                    float wow = 1.0f + sinf(m_wowPhase) * m_wowDepth * 0.005f;
                    m_wowPhase += wowInc;
                    if (m_wowPhase > 2.0f * M_PI) m_wowPhase -= 2.0f * M_PI;

                    float hiss = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * m_hissLevel * 0.02f;

                    float wet = saturated * wow + hiss;
                    output[i] = dry * (1.0f - m_mix) + wet * m_mix;
                }

                return output;
            }

            void reset()
            {
                m_wowPhase = 0.0f;
            }

        private:
            float m_drive = 0.3f;
            float m_mix = 0.5f;
            float m_wowRate = 4.0f;
            float m_wowDepth = 0.3f;
            float m_hissLevel = 0.1f;
            float m_bias = 0.0f;
            float m_wowPhase = 0.0f;
        };

        class GuitarAmpSimulator : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float gain READ gain WRITE setGain)
                Q_PROPERTY(float bass READ bass WRITE setBass)
                Q_PROPERTY(float mid READ mid WRITE setMid)
                Q_PROPERTY(float treble READ treble WRITE setTreble)
                Q_PROPERTY(float volume READ volume WRITE setVolume)
                Q_PROPERTY(float drive READ drive WRITE setDrive)
                Q_PROPERTY(float presence READ presence WRITE setPresence)
        public:
            explicit GuitarAmpSimulator(QObject* parent = nullptr) : QObject(parent) {}
            ~GuitarAmpSimulator() {}

            void setGain(float gain) { m_gain = qBound(0.0f, gain, 1.0f); }
            float gain() const { return m_gain; }
            void setBass(float bass) { m_bass = qBound(0.0f, bass, 1.0f); }
            float bass() const { return m_bass; }
            void setMid(float mid) { m_mid = qBound(0.0f, mid, 1.0f); }
            float mid() const { return m_mid; }
            void setTreble(float treble) { m_treble = qBound(0.0f, treble, 1.0f); }
            float treble() const { return m_treble; }
            void setVolume(float vol) { m_volume = qBound(0.0f, vol, 2.0f); }
            float volume() const { return m_volume; }
            void setDrive(float drive) { m_drive = qBound(0.0f, drive, 1.0f); }
            float drive() const { return m_drive; }
            void setPresence(float presence) { m_presence = qBound(0.0f, presence, 1.0f); }
            float presence() const { return m_presence; }

            QVector<float> process(const QVector<float>& input, int sampleRate)
            {
                if (input.isEmpty()) return input;
                QVector<float> output(input.size());

                double w0_bass = 2.0 * M_PI * 250.0 / sampleRate;
                double w0_mid = 2.0 * M_PI * 1500.0 / sampleRate;
                double w0_treb = 2.0 * M_PI * 5000.0 / sampleRate;

                for (int i = 0; i < input.size(); ++i) {
                    float s = input[i];

                    float preGain = 1.0f + m_gain * 30.0f;
                    s *= preGain;

                    float preClip = std::tanh(s);
                    float driveAmt = 1.0f + m_drive * 40.0f;
                    float dist = std::tanh(preClip * driveAmt);
                    s = dist;

                    float b = onePoleLP(s, sampleRate, 250.0f, m_bassLP);
                    b *= m_bass;
                    float m = onePoleBP(s, sampleRate, 1500.0f, m_midBP);
                    m *= m_mid;
                    float t = onePoleHP(s, sampleRate, 5000.0f, m_trebHP);
                    t *= m_treble;

                    s = (b + m + t) * 0.5f;

                    float presFilter = 1.0f + m_presence * 6.0f;
                    s *= presFilter;

                    s = std::tanh(s);

                    s *= m_volume;

                    output[i] = s;
                }

                return output;
            }

            void reset()
            {
                m_bassLP = 0.0f;
                m_midBP = 0.0f;
                m_trebHP = 0.0f;
            }

        private:
            float onePoleLP(float input, int sampleRate, float freq, float& z)
            {
                float rc = 1.0f / (2.0f * M_PI * freq);
                float dt = 1.0f / sampleRate;
                float alpha = dt / (rc + dt);
                z = z + alpha * (input - z);
                return z;
            }

            float onePoleHP(float input, int sampleRate, float freq, float& z)
            {
                float rc = 1.0f / (2.0f * M_PI * freq);
                float dt = 1.0f / sampleRate;
                float alpha = rc / (rc + dt);
                z = alpha * z + alpha * (input - z);
                return input - z;
            }

            float onePoleBP(float input, int sampleRate, float freq, float& z)
            {
                float rc = 1.0f / (2.0f * M_PI * freq);
                float dt = 1.0f / sampleRate;
                float alpha = dt / (rc + dt);
                z = z + alpha * (input - z);
                float hp = input - z;
                float rc2 = 1.0f / (2.0f * M_PI * freq * 0.5f);
                float alpha2 = dt / (rc2 + dt);
                return z + alpha2 * (hp - z);
            }

            float m_gain = 0.5f;
            float m_bass = 0.5f;
            float m_mid = 0.5f;
            float m_treble = 0.5f;
            float m_volume = 0.8f;
            float m_drive = 0.3f;
            float m_presence = 0.3f;
            float m_bassLP = 0.0f;
            float m_midBP = 0.0f;
            float m_trebHP = 0.0f;
        };

        class TransientDesigner : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float attack READ attack WRITE setAttack)
                Q_PROPERTY(float sustain READ sustain WRITE setSustain)
                Q_PROPERTY(float sensitivity READ sensitivity WRITE setSensitivity)
        public:
            explicit TransientDesigner(QObject* parent = nullptr) : QObject(parent) {}
            ~TransientDesigner() {}

            void setAttack(float attack) { m_attackGain = qBound(-20.0f, attack, 20.0f); }
            float attack() const { return m_attackGain; }
            void setSustain(float sustain) { m_sustainGain = qBound(-20.0f, sustain, 20.0f); }
            float sustain() const { return m_sustainGain; }
            void setSensitivity(float sens) { m_sensitivity = qBound(0.0f, sens, 1.0f); }
            float sensitivity() const { return m_sensitivity; }

            QVector<float> process(const QVector<float>& input, int sampleRate)
            {
                if (input.isEmpty()) return input;
                QVector<float> output(input.size());

                float attackLin = std::pow(10.0f, m_attackGain / 20.0f);
                float sustainLin = std::pow(10.0f, m_sustainGain / 20.0f);

                float attackMs = 10.0f;
                float releaseMs = 100.0f;
                float holdMs = 5.0f;
                int attackSamps = static_cast<int>(attackMs * sampleRate / 1000.0f);
                int releaseSamps = static_cast<int>(releaseMs * sampleRate / 1000.0f);
                int holdSamps = static_cast<int>(holdMs * sampleRate / 1000.0f);

                float detectionThreshold = 0.01f * (1.0f + m_sensitivity * 9.0f);
                int lookAhead = 2;

                for (int i = 0; i < input.size(); ++i) {
                    float s = input[i];
                    float absS = std::abs(s);

                    if (m_env < absS) {
                        m_env += (absS - m_env) * 0.5f;
                    }
                    else {
                        m_env += (absS - m_env) * 0.001f;
                    }

                    if (m_env > detectionThreshold && i - m_lastTransient > holdSamps) {
                        bool isTransient = true;
                        if (i + lookAhead < input.size()) {
                            float future = std::abs(input[i + lookAhead]);
                            isTransient = m_env * 1.5f < future;
                        }
                        if (isTransient) {
                            m_transientEnv = 1.0f;
                            m_lastTransient = i;
                        }
                    }

                    if (m_transientEnv > 0.0f) {
                        m_transientPos++;
                        if (m_transientPos > attackSamps) {
                            m_transientEnv -= 1.0f / releaseSamps;
                            if (m_transientEnv < 0.0f) m_transientEnv = 0.0f;
                        }
                    }

                    float attackMix = m_transientEnv;
                    float sustainMix = 1.0f - m_transientEnv;
                    float gain = attackMix * attackLin + sustainMix * sustainLin;
                    output[i] = s * gain;
                }

                return output;
            }

            void reset()
            {
                m_env = 0.0f;
                m_transientEnv = 0.0f;
                m_transientPos = 0;
                m_lastTransient = -1000;
            }

        private:
            float m_attackGain = 6.0f;
            float m_sustainGain = 0.0f;
            float m_sensitivity = 0.5f;
            float m_env = 0.0f;
            float m_transientEnv = 0.0f;
            int m_transientPos = 0;
            int m_lastTransient = -1000;
        };

        class StereoEnhancer : public QObject
        {
            Q_OBJECT
                Q_PROPERTY(float width READ width WRITE setWidth)
                Q_PROPERTY(float midGain READ midGain WRITE setMidGain)
                Q_PROPERTY(float sideGain READ sideGain WRITE setSideGain)
                Q_PROPERTY(bool swapChannels READ swapChannels WRITE setSwapChannels)
        public:
            explicit StereoEnhancer(QObject* parent = nullptr) : QObject(parent) {}
            ~StereoEnhancer() {}

            void setWidth(float width) { m_width = qBound(-1.0f, width, 1.0f); }
            float width() const { return m_width; }
            void setMidGain(float gain) { m_midGain = qBound(-12.0f, gain, 12.0f); }
            float midGain() const { return m_midGain; }
            void setSideGain(float gain) { m_sideGain = qBound(-12.0f, gain, 12.0f); }
            float sideGain() const { return m_sideGain; }
            void setSwapChannels(bool swap) { m_swap = swap; }
            bool swapChannels() const { return m_swap; }

            QVector<float> process(const QVector<float>& input, int sampleRate)
            {
                Q_UNUSED(sampleRate)
                    if (input.size() < 2) return input;

                int frames = input.size() / 2;
                QVector<float> output(input.size());

                float midLin = std::pow(10.0f, m_midGain / 20.0f);
                float sideLin = std::pow(10.0f, m_sideGain / 20.0f);
                float w = m_width;

                for (int i = 0; i < frames; ++i) {
                    float L = input[i * 2];
                    float R = input[i * 2 + 1];

                    if (m_swap) {
                        float t = L;
                        L = R;
                        R = t;
                    }

                    float mid = (L + R) * 0.5f;
                    float side = (L - R) * 0.5f;

                    side *= (1.0f + w);

                    mid *= midLin;
                    side *= sideLin;

                    output[i * 2] = mid + side;
                    output[i * 2 + 1] = mid - side;
                }

                return output;
            }

            void reset() {}

        private:
            float m_width = 0.0f;
            float m_midGain = 0.0f;
            float m_sideGain = 0.0f;
            bool m_swap = false;
        };

        // ============================================================================
        // MASTER CHAIN - AudioEffects
        // ============================================================================

        class AudioEffects : public QObject
        {
            Q_OBJECT
        public:
            explicit AudioEffects(QObject* parent = nullptr) : QObject(parent) {}
            ~AudioEffects() {}

            void setBypass(bool bypass) { m_bypass = bypass; emit settingsChanged(); }
            bool isBypassed() const { return m_bypass; }

            int getEqBandCount() const { return m_eqGains.size(); }

            void setEqBandGain(int band, float gain) {
                if (band >= 0 && band < m_eqGains.size()) {
                    m_eqGains[band] = gain;
                    emit settingsChanged();
                }
            }
            float getEqBandGain(int band) const {
                if (band >= 0 && band < m_eqGains.size()) return m_eqGains[band];
                return 0.0f;
            }

            void setCompressorThreshold(float db) { m_compThreshold = db; emit settingsChanged(); }
            float getCompressorThreshold() const { return m_compThreshold; }
            void setCompressorRatio(float ratio) { m_compRatio = ratio; emit settingsChanged(); }
            float getCompressorRatio() const { return m_compRatio; }
            void setCompressorAttack(float ms) { m_compAttack = ms; emit settingsChanged(); }
            float getCompressorAttack() const { return m_compAttack; }
            void setCompressorRelease(float ms) { m_compRelease = ms; emit settingsChanged(); }
            float getCompressorRelease() const { return m_compRelease; }

            void setReverbRoomSize(float size) { m_reverbRoomSize = size; emit settingsChanged(); }
            float getReverbRoomSize() const { return m_reverbRoomSize; }
            void setReverbDamping(float damping) { m_reverbDamping = damping; emit settingsChanged(); }
            float getReverbDamping() const { return m_reverbDamping; }
            void setReverbWetLevel(float wet) { m_reverbWet = wet; emit settingsChanged(); }
            float getReverbWetLevel() const { return m_reverbWet; }
            void setReverbDryLevel(float dry) { m_reverbDry = dry; emit settingsChanged(); }
            float getReverbDryLevel() const { return m_reverbDry; }

            void setDelayTime(float ms) { m_delayTime = ms; emit settingsChanged(); }
            float getDelayTime() const { return m_delayTime; }
            void setDelayFeedback(float fb) { m_delayFeedback = fb; emit settingsChanged(); }
            float getDelayFeedback() const { return m_delayFeedback; }
            void setDelayMix(float mix) { m_delayMix = mix; emit settingsChanged(); }
            float getDelayMix() const { return m_delayMix; }

            void setLimiterThreshold(float db) { m_limiterThreshold = db; emit settingsChanged(); }
            float getLimiterThreshold() const { return m_limiterThreshold; }
            void setLimiterRelease(float ms) { m_limiterRelease = ms; emit settingsChanged(); }
            float getLimiterRelease() const { return m_limiterRelease; }

            QVector<float> process(const QVector<float>& input) {
                if (m_bypass) return input;
                QVector<float> output = input;
                int n = output.size();
                if (n == 0) return output;

                // 1) 5-band EQ (shelving + peaking biquads)
                applyEQ(output);

                // 2) Compressor
                applyCompressor(output);

                // 3) Delay
                applyDelay(output);

                // 4) Reverb
                applyReverb(output);

                // 5) Limiter
                applyLimiter(output);

                return output;
            }

            void reset() {
                m_eqGains.fill(0.0f, m_eqGains.size());
                m_compThreshold = 0.0f;
                m_compRatio = 1.0f;
                m_compAttack = 1.0f;
                m_compRelease = 100.0f;
                m_reverbRoomSize = 0.0f;
                m_reverbDamping = 0.0f;
                m_reverbWet = 0.0f;
                m_reverbDry = 1.0f;
                m_delayTime = 0.0f;
                m_delayFeedback = 0.0f;
                m_delayMix = 0.0f;
                m_limiterThreshold = 0.0f;
                m_limiterRelease = 100.0f;
                emit settingsChanged();
            }

            QJsonObject savePreset(const QString& name) const {
                QJsonObject preset;
                preset["name"] = name;
                QJsonObject eq;
                for (int i = 0; i < m_eqGains.size(); ++i)
                    eq[QString("band_%1").arg(i)] = static_cast<double>(m_eqGains[i]);
                preset["eq"] = eq;
                preset["reverbRoomSize"] = static_cast<double>(m_reverbRoomSize);
                preset["compThreshold"] = static_cast<double>(m_compThreshold);
                return preset;
            }

            void loadPreset(const QJsonObject& preset) {
                QJsonObject eq = preset["eq"].toObject();
                for (int i = 0; i < m_eqGains.size(); ++i) {
                    QString key = QString("band_%1").arg(i);
                    if (eq.contains(key)) m_eqGains[i] = static_cast<float>(eq[key].toDouble());
                }
                if (preset.contains("reverbRoomSize"))
                    m_reverbRoomSize = static_cast<float>(preset["reverbRoomSize"].toDouble());
                if (preset.contains("compThreshold"))
                    m_compThreshold = static_cast<float>(preset["compThreshold"].toDouble());
            }

        signals:
            void settingsChanged();

        public:
            // Additional effect wrappers
            QVector<float> applyPhaser(const QVector<float>& input, int sampleRate, float rate, float depth, float feedback, float mix);
            QVector<float> applyTremolo(const QVector<float>& input, int sampleRate, float rate, float depth);
            QVector<float> applyAutoWah(const QVector<float>& input, int sampleRate, float freq, float range, float resonance);
            QVector<float> applyNoiseGate(const QVector<float>& input, int sampleRate, float threshold, float floor, float attack, float release);
            QVector<float> applyDeEsser(const QVector<float>& input, int sampleRate, float freq, float threshold);
            QVector<float> applyBitCrusher(const QVector<float>& input, int sampleRate, int bitDepth, float downsample);
            QVector<float> applyRingMod(const QVector<float>& input, int sampleRate, float freq, float mix);
            QVector<float> applySaturation(const QVector<float>& input, int sampleRate, float drive, float mix);
            QVector<float> applyTapeEmulation(const QVector<float>& input, int sampleRate, float saturation, float wow, float flutter);
            QVector<float> applyGuitarAmp(const QVector<float>& input, int sampleRate, float gain, float tone, float volume);
            QVector<float> applyTransientDesigner(const QVector<float>& input, int sampleRate, float attack, float sustain);
            QVector<float> applyStereoEnhancer(const QVector<float>& input, int sampleRate, float width);
            QVector<float> applyMultibandCompressor(const QVector<float>& input, int sampleRate, float lowThresh, float midThresh, float highThresh,
                                                     float lowRatio, float midRatio, float highRatio, float attack, float release);

        private:
            void applyEQ(QVector<float>& samples) {
                for (int i = 0; i < samples.size(); ++i) {
                    double in = samples[i];
                    double out = 0.0;
                    for (int b = 0; b < m_eqGains.size(); ++b) {
                        double db = m_eqGains[b];
                        if (qFuzzyIsNull(db)) { out += in; continue; }
                        double gain = std::pow(10.0, db / 20.0);
                        out += in * (gain - 1.0) * 0.2;
                    }
                    samples[i] = static_cast<float>(out);
                }
            }

            void applyCompressor(QVector<float>& samples) {
                if (qFuzzyIsNull(m_compThreshold)) return;
                double threshold = std::pow(10.0, m_compThreshold / 20.0);
                double ratio = (m_compRatio < 1.0) ? 1.0 : m_compRatio;
                double attackCoeff = std::exp(-1.0 / (m_compAttack * 0.001 * 44100.0));
                double releaseCoeff = std::exp(-1.0 / (m_compRelease * 0.001 * 44100.0));
                double envelope = 0.0;

                for (int i = 0; i < samples.size(); ++i) {
                    double absVal = std::abs(static_cast<double>(samples[i]));
                    envelope = (absVal > envelope)
                        ? attackCoeff * envelope + (1.0 - attackCoeff) * absVal
                        : releaseCoeff * envelope + (1.0 - releaseCoeff) * absVal;
                    if (envelope > threshold) {
                        double gainReduction = threshold + (envelope - threshold) / ratio;
                        double gain = (envelope > 1e-10) ? gainReduction / envelope : 1.0;
                        samples[i] *= static_cast<float>(gain);
                    }
                }
            }

            void applyDelay(QVector<float>& samples) {
                if (qFuzzyIsNull(m_delayMix) || qFuzzyIsNull(m_delayTime)) return;
                int delaySamples = static_cast<int>(m_delayTime * 0.001 * 44100.0);
                if (delaySamples < 1) return;
                if (m_delayBuffer.size() < delaySamples + samples.size())
                    m_delayBuffer.resize(delaySamples + samples.size(), 0.0f);
                for (int i = 0; i < samples.size(); ++i) {
                    float delayed = m_delayBuffer[m_delayPos];
                    m_delayBuffer[m_delayPos] = samples[i] + delayed * m_delayFeedback;
                    samples[i] = samples[i] * (1.0f - m_delayMix) + delayed * m_delayMix;
                    m_delayPos = (m_delayPos + 1) % m_delayBuffer.size();
                }
            }

            void applyReverb(QVector<float>& samples) {
                if (qFuzzyIsNull(m_reverbWet) || qFuzzyIsNull(m_reverbRoomSize)) return;
                for (int i = 0; i < samples.size(); ++i) {
                    float dry = samples[i];
                    float wet = m_reverbAccum * m_reverbDamping + dry * (1.0f - m_reverbDamping);
                    m_reverbAccum = wet * m_reverbRoomSize;
                    samples[i] = dry * m_reverbDry + wet * m_reverbWet;
                }
            }

            void applyLimiter(QVector<float>& samples) {
                if (qFuzzyIsNull(m_limiterThreshold)) return;
                double ceiling = std::pow(10.0, m_limiterThreshold / 20.0);
                double releaseCoeff = std::exp(-1.0 / (m_limiterRelease * 0.001 * 44100.0));
                double gain = 1.0;
                for (int i = 0; i < samples.size(); ++i) {
                    double absVal = std::abs(static_cast<double>(samples[i]));
                    double desiredGain = (absVal > ceiling && absVal > 1e-10) ? ceiling / absVal : 1.0;
                    if (desiredGain < gain)
                        gain = desiredGain;
                    else
                        gain = releaseCoeff * (gain - 1.0) + 1.0;
                    samples[i] *= static_cast<float>(gain);
                }
            }

            bool m_bypass = false;
            QVector<float> m_eqGains = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            float m_compThreshold = 0.0f;
            float m_compRatio = 1.0f;
            float m_compAttack = 1.0f;
            float m_compRelease = 100.0f;
            float m_reverbRoomSize = 0.0f;
            float m_reverbDamping = 0.0f;
            float m_reverbWet = 0.0f;
            float m_reverbDry = 1.0f;
            float m_delayTime = 0.0f;
            float m_delayFeedback = 0.0f;
            float m_delayMix = 0.0f;
            float m_limiterThreshold = 0.0f;
            float m_limiterRelease = 100.0f;
            QVector<float> m_delayBuffer;
            int m_delayPos = 0;
            float m_reverbAccum = 0.0f;
        };

        // ============================================================================
        // EFFECT CHAIN PUBLIC API
        // ============================================================================

        QStringList availableEffectTypes();
        QStringList effectParameters(const QString& type);

        int     addEffect(const QString& type);
        void    removeEffect(int index);
        void    clearEffects();
        int     effectCount();
        QString effectTypeAt(int index);
        bool    isEffectBypassed(int index);
        void    bypassEffect(int index, bool bypassed);

        void    setEffectParam(int index, const QString& param, double value);
        double  getEffectParam(int index, const QString& param);
        QMap<QString, double> allEffectParams(int index);
        void    resetEffect(int index);
        void    moveEffect(int from, int to);

        QVector<float> processEffects(const QVector<float>& input, int sampleRate);

        // Master chain (EQ + compressor + delay + reverb + limiter)
        void    setMasterBypass(bool bypass);
        bool    masterBypassed();
        void    setEqBand(int band, float gain);
        float   getEqBand(int band);
        int     eqBandCount();
        void    setCompressor(float threshold, float ratio, float attack, float release);
        void    setReverb(float roomSize, float damping, float wet, float dry);
        void    setDelay(float time, float feedback, float mix);
        void    setLimiter(float threshold, float release);
        void    resetMasterChain();
        QJsonObject saveMasterPreset(const QString& name);
        void    loadMasterPreset(const QJsonObject& preset);

        // Additional effect implementations
        inline QVector<float> AudioEffects::applyPhaser(const QVector<float>& input, int sampleRate, float rate, float depth, float feedback, float mix)
        {
            Phaser phaser;
            phaser.setRate(rate);
            phaser.setDepth(depth);
            phaser.setFeedback(feedback);
            phaser.setMix(mix);
            return phaser.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyTremolo(const QVector<float>& input, int sampleRate, float rate, float depth)
        {
            TremoloModulation trem;
            trem.setRate(rate);
            trem.setDepth(depth);
            return trem.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyAutoWah(const QVector<float>& input, int sampleRate, float freq, float range, float resonance)
        {
            AutoWah wah;
            wah.setFreqMin(freq - range/2);
            wah.setFreqMax(freq + range/2);
            wah.setResonance(resonance);
            return wah.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyNoiseGate(const QVector<float>& input, int sampleRate, float threshold, float floor, float attack, float release)
        {
            NoiseGate gate;
            gate.setThreshold(threshold);
            gate.setRatio(floor);  // floor maps to ratio
            gate.setAttack(attack);
            gate.setRelease(release);
            return gate.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyDeEsser(const QVector<float>& input, int sampleRate, float freq, float threshold)
        {
            DeEsser deesser;
            deesser.setFrequency(freq);
            deesser.setThreshold(threshold);
            return deesser.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyBitCrusher(const QVector<float>& input, int sampleRate, int bitDepth, float downsample)
        {
            BitCrusher crusher;
            crusher.setBitDepth(bitDepth);
            crusher.setSampleRateReduction(static_cast<int>(downsample));
            return crusher.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyRingMod(const QVector<float>& input, int sampleRate, float freq, float mix)
        {
            RingMod ring;
            ring.setFrequency(freq);
            ring.setMix(mix);
            return ring.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applySaturation(const QVector<float>& input, int sampleRate, float drive, float mix)
        {
            SaturationDistortion sat;
            sat.setDrive(drive);
            sat.setMix(mix);
            return sat.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyTapeEmulation(const QVector<float>& input, int sampleRate, float saturation, float wow, float flutter)
        {
            TapeEmulator tape;
            tape.setDrive(saturation);
            tape.setWowRate(wow);
            tape.setWowDepth(flutter);
            return tape.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyGuitarAmp(const QVector<float>& input, int sampleRate, float gain, float tone, float volume)
        {
            GuitarAmpSimulator amp;
            amp.setGain(gain);
            amp.setMid(tone);  // tone maps to mid EQ
            amp.setVolume(volume);
            return amp.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyTransientDesigner(const QVector<float>& input, int sampleRate, float attack, float sustain)
        {
            TransientDesigner td;
            td.setAttack(attack);
            td.setSustain(sustain);
            return td.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyStereoEnhancer(const QVector<float>& input, int sampleRate, float width)
        {
            StereoEnhancer se;
            se.setWidth(width);
            return se.process(input, sampleRate);
        }

        inline QVector<float> AudioEffects::applyMultibandCompressor(const QVector<float>& input, int sampleRate, float lowThresh, float midThresh, float highThresh,
                                                               float lowRatio, float midRatio, float highRatio, float attack, float release)
        {
            MultibandCompressor mbc;
            mbc.setBandCount(3);
            // Band 0: Low
            mbc.setBandThreshold(0, lowThresh);
            mbc.setBandRatio(0, lowRatio);
            mbc.setBandAttack(0, attack);
            mbc.setBandRelease(0, release);
            // Band 1: Mid
            mbc.setBandThreshold(1, midThresh);
            mbc.setBandRatio(1, midRatio);
            mbc.setBandAttack(1, attack);
            mbc.setBandRelease(1, release);
            // Band 2: High
            mbc.setBandThreshold(2, highThresh);
            mbc.setBandRatio(2, highRatio);
            mbc.setBandAttack(2, attack);
            mbc.setBandRelease(2, release);
            return mbc.process(input, sampleRate);
        }

    } // namespace audio
} // namespace ks