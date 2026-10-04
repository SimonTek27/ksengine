#include "Deflate.h"

namespace ks::engine::fileformat {
namespace {

constexpr int kMaxBits = 15;
constexpr int kMaxLCodes = 288;
constexpr int kMaxDCodes = 32;
constexpr int kSizeLens = 19;
constexpr int kMaxCodes = kMaxLCodes + kMaxDCodes;

constexpr int kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11, 13,
                              15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                              67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
                               2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr int kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                               17,   25,   33,   49,   65,   97,    129,   193,
                               257,  385,  513,  769,  1025, 1537,  2049,  3073,
                               4097, 6145, 8193, 12289, 16385, 24577};
constexpr int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3,  3,  4,  4,  5,  5,
                                6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool setErr(std::string* error, const char* msg) {
    if (error != nullptr && error->empty()) *error = msg;
    return false;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t c : data) {
        a += c;
        if (a >= 65521u) a -= 65521u;
        b += a;
        if (b >= 65521u) b -= 65521u;
    }
    return (b << 16) | a;
}

struct BitReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    std::uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool failed = false;

    int bits(int need) {
        if (need <= 0) return 0;
        while (bitcnt < need) {
            if (pos >= size) {
                failed = true;
                return 0;
            }
            bitbuf |= static_cast<std::uint32_t>(data[pos++]) << bitcnt;
            bitcnt += 8;
        }
        const int value = static_cast<int>(bitbuf & ((1u << need) - 1u));
        bitbuf >>= need;
        bitcnt -= need;
        return value;
    }

    // Discards the bits left over inside the current byte, then rewinds the
    // whole bytes that were read ahead so the caller can walk `data` directly.
    void byteAlign() {
        const int r = bitcnt & 7;
        if (r != 0) {
            bitbuf >>= r;
            bitcnt -= r;
        }
        pos -= static_cast<std::size_t>(bitcnt >> 3);
        bitbuf = 0;
        bitcnt = 0;
    }
};

// Canonical Huffman with the puff-style canonical decode: no per-bit tree, so
// an allocation-free 15-level walk is enough for the sizes FBX uses.
struct Huffman {
    std::uint16_t count[kMaxBits + 1] = {};
    std::uint16_t symbols[kMaxCodes] = {};
    int nSymbols = 0;

    bool build(const std::uint8_t* lengths, int n) {
        if (n > kMaxCodes) return false;
        for (int i = 0; i <= kMaxBits; ++i) count[i] = 0;
        nSymbols = 0;
        for (int i = 0; i < n; ++i) {
            if (lengths[i] > kMaxBits) return false;
            ++count[lengths[i]];
        }

        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;  // over-subscribed
        }

        std::uint16_t offs[kMaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) {
            offs[len + 1] = static_cast<std::uint16_t>(offs[len] + count[len]);
        }
        for (int i = 0; i < n; ++i) {
            if (lengths[i] == 0) continue;
            const int slot = offs[lengths[i]]++;
            if (slot >= kMaxCodes) return false;
            symbols[slot] = static_cast<std::uint16_t>(i);
            ++nSymbols;
        }
        return true;
    }

    int decode(BitReader& br) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= br.bits(1);
            if (br.failed) return -1;
            const int cnt = count[len];
            if (code - cnt < first) {
                const int slot = index + (code - first);
                if (slot < 0 || slot >= nSymbols) return -1;
                return symbols[slot];
            }
            index += cnt;
            first = (first + cnt) << 1;
            code <<= 1;
        }
        return -1;
    }
};

class Inflater {
public:
    Inflater(const std::uint8_t* src, std::size_t srcSize,
             std::vector<std::uint8_t>& out, std::string* error)
        : m_br{src, srcSize}, m_out(out), m_error(error) {}

    bool run() {
        for (;;) {
            const int bfinal = m_br.bits(1);
            const int btype = m_br.bits(2);
            if (m_br.failed) return setErr(m_error, "truncated deflate block header");
            const bool ok = (btype == 0)   ? stored()
                            : (btype == 1) ? compressed(true)
                            : (btype == 2) ? compressed(false)
                                           : setErr(m_error, "invalid deflate block type");
            if (!ok) return false;
            if (bfinal != 0) return true;
        }
    }

