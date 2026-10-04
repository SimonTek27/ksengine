#include "LADSPAHost.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <cstring>
#include <cmath>

// LADSPA port descriptor bits (from ladspa.h)
#define LADSPA_PORT_INPUT    0x1
#define LADSPA_PORT_OUTPUT   0x2
#define LADSPA_PORT_CONTROL  0x4
#define LADSPA_PORT_AUDIO    0x8
#define LADSPA_IS_PORT_INPUT(x)   ((x) & LADSPA_PORT_INPUT)
#define LADSPA_IS_PORT_OUTPUT(x)  ((x) & LADSPA_PORT_OUTPUT)
#define LADSPA_IS_PORT_CONTROL(x) ((x) & LADSPA_PORT_CONTROL)
#define LADSPA_IS_PORT_AUDIO(x)   ((x) & LADSPA_PORT_AUDIO)

#define LADSPA_HINT_DEFAULT_MASK      0x3C0
#define LADSPA_HINT_DEFAULT_NONE      0x000
#define LADSPA_HINT_DEFAULT_MINIMUM   0x040
#define LADSPA_HINT_DEFAULT_LOW       0x080
#define LADSPA_HINT_DEFAULT_MIDDLE    0x0C0
#define LADSPA_HINT_DEFAULT_HIGH      0x100
#define LADSPA_HINT_DEFAULT_MAXIMUM   0x140
#define LADSPA_HINT_DEFAULT_0         0x200
#define LADSPA_HINT_DEFAULT_1         0x240
#define LADSPA_HINT_DEFAULT_100       0x280
#define LADSPA_HINT_DEFAULT_440       0x2C0
#define LADSPA_HINT_LOGARITHMIC       0x010
#define LADSPA_HINT_BOUNDED_ABOVE     0x002
#define LADSPA_HINT_BOUNDED_BELOW     0x001

