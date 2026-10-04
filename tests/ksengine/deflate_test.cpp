// Deflate/zlib decompression test. zlib is not available to this build, so
// the decoder in FileFormat/Deflate.cpp carries the whole FBX compressed-array
// path; it has to handle every block type (stored, fixed, dynamic), the zlib
// wrapper and its adler32, plus the failure modes an out-of-tree asset can
// produce.

#include "engine/FileFormat/Deflate.h"
#include "KsTest.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace ff = ks::engine::fileformat;

// zlib.compress(b"the quick brown fox jumps over the lazy dog " * 30, 6)
// first block: fixed huffman
const char kLettersZlib[] =
    "789c2bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228c94855c8"
    "49acaa5448c94f077346d58eaa1d553baab67884a905001c87e2fe";

// zlib.compress(pattern, 6) with pattern[i] = (i*7 + (i>>3)) & 0xff, 4096 bytes
// first block: dynamic huffman
const char kPatternZlib[] =
    "789cedd5d7570f001c40f1ca485248d9952695945d66a290b2b790a2ec55592142766564b565"
    "561421a46c116546f62a5a5a42b6f33de7fe177eeff7edf370955435755a1a99b7b7b5eb3f78"
    "94abc7cc854b57ad0f0a0d8f8d3f79ee524676ce8bbce2caef4aaa9a3abac6161da4193d71ea"
    "2c698277451c90e6e6ddc72fa551ae53bfb134ddfa0c18228df7b2d51ba449483e7f599afc92"
    "cf3fa4d13369db519a3193a6cd96266477e441696edd7bf24a1a15b5064da4e96e3f70a8343e"
    "7e011ba539762af58a341f3e55fd9446dfd4b2933463277bce9166db9ea843d264decf7d2d4d"
    "8dba0d9b4ad3a3afd330697c97afd924cdf1d317ae4af3b1f4cb2f695ab56ed7599a716e5e73"
    "a5d9be37fab034b71f3c7d234d4d75ad66d2f4ec3768b8348b56acdd2c4de299b46bd214947d"
    "fd2d8d411bab2ed28c9f327d9e343bf6c51c91e6cec3676fa5a955af5173697a39388f9066f1"
    "ca755ba4494a49bf2e4d61f9b73fd2189a5977956682fb8cf9d2ec0cdb7f549aac47cfdf4953"
    "5b43bb8534bd1d5d464ab3c43f70ab3427ce5ebc214d5145f55f698cccdbdb48e3ea31738134"
    "a1e1b171d264e7bc782f0dec36b02f803d0ef6f7b0b7847d14ec41b067c0ae04bb2dec0b618f"
    "873d0f765dd847c31e0cfb4dd89561ef06bb37ec09b0e7c3ae07fb18d84360bf05bb0aecdd61"
    "f781fd18ec1f60d7877d2cecdb60cf84bd06ec3d60f785fd38ec1f616f05fb38d8b7c37e1bf6"
    "9ab0f7847d11ec89b017c06e00fb78d877c07e07f65ab0f7827d31ec49b017c26e08fb04d877"
    "c29e057b6dd87bc3be04f613b017c16e04bb2beca1b067c3ae0abb1dec4b613f097b31ecc6b0"
    "4f847d17ec7761af037b1fd897c19e0c7b09ec26b04f827d37ecf7605783dd1e763fd84fc1fe"
    "097653d827c3be07f6fbb0d785bd2feccb613f0d7b29ecad6177837d2fec0f605787bd1fec2b"
    "603f037b19ec6d609f02fb3ed81fc25e0f7607d857c29e027b39ec66b0bbc31e06fb23d83560"
    "7784dd1ff6b3b057c06e0ebb07ece1b0e7c0ae097b7fd857c17e0ef64ad82d609f0a7b04ec8f"
    "61af0ffb00d857c37e1ef6cfb0b7857d1aec91b03f81bd01ec03610f803d15f62ad82d61f784"
    "3d0af65cd81bc2ee04fb1ad82fc0fe05f676b07bc11e0dfb53d8b5601f04fb5ad8d360ff0abb"
    "15ecd3618f81fd19ec8d6077867d1dece9b07f83dd1af619b0ef87fd39ecdab0bbc01e08fb45"
    "d8ab6157fc5ff17fc5ff15ff57fc5ff17fc5ff7dff9fffff03c516f86a";

