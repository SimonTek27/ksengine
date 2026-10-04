#include "BankParserBridge.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace ks {
namespace audio {

namespace {

struct ParamRecord {
    unsigned char guidBytes[16] = {};
    std::string guid;
    std::string name;
    float defaultValue = 0.0f;
};

struct RawEvent {
    std::string guid;
    std::vector<unsigned char> body;
};

uint32_t rd16(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
}

uint32_t rd32(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t rd64(const unsigned char* p)
{
    return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

uint32_t rd24(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16);
}

float rdF32(const unsigned char* p)
{
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

std::string guidToString(const unsigned char* g)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                  g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10],
                  g[11], g[12], g[13], g[14], g[15]);
    return std::string(buf);
}

bool readFile(const std::string& path, std::vector<unsigned char>& out)
{
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec) ||
        !std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

struct ChunkView {
    char tag[5] = {};
    const unsigned char* content = nullptr;
    uint32_t contentSize = 0;
};

// Standard RIFF semantics: size = content bytes, chunks padded to even size.
template <typename Fn>
void forEachChunk(const unsigned char* data, size_t size, Fn&& fn)
{
    size_t pos = 0;
    while (pos + 8 <= size) {
        ChunkView c;
        std::memcpy(c.tag, data + pos, 4);
        c.tag[4] = '\0';
        const uint32_t sz = rd32(data + pos + 4);
        const size_t contentStart = pos + 8;
        if (sz > size - contentStart) {
            break;
        }
        c.content = data + contentStart;
        c.contentSize = sz;
        fn(c);
        pos = contentStart + sz + (sz & 1u);
    }
}

// Recursive chunk walk. LIST chunks expose their type as first 4 content bytes;
// children are everything after that type tag.
template <typename Fn>
void walkChunks(const unsigned char* data, size_t size, int depth, Fn&& fn)
{
    if (depth > 8) {
        return;
    }
    forEachChunk(data, size, [&](const ChunkView& c) {
        fn(c);
        if (std::strncmp(c.tag, "LIST", 4) == 0 && c.contentSize > 4) {
            walkChunks(c.content + 4, c.contentSize - 4, depth + 1, fn);
        }
    });
}

bool parseParamChunk(const ChunkView& c, ParamRecord& out)
{
    if (std::strncmp(c.tag, "PRMB", 4) != 0 || c.contentSize < 43) {
        return false;
    }
    const uint32_t nameLen = c.content[21];
    const size_t nameOff = 23;
    if (nameOff + nameLen + 4 + 4 > c.contentSize) {
        return false;
    }
    std::memcpy(out.guidBytes, c.content, 16);
    out.guid = guidToString(c.content);
    out.name.assign(reinterpret_cast<const char*>(c.content + nameOff), nameLen);
    out.defaultValue = rdF32(c.content + nameOff + nameLen + 4);
    return true;
}

// ---------------------------------------------------------------- FSB5 ----
//
// FSB5 v0/v1 sample block (FMOD Studio bank payload). Layout:
//   0x04 version, 0x08 numSamples, 0x0C sampleHeaderSize, 0x10 nameTableSize,
//   0x14 sampleDataSize, 0x18 codec, ... base header ends at 0x3C (v1) / 0x40 (v0).
//   [base .. base+sampleHeaderSize)  one variable-length record per sample
//   [base+sampleHeaderSize .. +nameTableSize)  u32-per-sample name offsets
//   [.. end)                          packed sample payloads
//
// A sample record starts with a little-endian u64 "sample mode":
//   bits 63..34  frame count
//   bits 33..7   payload offset inside the data section, in 32-byte units
//   bits 7..6    channel code  (0/1/2/3 -> 1/2/6/8)
//   bits 5..1    sample-rate code (table below)
//   bit  0       extra-flag chain present
// and is followed, when that chain is set, by u32 extra flags:
//   type = (x >> 25) & 0x7F, size = (x >> 1) & 0xFFFFFF, continue = x & 1,
//   payload starts at x + 4 and the whole flag takes 4 + size bytes.
//   type 0x01 overrides the channel count (u8), type 0x02 the rate (s32).

const uint32_t kFsb5RateTable[16] = {
    4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000,
    44100, 48000, 96000, 0, 0, 0, 0, 0
};

unsigned int fsb5Channels(uint64_t mode)
{
    switch ((mode >> 5) & 0x3u) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 6;
    default: return 8;
    }
}

