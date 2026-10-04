#ifndef KSAUDIOVST3HOST_H
#define KSAUDIOVST3HOST_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QLibrary>
#include <QMutex>
#include <QSharedPointer>
#include <cstdint>
#include <cstring>

// ─── VST3 Fundamental Types ─────────────────────────────────────────────────
using Vst3Result = int32_t;
using TChar = char16_t;
using Vst3Int32 = int32_t;
using Vst3Int64 = int64_t;

constexpr Vst3Result kResultOk = 0x00000000;
constexpr Vst3Result kResultFalse = 0x00000001;
constexpr Vst3Result kInvalidArgument = 0x80000008;
constexpr Vst3Result kNoInterface = 0x80000004;

struct Vst3FUID {
    uint32_t data[4] = {};
    bool operator==(const Vst3FUID& o) const {
        return data[0] == o.data[0] && data[1] == o.data[1]
            && data[2] == o.data[2] && data[3] == o.data[3];
    }
    bool operator!=(const Vst3FUID& o) const { return !(*this == o); }
    static Vst3FUID fromString(const char* hex);
};

// ─── VST3 COM-like Interfaces ──────────────────────────────────────────────

class FUnknown {
public:
    virtual Vst3Result queryInterface(const Vst3FUID& iid, void** obj) = 0;
    virtual uint32_t addRef() = 0;
    virtual uint32_t release() = 0;
    virtual ~FUnknown() = default;
};

// Pre-declare well-known FUIDs
namespace VST3FUIDs {
    extern const Vst3FUID IUnknown;
    extern const Vst3FUID IPluginBase;
    extern const Vst3FUID IComponent;
    extern const Vst3FUID IAudioProcessor;
    extern const Vst3FUID IEditController;
    extern const Vst3FUID IEditController2;
    extern const Vst3FUID IConnectionPoint;
}

// ─── IPluginBase ────────────────────────────────────────────────────────────
class IPluginBase : public FUnknown {
public:
    virtual Vst3Result initialize(void* context) = 0;
    virtual Vst3Result terminate() = 0;
};

// ─── Factory ────────────────────────────────────────────────────────────────
struct PFactoryInfo {
    enum FactoryFlags { kNoFlags = 0, kClassesDiscardable = 1, kLicenseCheck = 2 };
    TChar vendor[128] = {};
    TChar url[256] = {};
    TChar email[128] = {};
    int32_t flags = 0;
};

struct ClassInfo {
    Vst3FUID cid;
    Vst3FUID cardinality;
    TChar category[256] = {};
    TChar name[256] = {};
    TChar vendor[256] = {};
    TChar version[256] = {};
    TChar sdkVersion[128] = {};
    int32_t subCategories = 0;
};

class IPluginFactory : public FUnknown {
public:
    virtual Vst3Result getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32_t countClasses() = 0;
    virtual Vst3Result getClassInfo(int32_t index, ClassInfo* info) = 0;
    virtual Vst3Result createInstance(const Vst3FUID& cid, const Vst3FUID& iid, void** obj) = 0;
};

// ─── Component / Bus Types ──────────────────────────────────────────────────
enum MediaTypes { kAudio = 0, kEvent = 1, kNumMediaTypes };
enum BusDirections { kInput = 0, kOutput = 1 };
enum BusTypes { kMain = 0, kAux = 1 };

struct BusInfo {
    Vst3FUID mediaType;
    BusDirections direction = kInput;
    BusTypes busType = kMain;
    int32_t flags = 0;
    TChar name[128] = {};
    int32_t numChannels = 0;
    TChar busName[128] = {};
};

struct AudioBusBuffers {
    int32_t numChannels = 0;
    float** channelBuffers = nullptr;
    int32_t silenceFlags = 0;
};

// ─── Event Types ────────────────────────────────────────────────────────────
enum EventTypes { kNoteOnEvent = 0, kNoteOffEvent = 1, kDataEvent = 2, kPolyPressureEvent = 3, kNoteExpressionValueEvent = 4 };

struct NoteOnEvent {
    int16_t channel = 0;
    int16_t pitch = 60;
    float tuning = 0.0f;
    float velocity = 0.5f;
    int32_t length = 0;
    int32_t noteId = -1;
};

struct NoteOffEvent {
    int16_t channel = 0;
    int16_t pitch = 60;
    float velocity = 0.0f;
    int32_t noteId = -1;
    float tuning = 0.0f;
};