namespace ks { namespace audio {

// ============================================================================
LADSPAHost::LADSPAHost(QObject* parent) : QObject(parent) {}

LADSPAHost::~LADSPAHost()
{
    cleanup();
}

// ============================================================================
bool LADSPAHost::load(const QString& path, unsigned long pluginIndex)
{
    cleanup();
    m_library.setFileName(path);
    if (!m_library.load()) {
        emit error(QString("Cannot load LADSPA library: %1 (%2)")
                   .arg(path, m_library.errorString()));
        return false;
    }

    auto descFn = reinterpret_cast<LADSPA_Descriptor_Function>(
        m_library.resolve("ladspa_descriptor"));
    if (!descFn) {
        emit error("Not a LADSPA plugin (no ladspa_descriptor symbol)");
        m_library.unload();
        return false;
    }

    m_descriptor = descFn(pluginIndex);
    if (!m_descriptor) {
        emit error(QString("Plugin index %1 not found in %2").arg(pluginIndex).arg(path));
        m_library.unload();
        return false;
    }

    m_uniqueID = m_descriptor->UniqueID;
    m_name     = QString::fromLatin1(m_descriptor->Name);
    m_label    = QString::fromLatin1(m_descriptor->Label);
    m_maker    = QString::fromLatin1(m_descriptor->Maker);

    buildPortInfo();

    if (!initPlugin(unsigned(m_sampleRate))) {
        cleanup();
        return false;
    }

    emit pluginLoaded(m_name);
    return true;
}

void LADSPAHost::unload()
{
    cleanup();
    emit pluginUnloaded();
}

// ============================================================================
void LADSPAHost::buildPortInfo()
{
    m_ports.clear();
    m_audioInPort  = -1;
    m_audioOutPort = -1;

    for (unsigned long i = 0; i < m_descriptor->PortCount; ++i) {
        LADSPAPort p;
        p.index    = int(i);
        p.name     = QString::fromLatin1(m_descriptor->PortNames[i]);
        auto desc  = m_descriptor->PortDescriptors[i];
        p.isInput  = LADSPA_IS_PORT_INPUT(desc);
        p.isOutput = LADSPA_IS_PORT_OUTPUT(desc);
        p.isAudio  = LADSPA_IS_PORT_AUDIO(desc);
        p.isControl= LADSPA_IS_PORT_CONTROL(desc);

        const auto& hint = m_descriptor->PortRangeHints[i];
        p.minValue  = (hint.HintDescriptor & LADSPA_HINT_BOUNDED_BELOW) ? hint.LowerBound : 0.f;
        p.maxValue  = (hint.HintDescriptor & LADSPA_HINT_BOUNDED_ABOVE) ? hint.UpperBound : 1.f;
        p.defaultValue = defaultForPort(int(i));
        p.currentValue = p.defaultValue;

        m_ports.append(p);

        // Remember first audio in/out ports
        if (p.isAudio && p.isInput  && m_audioInPort  < 0) m_audioInPort  = int(i);
        if (p.isAudio && p.isOutput && m_audioOutPort < 0) m_audioOutPort = int(i);
    }

    m_controlBuf.resize(m_ports.size(), 0.f);
    for (int i = 0; i < m_ports.size(); ++i)
        m_controlBuf[i] = m_ports[i].defaultValue;
}

float LADSPAHost::defaultForPort(int pi) const
{
    const auto& h = m_descriptor->PortRangeHints[pi];
    auto hint = h.HintDescriptor & LADSPA_HINT_DEFAULT_MASK;
    bool log  = (h.HintDescriptor & LADSPA_HINT_LOGARITHMIC);
    switch (hint) {
    case LADSPA_HINT_DEFAULT_MINIMUM: return h.LowerBound;
    case LADSPA_HINT_DEFAULT_MAXIMUM: return h.UpperBound;
    case LADSPA_HINT_DEFAULT_LOW:
        return log ? std::exp(std::log(h.LowerBound)*0.75f + std::log(h.UpperBound)*0.25f)
                   : h.LowerBound * 0.75f + h.UpperBound * 0.25f;
    case LADSPA_HINT_DEFAULT_MIDDLE:
        return log ? std::sqrt(h.LowerBound * h.UpperBound)
                   : (h.LowerBound + h.UpperBound) * 0.5f;
    case LADSPA_HINT_DEFAULT_HIGH:
        return log ? std::exp(std::log(h.LowerBound)*0.25f + std::log(h.UpperBound)*0.75f)
                   : h.LowerBound * 0.25f + h.UpperBound * 0.75f;
    case LADSPA_HINT_DEFAULT_0:   return 0.f;
    case LADSPA_HINT_DEFAULT_1:   return 1.f;
    case LADSPA_HINT_DEFAULT_100: return 100.f;
    case LADSPA_HINT_DEFAULT_440: return 440.f;
    default: return 0.f;
    }
}

// ============================================================================
bool LADSPAHost::initPlugin(unsigned long sampleRate)
{
    if (!m_descriptor) return false;
    m_handle = m_descriptor->instantiate(m_descriptor, sampleRate);
    if (!m_handle) {
        emit error("LADSPA instantiate() failed");
        return false;
    }

    // Connect control ports to our buffer
    for (int i = 0; i < m_ports.size(); ++i) {
        if (m_ports[i].isControl)
            m_descriptor->connect_port(m_handle, unsigned(i), &m_controlBuf[i]);
    }

    if (m_descriptor->activate)
        m_descriptor->activate(m_handle);

    return true;
}

void LADSPAHost::cleanup()
{
    if (m_handle && m_descriptor) {
        if (m_descriptor->deactivate) m_descriptor->deactivate(m_handle);
        if (m_descriptor->cleanup)    m_descriptor->cleanup(m_handle);
        m_handle     = nullptr;
        m_descriptor = nullptr;
    }
    if (m_library.isLoaded()) m_library.unload();
    m_ports.clear();
    m_controlBuf.clear();
}

// ============================================================================
void LADSPAHost::setSampleRate(int sr)
{
    if (m_sampleRate == sr) return;
    m_sampleRate = sr;
    if (m_handle) {
        // Re-instantiate (LADSPA doesn't support changing SR on the fly)
        QString path = m_library.fileName();
        unsigned long uid = m_uniqueID;
        cleanup();
        load(path, uid);
    }
}

void LADSPAHost::setBlockSize(int bs)
{
    m_blockSize = bs;
    m_audioBufIn.resize(bs, 0.f);
    m_audioBufOut.resize(bs, 0.f);
}

void LADSPAHost::setControlValue(int portIndex, float value)
{
    if (portIndex < 0 || portIndex >= m_ports.size()) return;
    m_ports[portIndex].currentValue = value;
    m_controlBuf[portIndex] = value;
}

float LADSPAHost::controlValue(int portIndex) const
{
    if (portIndex < 0 || portIndex >= m_controlBuf.size()) return 0.f;
    return m_controlBuf[portIndex];
}

// ============================================================================
QVector<float> LADSPAHost::process(const QVector<float>& input)
{
    QVector<float> out = input;
    processInPlace(out);
    return out;
}

void LADSPAHost::processInPlace(QVector<float>& samples)
{
    if (!m_handle || !m_descriptor || m_audioInPort < 0 || m_audioOutPort < 0) return;

    const int total = samples.size();
    m_audioBufOut.resize(total, 0.f);

    // Connect audio ports and run in blocks
    m_descriptor->connect_port(m_handle, unsigned(m_audioInPort),  samples.data());
    m_descriptor->connect_port(m_handle, unsigned(m_audioOutPort), m_audioBufOut.data());

    if (m_descriptor->run)
        m_descriptor->run(m_handle, unsigned(total));

    samples = m_audioBufOut;
}

// ============================================================================
QStringList LADSPAHost::scan(const QString& directory)
{
    QStringList result;
    QDir dir(directory);
    const auto entries = dir.entryInfoList(
        QStringList() << "*.so" << "*.dll", QDir::Files);
    for (const QFileInfo& fi : entries) {
        QLibrary lib(fi.absoluteFilePath());
        if (lib.load()) {
            if (lib.resolve("ladspa_descriptor"))
                result.append(fi.absoluteFilePath());
            lib.unload();
        }
    }
    return result;
}

QStringList LADSPAHost::pluginsInFile(const QString& path)
{
    QStringList names;
    QLibrary lib(path);
    if (!lib.load()) return names;
    auto descFn = reinterpret_cast<LADSPA_Descriptor_Function>(
        lib.resolve("ladspa_descriptor"));
    if (descFn) {
        for (unsigned long i = 0; ; ++i) {
            const LADSPA_Descriptor* d = descFn(i);
            if (!d) break;
            names.append(QString("[%1] %2").arg(d->UniqueID).arg(d->Name));
        }
    }
    lib.unload();
    return names;
}

// ============================================================================
// LADSPAManager
// ============================================================================
LADSPAManager::LADSPAManager(QObject* parent) : QObject(parent) {}

LADSPAManager::~LADSPAManager()
{
    qDeleteAll(m_chain);
}

int LADSPAManager::addPlugin(const QString& path, unsigned long pluginIndex)
{
    auto* host = new LADSPAHost(this);
    host->setSampleRate(m_sampleRate);
    host->setBlockSize(m_blockSize);
    connect(host, &LADSPAHost::error, this, &LADSPAManager::error);
    if (!host->load(path, pluginIndex)) { delete host; return -1; }
    m_chain.append(host);
    emit chainChanged();
    return m_chain.size() - 1;
}

bool LADSPAManager::removePlugin(int index)
{
    if (index < 0 || index >= m_chain.size()) return false;
    delete m_chain.takeAt(index);
    emit chainChanged();
    return true;
}

void LADSPAManager::removeAll()
{
    qDeleteAll(m_chain);
    m_chain.clear();
    emit chainChanged();
}

void LADSPAManager::setSampleRate(int sr)
{
    m_sampleRate = sr;
    for (auto* h : m_chain) h->setSampleRate(sr);
}

void LADSPAManager::setBlockSize(int bs)
{
    m_blockSize = bs;
    for (auto* h : m_chain) h->setBlockSize(bs);
}

QVector<float> LADSPAManager::processChain(const QVector<float>& input)
{
    QVector<float> buf = input;
    for (auto* h : m_chain)
        if (h->isLoaded()) h->processInPlace(buf);
    return buf;
}

QStringList LADSPAManager::pluginNames() const
{
    QStringList names;
    for (const auto* h : m_chain)
        names.append(h->name());
    return names;
}

}} // namespace ks::audio
