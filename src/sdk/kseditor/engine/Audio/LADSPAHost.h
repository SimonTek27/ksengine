#pragma once
// ============================================================================
// LADSPAHost.h
// LADSPA (Linux Audio Developer's Simple Plugin API) plugin host.
// Loads .so/.dll LADSPA plugins dynamically and processes audio through them.
// Works alongside the existing KSAudioVSTHost to add access to hundreds
// of free Linux/Mac plugins (noise reduction, EQ, compressors, reverbs…).
// ============================================================================

#include <QObject>
#include <QVector>
#include <QString>
#include <QLibrary>
#include <QStringList>
#include <functional>

// ============================================================================
// LADSPA ABI types (from ladspa.h, replicated to avoid build dependency)
// ============================================================================
typedef float         LADSPA_Data;
typedef unsigned long LADSPA_Properties;
typedef unsigned long LADSPA_PortDescriptor;
typedef unsigned long LADSPA_PortRangeHintDescriptor;

struct LADSPA_PortRangeHint {
    LADSPA_PortRangeHintDescriptor HintDescriptor;
    LADSPA_Data LowerBound;
    LADSPA_Data UpperBound;
};

typedef void* LADSPA_Handle;

struct LADSPA_Descriptor {
    unsigned long  UniqueID;
    const char*    Label;
    LADSPA_Properties Properties;
    const char*    Name;
    const char*    Maker;
    const char*    Copyright;
    unsigned long  PortCount;
    const LADSPA_PortDescriptor*     PortDescriptors;
    const char* const*               PortNames;
    const LADSPA_PortRangeHint*      PortRangeHints;
    void*          ImplementationData;

    LADSPA_Handle (*instantiate)(const LADSPA_Descriptor*, unsigned long SampleRate);
    void (*connect_port)(LADSPA_Handle, unsigned long Port, LADSPA_Data* DataLocation);
    void (*activate)(LADSPA_Handle);
    void (*run)(LADSPA_Handle, unsigned long SampleCount);
    void (*run_adding)(LADSPA_Handle, unsigned long SampleCount);
    void (*set_run_adding_gain)(LADSPA_Handle, LADSPA_Data Gain);
    void (*deactivate)(LADSPA_Handle);
    void (*cleanup)(LADSPA_Handle);
};

typedef const LADSPA_Descriptor* (*LADSPA_Descriptor_Function)(unsigned long Index);

// ============================================================================
namespace ks {
namespace audio {

// Port info exposed to the outside world
struct LADSPAPort {
    int     index       = 0;
    QString name;
    bool    isInput     = false;
    bool    isOutput    = false;
    bool    isAudio     = false;
    bool    isControl   = false;
    float   defaultValue = 0.f;
    float   minValue    = 0.f;
    float   maxValue    = 1.f;
    float   currentValue = 0.f;
};

// ============================================================================
class LADSPAHost : public QObject
{
    Q_OBJECT
public:
    explicit LADSPAHost(QObject* parent = nullptr);
    ~LADSPAHost() override;

    // ---- Load/unload -------------------------------------------------------
    bool load(const QString& path, unsigned long pluginIndex = 0);
    void unload();
    bool isLoaded() const { return m_handle != nullptr; }

    // ---- Plugin info -------------------------------------------------------
    unsigned long uniqueID()    const { return m_uniqueID; }
    QString       name()        const { return m_name; }
    QString       label()       const { return m_label; }
    QString       maker()       const { return m_maker; }
    int           portCount()   const { return m_ports.size(); }
    const QVector<LADSPAPort>& ports() const { return m_ports; }

    // ---- Parameters (control ports) ----------------------------------------
    void  setControlValue(int portIndex, float value);
    float controlValue(int portIndex) const;

    // ---- Processing --------------------------------------------------------
    void   setSampleRate(int sr);
    int    sampleRate() const  { return m_sampleRate; }
    void   setBlockSize(int bs);

    /// Process mono audio through the plugin.
    QVector<float> process(const QVector<float>& input);

    /// Process in place
    void processInPlace(QVector<float>& samples);

    // ---- Static utilities --------------------------------------------------
    /// Scan a directory for LADSPA plugins and return their paths.
    static QStringList scan(const QString& directory);

    /// Return a list of all plugins inside a single .so/.dll (may be > 1).
    static QStringList pluginsInFile(const QString& path);

signals:
    void pluginLoaded(const QString& name);
    void pluginUnloaded();
    void error(const QString& msg);

private:
    bool   initPlugin(unsigned long sampleRate);
    void   cleanup();
    void   buildPortInfo();
    float  defaultForPort(int portIndex) const;

    QLibrary                    m_library;
    const LADSPA_Descriptor*    m_descriptor = nullptr;
    LADSPA_Handle               m_handle     = nullptr;

    int           m_sampleRate  = 44100;
    int           m_blockSize   = 512;
    unsigned long m_uniqueID    = 0;
    QString       m_name;
    QString       m_label;
    QString       m_maker;

    QVector<LADSPAPort>   m_ports;
    QVector<float>        m_controlBuf;  // one float per port (control ports wired here)
    QVector<float>        m_audioBufIn;
    QVector<float>        m_audioBufOut;

    int m_audioInPort  = -1;
    int m_audioOutPort = -1;
};

// ============================================================================
// LADSPAManager – manages multiple LADSPA plugins in a chain
// ============================================================================
class LADSPAManager : public QObject
{
    Q_OBJECT
public:
    explicit LADSPAManager(QObject* parent = nullptr);
    ~LADSPAManager() override;

    int     addPlugin(const QString& path, unsigned long pluginIndex = 0);
    bool    removePlugin(int index);
    void    removeAll();

    int         pluginCount() const     { return m_chain.size(); }
    LADSPAHost* pluginAt(int index)     { return m_chain.value(index); }

    void setSampleRate(int sr);
    void setBlockSize(int bs);

    /// Process audio through the entire chain sequentially.
    QVector<float> processChain(const QVector<float>& input);

    QStringList pluginNames() const;
    static QStringList scan(const QString& directory) { return LADSPAHost::scan(directory); }

signals:
    void chainChanged();
    void error(const QString& msg);

private:
    QVector<LADSPAHost*> m_chain;
    int                  m_sampleRate = 44100;
    int                  m_blockSize  = 512;
};

} // namespace audio
} // namespace ks
