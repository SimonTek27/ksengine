#include "AcdReader.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ks::engine::fileformat {
namespace {

// Folders are always ASCII; read them as unsigned bytes so names with any
// high-bit character still index safely.
unsigned char charAt(const std::string& name, int i) {
    return static_cast<unsigned char>(name[static_cast<std::size_t>(i)]);
}

int mask8(std::int64_t v) { return static_cast<int>(v & 0xFF); }

// Lowercases and reduces the input to the bare car folder name, so callers
// may pass "abarth500", a directory path, or the data.acd path itself.
std::string normaliseFolder(std::string_view folderName) {
    std::string name(folderName);

    auto lastSeparator = [&name] { return name.find_last_of("/\\"); };

    // Trailing separators would otherwise make the last component empty.
    while (!name.empty() && (name.back() == '/' || name.back() == '\\'))
        name.pop_back();

    // ".../cars/tatuusfa1/data.acd": a file name is not a folder name, and it
    // hides the one that seeds the key. Drop the whole component, not just the
    // ".acd" suffix — otherwise the remainder ends in "data".
    if (name.size() > 4) {
        std::string suffix = name.substr(name.size() - 4);
        for (char& c : suffix)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (suffix == ".acd") {
            const std::size_t slash = lastSeparator();
            if (slash != std::string::npos) {
                name.erase(slash);
            } else {
                name.erase(name.size() - 4); // bare "data.acd" -> "data"
            }
        }
    }

    const std::size_t slash = lastSeparator();
    if (slash != std::string::npos) name.erase(0, slash + 1);

    for (char& c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}

bool readU32(std::string_view bytes, std::size_t pos, std::uint32_t& out) {
    if (pos + 4 > bytes.size()) return false;
    const auto b = reinterpret_cast<const unsigned char*>(bytes.data() + pos);
    out = static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
          (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

bool printable(std::string_view s) {
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Walks the record list from `start`. When `key` is null the payload is left
// undecrypted, which is all looksLikeAcd() needs. Returns an empty string on
// success.
std::string walkAcd(std::string_view bytes, std::size_t start, const std::string* key,
                    AcdArchive* out) {
    const std::size_t size = bytes.size();
    std::size_t pos = start;
    std::size_t count = 0;

    while (pos < size) {
        if (count >= kAcdMaxFiles) return "too many records";

        std::uint32_t nameLen = 0;
        if (!readU32(bytes, pos, nameLen)) return "truncated name length";
        pos += 4;
        if (nameLen == 0 || nameLen > kAcdMaxNameLen) {
            return "implausible name length " + std::to_string(nameLen);
        }
        if (pos + nameLen + 4 > size) return "truncated record name";

        const std::string_view name = bytes.substr(pos, nameLen);
        if (!printable(name)) return "non-printable record name";
        pos += nameLen;

        std::uint32_t charCount = 0;
        if (!readU32(bytes, pos, charCount)) return "truncated char count";
        pos += 4;
        // Overflow-safe form of `pos + charCount * 4 <= size`.
        if (charCount > (size - pos) / 4) return "truncated record payload";

        if (out != nullptr) {
            AcdEntry entry;
            entry.name.assign(name);
            if (key != nullptr) {
                entry.data = decryptAcdData(
                    reinterpret_cast<const std::uint8_t*>(bytes.data() + pos),
                    (size - pos), static_cast<std::size_t>(charCount), *key);
            }
            out->files.push_back(std::move(entry));
        }

        pos += static_cast<std::size_t>(charCount) * 4;
        ++count;
    }

    if (count == 0) return "no records";
    return {};
}

// aluigi's reference extractor reads a signed long and, only when it is
// negative, consumes a second one before the records start.
std::size_t recordStart(std::string_view bytes) {
    std::uint32_t first = 0;
    if (readU32(bytes, 0, first) && static_cast<std::int32_t>(first) < 0) return 8;
    return 0;
}

} // namespace

std::string createAcdKey(std::string_view folderName) {
    const std::string name = normaliseFolder(folderName);
    const int n = static_cast<int>(name.size());
    const auto c = [&](int i) { return static_cast<int>(charAt(name, i)); };

    // Octet 1 — plain sum of the lowercased folder characters.
    std::int64_t k1 = 0;
    for (int i = 0; i < n; ++i) k1 += c(i);

    // Octet 2 — rolling accumulate, two characters per step. Unbounded in the
    // reference (it overflows its 64-bit accumulator on long names), so only
    // the surviving low byte is tracked here.
    int k2 = 0;
    for (int i = 0; i < n - 1; i += 2) k2 = mask8(std::int64_t(k2) * c(i) - c(i + 1));

    // Octet 3 — gated accumulate with a division. The division truncates
    // toward zero (not floor), which is what distinguishes the reference
    // result from the nearest alternative.
    std::int64_t k3 = 0;
    for (int i = 1; i < n - 3; i += 3) {
        const std::int64_t divisor = c(i + 1) + 0x1B;
        if (divisor == 0) continue;
        k3 = (k3 * c(i)) / divisor;
        k3 += -0x1B - c(i - 1);
    }

    // Octet 4 — 0x1683 minus every character after the first.
    std::int64_t k4 = 0x1683;
    for (int i = 1; i < n; ++i) k4 -= c(i);

    // Octet 5 — nested product, four characters per step; low byte only.
    int k5 = 0x42;
    for (int i = 1; i < n - 4; i += 4) {
        const int inner = mask8(std::int64_t(c(i) + 0x0F) * k5);
        k5 = mask8(std::int64_t(c(i - 1) + 0x0F) * inner + 0x16);
    }

    // Octet 6 — alternating subtraction from 0x65, two characters per step.
    std::int64_t k6 = 0x65;
    for (int i = 0; i < n - 2; i += 2) k6 -= c(i);

    // Octet 7 — alternating modulo from 0xab, same cadence as octet 6.
    std::int64_t k7 = 0xAB;
    for (int i = 0; i < n - 2; i += 2) {
        const int divisor = c(i);
        if (divisor != 0) k7 %= divisor;
    }

    // Octet 8 — divide/add ping-pong over adjacent pairs.
    std::int64_t k8 = 0xAB;
    for (int i = 0; i < n - 1; ++i) {
        const int divisor = c(i);
        if (divisor == 0) continue;
        k8 = k8 / divisor + c(i + 1);
    }

    const int values[8] = {mask8(k1), mask8(k2), mask8(k3), mask8(k4),
                           mask8(k5), mask8(k6), mask8(k7), mask8(k8)};

    std::string key = std::to_string(values[0]);
    for (int i = 1; i < 8; ++i) {
        key += '-';
        key += std::to_string(values[i]);
    }
    return key;
}

std::string decryptAcdData(const std::uint8_t* cipher, std::size_t cipherBytes,
                           std::size_t charCount, std::string_view key) {
    std::string out;
    if (cipher == nullptr || charCount == 0 || key.empty()) return out;
    if (cipherBytes < charCount * 4) return out;

    out.resize(charCount);
    for (std::size_t i = 0; i < charCount; ++i) {
        // One plaintext byte per 32-bit word; only the low byte carries data.
        const unsigned plain =
            (cipher[i * 4] - static_cast<unsigned char>(key[i % key.size()])) & 0xFFu;
        out[i] = static_cast<char>(plain);
    }
    return out;
}

const AcdEntry* AcdArchive::find(std::string_view name) const {
    for (const AcdEntry& entry : files) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

bool looksLikeAcd(std::string_view bytes) {
    if (bytes.size() < 16) return false;
    return walkAcd(bytes, recordStart(bytes), nullptr, nullptr).empty();
}

AcdArchive parseAcd(std::string_view bytes, std::string_view folderName) {
    AcdArchive archive;
    if (bytes.size() < 16) {
        archive.error = "file too small";
        return archive;
    }

    const std::string key = createAcdKey(folderName);
    const std::size_t start = recordStart(bytes);
    archive.error = walkAcd(bytes, start, &key, &archive);
    if (!archive.error.empty()) {
        archive.files.clear();
        archive.error = "offset " + std::to_string(start) + ": " + archive.error;
    }
    return archive;
}

AcdArchive parseAcdFile(const std::string& path, std::string_view folderName) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        AcdArchive archive;
        archive.error = "cannot open " + path;
        return archive;
    }
    const std::string bytes((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
    return parseAcd(bytes, folderName);
}

} // namespace ks::engine::fileformat
