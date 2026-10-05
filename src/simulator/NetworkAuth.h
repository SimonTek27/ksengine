#pragma once
/**
 * Application-level auth for ksnet (P2).
 * Empty token = open server (LAN legacy). Non-empty = required on ClientJoin.
 * Constant-time compare; no plaintext logging of tokens.
 */
#include <cstdint>
#include <cstring>
#include <string>

namespace ks {
namespace sim {
namespace net {

inline constexpr size_t kAuthTokenMax = 64;

/** Constant-time equality for up to n bytes (null-terminated safe). */
inline bool tokenEquals(const char* a, const char* b, size_t n = kAuthTokenMax) {
    unsigned char diff = 0;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        diff |= static_cast<unsigned char>(ca ^ cb);
        if (ca == 0 && cb == 0) break;
    }
    return diff == 0;
}

inline bool tokenEquals(const std::string& a, const std::string& b) {
    char bufA[kAuthTokenMax] = {};
    char bufB[kAuthTokenMax] = {};
    std::strncpy(bufA, a.c_str(), kAuthTokenMax - 1);
    std::strncpy(bufB, b.c_str(), kAuthTokenMax - 1);
    return tokenEquals(bufA, bufB, kAuthTokenMax);
}

/** Simple FNV-1a 64-bit hash (for lobby fingerprints, not password storage). */
inline uint64_t fnv1a64(const char* s, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

inline uint64_t tokenFingerprint(const std::string& token) {
    if (token.empty()) return 0;
    return fnv1a64(token.c_str(), token.size());
}

} // namespace net
} // namespace sim
} // namespace ks
