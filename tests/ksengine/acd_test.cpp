// data.acd reader test: the eight key octets against keys recovered from known
// plaintext, a synthetic pack/unpack round-trip (including an empty file and
// the rotation crossing a dash), the corruption/rejection paths, and — when AC
// is installed — the real archive walk plus byte-exact decryption of cars that
// ship both data.acd and a loose data/ directory.

#include "engine/FileFormat/AcdReader.h"
#include "KsTest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace ff = ks::engine::fileformat;

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

std::string readBinary(const std::string& path) {
    std::ifstream stream(fs::u8path(path), std::ios::binary);
    if (!stream) return {};
    return std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
}

std::string encrypt(const std::string& plain, const std::string& key) {
    std::string out(plain.size(), '\0');
    for (std::size_t i = 0; i < plain.size(); ++i) {
        const unsigned c = (static_cast<unsigned char>(plain[i]) +
                            static_cast<unsigned char>(key[i % key.size()])) &
                           0xFFu;
        out[i] = static_cast<char>(c);
    }
    return out;
}

// Builds a data.acd image: nameLen / name / charCount / one u32 per byte.
std::vector<std::uint8_t> buildAcd(
    const std::vector<std::pair<std::string, std::string>>& files,
    const std::string& key) {
    std::vector<std::uint8_t> out;
    for (const auto& [name, plain] : files) {
        const std::string cipher = encrypt(plain, key);
        putU32(out, static_cast<std::uint32_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        putU32(out, static_cast<std::uint32_t>(plain.size()));
        for (const char ch : cipher) {
            putU32(out, static_cast<std::uint32_t>(static_cast<unsigned char>(ch)));
        }
    }
    return out;
}

std::string asString(const std::vector<std::uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Keys recovered from known plaintext (loose data/*.ini vs data.acd) for four
// cars that ship both. They are the independent ground truth for createAcdKey.
struct KnownKey {
    const char* folder;
    const char* key;
};

constexpr KnownKey kKnownKeys[] = {
    {"acfl_2006_bmwsauber", "228-177-90-0-238-61-26-115"},
    {"tatuusfa1", "158-255-251-89-182-162-55-50"},
    {"lotus_exos_125_s1", "79-130-177-160-94-78-13-50"},
    {"dallara_f317", "49-16-225-182-182-109-71-56"},
};

void testKeyDerivation() {
    for (const KnownKey& known : kKnownKeys) {
        const std::string derived = ff::createAcdKey(known.folder);
        KS_CHECK(derived == known.key);
        if (derived != known.key) {
            std::printf("  key mismatch for %s: got %s expected %s\n", known.folder,
                        derived.c_str(), known.key);
        }
    }

    // Case-insensitive, and any leading directory component is dropped.
    KS_CHECK(ff::createAcdKey("ACFL_2006_BMWSAUBER") ==
             ff::createAcdKey("acfl_2006_bmwsauber"));
    KS_CHECK(ff::createAcdKey("C:/Games/AC/content/cars/TatuusFa1") ==
             ff::createAcdKey("tatuusfa1"));
    KS_CHECK(ff::createAcdKey("C:/Games/AC/content/cars/TatuusFa1/") ==
             ff::createAcdKey("tatuusfa1"));
    KS_CHECK(ff::createAcdKey("C:/Games/AC/content/cars/TatuusFa1/data.acd") ==
             ff::createAcdKey("tatuusfa1"));
    KS_CHECK(ff::createAcdKey("content\\cars\\TatuusFa1\\data.acd") ==
             ff::createAcdKey("tatuusfa1"));

    // Sanity on the shape: eight dash-separated decimal octets.
    const std::string key = ff::createAcdKey("abarth500");
    int separators = 0;
    for (const char ch : key) {
        if (ch == '-') {
            ++separators;
            continue;
        }
        KS_CHECK(ch >= '0' && ch <= '9');
    }
    KS_CHECK(separators == 7);
    // Known from the abarth500 aero.ini cipher: first octet is '7' (0x92 - '[').
    KS_CHECK(key.rfind("7-", 0) == 0);
}

void testRoundTrip() {
    const std::string folder = "ks_test_car";
    const std::string key = ff::createAcdKey(folder);

    // Content long enough to run past several octets of the key, so the
    // rotation (and the '-' characters inside it) is actually exercised.
    const std::string aero =
        "[GEARS]\n"
        "CHANGE_UPTIME=0.2\n"
        "CHANGE_DOWNTIME=0.2\n"
        "UPPER_GEAR_RPM=8500\n"
        "LOWER_GEAR_RPM=3800\n"
        "\n"
        "[AERO]\n"
        "CL_FRONT=0.1\n";
    const std::string body = std::string(60, 'x') + std::string(60, 'y');

    const std::vector<std::pair<std::string, std::string>> files = {
        {"aero.ini", aero},
        {"empty.ini", ""},
        {"body.txt", body},
        {"fuel_cons.ini", "0|0.5\n1|1.2\n"},
    };

    const auto image = buildAcd(files, key);
    const std::string blob = asString(image);

    KS_CHECK(ff::looksLikeAcd(blob));
    const ff::AcdArchive archive = ff::parseAcd(blob, folder);
    KS_CHECK(archive.ok());
    KS_CHECK(archive.files.size() == files.size());

    for (const auto& [name, plain] : files) {
        const ff::AcdEntry* entry = archive.find(name);
        KS_CHECK(entry != nullptr);
        if (entry != nullptr) {
            KS_CHECK(entry->data == plain);
            if (entry->data != plain) {
                std::printf("  %s round-trip differs (got %zu bytes, expected %zu)\n",
                            name.c_str(), entry->data.size(), plain.size());
            }
        }
    }
    KS_CHECK(archive.find("missing.ini") == nullptr);

    // The folder name is the key: a different car yields garbage, not an
    // error — parse still succeeds structurally but the content is wrong.
    const ff::AcdArchive wrong = ff::parseAcd(blob, "some_other_car");
    KS_CHECK(wrong.ok());
    if (wrong.files.size() == files.size()) {
        KS_CHECK(wrong.files[0].data != files[0].second);
    }
}

void testRejection() {
    const std::string key = ff::createAcdKey("ks_test_car");

    KS_CHECK(!ff::looksLikeAcd(""));
    KS_CHECK(!ff::looksLikeAcd(std::string(8, '\0')));
    KS_CHECK(!ff::parseAcd("", "ks_test_car").ok());
    KS_CHECK(!ff::parseAcd(std::string(8, '\0'), "ks_test_car").ok());

    const auto good = buildAcd({{"aero.ini", "[AERO]\nCL=1\n"}}, key);
    const std::string goodBlob = asString(good);
    KS_CHECK(ff::looksLikeAcd(goodBlob));
    KS_CHECK(ff::parseAcd(goodBlob, "ks_test_car").ok());

    // Truncated inside the record payload.
    {
        std::string cut = goodBlob;
        cut.resize(cut.size() - 8);
        KS_CHECK(!ff::looksLikeAcd(cut));
        KS_CHECK(!ff::parseAcd(cut, "ks_test_car").ok());
    }

    // Truncated inside the name itself.
    {
        std::string cut = goodBlob;
        cut.resize(cut.size() - goodBlob.size() + 4 + 6); // nameLen + 6 of 8 chars
        KS_CHECK(!ff::looksLikeAcd(cut));
    }

    // Implausible name length (0, and past the 64-byte cap).
    {
        std::vector<std::uint8_t> bad;
        putU32(bad, 0);
        putU32(bad, 0);
        putU32(bad, 0);
        KS_CHECK(!ff::looksLikeAcd(asString(bad)));
    }
    {
        std::vector<std::uint8_t> bad;
        putU32(bad, 1000000);
        putU32(bad, 0);
        putU32(bad, 0);
        KS_CHECK(!ff::looksLikeAcd(asString(bad)));
    }

    // Non-printable record name — a raw binary blob is not an archive.
    {
        std::vector<std::uint8_t> bad;
        putU32(bad, 4);
        bad.push_back(0x01);
        bad.push_back(0x02);
        bad.push_back(0x03);
        bad.push_back(0x04);
        putU32(bad, 0);
        putU32(bad, 0);
        KS_CHECK(!ff::looksLikeAcd(asString(bad)));
    }

    // A record list that never terminates on EOF.
    {
        std::vector<std::uint8_t> bad = good;
        bad.push_back(0x00);
        KS_CHECK(!ff::looksLikeAcd(asString(bad)));
    }

    // Decrypt guards.
    KS_CHECK(ff::decryptAcdData(nullptr, 0, 0, key).empty());
    {
        const std::uint8_t word[4] = {0x41, 0, 0, 0};
        KS_CHECK(ff::decryptAcdData(word, 4, 8, key).empty()); // count * 4 > bytes
        KS_CHECK(ff::decryptAcdData(word, 4, 1, "").empty());  // no key
    }
}

constexpr const char* kCarsDir =
    "F:/SteamLibrary/steamapps/common/assettocorsa/content/cars/";

void testRealWalk() {
    // No key needed: the record list alone must land exactly on EOF.
    const std::string path = std::string(kCarsDir) + "abarth500/data.acd";
    if (!fs::exists(path)) return;

    const std::string blob = readBinary(path);
    KS_CHECK(!blob.empty());
    KS_CHECK(blob.size() == 156587);
    KS_CHECK(ff::looksLikeAcd(blob));

    const ff::AcdArchive archive = ff::parseAcd(blob, "abarth500");
    KS_CHECK(archive.ok());
    if (!archive.ok()) std::printf("  %s\n", archive.error.c_str());
    KS_CHECK(archive.files.size() == 43);

    // Alphabetically ordered names, empty entries preserved.
    if (archive.files.size() == 43) {
        KS_CHECK(archive.files.front().name == "aero.ini");
        KS_CHECK(archive.files.back().name == "wing_rear_AOA_CL.lut");
        const ff::AcdEntry* drs = archive.find("drs.ini");
        KS_CHECK(drs != nullptr);
        if (drs != nullptr) KS_CHECK(drs->data.empty());
    }
}

// Cars that ship both data.acd and an untouched data/ directory: every file
// whose loose length equals the archived charCount must decrypt byte-exact.
void testRealDecrypt() {
    const std::vector<const char*> cars = {"acfl_2006_bmwsauber", "tatuusfa1",
                                           "lotus_exos_125_s1", "dallara_f317"};

    int totalCompared = 0;
    int totalMismatches = 0;

    for (const char* car : cars) {
        const std::string acdPath = std::string(kCarsDir) + car + "/data.acd";
        const std::string dataDir = std::string(kCarsDir) + car + "/data/";
        if (!fs::exists(acdPath) || !fs::exists(dataDir)) continue;

        const ff::AcdArchive archive = ff::parseAcd(readBinary(acdPath), car);
        KS_CHECK(archive.ok());
        if (!archive.ok()) {
            std::printf("  %s: %s\n", car, archive.error.c_str());
            continue;
        }
        KS_CHECK(!archive.files.empty());

        for (const ff::AcdEntry& entry : archive.files) {
            const std::string loose = dataDir + entry.name;
            if (!fs::exists(loose)) continue;
            const std::string expected = readBinary(loose);
            // Length drift means the loose file was edited after packing; the
            // archived copy is simply the older one, so there is nothing to
            // compare. Decryption is length-preserving, so a length match is
            // a fair precondition and never masks a wrong key.
            if (expected.size() != entry.data.size()) continue;

            ++totalCompared;
            if (expected != entry.data) {
                ++totalMismatches;
                if (totalMismatches <= 5) {
                    std::printf("  %s/%s: decrypted content differs\n", car,
                                entry.name.c_str());
                }
            }
        }

        // The folder name really is the seed: decrypting with another car's
        // name must not reproduce the loose file.
        const ff::AcdArchive wrong = ff::parseAcd(readBinary(acdPath), "abarth500");
        if (wrong.ok() && !wrong.files.empty() && !archive.files.empty()) {
            const ff::AcdEntry* looseEntry = archive.find("aero.ini");
            const ff::AcdEntry* wrongEntry = wrong.find("aero.ini");
            if (looseEntry != nullptr && wrongEntry != nullptr &&
                wrongEntry->data.size() == looseEntry->data.size()) {
                KS_CHECK(wrongEntry->data != looseEntry->data);
            }
        }
    }

    std::printf("  real files: %d compared, %d mismatches\n", totalCompared,
                totalMismatches);
    KS_CHECK(totalCompared > 50);
    KS_CHECK(totalMismatches == 0);
}

} // namespace

int main() {
    testKeyDerivation();
    testRoundTrip();
    testRejection();
    testRealWalk();
    testRealDecrypt();
    return KS_TEST_RESULT("acd_test");
}
