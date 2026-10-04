#include "AudioVST3Host.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ks {
namespace audio {

// ─── FUID Definitions ──────────────────────────────────────────────────────
#pragma warning(push)
#pragma warning(disable:4003)  // MAKE_FUID macro not used here
#define KS_MAKE_FUID4(a,b,c,d)  Vst3FUID{{uint32_t(a),uint32_t(b),uint32_t(c),uint32_t(d)}}

namespace VST3FUIDs {
    const Vst3FUID IUnknown         = KS_MAKE_FUID4(0x00000000,0x00000000,0x00000000,0x00000000);
    const Vst3FUID IPluginBase      = KS_MAKE_FUID4(0x00000001,0x00000000,0x00000000,0x00000000);
    const Vst3FUID IComponent       = KS_MAKE_FUID4(0x00000002,0x00000000,0x00000000,0x00000000);
    const Vst3FUID IAudioProcessor  = KS_MAKE_FUID4(0x00000003,0x00000000,0x00000000,0x00000000);
    const Vst3FUID IEditController  = KS_MAKE_FUID4(0x00000004,0x00000000,0x00000000,0x00000000);
    const Vst3FUID IEditController2 = KS_MAKE_FUID4(0x00000005,0x00000000,0x00000000,0x00000000);
    const Vst3FUID IConnectionPoint = KS_MAKE_FUID4(0x00000006,0x00000000,0x00000000,0x00000000);
}
#pragma warning(pop)

} // namespace audio
} // namespace ks

Vst3FUID Vst3FUID::fromString(const char* hex) {
    Vst3FUID uid;
    // Simplified: parse 32 hex chars into data[0..3]
    unsigned long long v[2] = {0,0};
    if (hex) {
        size_t len = std::strlen(hex);
        char buf[17] = {};
        if (len >= 16) { std::strncpy(buf, hex, 16); buf[16]=0; v[0] = std::strtoull(buf,nullptr,16); }
        if (len >= 32) { std::strncpy(buf, hex+16, 16); buf[16]=0; v[1] = std::strtoull(buf,nullptr,16); }
    }
    ::Vst3FUID r;
    r.data[0] = uint32_t(v[0] >> 32);
    r.data[1] = uint32_t(v[0] & 0xFFFFFFFF);
    r.data[2] = uint32_t(v[1] >> 32);
    r.data[3] = uint32_t(v[1] & 0xFFFFFFFF);
    return r;
}