struct Event {
    int32_t busIndex = 0;
    int32_t sampleOffset = 0;
    int16_t type = kNoteOnEvent;
    int16_t flags = 0;
    union {
        NoteOnEvent noteOn;
        NoteOffEvent noteOff;
    };
};

struct EventList {
    int32_t eventCount = 0;
    Event* events = nullptr;
};

// ─── Process Context & Data ────────────────────────────────────────────────
struct ProcessContext {
    int32_t state = 0;
    double sampleRate = 44100.0;
    Vst3Int64 projectTimeSamples = 0;
    int64_t systemTimeNs = 0;
    double tempo = 120.0;
    double timeSigNumerator = 4.0;
    double timeSigDenominator = 4.0;
    int32_t chordMask = 0;
    int32_t barPositionSamples = 0;
    double playBackSpeed = 1.0;
};

struct ProcessSetup {
    int32_t maxSamplesPerBlock = 512;
    int32_t sampleRate = 44100;
    int32_t processMode = 0;
    int32_t symbolicSampleSize = 32;
};

enum ProcessModes { kRealtime = 0, kPrefetch = 1, kOffline = 2 };
enum SymbolicSampleSizes { kSample32 = 0, kSample64 = 1 };

struct ProcessData {
    int32_t processMode = kRealtime;
    int32_t symbolicSampleSize = kSample32;
    int32_t numInputs = 0;
    int32_t numOutputs = 0;
    AudioBusBuffers* inputs = nullptr;
    AudioBusBuffers* outputs = nullptr;
    int32_t numSamples = 0;
    int32_t numEvents = 0;
    EventList* inputEvents = nullptr;
    EventList* outputEvents = nullptr;
    ProcessContext* processContext = nullptr;
    double* outputParameterChanges = nullptr;
};

// ─── IComponent ─────────────────────────────────────────────────────────────
class IComponent : public IPluginBase {
public:
    virtual Vst3Result getControllerClassId(Vst3FUID& cid) = 0;
    virtual Vst3Result setIoMode(int32_t mode) = 0;
    virtual int32_t getBusCount(MediaTypes type, BusDirections dir) = 0;
    virtual Vst3Result getBusInfo(MediaTypes type, BusDirections dir, int32_t index, BusInfo& info) = 0;
    virtual Vst3Result getRoutingInfo(void* inInfo, void* outInfo) = 0;
    virtual Vst3Result activateBus(MediaTypes type, BusDirections dir, int32_t index, bool state) = 0;
    virtual Vst3Result setActive(bool active) = 0;
    virtual Vst3Result setState(void* state) = 0;
    virtual Vst3Result getState(void* state) = 0;
};

// ─── IAudioProcessor ────────────────────────────────────────────────────────
class IAudioProcessor : public FUnknown {
public:
    virtual Vst3Result setBusArrangements(void* inputs, int32_t numIns, void* outputs, int32_t numOuts) = 0;
    virtual Vst3Result setupProcessing(const ProcessSetup& setup) = 0;
    virtual Vst3Result setProcessing(bool state) = 0;
    virtual Vst3Result process(ProcessData& data) = 0;
    virtual uint32_t getLatencySamples() = 0;
    virtual Vst3Result getTailSamples(uint32_t* samples) = 0;
};

// ─── IEditController ────────────────────────────────────────────────────────
struct ParameterInfo {
    Vst3FUID id;
    TChar title[128] = {};
    TChar shortTitle[128] = {};
    TChar units[128] = {};
    int32_t stepCount = 0;
    float defaultValue = 0.0f;
    float unitId = 0.0f;
    int32_t flags = 0;
};

class IEditController : public IPluginBase {
public:
    virtual Vst3Result setComponentState(void* state) = 0;
    virtual Vst3Result setParamNormalized(Vst3Int32 tag, float value) = 0;
    virtual Vst3Result getParamObject(Vst3Int32 tag, void** obj) = 0;
    virtual Vst3Result getParamInfo(int32_t index, ParameterInfo& info) = 0;
    virtual int32_t getParamCount() = 0;
    virtual Vst3Result getParamStringByValue(Vst3Int32 tag, float value, TChar* str, int32_t len) = 0;
    virtual Vst3Result getParamValueByString(Vst3Int32 tag, const TChar* str, float& value) = 0;
    virtual float normalizeParamToPlain(Vst3Int32 tag, float valueNormalized) = 0;
    virtual float plainParamToNormalized(Vst3Int32 tag, float plainValue) = 0;
    virtual Vst3Result getParamUnitInfo(Vst3Int32 tag, void* info) = 0;
    virtual Vst3Result connect(FUnknown* other) = 0;
    virtual Vst3Result disconnect(FUnknown* other) = 0;
    virtual Vst3Result notify(void* changes) = 0;
};

