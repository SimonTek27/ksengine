#pragma once
#include "AcdReader.h"

// Qt-free writer for the `data.acd` archive, the exact mirror of AcdReader:
//   u32 nameLen | name | u32 charCount | charCount u32 words (one byte each)
// encrypted with the same ROT the reader undoes, keyed on the car folder name
// through createAcdKey(). Byte layout and packing verified against the loose
// data/ of real cars (1st.rto is 67 bytes on disk and charCount 67 in the
// archive; every word carries the byte in its low octet).
//
// Header-only and inline on purpose: the archive format belongs to the reader,
// and this is the writing half of the same two functions, so it costs no new
// translation unit and needs no export macro.

#include <cstdint>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

// Appends a little-endian u32.
inline void appendAcdU32(std::string& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFFu));
    }
}

// cipher[i] = (plain[i] + key[i % key.size()]) & 0xff, the inverse of
// decryptAcdData(). The index restarts at 0 for every file in the archive.
inline std::string encryptAcdData(const std::string& plain, std::string_view key) {
    if (key.empty()) return plain;
    std::string out(plain.size(), '\0');
    for (std::size_t i = 0; i < plain.size(); ++i) {
        const unsigned sum = (static_cast<unsigned char>(plain[i]) +
                              static_cast<unsigned char>(key[i % key.size()])) &
                             0xFFu;
        out[i] = static_cast<char>(sum);
    }
    return out;
}

// Serialises the archive image for `files`. `folderName` is the car directory
// name: the key is derived from it, so the archive only opens next to a folder
// with that name.
inline std::string buildAcdImage(const std::vector<AcdEntry>& files,
                                 std::string_view folderName) {
    const std::string key = createAcdKey(folderName);
    std::string image;
    for (const AcdEntry& entry : files) {
        const std::string cipher = encryptAcdData(entry.data, key);
        appendAcdU32(image, static_cast<std::uint32_t>(entry.name.size()));
        image += entry.name;
        appendAcdU32(image, static_cast<std::uint32_t>(entry.data.size()));
        for (std::size_t i = 0; i < cipher.size(); ++i) {
            appendAcdU32(image, static_cast<std::uint32_t>(
                                    static_cast<unsigned char>(cipher[i])));
        }
    }
    return image;
}

// Packs every regular file of `dataDir` (bare name, sorted, so the image is
// deterministic) into `acdPath`. Returns the number of packed files and leaves
// `error` empty on success.
inline std::size_t writeAcdFromDirectory(const std::filesystem::path& dataDir,
                                         std::string_view folderName,
                                         const std::filesystem::path& acdPath,
                                         std::string* error = nullptr) {
    namespace fs = std::filesystem;
    if (error != nullptr) error->clear();
    std::error_code ec;
    if (!fs::exists(dataDir, ec) || !fs::is_directory(dataDir, ec)) {
        if (error != nullptr) *error = "no data directory at " + dataDir.string();
        return 0;
    }
    std::vector<AcdEntry> files;
    for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) {
            if (error != nullptr) *error = "cannot read " + entry.path().string();
            return 0;
        }
        AcdEntry packed;
        packed.name = name;
        packed.data.assign(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
        files.push_back(std::move(packed));
    }
    if (files.empty()) {
        if (error != nullptr) *error = "nothing to pack in " + dataDir.string();
        return 0;
    }
    std::sort(files.begin(), files.end(),
              [](const AcdEntry& a, const AcdEntry& b) { return a.name < b.name; });
    const std::string image = buildAcdImage(files, folderName);
    std::ofstream out(acdPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) *error = "cannot write " + acdPath.string();
        return 0;
    }
    out.write(image.data(), static_cast<std::streamsize>(image.size()));
    out.close();
    if (!out) {
        if (error != nullptr) *error = "write failed for " + acdPath.string();
        return 0;
    }
    return files.size();
}

} // namespace ks::engine::fileformat