// Same payload as a headerless raw DEFLATE stream (inflateRaw).
const char kPatternRaw[] =
    "edd5d7570f001c40f1ca485248d9952695945d66a290b2b790a2ec55592142766564b5655614"
    "21a46c116546f62a5a5a42b6f33de7fe177eeff7edf370955435755a1a99b7b7b5eb3f7894ab"
    "c7cc854b57ad0f0a0d8f8d3f79ee524676ce8bbce2caef4aaa9a3abac6161da4193d71ea2c69"
    "8277451c90e6e6ddc72fa551ae53bfb134ddfa0c18228df7b2d51ba449483e7f599afc92cf3f"
    "a4d13369db519a3193a6cd96266477e441696edd7bf24a1a15b5064da4e96e3f70a8343e7e01"
    "1ba539762af58a341f3e55fd9446dfd4b2933463277bce9166db9ea843d264decf7d2d4d8dba"
    "0d9b4ad3a3afd330697c97afd924cdf1d317ae4af3b1f4cb2f695ab56ed7599a716e5e73a5d9"
    "be37fab034b71f3c7d234d4d75ad66d2f4ec3768b8348b56acdd2c4de299b46bd214947dfd2d"
    "8d411bab2ed28c9f327d9e343bf6c51c91e6cec3676fa5a955af5173697a39388f9066f1ca75"
    "5ba4494a49bf2e4d61f9b73fd2189a5977956682fb8cf9d2ec0cdb7f549aac47cfdf49535b43"
    "bb8534bd1d5d464ab3c43f70ab3427ce5ebc214d5145f55f698cccdbdb48e3ea31738134a1e1"
    "b171d264e7bc782f0dec36b02f803d0ef6f7b0b7847d14ec41b067c0ae04bb2dec0b618f873d"
    "0f765dd847c31e0cfb4dd89561ef06bb37ec09b0e7c3ae07fb18d84360bf05bb0aecdd61f781"
    "fd18ec1f60d7877d2cecdb60cf84bd06ec3d60f785fd38ec1f616f05fb38d8b7c37e1bf69ab0"
    "f7847d11ec89b017c06e00fb78d877c07e07f65ab0f7827d31ec49b017c26e08fb04d877c29e"
    "057b6dd87bc3be04f613b017c16e04bb2beca1b067c3ae0abb1dec4b613f097b31ecc6b04f84"
    "7d17ec7761af037b1fd897c19e0c7b09ec26b04f827d37ecf7605783dd1e763fd84fc1fe0976"
    "53d827c3be07f6fbb0d785bd2feccb613f0d7b29ecad6177837d2fec0f605787bd1fec2b603f"
    "037b19ec6d609f02fb3ed81fc25e0f7607d857c29e027b39ec66b0bbc31e06fb23d835607784"
    "dd1ff6b3b057c06e0ebb07ece1b0e7c0ae097b7fd857c17e0ef64ad82d609f0a7b04ec8f61af"
    "0ffb00d857c37e1ef6cfb0b7857d1aec91b03f81bd01ec03610f803d15f62ad82d61f7843d0a"
    "f65cd81bc2ee04fb1ad82fc0fe05f676b07bc11e0dfb53d8b5601f04fb5ad8d360ff0abb15ec"
    "d3618f81fd19ec8d6077867d1dece9b07f83dd1af619b0ef87fd39ecdab0bbc01e08fb45d8ab"
    "6157fc5ff17fc5ff15ff57fc5ff17fc5ff7dff9fffff03";

std::uint8_t nibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
    return 0;
}