    // Byte offset of the first byte after the last consumed whole byte.
    std::size_t nextByte() const { return m_br.pos - static_cast<std::size_t>(m_br.bitcnt >> 3); }

private:
    bool stored() {
        m_br.byteAlign();
        if (m_br.pos + 4 > m_br.size) return setErr(m_error, "truncated stored block header");
        const std::uint32_t len =
            static_cast<std::uint32_t>(m_br.data[m_br.pos]) |
            (static_cast<std::uint32_t>(m_br.data[m_br.pos + 1]) << 8);
        const std::uint32_t nlen =
            static_cast<std::uint32_t>(m_br.data[m_br.pos + 2]) |
            (static_cast<std::uint32_t>(m_br.data[m_br.pos + 3]) << 8);
        if ((len ^ 0xFFFFu) != nlen) return setErr(m_error, "stored block length check failed");
        m_br.pos += 4;
        if (m_br.pos + len > m_br.size) return setErr(m_error, "truncated stored block data");
        m_out.insert(m_out.end(), m_br.data + m_br.pos, m_br.data + m_br.pos + len);
        m_br.pos += len;
        return true;
    }

    bool compressed(bool fixed) {
        Huffman lit;
        Huffman dist;
        if (fixed) {
            if (!buildFixed(lit, dist)) return setErr(m_error, "bad fixed huffman tables");
        } else if (!buildDynamic(lit, dist)) {
            return false;
        }
        return codes(lit, dist);
    }

    static bool buildFixed(Huffman& lit, Huffman& dist) {
        std::uint8_t litLen[kMaxLCodes];
        for (int i = 0; i < 144; ++i) litLen[i] = 8;
        for (int i = 144; i < 256; ++i) litLen[i] = 9;
        for (int i = 256; i < 280; ++i) litLen[i] = 7;
        for (int i = 280; i < kMaxLCodes; ++i) litLen[i] = 8;
        std::uint8_t distLen[kMaxDCodes];
        for (int i = 0; i < kMaxDCodes; ++i) distLen[i] = 5;
        return lit.build(litLen, kMaxLCodes) && dist.build(distLen, kMaxDCodes);
    }

    bool buildDynamic(Huffman& lit, Huffman& dist) {
        static const int kOrder[kSizeLens] = {16, 17, 18, 0, 8, 7, 9,  6, 10, 5, 11,
                                              4,  12, 3,  13, 2, 14, 1, 15};
        const int hlit = m_br.bits(5) + 257;
        const int hdist = m_br.bits(5) + 1;
        const int hclen = m_br.bits(4) + 4;
        if (m_br.failed) return setErr(m_error, "truncated dynamic block header");
        if (hlit > 286 || hdist > kMaxDCodes) return setErr(m_error, "bad dynamic block counts");

        std::uint8_t codeLenLen[kSizeLens] = {};
        for (int i = 0; i < hclen; ++i) codeLenLen[kOrder[i]] = static_cast<std::uint8_t>(m_br.bits(3));
        if (m_br.failed) return setErr(m_error, "truncated code length code lengths");

        Huffman codeLens;
        if (!codeLens.build(codeLenLen, kSizeLens)) {
            return setErr(m_error, "bad code length huffman table");
        }

        std::uint8_t lengths[kMaxCodes] = {};
        int idx = 0;
        const int total = hlit + hdist;
        while (idx < total) {
            const int sym = codeLens.decode(m_br);
            if (sym < 0) return setErr(m_error, "bad code length symbol");
            if (sym < 16) {
                lengths[idx++] = static_cast<std::uint8_t>(sym);
            } else {
                int repeat = 0;
                std::uint8_t value = 0;
                if (sym == 16) {
                    if (idx == 0) return setErr(m_error, "repeat of an absent length");
                    repeat = 3 + m_br.bits(2);
                    value = lengths[idx - 1];
                } else if (sym == 17) {
                    repeat = 3 + m_br.bits(3);
                } else {
                    repeat = 11 + m_br.bits(7);
                }
                if (m_br.failed) return setErr(m_error, "truncated repeat count");
                while (repeat-- > 0 && idx < total) lengths[idx++] = value;
            }
            if (m_br.failed) return setErr(m_error, "truncated block code lengths");
        }

        if (!lit.build(lengths, hlit)) return setErr(m_error, "bad literal/length code lengths");
        if (!dist.build(lengths + hlit, hdist)) return setErr(m_error, "bad distance code lengths");
        return true;
    }

