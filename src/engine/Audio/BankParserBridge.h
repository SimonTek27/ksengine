#pragma once
#include "KsExport.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ks { namespace engine { namespace audio {
class BankParserBridge {
public:
    static BankParserBridge& instance() { static BankParserBridge s; return s; }
    bool initialize() { return true; }
    void shutdown() {}
};
}}} // namespace

namespace ks { namespace audio {

/** One FMOD event inside a .bank file, with its exposed parameters. */
struct BankEventMeta {
    std::string name;
    int parameterCount = 0;
    std::vector<std::string> parameterNames;
    std::vector<float> parameterDefaults;
};

/** One FMOD sound referenced by the bank (may or may not carry sample data). */
struct BankSoundMeta {
    std::string name;
    bool hasAudioData = false;
    unsigned int sampleCount = 0;   ///< decoded frames (0 when unknown)
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    unsigned int codec = 0;         ///< FMOD_SOUND_FORMAT (2 = PCM16)
    unsigned int bytesPerSample = 0;///< PCM bytes per channel frame (0 = compressed)
    std::uint64_t dataOffset = 0;   ///< absolute file offset of the payload
    std::uint64_t dataSize = 0;     ///< bytes from dataOffset to the next sample
};

/** One GUID -> event path pair decoded from an STDT string table. */
struct BankStringEntry {
    std::string guid;
    std::string path;
};

struct ParsedBank {
    bool valid = false;
    std::vector<BankEventMeta> events;
    std::vector<BankSoundMeta> sounds;
    std::vector<BankStringEntry> stringTable;
};

/**
 * Reads an FMOD .bank file (standalone RIFF/FEV parser, no Qt).
 *
 * Walks the RIFF chunk tree, decodes PRMB parameter definitions (name +
 * default value), links EVTB event records to their parameters by GUID, and
 * decodes the embedded FSB5 sample block: per-sample channel/rate/codec plus
 * the payload offset and size of every sample. The STDT radix-tree string
 * table (from this file, or from a companion `<bank>.strings.bank`) supplies
 * human-readable event names; events that have no entry keep their GUID.
 *
 * Non-RIFF/FEV, unreadable, or missing input yields `valid = false`.
 */
KSENGINE_API ParsedBank parseBankFile(const std::string& path);

/**
 * Writes sample `index` of the FSB5 block in `path` to a RIFF/WAVE file.
 *
 * Returns false for an out-of-range index, an unreadable file, or a codec
 * that is not raw PCM/float (Vorbis, ADPCM, XMA, ... need a full decoder).
 */
KSENGINE_API bool extractBankSampleToWav(const std::string& path, std::size_t index,
                            const std::string& outPath);

}} // namespace ks::audio
