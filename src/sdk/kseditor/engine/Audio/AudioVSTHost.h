#ifndef KSAUDIOVSTHOST_H
#define KSAUDIOVSTHOST_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QLibrary>
#include <QMutex>

// VST calling convention
#ifdef Q_OS_WIN
    #define VSTCALLBACK __stdcall
#else
    #define VSTCALLBACK
#endif

namespace ks {
namespace audio {

// VST 2.x opcodes
enum VSTOpCode {
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effSetProgramName = 4,
    effGetProgramName = 5,
    effGetParamLabel = 7,
    effGetParamDisplay = 8,
    effGetParamName = 9,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effGetChunk = 23,
    effSetChunk = 24,
    effProcessEvents = 25,
    effCanDo = 51,
    effGetEffectName = 57,
    effGetVendorString = 55,
    effGetProductString = 56,
    effGetVendorVersion = 58,
    effGetParamProperties = 28,
    effGetPlugCategory = 54,
    effStartProcess = 82,
    effStopProcess = 83
};

// VST 2.x AEffect structure (simplified)
struct AEffect {
    long magic;                      // 'VstP'
    long (__stdcall *dispatcher)(AEffect*, long, long, long, void*, float);
    void (__stdcall *process)(AEffect*, float**, float**, long);
    float (__stdcall *setParameter)(AEffect*, long, float);
    float (__stdcall *getParameter)(AEffect*, long);
    long numPrograms;
    long numParams;
    long numInputs;
    long numOutputs;
    long flags;
    long resvd1;
    long resvd2;
    long initialDelay;
    long realQualities;
    void* offline;
    void* ioRatio;
    AEffect* object;
    void* user;
    long uniqueID;
    long version;
    void (__stdcall *processReplacing)(AEffect*, float**, float**, long);
    void* processDoubleReplacing;
    char future[56];
};

// Audio master callback type
typedef long (__stdcall *audioMasterCallback)(AEffect*, long, long, long, void*, float);

// VST Plugin Info
struct VSTPluginInfo {
    QString path;
    QString name;
    QString vendor;
    long uniqueID = 0;
    long version = 0;
    long numParams = 0;
    long numPrograms = 0;
    long numInputs = 2;
    long numOutputs = 2;
    bool isSynth = false;
    bool hasEditor = false;
};

// ============================================================================
// KSAudioVSTHost - VST Plugin Host
// ============================================================================

class KSAudioVSTHost : public QObject {
    Q_OBJECT

public:
    explicit KSAudioVSTHost(QObject* parent = nullptr);
    ~KSAudioVSTHost();

    bool loadPlugin(const QString& dllPath);
    void unloadPlugin();
    bool isLoaded() const { return m_loaded; }

    QString pluginName() const { return m_info.name; }
    QString pluginVendor() const { return m_info.vendor; }
    long uniqueID() const { return m_info.uniqueID; }
    long version() const { return m_info.version; }

    long parameterCount() const { return m_info.numParams; }
    long programCount() const { return m_info.numPrograms; }

    float getParameter(long index);
    void setParameter(long index, float value);

    QString getParameterName(long index);
    QString getParameterDisplay(long index);
    QString getParameterLabel(long index);

    bool canDo(const QString& what);
    void setSampleRate(float sampleRate);
    void setBlockSize(long blockSize);

    void startProcessing();
    void stopProcessing();

    void process(float** inputs, float** outputs, long sampleFrames);

    QVector<QString> getPluginParameterNames();

signals:
    void pluginLoaded(const QString& name);
    void pluginUnloaded();
    void error(const QString& message);

private:
    QLibrary m_library;
    AEffect* m_effect;
    VSTPluginInfo m_info;
    bool m_loaded;
    bool m_processing;

    // VST entry point
    typedef AEffect* (__stdcall *vstMainFunc)(audioMasterCallback);
    vstMainFunc m_entryPoint;
    static audioMasterCallback s_masterCallback;

    static long __stdcall masterCallback(AEffect* effect, long opcode, long index, long value, void* ptr, float opt);
    bool initPlugin();
    void cleanupPlugin();
};

// ============================================================================
// KSAudioVSTManager - Manage multiple VST plugins
// ============================================================================

class KSAudioVSTManager : public QObject {
    Q_OBJECT

public:
    explicit KSAudioVSTManager(QObject* parent = nullptr);
    ~KSAudioVSTManager();

    int loadPlugin(const QString& dllPath);
    bool unloadPlugin(int pluginIndex);
    void unloadAllPlugins();

    int pluginCount() const { return m_plugins.size(); }
    KSAudioVSTHost* getPlugin(int index);
    const KSAudioVSTHost* getPlugin(int index) const;

    QStringList getPluginNames() const;
    QStringList scanDirectory(const QString& directory);

    void setSampleRate(float sampleRate);
    void setBlockSize(long blockSize);

signals:
    void pluginLoaded(int index, const QString& name);
    void pluginUnloaded(int index);
    void error(const QString& message);

private:
    QVector<KSAudioVSTHost*> m_plugins;
};

} // namespace audio
} // namespace ks

#endif // KSAUDIOVSTHOST_H
