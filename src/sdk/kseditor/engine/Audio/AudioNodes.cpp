#include "AudioGraph.h"
#include "DSPLibrary.h"
#include <QJsonObject>
#include <QJsonArray>

namespace ks {
namespace audio {

// â”€â”€â”€ Base Audio Node with DSP â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

template<typename DspType>
class DspNode : public AudioNode {

public:
    DspNode(const QString& typeName, const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode(typeName, id, parent)
    {
        dsp.prepare(44100.0);
    }

    void prepare(double sampleRate, int maxBlockSize) override {
        dsp.prepare(float(sampleRate));
    }

    void reset() override {
        dsp.reset();
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

protected:
    DspType dsp;
};

// â”€â”€â”€ Generator Nodes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

class SineGenerator : public AudioNode {
    Q_OBJECT

public:
    explicit SineGenerator(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("SineGenerator", id, parent) {
        m_displayName = "Sine Generator";
        setParameter("frequency", 440.0);
        setParameter("amplitude", 0.5);
        setParameter("phase", 0.0);
        
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void prepare(double sampleRate, int maxBlockSize) override {
        m_sampleRate = float(sampleRate);
        m_phaseAcc = 0;
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!outPort || !outPort->outputBuffer) return;
        
        float freq = getParameter("frequency").toFloat();
        float amp = getParameter("amplitude").toFloat();
        float phase = getParameter("phase").toFloat();
        
        float phaseInc = freq * 2.0f * M_PI / m_sampleRate;
        float* out = outPort->outputBuffer;
        
        for (int i = 0; i < numFrames; ++i) {
            out[i] = amp * sinf(m_phaseAcc + phase);
            m_phaseAcc += phaseInc;
            if (m_phaseAcc > 2.0f * M_PI) m_phaseAcc -= 2.0f * M_PI;
        }
    }

    void reset() override { m_phaseAcc = 0; }

private:
    float m_sampleRate = 44100;
    float m_phaseAcc = 0;
};

class NoiseGenerator : public AudioNode {
    Q_OBJECT

public:
    explicit NoiseGenerator(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("NoiseGenerator", id, parent) {
        m_displayName = "Noise Generator";
        setParameter("type", 0); // 0=white, 1=pink, 2=brown
        setParameter("amplitude", 0.5);
        
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void prepare(double sampleRate, int) override { m_sampleRate = float(sampleRate); }

    void process(const QMap<QUuid, const float*>&, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!outPort || !outPort->outputBuffer) return;
        
        int type = getParameter("type").toInt();
        float amp = getParameter("amplitude").toFloat();
        float* out = outPort->outputBuffer;
        
        if (type == 0) { // White noise
            for (int i = 0; i < numFrames; ++i) {
                out[i] = amp * (2.0f * (rand() / float(RAND_MAX)) - 1.0f);
            }
        } else if (type == 1) { // Pink noise (approximation)
            static float pinkState[6] = {0};
            for (int i = 0; i < numFrames; ++i) {
                float white = 2.0f * (rand() / float(RAND_MAX)) - 1.0f;
                pinkState[0] = 0.99886f * pinkState[0] + white * 0.0555179f;
                pinkState[1] = 0.99332f * pinkState[1] + white * 0.0750759f;
                pinkState[2] = 0.96900f * pinkState[2] + white * 0.1538520f;
                pinkState[3] = 0.86650f * pinkState[3] + white * 0.3104856f;
                pinkState[4] = 0.55000f * pinkState[4] + white * 0.5329522f;
                pinkState[5] = -0.7616f * pinkState[5] - white * 0.0168980f;
                float sum = pinkState[0] + pinkState[1] + pinkState[2] + pinkState[3] + pinkState[4] + pinkState[5] + pinkState[6];
                out[i] = amp * sum * 0.11f;
            }
        }
    }

    void reset() override {}

private:
    float m_sampleRate = 44100;
};

// â”€â”€â”€ Effect Nodes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

class GainNode : public AudioNode {
    Q_OBJECT

public:
    explicit GainNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("Gain", id, parent) {
        m_displayName = "Gain";
        setParameter("gainDb", 0.0);
        setParameter("pan", 0.0);
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void prepare(double sampleRate, int) override { m_sampleRate = float(sampleRate); }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        float gainDb = getParameter("gainDb").toFloat();
        float pan = getParameter("pan").toFloat(); // -1 to 1
        float gain = dsp::dbToGain(gainDb);
        
        for (int i = 0; i < numFrames; ++i) {
            out[i] = in[i] * gain;
        }
    }

    void reset() override {}

private:
    float m_sampleRate = 44100;
};

class ParametricEQNode : public DspNode<dsp::ParametricEQ> {
    Q_OBJECT
public:
    explicit ParametricEQNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : DspNode("ParametricEQ", id, parent) {
        m_displayName = "Parametric EQ";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
        
        // Add parameters for each band
        for (int i = 0; i < 6; ++i) {
            setParameter(QString("band%1_freq").arg(i), QVector<float>{60, 250, 1000, 4000, 8000, 16000}[i]);
            setParameter(QString("band%1_q").arg(i), 1.0f);
            setParameter(QString("band%1_gain").arg(i), 0.0f);
            setParameter(QString("band%1_type").arg(i), 0);
            setParameter(QString("band%1_enabled").arg(i), true);
        }
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        // Update bands from parameters
        for (int i = 0; i < 6; ++i) {
            float freq = getParameter(QString("band%1_freq").arg(i)).toFloat();
            float q = getParameter(QString("band%1_q").arg(i)).toFloat();
            float gain = getParameter(QString("band%1_gain").arg(i)).toFloat();
            int type = getParameter(QString("band%1_type").arg(i)).toInt();
            bool enabled = getParameter(QString("band%1_enabled").arg(i)).toBool();
            
            dsp.bands[i].enabled = enabled;
            if (enabled) dsp.setBand(i, freq, q, gain, type);
        }
        
        dsp.processBlock(in, out, numFrames);
    }
};

class CompressorNode : public DspNode<dsp::Compressor> {
    Q_OBJECT
public:
    explicit CompressorNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : DspNode("Compressor", id, parent) {
        m_displayName = "Compressor";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addInputPort({QUuid::createUuid(), "sidechain", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
        
        setParameter("thresholdDb", -12.0f);
        setParameter("ratio", 4.0f);
        setParameter("attackMs", 10.0f);
        setParameter("releaseMs", 100.0f);
        setParameter("kneeDb", 6.0f);
        setParameter("makeupGainDb", 0.0f);
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* scPort = getInputPort(m_inputPorts[1].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        const float* sc = inputs.value(scPort->id, scPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        dsp.thresholdDb = getParameter("thresholdDb").toFloat();
        dsp.ratio = getParameter("ratio").toFloat();
        dsp.attackMs = getParameter("attackMs").toFloat();
        dsp.releaseMs = getParameter("releaseMs").toFloat();
        dsp.kneeDb = getParameter("kneeDb").toFloat();
        dsp.makeupGainDb = getParameter("makeupGainDb").toFloat();
        
        // Use sidechain if connected, otherwise use input
        const float* detector = sc ? sc : in;
        dsp.processBlock(detector, out, numFrames);
        
        // Apply gain to main signal if different from detector
        if (sc && sc != in) {
            for (int i = 0; i < numFrames; ++i) {
                out[i] = in[i] * (out[i] / (detector[i] + 1e-10f));
            }
        }
    }
};

class ReverbNode : public DspNode<dsp::Reverb> {
    Q_OBJECT
public:
    explicit ReverbNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : DspNode("Reverb", id, parent) {
        m_displayName = "Reverb";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
        
        setParameter("roomSize", 0.5f);
        setParameter("damping", 0.5f);
        setParameter("wet", 0.3f);
        setParameter("dry", 0.5f);
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        dsp.roomSize = getParameter("roomSize").toFloat();
        dsp.damping = getParameter("damping").toFloat();
        dsp.wet = getParameter("wet").toFloat();
        dsp.dry = getParameter("dry").toFloat();
        
        dsp.processBlock(in, out, numFrames);
    }
};

class DelayNode : public DspNode<dsp::DelayLine> {
    Q_OBJECT
public:
    explicit DelayNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : DspNode("Delay", id, parent) {
        m_displayName = "Delay";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
        
        setParameter("delayMs", 500.0f);
        setParameter("feedback", 0.5f);
        setParameter("mix", 0.5f);
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        dsp.setDelayMs(getParameter("delayMs").toFloat());
        dsp.feedback = getParameter("feedback").toFloat();
        dsp.mix = getParameter("mix").toFloat();
        
        dsp.processBlock(in, out, numFrames);
    }
};

class ChorusNode : public DspNode<dsp::Chorus> {
    Q_OBJECT
public:
    explicit ChorusNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : DspNode("Chorus", id, parent) {
        m_displayName = "Chorus";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
        
        setParameter("rate", 1.5f);
        setParameter("depth", 0.003f);
        setParameter("mix", 0.5f);
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        dsp.rate = getParameter("rate").toFloat();
        dsp.depth = getParameter("depth").toFloat();
        dsp.mix = getParameter("mix").toFloat();
        
        dsp.processBlock(in, out, numFrames);
    }
};

class DistortionNode : public AudioNode {
    Q_OBJECT

public:
    enum Type { SoftClip, HardClip, WaveShape, Tube, Tape };
    
    explicit DistortionNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("Distortion", id, parent) {
        m_displayName = "Distortion";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
        
        setParameter("type", 0);
        setParameter("drive", 1.0f);
        setParameter("mix", 1.0f);
        setParameter("tone", 0.5f);
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void prepare(double sampleRate, int) override { m_sampleRate = float(sampleRate); }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!inPort || !outPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!in || !out) return;
        
        int type = getParameter("type").toInt();
        float drive = getParameter("drive").toFloat();
        float mix = getParameter("mix").toFloat();
        
        for (int i = 0; i < numFrames; ++i) {
            float x = in[i] * drive;
            float y = x;
            
            switch (type) {
                case SoftClip: y = tanhf(x); break;
                case HardClip: y = fmaxf(-1.0f, fminf(1.0f, x)); break;
                case Tube: y = x / (1.0f + fabsf(x)); break;
                case Tape: y = (1.0f - expf(-fabsf(x))) * (x >= 0 ? 1.0f : -1.0f); break;
                case WaveShape: {
                    // Chebyshev 2nd order
                    y = x + 0.5f * (2.0f * x * x - 1.0f);
                    y = fmaxf(-1.0f, fminf(1.0f, y));
                    break;
                }
            }
            
            out[i] = in[i] * (1.0f - mix) + y * mix;
        }
    }

    void reset() override {}

private:
    float m_sampleRate = 44100;
};

// â”€â”€â”€ Analyzer Nodes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

class SpectrumAnalyzerNode : public AudioNode {
    Q_OBJECT

public:
    explicit SpectrumAnalyzerNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("SpectrumAnalyzer", id, parent) {
        m_displayName = "Spectrum Analyzer";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        // Output is control data (magnitudes)
        addOutputPort({QUuid::createUuid(), "magnitudes", AudioPortType::Control, PortDirection::Output});
        addOutputPort({QUuid::createUuid(), "peaks", AudioPortType::Control, PortDirection::Output});
        
        setParameter("fftSize", 1024);
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void prepare(double sampleRate, int maxBlockSize) override {
        m_sampleRate = float(sampleRate);
        m_analyzer.prepare(getParameter("fftSize").toInt(), m_sampleRate);
        m_magnitudes.resize(m_analyzer.magnitudes.size());
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        if (!inPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        if (!in) return;
        
        m_analyzer.processBlock(in, numFrames);
        
        // Output magnitudes as control data
        auto* magPort = getOutputPort(m_outputPorts[0].id);
        if (magPort) {
            magPort->value = QVariant::fromValue(m_analyzer.getMagnitudes());
        }
        auto* peakPort = getOutputPort(m_outputPorts[1].id);
        if (peakPort) {
            peakPort->value = QVariant::fromValue(m_analyzer.getPeakHold());
        }
    }

    void reset() override {}

private:
    float m_sampleRate = 44100;
    dsp::SpectrumAnalyzer m_analyzer;
    QVector<float> m_magnitudes;
};

class PeakMeterNode : public AudioNode {
    Q_OBJECT

public:
    explicit PeakMeterNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("PeakMeter", id, parent) {
        m_displayName = "Peak Meter";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "peak", AudioPortType::Control, PortDirection::Output});
        addOutputPort({QUuid::createUuid(), "rms", AudioPortType::Control, PortDirection::Output});
        
        setParameter("windowMs", 300.0f);
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void prepare(double sampleRate, int) override {
        m_meter.configure(float(sampleRate));
    }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* inPort = getInputPort(m_inputPorts[0].id);
        if (!inPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        if (!in) return;
        
        m_meter.processBlock(in, numFrames);
        
        auto* peakPort = getOutputPort(m_outputPorts[0].id);
        if (peakPort) peakPort->value = m_meter.getPeakDb();
        auto* rmsPort = getOutputPort(m_outputPorts[1].id);
        if (rmsPort) rmsPort->value = m_meter.getRmsDb();
    }

    void reset() override { m_meter.reset(); }

private:
    dsp::PeakMeter m_meter;
};

// â”€â”€â”€ Utility Nodes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

class SplitterNode : public AudioNode {
    Q_OBJECT

public:
    explicit SplitterNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("Splitter", id, parent) {
        m_displayName = "Channel Splitter";
        
        addInputPort({QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "left", AudioPortType::Audio, PortDirection::Output});
        addOutputPort({QUuid::createUuid(), "right", AudioPortType::Audio, PortDirection::Output});
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        // Expects interleaved stereo input
        auto* inPort = getInputPort(m_inputPorts[0].id);
        if (!inPort) return;
        
        const float* in = inputs.value(inPort->id, inPort->inputBuffer);
        if (!in) return;
        
        auto* leftPort = getOutputPort(m_outputPorts[0].id);
        auto* rightPort = getOutputPort(m_outputPorts[1].id);
        if (!leftPort || !rightPort) return;
        
        float* left = leftPort->outputBuffer;
        float* right = rightPort->outputBuffer;
        if (!left || !right) return;
        
        for (int i = 0; i < numFrames; ++i) {
            left[i] = in[i * 2];
            right[i] = in[i * 2 + 1];
        }
    }

    void prepare(double, int) override {}
    void reset() override {}
};

class MergerNode : public AudioNode {
    Q_OBJECT

public:
    explicit MergerNode(const QUuid& id = QUuid::createUuid(), QObject* parent = nullptr)
        : AudioNode("Merger", id, parent) {
        m_displayName = "Channel Merger";
        
        addInputPort({QUuid::createUuid(), "left", AudioPortType::Audio, PortDirection::Input});
        addInputPort({QUuid::createUuid(), "right", AudioPortType::Audio, PortDirection::Input});
        addOutputPort({QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output});
    }

    QVector<AudioPort> getInputPorts() const override { return m_inputPorts; }
    QVector<AudioPort> getOutputPorts() const override { return m_outputPorts; }

    void process(const QMap<QUuid, const float*>& inputs, 
                 QMap<QUuid, float*>& outputs, int numFrames) override {
        auto* leftPort = getInputPort(m_inputPorts[0].id);
        auto* rightPort = getInputPort(m_inputPorts[1].id);
        auto* outPort = getOutputPort(m_outputPorts[0].id);
        if (!leftPort || !rightPort || !outPort) return;
        
        const float* left = inputs.value(leftPort->id, leftPort->inputBuffer);
        const float* right = inputs.value(rightPort->id, rightPort->inputBuffer);
        float* out = outPort->outputBuffer;
        if (!left || !right || !out) return;
        
        for (int i = 0; i < numFrames; ++i) {
            out[i * 2] = left[i];
            out[i * 2 + 1] = right[i];
        }
    }

    void prepare(double, int) override {}
    void reset() override {}
};

// â”€â”€â”€ Node Registration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static bool registerAudioNodes() {
    // Generators
    AudioGraph::registerNodeType("SineGenerator", 
        AudioNodeInfo{"SineGenerator", "Sine Generator", "Generator", "Sine wave oscillator",
            {}, {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"frequency", 440.0}, {"amplitude", 0.5}, {"phase", 0.0}},
            true, false, false, false},
        [](const QUuid& id) { return std::make_unique<SineGenerator>(id); });
    
    AudioGraph::registerNodeType("NoiseGenerator",
        AudioNodeInfo{"NoiseGenerator", "Noise Generator", "Generator", "White/pink/brown noise",
            {}, {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"type", 0}, {"amplitude", 0.5}},
            true, false, false, false},
        [](const QUuid& id) { return std::make_unique<NoiseGenerator>(id); });
    
    // Effects
    AudioGraph::registerNodeType("Gain",
        AudioNodeInfo{"Gain", "Gain/Pan", "Effect", "Gain and panning",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"gainDb", 0.0}, {"pan", 0.0}},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<GainNode>(id); });
    
    AudioGraph::registerNodeType("ParametricEQ",
        AudioNodeInfo{"ParametricEQ", "Parametric EQ", "Effect", "6-band parametric equalizer",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<ParametricEQNode>(id); });
    
    AudioGraph::registerNodeType("Compressor",
        AudioNodeInfo{"Compressor", "Compressor", "Effect", "Dynamic range compressor with sidechain",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input},
             {QUuid::createUuid(), "sidechain", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"thresholdDb", -12.0}, {"ratio", 4.0}, {"attackMs", 10.0}, {"releaseMs", 100.0}, {"kneeDb", 6.0}, {"makeupGainDb", 0.0}},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<CompressorNode>(id); });
    
    AudioGraph::registerNodeType("Reverb",
        AudioNodeInfo{"Reverb", "Reverb", "Effect", "Algorithmic reverb",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"roomSize", 0.5}, {"damping", 0.5}, {"wet", 0.3}, {"dry", 0.5}},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<ReverbNode>(id); });
    