    bool codes(const Huffman& lit, const Huffman& dist) {
        for (;;) {
            const int sym = lit.decode(m_br);
            if (sym < 0) return setErr(m_error, "bad literal/length symbol");
            if (sym < 256) {
                m_out.push_back(static_cast<std::uint8_t>(sym));
                continue;
            }
            if (sym == 256) return true;
            if (sym > 285) return setErr(m_error, "invalid length symbol");

            const int lc = sym - 257;
            const int length = kLenBase[lc] + m_br.bits(kLenExtra[lc]);
            const int dsym = dist.decode(m_br);
            if (dsym < 0 || dsym >= 30) return setErr(m_error, "bad distance symbol");
            const std::size_t distance =
                static_cast<std::size_t>(kDistBase[dsym]) +
                static_cast<std::size_t>(m_br.bits(kDistExtra[dsym]));
            if (m_br.failed) return setErr(m_error, "truncated length/distance pair");
            if (distance > m_out.size()) return setErr(m_error, "distance before start of output");

            // Index-based, not pointer-based: push_back may reallocate.
            const std::size_t start = m_out.size() - distance;
            m_out.reserve(m_out.size() + static_cast<std::size_t>(length));
            for (int i = 0; i < length; ++i) m_out.push_back(m_out[start + static_cast<std::size_t>(i)]);
        }
    }

    BitReader m_br;
    std::vector<std::uint8_t>& m_out;
    std::string* m_error;
};

}  // namespace

bool inflateZlib(std::string_view src, std::vector<std::uint8_t>& out, std::string* error) {
    out.clear();
    if (error != nullptr) error->clear();
    // 2 header bytes + 1-byte minimum block + 4-byte trailer.
    if (src.size() < 7) return setErr(error, "zlib stream too short");

    const auto cmf = static_cast<std::uint8_t>(src[0]);
    const auto flg = static_cast<std::uint8_t>(src[1]);
    if ((cmf & 0x0F) != 8) return setErr(error, "not a deflate zlib stream");
    if ((static_cast<unsigned>(cmf) << 8 | flg) % 31u != 0) return setErr(error, "bad zlib header check");
    if ((flg & 0x20u) != 0) return setErr(error, "zlib preset dictionary not supported");

    Inflater inflater(reinterpret_cast<const std::uint8_t*>(src.data()) + 2, src.size() - 2, out, error);
    if (!inflater.run()) return false;

    const std::size_t trailer = 2 + inflater.nextByte();
    if (trailer + 4 > src.size()) return setErr(error, "missing zlib adler32 trailer");
    const auto* p = reinterpret_cast<const std::uint8_t*>(src.data()) + trailer;
    const std::uint32_t stored = (static_cast<std::uint32_t>(p[0]) << 24) |
                                 (static_cast<std::uint32_t>(p[1]) << 16) |
                                 (static_cast<std::uint32_t>(p[2]) << 8) |
                                 static_cast<std::uint32_t>(p[3]);
    if (adler32(out) != stored) return setErr(error, "zlib adler32 mismatch");
    return true;
}

bool inflateRaw(std::string_view src, std::vector<std::uint8_t>& out, std::string* error) {
    out.clear();
    if (error != nullptr) error->clear();
    if (src.empty()) return setErr(error, "deflate stream is empty");
    Inflater inflater(reinterpret_cast<const std::uint8_t*>(src.data()), src.size(), out, error);
    return inflater.run();
}

}  // namespace ks::engine::fileformat
