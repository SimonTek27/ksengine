#pragma once

// Qt-free reader for the Assetto Corsa `data.acd` car data archive.
//
// The format has no magic number (it starts straight at the first record), so
// identification is structural: walk the record list and require it to land
// exactly on EOF. Verified byte-for-byte against real content in
// content/cars/* (abarth500: 43 records / 156587 bytes, acfl_2006_bmwsauber:
// 70 records / 199569 bytes — both walks end on the last byte).
//
// Record layout (little-endian throughout):
//   u32   nameLen            1..64, ASCII file name including the extension
//   char  name[nameLen]
//   u32   charCount          plaintext length in bytes (== number of words)
//   u32   cipher[charCount]  one encrypted byte per 32-bit word
//
// That "one byte per 32-bit field" packing is why data.acd is ~4x the size of
// the data/ directory it stands in for. charCount matches the length of the
// loose file it was packed from exactly (verified on fuel_cons.ini,
// escmode.ini, mirrors.ini, lods.ini); empty files (drs.ini,
// wing_animations.ini) simply carry charCount = 0.
//
// An optional 8-byte header is tolerated: aluigi's reference extractor reads a
// signed long first and, only when it is negative, consumes a second long
// before the record list begins. Normal files start with a positive nameLen,
// so they parse from offset 0.
//
// Encryption is a byte ROT over the plaintext length with an 8-octet key
// derived from the car folder name:
//   plain[i] = (cipher[i] - key[i % key.length()]) & 0xff
// where `key` is the *formatted key string* ("228-177-90-0-238-61-26-115"),
// not an 8-byte array — the dash characters are part of the rotation. The
// index restarts at 0 for every file in the archive. The folder name is the
// seed: renaming the car directory makes the archive undecryptable.
//
// createAcdKey() implements the eight octet algorithms from aluigi's
// QuickBMS script (assetto_corsa_acd.bms), which is the reference for the
// original game. All eight were cross-checked by hand against keys recovered
// from known plaintext for acfl_2006_bmwsauber, tatuusfa1, lotus_exos_125_s1
// and dallara_f317.
//
// Note the Qt ACDParser in the editor plugin assumes an "ACD\0" magic and a
// fixed 15-file name table; real data.acd files carry neither, so that reader
// does not apply to shipped content and is deliberately not mirrored here.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

inline constexpr std::uint32_t kAcdMaxNameLen = 64;
inline constexpr std::uint32_t kAcdMaxFiles = 4096;

// Derives the ROT key for `folderName`. Case-insensitive; a directory path
// and a `.../<car>/data.acd` path are both accepted and reduce to the car
// folder name. The result is the dash-joined
// "%d-%d-%d-%d-%d-%d-%d-%d" string that the rotation indexes into.
std::string createAcdKey(std::string_view folderName);

// Decrypts `charCount` cipher words starting at `cipher` using `key`.
// `cipherBytes` must be >= charCount * 4.
std::string decryptAcdData(const std::uint8_t* cipher, std::size_t cipherBytes,
                           std::size_t charCount, std::string_view key);

struct AcdEntry {
    std::string name;
    std::string data; // decrypted plaintext bytes (not necessarily NUL-terminated)
};

struct AcdArchive {
    std::vector<AcdEntry> files;
    std::string error; // empty on success, "offset N: ..." on failure

    bool ok() const { return error.empty(); }
    const AcdEntry* find(std::string_view name) const;
};

// True when the bytes walk as an acd record list and end exactly on EOF.
// Purely structural — no key required, since none is needed to parse.
bool looksLikeAcd(std::string_view bytes);

// Parses and decrypts. `folderName` is the car directory name; pass the name,
// not the path.
AcdArchive parseAcd(std::string_view bytes, std::string_view folderName);

// Reads the whole file into memory, then parses it.
AcdArchive parseAcdFile(const std::string& path, std::string_view folderName);

} // namespace ks::engine::fileformat