unsigned int pcmBytesPerSample(uint32_t codec)
{
    switch (codec) {
    case 1: return 1;   // PCM8
    case 2: return 2;   // PCM16
    case 3: return 3;   // PCM24
    case 4: return 4;   // PCM32
    case 5: return 4;   // PCMFLOAT
    default: return 0;  // compressed codecs need a decoder
    }
}

struct Fsb5Sample {
    uint32_t numSamples = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    std::string name;
};

struct Fsb5Info {
    bool ok = false;
    uint32_t codec = 0;
    uint32_t dataSize = 0;
    size_t dataStart = 0;  // relative to the FSB5 magic
    std::vector<Fsb5Sample> samples;
};

Fsb5Info parseFsb5(const unsigned char* p, size_t avail)
{
    Fsb5Info f;
    if (avail < 0x40 || std::memcmp(p, "FSB5", 4) != 0) {
        return f;
    }
    const uint32_t version    = rd32(p + 0x04);
    const uint32_t numSamples = rd32(p + 0x08);
    const uint32_t hdrSize    = rd32(p + 0x0C);
    const uint32_t nameSize   = rd32(p + 0x10);
    const uint32_t dataSize   = rd32(p + 0x14);
    if (version > 1 || numSamples == 0 || numSamples > 1000000u) {
        return f;
    }
    const size_t base = (version == 1) ? 0x3C : 0x40;
    if (base + static_cast<size_t>(hdrSize) + nameSize + dataSize > avail) {
        return f;
    }
    const size_t nameStart = base + hdrSize;
    const size_t dataStart = nameStart + nameSize;

    f.ok = true;
    f.codec = rd32(p + 0x18);
    f.dataSize = dataSize;
    f.dataStart = dataStart;

    f.samples.reserve(numSamples);
    size_t off = base;
    for (uint32_t i = 0; i < numSamples && off + 8 <= nameStart; ++i) {
        const uint64_t mode = rd64(p + off);
        size_t adv = 8;

        Fsb5Sample s;
        s.numSamples = static_cast<uint32_t>((mode >> 34) & 0x3FFFFFFFull);
        s.dataOffset = static_cast<uint32_t>(((mode >> 7) & 0x07FFFFFFull) << 5);
        s.channels = fsb5Channels(mode);
        s.sampleRate = kFsb5RateTable[(mode >> 1) & 0xFull];

        if (mode & 1u) {
            size_t eo = off + 8;
            while (eo + 4 <= nameStart) {
                const uint32_t x = rd32(p + eo);
                const uint32_t type = (x >> 25) & 0x7Fu;
                const uint32_t size = (x >> 1) & 0xFFFFFFu;
                const uint32_t cont = x & 1u;
                if (eo + 4 + size > nameStart) {
                    break;
                }
                if (type == 0x01 && size >= 1) {
                    s.channels = p[eo + 4];
                } else if (type == 0x02 && size >= 4) {
                    s.sampleRate = rd32(p + eo + 4);
                }
                adv += 4 + size;
                eo += 4 + size;
                if (cont == 0) {
                    break;
                }
            }
        }

        f.samples.push_back(s);
        off += adv;
    }

    // Payload span: everything up to the next sample's offset, or to the end.
    for (size_t i = 0; i < f.samples.size(); ++i) {
        const uint32_t next = (i + 1 < f.samples.size())
                                  ? f.samples[i + 1].dataOffset
                                  : dataSize;
        const uint32_t cur = f.samples[i].dataOffset;
        f.samples[i].dataSize = (next > cur && cur <= dataSize) ? (next - cur) : 0;
    }

    if (nameSize >= f.samples.size() * 4u) {
        for (size_t i = 0; i < f.samples.size(); ++i) {
            const uint32_t rel = rd32(p + nameStart + i * 4);
            if (rel >= nameSize) {
                continue;
            }
            const unsigned char* np = p + nameStart + rel;
            const size_t maxLen = nameSize - rel;
            size_t len = 0;
            while (len < maxLen && np[len] != 0) {
                ++len;
            }
            f.samples[i].name.assign(reinterpret_cast<const char*>(np), len);
        }
    }
    return f;
}