    AudioGraph::registerNodeType("Delay",
        AudioNodeInfo{"Delay", "Delay", "Effect", "Feedback delay line",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"delayMs", 500.0}, {"feedback", 0.5}, {"mix", 0.5}},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<DelayNode>(id); });
    
    AudioGraph::registerNodeType("Chorus",
        AudioNodeInfo{"Chorus", "Chorus", "Effect", "Modulated delay chorus",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"rate", 1.5}, {"depth", 0.003}, {"mix", 0.5}},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<ChorusNode>(id); });
    
    AudioGraph::registerNodeType("Distortion",
        AudioNodeInfo{"Distortion", "Distortion", "Effect", "Multiple distortion types",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {{"type", 0}, {"drive", 1.0}, {"mix", 1.0}, {"tone", 0.5}},
            false, true, false, false},
        [](const QUuid& id) { return std::make_unique<DistortionNode>(id); });
    
    // Analyzers
    AudioGraph::registerNodeType("SpectrumAnalyzer",
        AudioNodeInfo{"SpectrumAnalyzer", "Spectrum Analyzer", "Analyzer", "FFT spectrum analyzer",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "magnitudes", AudioPortType::Control, PortDirection::Output},
             {QUuid::createUuid(), "peaks", AudioPortType::Control, PortDirection::Output}},
            {{"fftSize", 1024}},
            false, false, true, false},
        [](const QUuid& id) { return std::make_unique<SpectrumAnalyzerNode>(id); });
    
    AudioGraph::registerNodeType("PeakMeter",
        AudioNodeInfo{"PeakMeter", "Peak Meter", "Analyzer", "Peak and RMS level meter",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "peak", AudioPortType::Control, PortDirection::Output},
             {QUuid::createUuid(), "rms", AudioPortType::Control, PortDirection::Output}},
            {{"windowMs", 300.0}},
            false, false, true, false},
        [](const QUuid& id) { return std::make_unique<PeakMeterNode>(id); });
    
    // Utilities
    AudioGraph::registerNodeType("Splitter",
        AudioNodeInfo{"Splitter", "Channel Splitter", "Utility", "Split stereo to mono",
            {{QUuid::createUuid(), "in", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "left", AudioPortType::Audio, PortDirection::Output},
             {QUuid::createUuid(), "right", AudioPortType::Audio, PortDirection::Output}},
            {},
            false, false, false, false},
        [](const QUuid& id) { return std::make_unique<SplitterNode>(id); });
    
    AudioGraph::registerNodeType("Merger",
        AudioNodeInfo{"Merger", "Channel Merger", "Utility", "Merge mono to stereo",
            {{QUuid::createUuid(), "left", AudioPortType::Audio, PortDirection::Input},
             {QUuid::createUuid(), "right", AudioPortType::Audio, PortDirection::Input}},
            {{QUuid::createUuid(), "out", AudioPortType::Audio, PortDirection::Output}},
            {},
            false, false, false, false},
        [](const QUuid& id) { return std::make_unique<MergerNode>(id); });
    
    return true;
}

// Auto-registration
static bool s_audioNodesRegistered = registerAudioNodes();

} // namespace audio
} // namespace ks

#include "AudioNodes.moc"
