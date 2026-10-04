#include "AudioVSTHost.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace ks {
namespace audio {

audioMasterCallback KSAudioVSTHost::s_masterCallback = nullptr;

// ============================================================================
// VST Master Callback
// ============================================================================

long __stdcall KSAudioVSTHost::masterCallback(AEffect* effect, long opcode, long index, long value, void* ptr, float opt) {
    switch (opcode) {
        case 0: // audioMasterAutomate
            return 0;
        case 1: // audioMasterVersion
            return 2400; // VST 2.4
        case 2: // audioMasterCurrentId
            return effect ? effect->uniqueID : 0;
        case 3: // audioMasterIdle
            return 0;
        case 4: // audioMasterPinConnected
            return 0;
        case 5: // audioMasterWantMidi
            return 0; // No MIDI support yet
        case 6: // audioMasterGetTime
            return 0;
        case 7: // audioMasterProcessEvents
            return 0;
        case 8: // audioMasterSetTime
            return 0;
        case 9: // audioMasterTempoAt
            return 0;
        case 10: // audioMasterGetNumAutomatableParameters
            return effect ? effect->numParams : 0;
        case 11: // audioMasterGetParameterQuantization
            return effect ? effect->numParams : 0;
        case 12: // audioMasterIOChanged
            return 0;
        case 13: // audioMasterNeedIdle
            return 0;
        case 14: // audioMasterSizeWindow
            return 0;
        case 15: // audioMasterGetSampleRate
            return 44100;
        case 16: // audioMasterGetBlockSize
            return 512;
        case 17: // audioMasterGetInputLatency
            return 0;
        case 18: // audioMasterGetOutputLatency
            return 0;
        case 30: // audioMasterGetVendorString
            if (ptr) strncpy((char*)ptr, "KSAudio", 63); ((char*)ptr)[63] = '\0';
            return 1;
        case 31: // audioMasterGetProductString
            if (ptr) strncpy((char*)ptr, "KSAudio VST Host", 63); ((char*)ptr)[63] = '\0';
            return 1;
        case 32: // audioMasterGetVendorVersion
            return 100;
        case 35: // audioMasterCanDo
            if (ptr) {
                QString cando((char*)ptr);
                if (cando == "supplyIdle" || cando == "supplySetBlockSize" || cando == "supplySampleRate") {
                    return 1;
                }
            }
            return 0;
        default:
            return 0;
    }
}

// ============================================================================
// KSAudioVSTHost Implementation
// ============================================================================

KSAudioVSTHost::KSAudioVSTHost(QObject* parent)
    : QObject(parent), m_effect(nullptr), m_loaded(false), m_processing(false)
{
    s_masterCallback = masterCallback;
}

KSAudioVSTHost::~KSAudioVSTHost() {
    unloadPlugin();
}

bool KSAudioVSTHost::loadPlugin(const QString& dllPath) {
    if (m_loaded) unloadPlugin();

    QFileInfo info(dllPath);
    if (!info.exists()) {
        emit error("Plugin not found: " + dllPath);
        return false;
    }

    m_library.setFileName(dllPath);
    if (!m_library.load()) {
        emit error("Failed to load plugin: " + m_library.errorString());
        return false;
    }

    // Get VST entry point
    m_entryPoint = (vstMainFunc)m_library.resolve("VSTPluginMain");
    if (!m_entryPoint) {
        m_entryPoint = (vstMainFunc)m_library.resolve("main");
    }
    if (!m_entryPoint) {
        emit error("Not a valid VST plugin (no VSTPluginMain or main)");
        m_library.unload();
        return false;
    }

    // Create VST instance
    m_effect = m_entryPoint(s_masterCallback);
    if (!m_effect) {
        emit error("Failed to create VST instance");
        m_library.unload();
        return false;
    }

    // Initialize plugin
    if (!initPlugin()) {
        emit error("Failed to initialize VST plugin");
        unloadPlugin();
        return false;
    }

    m_info.path = dllPath;
    m_info.name = info.baseName();
    m_loaded = true;

    emit pluginLoaded(m_info.name);
    qInfo() << "KSAudioVSTHost: Loaded plugin" << m_info.name;
    return true;
}

void KSAudioVSTHost::unloadPlugin() {
    if (!m_loaded) return;

    if (m_processing) stopProcessing();

    cleanupPlugin();

    if (m_effect && m_effect->dispatcher) {
        m_effect->dispatcher(m_effect, effClose, 0, 0, nullptr, 0.0f);
    }

    m_library.unload();
    m_effect = nullptr;
    m_loaded = false;

    emit pluginUnloaded();
    qInfo() << "KSAudioVSTHost: Unloaded plugin";
}

bool KSAudioVSTHost::initPlugin() {
    if (!m_effect || !m_effect->dispatcher) return false;

    // Open plugin
    m_effect->dispatcher(m_effect, effOpen, 0, 0, nullptr, 0.0f);

    // Get plugin info
    m_info.numPrograms = m_effect->numPrograms;
    m_info.numParams = m_effect->numParams;
    m_info.numInputs = m_effect->numInputs;
    m_info.numOutputs = m_effect->numOutputs;

    return true;
}

void KSAudioVSTHost::cleanupPlugin() {
    // Cleanup handled in unloadPlugin
}

float KSAudioVSTHost::getParameter(long index) {
    if (!m_effect || !m_effect->getParameter || index < 0 || index >= m_effect->numParams) {
        return 0.0f;
    }
    return m_effect->getParameter(m_effect, index);
}

void KSAudioVSTHost::setParameter(long index, float value) {
    if (!m_effect || !m_effect->setParameter || index < 0 || index >= m_effect->numParams) {
        return;
    }
    m_effect->setParameter(m_effect, index, qBound(0.0f, value, 1.0f));
}

QString KSAudioVSTHost::getParameterName(long index) {
    if (!m_effect || index < 0 || index >= m_effect->numParams) return QString();

    char label[64] = {0};
    m_effect->dispatcher(m_effect, effGetParamName, index, 0, label, 0.0f);
    return QString::fromLatin1(label);
}

QString KSAudioVSTHost::getParameterDisplay(long index) {
    if (!m_effect || index < 0 || index >= m_effect->numParams) return QString();

    char display[64] = {0};
    m_effect->dispatcher(m_effect, effGetParamDisplay, index, 0, display, 0.0f);
    return QString::fromLatin1(display);
}

QString KSAudioVSTHost::getParameterLabel(long index) {
    if (!m_effect || index < 0 || index >= m_effect->numParams) return QString();

    char label[64] = {0};
    m_effect->dispatcher(m_effect, effGetParamLabel, index, 0, label, 0.0f);
    return QString::fromLatin1(label);
}

bool KSAudioVSTHost::canDo(const QString& what) {
    if (!m_effect) return false;
    return m_effect->dispatcher(m_effect, effCanDo, 0, 0, (void*)what.toLatin1().constData(), 0.0f) > 0;

}

void KSAudioVSTHost::setSampleRate(float sampleRate) {
    if (!m_effect) return;
    m_effect->dispatcher(m_effect, effSetSampleRate, 0, 0, nullptr, sampleRate);
}

void KSAudioVSTHost::setBlockSize(long blockSize) {
    if (!m_effect) return;
    m_effect->dispatcher(m_effect, effSetBlockSize, 0, blockSize, nullptr, 0.0f);

}

void KSAudioVSTHost::startProcessing() {
    if (!m_effect) return;
    m_effect->dispatcher(m_effect, effStartProcess, 0, 0, nullptr, 0.0f);
    m_processing = true;
}

void KSAudioVSTHost::stopProcessing() {
    if (!m_effect) return;
    m_effect->dispatcher(m_effect, effStopProcess, 0, 0, nullptr, 0.0f);
    m_processing = false;
}

void KSAudioVSTHost::process(float** inputs, float** outputs, long sampleFrames) {
    if (!m_effect || !m_effect->process || !m_processing) return;
    m_effect->process(m_effect, inputs, outputs, sampleFrames);
}

QVector<QString> KSAudioVSTHost::getPluginParameterNames() {
    QVector<QString> names;
    if (!m_effect) return names;

    for (long i = 0; i < m_effect->numParams; ++i) {
        names.append(getParameterName(i));
    }
    return names;
}

// ============================================================================
// KSAudioVSTManager Implementation
// ============================================================================

KSAudioVSTManager::KSAudioVSTManager(QObject* parent) : QObject(parent) {}

KSAudioVSTManager::~KSAudioVSTManager() {
    unloadAllPlugins();
}

int KSAudioVSTManager::loadPlugin(const QString& dllPath) {
    KSAudioVSTHost* host = new KSAudioVSTHost(this);
    if (host->loadPlugin(dllPath)) {
        m_plugins.append(host);
        int idx = m_plugins.size() - 1;
        emit pluginLoaded(idx, host->pluginName());
        return idx;
    }
    delete host;
    return -1;
}

bool KSAudioVSTManager::unloadPlugin(int idx) {
    if (idx < 0 || idx >= m_plugins.size()) return false;
    m_plugins[idx]->unloadPlugin();
    delete m_plugins[idx];
    m_plugins.removeAt(idx);
    emit pluginUnloaded(idx);
    return true;
}

void KSAudioVSTManager::unloadAllPlugins() {
    for (auto* plugin : m_plugins) {
        plugin->unloadPlugin();
        delete plugin;
    }
    m_plugins.clear();
}

KSAudioVSTHost* KSAudioVSTManager::getPlugin(int idx) {
    return (idx >= 0 && idx < m_plugins.size()) ? m_plugins[idx] : nullptr;
}

const KSAudioVSTHost* KSAudioVSTManager::getPlugin(int idx) const {
    return (idx >= 0 && idx < m_plugins.size()) ? m_plugins[idx] : nullptr;
}

QStringList KSAudioVSTManager::getPluginNames() const {
    QStringList names;
    for (auto* plugin : m_plugins) {
        names.append(plugin->pluginName());
    }
    return names;
}

QStringList KSAudioVSTManager::scanDirectory(const QString& directory) {
    QStringList found;
    QDir dir(directory);
    if (!dir.exists()) return found;

    QStringList filters;
    filters << "*.dll" << "*.vst" << "*.vst3";
    dir.setNameFilters(filters);

    QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const auto& file : files) {
        found.append(file.absoluteFilePath());
    }

    return found;
}

void KSAudioVSTManager::setSampleRate(float sampleRate) {
    for (auto* plugin : m_plugins) {
        plugin->setSampleRate(sampleRate);
    }
}

void KSAudioVSTManager::setBlockSize(long blockSize) {
    for (auto* plugin : m_plugins) {
        plugin->setBlockSize(blockSize);
    }
}

} // namespace audio
} // namespace ks