namespace ks {
namespace audio {

// ─── KSAudioVST3Host ──────────────────────────────────────────────────────

KSAudioVST3Host::KSAudioVST3Host(QObject* parent)
    : QObject(parent)
{
}

KSAudioVST3Host::~KSAudioVST3Host()
{
    unloadPlugin();
}

bool KSAudioVST3Host::loadPlugin(const QString& dllPath)
{
    if (m_loaded) unloadPlugin();

    QFileInfo fi(dllPath);
    if (!fi.exists()) {
        emit error("VST3 file not found: " + dllPath);
        return false;
    }

    m_library.setFileName(dllPath);
    if (!m_library.load()) {
        emit error("Failed to load VST3 DLL: " + m_library.errorString());
        return false;
    }

    // VST3 entry point
    m_getFactory = reinterpret_cast<GetFactoryFunc>(m_library.resolve("GetPluginFactory"));
    if (!m_getFactory) {
        m_library.unload();
        emit error("VST3 plugin missing GetPluginFactory entry point");
        return false;
    }

    m_factory = m_getFactory();
    if (!m_factory) {
        m_library.unload();
        emit error("VST3 GetPluginFactory returned null");
        return false;
    }

    m_info.path = dllPath;

    // Enumerate classes
    int32_t numClasses = m_factory->countClasses();
    bool foundAudioPlugin = false;

    for (int32_t i = 0; i < numClasses; ++i) {
        ClassInfo ci = {};
        if (m_factory->getClassInfo(i, &ci) != kResultOk) continue;

        // Convert TChar name to QString
        QString className;
        for (int c = 0; c < 256 && ci.name[c]; ++c)
            className += QChar(ci.name[c]);

        QString category;
        for (int c = 0; c < 256 && ci.category[c]; ++c)
            category += QChar(ci.category[c]);

        // Look for audio processor class
        if (category.contains("Audio Module") || category.contains("Plug")) {
            if (!foundAudioPlugin) {
                m_info.uid = ci.cid;
                m_info.name = className;
                for (int c = 0; c < 256 && ci.vendor[c]; ++c)
                    m_info.vendor += QChar(ci.vendor[c]);
                for (int c = 0; c < 256 && ci.version[c]; ++c)
                    m_info.version += QChar(ci.version[c]);

                if (ci.subCategories & (1 << 2))
                    m_info.isSynth = true;

                foundAudioPlugin = true;
            }
        }
    }

    if (!foundAudioPlugin) {
        m_library.unload();
        emit error("No audio plugin class found in VST3");
        return false;
    }

    if (!initPlugin()) {
        cleanupPlugin();
        return false;
    }

    m_loaded = true;
    emit pluginLoaded(m_info.name);
    return true;
}

void KSAudioVST3Host::unloadPlugin()
{
    if (m_processing) stopProcessing();
    cleanupPlugin();
    if (m_library.isLoaded())
        m_library.unload();
    m_loaded = false;
    emit pluginUnloaded();
}

bool KSAudioVST3Host::initPlugin()
{
    // Create component
    Vst3Result res = m_factory->createInstance(m_info.uid, VST3FUIDs::IComponent, reinterpret_cast<void**>(&m_component));
    if (res != kResultOk || !m_component) {
        emit error("Failed to create VST3 component instance");
        return false;
    }

    // Initialize component with host context
    if (m_component->initialize(&m_hostContext) != kResultOk) {
        emit error("VST3 component initialization failed");
        return false;
    }

    // Query audio processor
    if (m_component->queryInterface(VST3FUIDs::IAudioProcessor, reinterpret_cast<void**>(&m_processor)) != kResultOk) {
        emit error("VST3 component does not support IAudioProcessor");
        return false;
    }

    // Query edit controller
    m_component->queryInterface(VST3FUIDs::IEditController, reinterpret_cast<void**>(&m_controller));
    if (!m_controller) {
        // Try controller class ID from component
        Vst3FUID cid;
        if (m_component->getControllerClassId(cid) == kResultOk) {
            m_factory->createInstance(cid, VST3FUIDs::IEditController, reinterpret_cast<void**>(&m_controller));
            if (m_controller) {
                m_controller->initialize(&m_hostContext);
            }
        }
    }

    // Count audio busses
    m_audioInputs = m_component->getBusCount(kAudio, kInput);
    m_audioOutputs = m_component->getBusCount(kAudio, kOutput);

    if (m_audioInputs == 0 && m_audioOutputs == 0) {
        emit error("VST3 plugin has no audio busses");
        return false;
    }

    BusInfo info;
    for (int32_t i = 0; i < m_audioInputs; ++i) {
        m_component->getBusInfo(kAudio, kInput, i, info);
        m_component->activateBus(kAudio, kInput, i, true);
    }
    for (int32_t i = 0; i < m_audioOutputs; ++i) {
        m_component->getBusInfo(kAudio, kOutput, i, info);
        m_component->activateBus(kAudio, kOutput, i, true);
    }

    m_info.numInputs = m_audioInputs > 0 ? info.numChannels : 0;
    m_info.numOutputs = m_audioOutputs > 0 ? info.numChannels : 0;

    // Ensure at least stereo
    if (m_info.numInputs < 2 && m_audioInputs > 0) m_info.numInputs = 2;
    if (m_info.numOutputs < 2 && m_audioOutputs > 0) m_info.numOutputs = 2;

    // Set active
    m_component->setActive(true);

    // Count parameters from controller
    if (m_controller) {
        m_info.numParams = m_controller->getParamCount();
    }

    allocProcessBuffers();
    return true;
}

void KSAudioVST3Host::cleanupPlugin()
{
    freeProcessBuffers();
    if (m_component) {
        m_component->setActive(false);
        m_component->terminate();
    }
    if (m_controller) {
        m_controller->terminate();
    }
    m_component = nullptr;
    m_processor = nullptr;
    m_controller = nullptr;
    m_factory = nullptr;
    m_getFactory = nullptr;
}

void KSAudioVST3Host::allocProcessBuffers()
{
    freeProcessBuffers();
    int32_t numInChannels = m_info.numInputs;
    int32_t numOutChannels = m_info.numOutputs;

    if (numInChannels > 0) {
        m_inBufs = new float*[numInChannels];
        for (int i = 0; i < numInChannels; ++i)
            m_inBufs[i] = new float[m_blockSize]();
    }
    if (numOutChannels > 0) {
        m_outBufs = new float*[numOutChannels];
        for (int i = 0; i < numOutChannels; ++i)
            m_outBufs[i] = new float[m_blockSize]();
    }

    if (m_audioInputs > 0) {
        m_inBusBufs = new AudioBusBuffers[m_audioInputs];
        for (int32_t i = 0; i < m_audioInputs; ++i) {
            m_inBusBufs[i].numChannels = numInChannels;
            m_inBusBufs[i].channelBuffers = m_inBufs;
        }
    }
    if (m_audioOutputs > 0) {
        m_outBusBufs = new AudioBusBuffers[m_audioOutputs];
        for (int32_t i = 0; i < m_audioOutputs; ++i) {
            m_outBusBufs[i].numChannels = numOutChannels;
            m_outBusBufs[i].channelBuffers = m_outBufs;
        }
    }

    m_processData.numInputs = m_audioInputs;
    m_processData.numOutputs = m_audioOutputs;
    m_processData.inputs = m_inBusBufs;
    m_processData.outputs = m_outBusBufs;
}

void KSAudioVST3Host::freeProcessBuffers()
{
    delete[] m_inBusBufs; m_inBusBufs = nullptr;
    delete[] m_outBusBufs; m_outBusBufs = nullptr;

    if (m_inBufs) {
        for (int i = 0; i < m_info.numInputs; ++i) delete[] m_inBufs[i];
        delete[] m_inBufs; m_inBufs = nullptr;
    }
    if (m_outBufs) {
        for (int i = 0; i < m_info.numOutputs; ++i) delete[] m_outBufs[i];
        delete[] m_outBufs; m_outBufs = nullptr;
    }
}

void KSAudioVST3Host::setSampleRate(int32_t sampleRate)
{
    m_sampleRate = sampleRate;
}

void KSAudioVST3Host::setBlockSize(int32_t blockSize)
{
    m_blockSize = blockSize;
}

void KSAudioVST3Host::startProcessing()
{
    if (!m_processor) return;
    ProcessSetup setup;
    setup.maxSamplesPerBlock = m_blockSize;
    setup.sampleRate = m_sampleRate;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;

    m_processor->setupProcessing(setup);
    m_processor->setProcessing(true);
    m_processing = true;
}

void KSAudioVST3Host::stopProcessing()
{
    if (m_processor && m_processing)
        m_processor->setProcessing(false);
    m_processing = false;
}

void KSAudioVST3Host::process(const float* const* inputs, float** outputs, int32_t sampleFrames)
{
    if (!m_processor || !m_processing) return;
    if (sampleFrames > m_blockSize)
        sampleFrames = m_blockSize;

    // Copy input into our buffers
    int32_t numInCh = m_info.numInputs;
    int32_t numOutCh = m_info.numOutputs;

    for (int32_t ch = 0; ch < numInCh && ch < m_audioInputs; ++ch) {
        if (inputs[ch]) {
            std::memcpy(m_inBufs[ch], inputs[ch], sampleFrames * sizeof(float));
        } else {
            std::memset(m_inBufs[ch], 0, sampleFrames * sizeof(float));
        }
    }

    // Zero output buffers
    for (int32_t ch = 0; ch < numOutCh; ++ch) {
        std::memset(m_outBufs[ch], 0, sampleFrames * sizeof(float));
    }

    m_processData.numSamples = sampleFrames;
    m_processData.processContext = nullptr;

    m_processor->process(m_processData);

    // Copy output
    for (int32_t ch = 0; ch < numOutCh; ++ch) {
        if (outputs[ch]) {
            std::memcpy(outputs[ch], m_outBufs[ch], sampleFrames * sizeof(float));
        }
    }
}

float KSAudioVST3Host::getParameter(int32_t tag)
{
    if (!m_controller) return 0.0f;
    ParameterInfo info;
    for (int32_t i = 0; i < m_controller->getParamCount(); ++i) {
        if (m_controller->getParamInfo(i, info) == kResultOk) {
            if (info.unitId == tag || i == tag) {
                // Return the normalized value (we'd need to track it)
                float val = 0.0f;
                // For simplicity, we track in the controller
                return info.defaultValue;
            }
        }
    }
    return 0.0f;
}

void KSAudioVST3Host::setParameter(int32_t tag, float value)
{
    if (!m_controller) return;
    m_controller->setParamNormalized(tag, value);
}

QString KSAudioVST3Host::getParameterName(int32_t tag)
{
    if (!m_controller) return {};
    ParameterInfo info;
    for (int32_t i = 0; i < m_controller->getParamCount(); ++i) {
        if (m_controller->getParamInfo(i, info) == kResultOk) {
            if (info.unitId == tag || i == tag) {
                QString name;
                for (int c = 0; c < 128 && info.title[c]; ++c)
                    name += QChar(info.title[c]);
                return name;
            }
        }
    }
    return {};
}

QString KSAudioVST3Host::getParameterString(int32_t tag, float value)
{
    if (!m_controller) return {};
    TChar buf[256] = {};
    if (m_controller->getParamStringByValue(tag, value, buf, 256) == kResultOk) {
        QString str;
        for (int c = 0; c < 256 && buf[c]; ++c)
            str += QChar(buf[c]);
        return str;
    }
    return {};
}

// ─── KSAudioVST3Manager ────────────────────────────────────────────────────

KSAudioVST3Manager::KSAudioVST3Manager(QObject* parent)
    : QObject(parent)
{
}

KSAudioVST3Manager::~KSAudioVST3Manager()
{
    unloadAllPlugins();
}

int KSAudioVST3Manager::loadPlugin(const QString& dllPath)
{
    auto* host = new KSAudioVST3Host(this);
    if (!host->loadPlugin(dllPath)) {
        delete host;
        emit error("Failed to load VST3: " + dllPath);
        return -1;
    }

    int index = m_plugins.size();
    m_plugins.append(host);

    connect(host, &KSAudioVST3Host::pluginLoaded, this, [this, index](const QString& name) {
        emit pluginLoaded(index, name);
    });
    connect(host, &KSAudioVST3Host::error, this, &KSAudioVST3Manager::error);

    emit pluginLoaded(index, host->pluginName());
    return index;
}

bool KSAudioVST3Manager::unloadPlugin(int index)
{
    if (index < 0 || index >= m_plugins.size()) return false;
    KSAudioVST3Host* host = m_plugins[index];
    m_plugins.removeAt(index);
    host->deleteLater();
    emit pluginUnloaded(index);
    return true;
}

void KSAudioVST3Manager::unloadAllPlugins()
{
    for (int i = m_plugins.size() - 1; i >= 0; --i) {
        m_plugins[i]->deleteLater();
    }
    m_plugins.clear();
}

KSAudioVST3Host* KSAudioVST3Manager::getPlugin(int index)
{
    if (index < 0 || index >= m_plugins.size()) return nullptr;
    return m_plugins[index];
}

const KSAudioVST3Host* KSAudioVST3Manager::getPlugin(int index) const
{
    return const_cast<KSAudioVST3Manager*>(this)->getPlugin(index);
}

QStringList KSAudioVST3Manager::getPluginNames() const
{
    QStringList names;
    for (const auto* p : m_plugins)
        names.append(p->pluginName());
    return names;
}

QStringList KSAudioVST3Manager::scanDirectory(const QString& directory)
{
    QStringList found;
    QDirIterator it(directory, QDir::Files);
    while (it.hasNext()) {
        QString path = it.next();
        if (path.endsWith(".vst3", Qt::CaseInsensitive)) {
            found.append(path);
        }
    }
    return found;
}

void KSAudioVST3Manager::setSampleRate(int32_t sampleRate)
{
    for (auto* p : m_plugins)
        p->setSampleRate(sampleRate);
}

void KSAudioVST3Manager::setBlockSize(int32_t blockSize)
{
    for (auto* p : m_plugins)
        p->setBlockSize(blockSize);
}

} // namespace audio
} // namespace ks