// --------------------------------------------------------------- STDT ----
//
// FMOD string table, contents start with a u32 type (1 = RadixTree_24Bit)
// followed by five lists, all little-endian:
//   Nodes         elem list: ReadX16 count>>1, u16 stride, stride*count bytes
//                 each node = u32 KeyInfo (StringOffset = low 24 bits,
//                 0xFFFFFF = none) + u32 ChildInfo
//   Guids         elem list: ReadX16 count>>1, u16 stride (16), 16 raw bytes
//                 per GUID (same byte order as EVTB / guidToString)
//   StringBlob    simple array: ReadX16 byte count, then NUL-terminated strings
//   LeafIndices   simple array: ReadX16 count, then count * u24 node indices
//   ParentIndices simple array: ReadX16 count, then count * u24 node indices
//
// ReadX16 reads a u16; when its high bit is set a second u16 follows and the
// value is (low & 0x7FFF) | (high << 15).
//
// The path of GUID i is the concatenation of LeafIndices[i] and its parent
// chain's KeyInfo strings, emitted root-first.

constexpr uint32_t kNoString = 0x00FFFFFFu;

bool stdtParse(const unsigned char* p, size_t size, std::vector<BankStringEntry>& out)
{
    out.clear();
    if (size < 8 || rd32(p) != 1) {
        return false;
    }

    size_t pos = 4;
    auto readX16 = [&](size_t& at, uint32_t& v) -> bool {
        if (at + 2 > size) {
            return false;
        }
        const uint32_t low = rd16(p + at);
        at += 2;
        if ((low & 0x8000u) == 0) {
            v = low;
            return true;
        }
        if (at + 2 > size) {
            return false;
        }
        const uint32_t high = rd16(p + at);
        at += 2;
        v = (low & 0x7FFFu) | (high << 15);
        return true;
    };
    // Element list: count encoded as (count << 1) plus a trailing flag bit,
    // followed by a u16 element stride.
    auto readElemList = [&](size_t& at, uint32_t& count, uint32_t& stride,
                            const unsigned char*& base) -> bool {
        uint32_t raw = 0;
        if (!readX16(at, raw) || at + 2 > size) {
            return false;
        }
        count = raw >> 1;
        stride = rd16(p + at);
        at += 2;
        if (stride == 0 || count > (size - at) / stride) {
            return false;
        }
        base = p + at;
        at += static_cast<size_t>(count) * stride;
        return true;
    };
    // Simple array: count then raw elements, no stride header.
    auto readArray = [&](size_t& at, uint32_t elemSize, uint32_t& count,
                         const unsigned char*& base) -> bool {
        uint32_t raw = 0;
        if (!readX16(at, raw)) {
            return false;
        }
        count = raw;
        if (elemSize == 0 || count > (size - at) / elemSize) {
            return false;
        }
        base = p + at;
        at += static_cast<size_t>(count) * elemSize;
        return true;
    };

    uint32_t nodeCount = 0, nodeStride = 0;
    uint32_t guidCount = 0, guidStride = 0;
    uint32_t blobLen = 0, leafCount = 0, parentCount = 0;
    const unsigned char *nodeBase = nullptr, *guidBase = nullptr, *blob = nullptr;
    const unsigned char *leafBase = nullptr, *parentBase = nullptr;

    if (!readElemList(pos, nodeCount, nodeStride, nodeBase) ||
        !readElemList(pos, guidCount, guidStride, guidBase) ||
        !readArray(pos, 1, blobLen, blob) ||
        !readArray(pos, 3, leafCount, leafBase) ||
        !readArray(pos, 3, parentCount, parentBase)) {
        return false;
    }
    if (nodeStride != 8 || guidStride != 16 || leafCount != guidCount ||
        parentCount != nodeCount) {
        return false;
    }

    out.reserve(guidCount);
    for (uint32_t i = 0; i < guidCount; ++i) {
        uint32_t node = rd24(leafBase + static_cast<size_t>(i) * 3);
        std::vector<std::string> segments;
        for (int guard = 0; node != kNoString && guard < 4096; ++guard) {
            if (node >= nodeCount) {
                segments.clear();
                break;
            }
            const uint32_t keyInfo = rd32(nodeBase + static_cast<size_t>(node) * 8);
            const uint32_t strOff = keyInfo & 0x00FFFFFFu;
            if (strOff != kNoString && strOff < blobLen) {
                size_t end = strOff;
                while (end < blobLen && blob[end] != 0) {
                    ++end;
                }
                segments.emplace_back(reinterpret_cast<const char*>(blob + strOff),
                                      end - strOff);
            }
            node = rd24(parentBase + static_cast<size_t>(node) * 3);
        }

        std::string path;
        for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
            path += *it;
        }
        BankStringEntry entry;
        entry.guid = guidToString(guidBase + static_cast<size_t>(i) * 16);
        entry.path = std::move(path);
        out.push_back(std::move(entry));
    }
    return true;
}