// ─── IConnectionPoint ───────────────────────────────────────────────────────
class IConnectionPoint : public FUnknown {
public:
    virtual Vst3Result connect(FUnknown* other) = 0;
    virtual Vst3Result disconnect(FUnknown* other) = 0;
    virtual Vst3Result notify(void* changes) = 0;
};

// ─── VST3 Plugin Info ──────────────────────────────────────────────────────
struct VST3PluginInfo {
    QString path;
    QString name;
    QString vendor;
    QString version;
    Vst3FUID uid;
    int32_t numParams = 0;
    int32_t numPrograms = 0;
    int32_t numInputs = 2;
    int32_t numOutputs = 2;
    bool isSynth = false;
    bool hasEditor = false;
};

// ─── KSAudioVST3Host ────────────────────────────────────────────────────────
namespace ks {
namespace audio {

class KSAudioVST3Host : public QObject {
    Q_OBJECT
public:
    explicit KSAudioVST3Host(QObject* parent = nullptr);
    ~KSAudioVST3Host() override;

    bool loadPlugin(const QString& dllPath);
    void unloadPlugin();
    bool isLoaded() const { return m_loaded; }

    QString pluginName() const { return m_info.name; }
    QString pluginVendor() const { return m_info.vendor; }
    QString pluginVersion() const { return m_info.version; }
    int32_t parameterCount() const { return m_info.numParams; }
    int32_t programCount() const { return m_info.numPrograms; }

    float getParameter(int32_t tag);
    void setParameter(int32_t tag, float value);

    QString getParameterName(int32_t tag);
    QString getParameterString(int32_t tag, float value);

    void setSampleRate(int32_t sampleRate);
    void setBlockSize(int32_t blockSize);
    void startProcessing();
    void stopProcessing();
    void process(const float* const* inputs, float** outputs, int32_t sampleFrames);

signals:
    void pluginLoaded(const QString& name);
    void pluginUnloaded();
    void error(const QString& message);

private:
    QLibrary m_library;
    VST3PluginInfo m_info;
    bool m_loaded = false;
    bool m_processing = false;

    int32_t m_sampleRate = 44100;
    int32_t m_blockSize = 512;

    // VST3 module entry point
    typedef IPluginFactory* (*GetFactoryFunc)();
    GetFactoryFunc m_getFactory = nullptr;
    IPluginFactory* m_factory = nullptr;

    // Plugin interfaces
    IComponent* m_component = nullptr;
    IAudioProcessor* m_processor = nullptr;
    IEditController* m_controller = nullptr;

    // Internal audio buffers
    float** m_inBufs = nullptr;
    float** m_outBufs = nullptr;
    int32_t m_audioInputs = 0;
    int32_t m_audioOutputs = 0;

    struct VST3HostContext : FUnknown {
        Vst3Result queryInterface(const Vst3FUID&, void**) override { return kNoInterface; }
        uint32_t addRef() override { return 1; }
        uint32_t release() override { return 1; }
    };
    VST3HostContext m_hostContext;

    // Keep an owned copy of ProcessData buffers that persist between calls
    ProcessData m_processData = {};
    AudioBusBuffers* m_inBusBufs = nullptr;
    AudioBusBuffers* m_outBusBufs = nullptr;

    bool initPlugin();
    void cleanupPlugin();
    void allocProcessBuffers();
    void freeProcessBuffers();
};

// ─── KSAudioVST3Manager ─────────────────────────────────────────────────────
class KSAudioVST3Manager : public QObject {
    Q_OBJECT
public:
    explicit KSAudioVST3Manager(QObject* parent = nullptr);
    ~KSAudioVST3Manager() override;

    int loadPlugin(const QString& dllPath);
    bool unloadPlugin(int index);
    void unloadAllPlugins();

    int pluginCount() const { return m_plugins.size(); }
    KSAudioVST3Host* getPlugin(int index);
    const KSAudioVST3Host* getPlugin(int index) const;

    QStringList getPluginNames() const;
    QStringList scanDirectory(const QString& directory);

    void setSampleRate(int32_t sampleRate);
    void setBlockSize(int32_t blockSize);

signals:
    void pluginLoaded(int index, const QString& name);
    void pluginUnloaded(int index);
    void error(const QString& message);

private:
    QVector<KSAudioVST3Host*> m_plugins;
};

} // namespace audio
} // namespace ks

#endif // KSAUDIOVST3HOST_H
