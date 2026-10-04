#include <QtTest>
#include <QVector>
#include <cmath>

#include "engine/Audio/AudioEffectsAdvanced.h"

using namespace ks::audio;

class TestAudioEffectsAdvanced : public QObject {
    Q_OBJECT

private slots:
    void test_convolutionReverb_defaultParams();
    void test_convolutionReverb_process();
    void test_convolutionReverb_reset();

    void test_multibandCompressor_defaultParams();
    void test_multibandCompressor_process();
    void test_multibandCompressor_reset();

    void test_tapeEmulator_defaultParams();
    void test_tapeEmulator_process();
    void test_tapeEmulator_reset();

    void test_guitarAmp_defaultParams();
    void test_guitarAmp_process();
    void test_guitarAmp_reset();

    void test_transientDesigner_defaultParams();
    void test_transientDesigner_process();
    void test_transientDesigner_reset();

    void test_stereoEnhancer_defaultParams();
    void test_stereoEnhancer_processMono();
    void test_stereoEnhancer_processStereo();
    void test_stereoEnhancer_swap();
};

void TestAudioEffectsAdvanced::test_convolutionReverb_defaultParams()
{
    ConvolutionReverb rev;
    QCOMPARE(rev.mix(), 0.3f);
    QCOMPARE(rev.preDelay(), 20.0f);
    QCOMPARE(rev.gain(), 1.0f);
    QVERIFY(!rev.isImpulseLoaded());
}

void TestAudioEffectsAdvanced::test_convolutionReverb_process()
{
    ConvolutionReverb rev;
    QVector<float> input(100, 0.5f);
    QVector<float> output = rev.process(input, 44100);
    QCOMPARE(output.size(), input.size());
}

void TestAudioEffectsAdvanced::test_convolutionReverb_reset()
{
    ConvolutionReverb rev;
    rev.setMix(0.8f);
    rev.process({0.5f}, 44100);
    rev.reset();
    QVERIFY(!rev.isImpulseLoaded());
    QCOMPARE(rev.mix(), 0.8f);
}

void TestAudioEffectsAdvanced::test_multibandCompressor_defaultParams()
{
    MultibandCompressor comp;
    QCOMPARE(comp.bandCount(), 3);
    QCOMPARE(comp.crossover0(), 250.0f);
    QCOMPARE(comp.crossover1(), 2000.0f);
    QCOMPARE(comp.crossover2(), 8000.0f);
}

void TestAudioEffectsAdvanced::test_multibandCompressor_process()
{
    MultibandCompressor comp;
    QVector<float> input(200, 1.0f);
    QVector<float> output = comp.process(input, 44100);
    QCOMPARE(output.size(), input.size());
}

void TestAudioEffectsAdvanced::test_multibandCompressor_reset()
{
    MultibandCompressor comp;
    comp.setBandCount(4);
    comp.setCrossover0(100.0f);
    comp.process({1.0f}, 44100);
    comp.reset();
    QCOMPARE(comp.bandCount(), 4);
}

void TestAudioEffectsAdvanced::test_tapeEmulator_defaultParams()
{
    TapeEmulator tape;
    QCOMPARE(tape.drive(), 0.3f);
    QCOMPARE(tape.mix(), 0.5f);
    QCOMPARE(tape.wowRate(), 4.0f);
    QCOMPARE(tape.wowDepth(), 0.3f);
    QCOMPARE(tape.hissLevel(), 0.1f);
    QCOMPARE(tape.bias(), 0.0f);
}

void TestAudioEffectsAdvanced::test_tapeEmulator_process()
{
    TapeEmulator tape;
    QVector<float> input(500, 0.25f);
    QVector<float> output = tape.process(input, 44100);
    QCOMPARE(output.size(), input.size());
}

void TestAudioEffectsAdvanced::test_tapeEmulator_reset()
{
    TapeEmulator tape;
    tape.setDrive(0.8f);
    tape.process({0.5f}, 44100);
    tape.reset();
    QCOMPARE(tape.drive(), 0.8f);
}

void TestAudioEffectsAdvanced::test_guitarAmp_defaultParams()
{
    GuitarAmpSimulator amp;
    QCOMPARE(amp.gain(), 0.5f);
    QCOMPARE(amp.bass(), 0.5f);
    QCOMPARE(amp.mid(), 0.5f);
    QCOMPARE(amp.treble(), 0.5f);
    QCOMPARE(amp.volume(), 0.8f);
    QCOMPARE(amp.drive(), 0.3f);
    QCOMPARE(amp.presence(), 0.3f);
}

void TestAudioEffectsAdvanced::test_guitarAmp_process()
{
    GuitarAmpSimulator amp;
    QVector<float> input(200, 0.1f);
    QVector<float> output = amp.process(input, 44100);
    QCOMPARE(output.size(), input.size());
}

void TestAudioEffectsAdvanced::test_guitarAmp_reset()
{
    GuitarAmpSimulator amp;
    amp.setGain(1.0f);
    amp.process({0.5f}, 44100);
    amp.reset();
    QCOMPARE(amp.gain(), 1.0f);
}

void TestAudioEffectsAdvanced::test_transientDesigner_defaultParams()
{
    TransientDesigner td;
    QCOMPARE(td.attack(), 6.0f);
    QCOMPARE(td.sustain(), 0.0f);
    QCOMPARE(td.sensitivity(), 0.5f);
}

void TestAudioEffectsAdvanced::test_transientDesigner_process()
{
    TransientDesigner td;
    QVector<float> input(1000, 0.0f);
    for (int i = 100; i < 110; ++i) input[i] = 1.0f;
    QVector<float> output = td.process(input, 44100);
    QCOMPARE(output.size(), input.size());
}

void TestAudioEffectsAdvanced::test_transientDesigner_reset()
{
    TransientDesigner td;
    td.setAttack(12.0f);
    td.setSustain(-6.0f);
    td.process({1.0f}, 44100);
    td.reset();
    QCOMPARE(td.attack(), 12.0f);
    QCOMPARE(td.sustain(), -6.0f);
}

void TestAudioEffectsAdvanced::test_stereoEnhancer_defaultParams()
{
    StereoEnhancer se;
    QCOMPARE(se.width(), 0.0f);
    QCOMPARE(se.midGain(), 0.0f);
    QCOMPARE(se.sideGain(), 0.0f);
    QVERIFY(!se.swapChannels());
}

void TestAudioEffectsAdvanced::test_stereoEnhancer_processMono()
{
    StereoEnhancer se;
    QVector<float> mono(10, 0.5f);
    QVector<float> output = se.process(mono, 44100);
    QCOMPARE(output, mono);
}

void TestAudioEffectsAdvanced::test_stereoEnhancer_processStereo()
{
    StereoEnhancer se;
    QVector<float> stereo = {0.5f, 0.3f, 0.8f, 0.2f, 1.0f, 0.0f};
    QVector<float> output = se.process(stereo, 44100);
    QCOMPARE(output.size(), stereo.size());

    se.setWidth(0.5f);
    se.setMidGain(1.0f);
    se.setSideGain(2.0f);
    output = se.process(stereo, 44100);
    QCOMPARE(output.size(), stereo.size());
}

void TestAudioEffectsAdvanced::test_stereoEnhancer_swap()
{
    StereoEnhancer se;
    QVector<float> stereo = {0.5f, 0.3f, 0.8f, 0.2f};
    se.setSwapChannels(true);
    QVector<float> output = se.process(stereo, 44100);
    QCOMPARE(output.size(), stereo.size());
}

QTEST_MAIN(TestAudioEffectsAdvanced)
#include "test_AudioEffectsAdvanced.moc"