bool hasSuffix(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
}

// Reads the STDT table out of a companion `<bank>.strings.bank`, if present.
std::vector<BankStringEntry> loadSiblingStringTable(const std::string& bankPath)
{
    std::vector<BankStringEntry> out;
    const std::filesystem::path p(bankPath);
    const std::string fname = p.filename().string();
    if (!hasSuffix(fname, ".bank") || hasSuffix(fname, ".strings.bank")) {
        return out;
    }
    std::filesystem::path sibling(p);
    sibling.replace_filename(fname.substr(0, fname.size() - 5) + ".strings.bank");

    std::vector<unsigned char> buf;
    if (!readFile(sibling.string(), buf) || buf.size() < 16 ||
        std::memcmp(buf.data(), "RIFF", 4) != 0) {
        return out;
    }
    const size_t riffEnd = std::min<size_t>(8u + rd32(buf.data() + 4), buf.size());
    if (riffEnd <= 12) {
        return out;
    }
    walkChunks(buf.data() + 12, riffEnd - 12, 0, [&](const ChunkView& c) {
        if (out.empty() && std::strncmp(c.tag, "STDT", 4) == 0) {
            stdtParse(c.content, c.contentSize, out);
        }
    });
    return out;
}

} // namespace

ParsedBank parseBankFile(const std::string& path)
{
    ParsedBank parsed;

    std::vector<unsigned char> buf;
    if (!readFile(path, buf)) {
        if (!path.empty()) {
            std::fprintf(stderr, "BankParserBridge: cannot read bank: %s\n", path.c_str());
        }
        return parsed;
    }

    if (buf.size() < 16 || std::memcmp(buf.data(), "RIFF", 4) != 0 ||
        std::memcmp(buf.data() + 8, "FEV ", 4) != 0) {
        std::fprintf(stderr, "BankParserBridge: not a RIFF/FEV bank: %s\n", path.c_str());
        return parsed;
    }

    const size_t riffEnd = std::min<size_t>(8u + rd32(buf.data() + 4), buf.size());

    std::vector<ParamRecord> params;
    std::vector<RawEvent> events;
    bool sawFmt = false;

    walkChunks(buf.data() + 12, riffEnd - 12, 0, [&](const ChunkView& c) {
        if (std::strncmp(c.tag, "FMT ", 4) == 0) {
            sawFmt = c.contentSize >= 4;
            return;
        }
        if (std::strncmp(c.tag, "PRMB", 4) == 0) {
            ParamRecord p;
            if (parseParamChunk(c, p)) {
                params.push_back(std::move(p));
            }
            return;
        }
        if (std::strncmp(c.tag, "EVTB", 4) == 0) {
            RawEvent ev;
            if (c.contentSize >= 16) {
                ev.guid = guidToString(c.content);
                ev.body.assign(c.content, c.content + c.contentSize);
                events.push_back(std::move(ev));
            }
            return;
        }
        if (std::strncmp(c.tag, "STDT", 4) == 0) {
            if (parsed.stringTable.empty()) {
                stdtParse(c.content, c.contentSize, parsed.stringTable);
            }
            return;
        }
        if (std::strncmp(c.tag, "FSB5", 4) == 0) {
            // (not expected as a chunk tag; FSB5 lives inside the SND payload)
            return;
        }
    });

    // Locate the embedded FSB5 sample block anywhere in the file.
    std::vector<Fsb5Sample> fsbSamples;
    uint32_t fsbCodec = 0;
    size_t fsbDataStart = 0;
    for (size_t i = 0; i + 4 <= riffEnd; ++i) {
        if (std::memcmp(buf.data() + i, "FSB5", 4) != 0) {
            continue;
        }
        const Fsb5Info fsb = parseFsb5(buf.data() + i, riffEnd - i);
        if (fsb.ok) {
            fsbSamples = std::move(fsb.samples);
            fsbCodec = fsb.codec;
            fsbDataStart = i + fsb.dataStart;
        }
        break;
    }

    parsed.valid = sawFmt;

    // A companion .strings.bank carries the readable event paths.
    if (parsed.stringTable.empty()) {
        parsed.stringTable = loadSiblingStringTable(path);
    }
    std::unordered_map<std::string, std::string> pathByGuid;
    for (const BankStringEntry& e : parsed.stringTable) {
        if (!e.path.empty()) {
            pathByGuid.emplace(e.guid, e.path);
        }
    }

    // Resolve event parameter links (parameter GUIDs are embedded in EVTB bodies).
    for (const RawEvent& raw : events) {
        BankEventMeta ev;
        const auto named = pathByGuid.find(raw.guid);
        ev.name = (named != pathByGuid.end()) ? named->second : raw.guid;
        for (size_t off = 0; off + 16 <= raw.body.size(); ++off) {
            bool matched = false;
            for (const ParamRecord& p : params) {
                if (std::memcmp(raw.body.data() + off, p.guidBytes, 16) == 0) {
                    ev.parameterNames.push_back(p.name);
                    ev.parameterDefaults.push_back(p.defaultValue);
                    matched = true;
                    break;
                }
            }
            if (matched) {
                off += 15;
            }
        }
        ev.parameterCount = static_cast<int>(ev.parameterNames.size());
        parsed.events.push_back(std::move(ev));
    }

    parsed.sounds.reserve(fsbSamples.size());
    for (size_t i = 0; i < fsbSamples.size(); ++i) {
        const Fsb5Sample& s = fsbSamples[i];
        BankSoundMeta m;
        m.name = s.name.empty() ? ("sound_" + std::to_string(i)) : s.name;
        m.hasAudioData = s.dataSize > 0;
        m.sampleCount = s.numSamples;
        m.channels = s.channels;
        m.sampleRate = s.sampleRate;
        m.codec = fsbCodec;
        m.bytesPerSample = pcmBytesPerSample(fsbCodec);
        m.dataOffset = static_cast<std::uint64_t>(fsbDataStart) + s.dataOffset;
        m.dataSize = s.dataSize;
        parsed.sounds.push_back(std::move(m));
    }

    return parsed;
}