std::vector<std::uint8_t> fromHex(const char* hex) {
    std::vector<std::uint8_t> out;
    for (const char* p = hex; *p != '\0' && p[1] != '\0'; p += 2) {
        out.push_back(static_cast<std::uint8_t>((nibble(p[0]) << 4) | nibble(p[1])));
    }
    return out;
}

std::string asString(const std::vector<std::uint8_t>& v) {
    return std::string(reinterpret_cast<const char*>(v.data()), v.size());
}

std::vector<std::uint8_t> patternPayload() {
    std::vector<std::uint8_t> p(4096);
    for (std::size_t i = 0; i < p.size(); ++i) {
        p[i] = static_cast<std::uint8_t>((i * 7 + (i >> 3)) & 0xFF);
    }
    return p;
}

std::string lettersPayload() {
    std::string s;
    for (int i = 0; i < 30; ++i) s += "the quick brown fox jumps over the lazy dog ";
    return s;
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

// Wraps a raw DEFLATE stream in a zlib container with a correct adler32.
std::vector<std::uint8_t> wrapZlib(const std::vector<std::uint8_t>& raw,
                                    const std::vector<std::uint8_t>& plain) {
    std::vector<std::uint8_t> out;
    out.push_back(0x78);
    out.push_back(0x01);
    out.insert(out.end(), raw.begin(), raw.end());
    const std::uint32_t ad = adler32(plain);
    out.push_back(static_cast<std::uint8_t>(ad >> 24));
    out.push_back(static_cast<std::uint8_t>(ad >> 16));
    out.push_back(static_cast<std::uint8_t>(ad >> 8));
    out.push_back(static_cast<std::uint8_t>(ad));
    return out;
}

// A stored block header is 3 bits followed by padding to the byte boundary,
// so a byte-aligned stream writes it as a single byte: bfinal in bit 0, btype
// 00 in bits 1-2.
void appendStoredBlock(std::vector<std::uint8_t>& stream, bool isFinal,
                       const std::uint8_t* data, std::size_t len) {
    const auto l = static_cast<std::uint16_t>(len & 0xFFFFu);
    const auto n = static_cast<std::uint16_t>(~l);
    stream.push_back(isFinal ? 1 : 0);
    stream.push_back(static_cast<std::uint8_t>(l & 0xFFu));
    stream.push_back(static_cast<std::uint8_t>(l >> 8));
    stream.push_back(static_cast<std::uint8_t>(n & 0xFFu));
    stream.push_back(static_cast<std::uint8_t>(n >> 8));
    stream.insert(stream.end(), data, data + len);
}

std::vector<std::uint8_t> storedStream(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> stream;
    std::size_t off = 0;
    if (payload.empty()) {
        appendStoredBlock(stream, true, nullptr, 0);
        return stream;
    }
    while (off < payload.size()) {
        const std::size_t chunk = payload.size() - off > 65535 ? 65535 : payload.size() - off;
        const bool last = off + chunk >= payload.size();
        appendStoredBlock(stream, last, payload.data() + off, chunk);
        off += chunk;
    }
    return stream;
}

struct BitPacker {
    std::vector<std::uint8_t> bytes;
    std::uint32_t acc = 0;
    int n = 0;

    void put(std::uint32_t value, int k) {
        acc |= (value & ((1u << k) - 1u)) << n;
        n += k;
        while (n >= 8) {
            bytes.push_back(static_cast<std::uint8_t>(acc & 0xFFu));
            acc >>= 8;
            n -= 8;
        }
    }
    void flush() {
        if (n > 0) {
            bytes.push_back(static_cast<std::uint8_t>(acc & 0xFFu));
            acc = 0;
            n = 0;
        }
    }
};

std::uint32_t reverseBits(std::uint32_t value, int k) {
    std::uint32_t r = 0;
    for (int i = 0; i < k; ++i) r = (r << 1) | ((value >> i) & 1u);
    return r;
}

// Fixed literal/length code (RFC 1951 section 3.2.6).
void putFixedSymbol(BitPacker& bw, int sym) {
    int len = 0;
    std::uint32_t code = 0;
    if (sym < 144) {
        len = 8;
        code = 0x30u + static_cast<std::uint32_t>(sym);
    } else if (sym < 256) {
        len = 9;
        code = 0x190u + static_cast<std::uint32_t>(sym - 144);
    } else if (sym < 280) {
        len = 7;
        code = static_cast<std::uint32_t>(sym - 256);
    } else {
        len = 8;
        code = 0xC0u + static_cast<std::uint32_t>(sym - 280);
    }
    bw.put(reverseBits(code, len), len);
}

void putFixedHeader(BitPacker& bw, bool isFinal) {
    bw.put(isFinal ? 1u : 0u, 1);
    bw.put(1u, 2);  // btype = 1, fixed huffman
}

void testStored() {
    std::vector<std::uint8_t> payload;
    for (std::size_t i = 0; i < 70000; ++i) payload.push_back(static_cast<std::uint8_t>(i & 0xFF));

    const std::vector<std::uint8_t> raw = storedStream(payload);
    // Two blocks: 65535 bytes then 4465, so the 16-bit LEN field is exercised.
    std::size_t blocks = 0;
    std::size_t off = 0;
    while (off < raw.size()) {
        ++blocks;
        off += 5 + (static_cast<std::size_t>(raw[off + 1]) | (static_cast<std::size_t>(raw[off + 2]) << 8));
    }
    KS_CHECK(blocks == 2);

    std::vector<std::uint8_t> out;
    std::string err;
    KS_CHECK(ff::inflateRaw(asString(raw), out, &err));
    KS_CHECK(err.empty());
    KS_CHECK(out == payload);

    // The same stream inside a zlib container.
    const std::vector<std::uint8_t> wrapped = wrapZlib(raw, payload);
    KS_CHECK(ff::inflateZlib(asString(wrapped), out, &err));
    KS_CHECK(out == payload);
}

void testFixed() {
    std::vector<std::uint8_t> out;
    std::string err;

    // Every byte value as a literal: covers the 8-bit (0..143), 9-bit (144..255)
    // and 7-bit end-of-block ranges of the fixed code.
    BitPacker bw;
    putFixedHeader(bw, true);
    for (int sym = 0; sym < 256; ++sym) putFixedSymbol(bw, sym);
    putFixedSymbol(bw, 256);
    bw.flush();

    KS_CHECK(ff::inflateRaw(asString(bw.bytes), out, &err));
    KS_CHECK(err.empty());
    KS_CHECK(out.size() == 256);
    bool ascending = true;
    for (std::size_t i = 0; i < out.size() && i < 256; ++i) {
        if (out[i] != static_cast<std::uint8_t>(i)) ascending = false;
    }
    KS_CHECK(ascending);

    // "abc" then a length-6/distance-3 back reference -> "abcabcabc".
    BitPacker bw2;
    putFixedHeader(bw2, true);
    putFixedSymbol(bw2, 'a');
    putFixedSymbol(bw2, 'b');
    putFixedSymbol(bw2, 'c');
    putFixedSymbol(bw2, 260);            // length 6, no extra bits
    bw2.put(reverseBits(2, 5), 5);       // distance code 2 -> distance 3
    putFixedSymbol(bw2, 256);
    bw2.flush();

    out.clear();
    KS_CHECK(ff::inflateRaw(asString(bw2.bytes), out, &err));
    KS_CHECK(err.empty());
    KS_CHECK(asString(out) == "abcabcabc");
}

void testZlibFixedBlock() {
    const std::vector<std::uint8_t> compressed = fromHex(kLettersZlib);
    const std::string plain = lettersPayload();

    std::vector<std::uint8_t> out;
    std::string err;
    KS_CHECK(ff::inflateZlib(asString(compressed), out, &err));
    if (!err.empty()) std::printf("  letters: %s\n", err.c_str());
    KS_CHECK(err.empty());
    KS_CHECK(asString(out) == plain);
}

void testDynamicBlocks() {
    const std::string pattern = asString(patternPayload());

    std::vector<std::uint8_t> out;
    std::string err;
    KS_CHECK(ff::inflateZlib(asString(fromHex(kPatternZlib)), out, &err));
    if (!err.empty()) std::printf("  pattern zlib: %s\n", err.c_str());
    KS_CHECK(err.empty());
    KS_CHECK(asString(out) == pattern);

    out.clear();
    err.clear();
    KS_CHECK(ff::inflateRaw(asString(fromHex(kPatternRaw)), out, &err));
    if (!err.empty()) std::printf("  pattern raw: %s\n", err.c_str());
    KS_CHECK(err.empty());
    KS_CHECK(asString(out) == pattern);
}

void testFailures() {
    const std::vector<std::uint8_t> letters = fromHex(kLettersZlib);
    std::vector<std::uint8_t> out;
    std::string err;

    KS_CHECK(!ff::inflateZlib("", out, &err));
    KS_CHECK(!err.empty());

    err.clear();
    KS_CHECK(!ff::inflateZlib(std::string(3, '\0'), out, &err));
    KS_CHECK(!err.empty());

    // CM must be 8 (deflate).
    err.clear();
    KS_CHECK(!ff::inflateZlib(std::string("\x19\x01\x00\x00\x00\x01\x00", 7), out, &err));
    KS_CHECK(err == "not a deflate zlib stream");

    // CMF/FLG must be a multiple of 31.
    err.clear();
    KS_CHECK(!ff::inflateZlib(std::string("\x78\x00\x00\x00\x00\x01\x00", 7), out, &err));
    KS_CHECK(err == "bad zlib header check");

    // FDICT (preset dictionary) is not supported.
    err.clear();
    KS_CHECK(!ff::inflateZlib(std::string("\x78\x20\x00\x00\x00\x01\x00", 7), out, &err));
    KS_CHECK(err == "zlib preset dictionary not supported");

    // Truncated mid-stream.
    err.clear();
    KS_CHECK(!ff::inflateZlib(asString(std::vector<std::uint8_t>(letters.begin(), letters.begin() + 30)),
                              out, &err));
    KS_CHECK(!err.empty());

    // Truncated trailer.
    err.clear();
    KS_CHECK(!ff::inflateZlib(asString(std::vector<std::uint8_t>(letters.begin(), letters.begin() + 62)),
                              out, &err));
    KS_CHECK(err == "missing zlib adler32 trailer");

    // Corrupt trailer -> adler32 mismatch, not silent garbage.
    std::vector<std::uint8_t> corrupt = letters;
    corrupt.back() ^= 0xFF;
    err.clear();
    KS_CHECK(!ff::inflateZlib(asString(corrupt), out, &err));
    KS_CHECK(err == "zlib adler32 mismatch");

    // btype = 3 is reserved.
    err.clear();
    KS_CHECK(!ff::inflateRaw(std::string("\x07", 1), out, &err));
    KS_CHECK(err == "invalid deflate block type");

    // A back reference that runs before the start of the output.
    BitPacker bw;
    putFixedHeader(bw, true);
    putFixedSymbol(bw, 257);  // length 3
    bw.put(reverseBits(0, 5), 5);  // distance 1 with an empty output
    bw.flush();
    err.clear();
    KS_CHECK(!ff::inflateRaw(asString(bw.bytes), out, &err));
    KS_CHECK(err == "distance before start of output");

    // Raw input that stops before the stream is finished.
    err.clear();
    KS_CHECK(!ff::inflateRaw("", out, &err));
    KS_CHECK(err == "deflate stream is empty");
}

}  // namespace

int main() {
    testStored();
    testFixed();
    testZlibFixedBlock();
    testDynamicBlocks();
    testFailures();
    return KS_TEST_RESULT("deflate_test");
}