bool extractBankSampleToWav(const std::string& path, std::size_t index,
                            const std::string& outPath)
{
    const ParsedBank bank = parseBankFile(path);
    if (index >= bank.sounds.size()) {
        return false;
    }
    const BankSoundMeta& s = bank.sounds[index];
    if (s.bytesPerSample == 0 || s.channels == 0 || s.sampleRate == 0) {
        return false;
    }
    const std::uint64_t want =
        static_cast<std::uint64_t>(s.sampleCount) * s.channels * s.bytesPerSample;
    if (want == 0 || want > s.dataSize || want > 0xFFFFFFFFull - 36u) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(static_cast<std::streamoff>(s.dataOffset));
    std::vector<char> data(static_cast<size_t>(want));
    if (!in.read(data.data(), static_cast<std::streamsize>(data.size()))) {
        return false;
    }

    auto put16 = [](std::ostream& o, std::uint16_t v) {
        const char b[2] = {static_cast<char>(v & 0xFFu),
                           static_cast<char>((v >> 8) & 0xFFu)};
        o.write(b, 2);
    };
    auto put32 = [](std::ostream& o, std::uint32_t v) {
        const char b[4] = {static_cast<char>(v & 0xFFu),
                           static_cast<char>((v >> 8) & 0xFFu),
                           static_cast<char>((v >> 16) & 0xFFu),
                           static_cast<char>((v >> 24) & 0xFFu)};
        o.write(b, 4);
    };

    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        return false;
    }
    const std::uint32_t payload = static_cast<std::uint32_t>(want);
    const std::uint16_t formatTag = (s.codec == 5) ? 3u : 1u; // IEEE float vs PCM
    const std::uint16_t blockAlign =
        static_cast<std::uint16_t>(s.channels * s.bytesPerSample);

    out.write("RIFF", 4);
    put32(out, 36u + payload);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    put32(out, 16u);
    put16(out, formatTag);
    put16(out, static_cast<std::uint16_t>(s.channels));
    put32(out, s.sampleRate);
    put32(out, s.sampleRate * blockAlign);
    put16(out, blockAlign);
    put16(out, static_cast<std::uint16_t>(s.bytesPerSample * 8));
    out.write("data", 4);
    put32(out, payload);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

} // namespace audio
} // namespace ks
