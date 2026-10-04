// KSAnim reader test: round-trips synthetic v1/v2 images through parse +
// sampling, checks the quaternion->matrix layout (KN5-compatible, translation
// at [12..14]), the corruption paths, and (when AC is installed) smokes a
// real v1 and a real v2 file.

#include "engine/FileFormat/KsAnim.h"
#include "KsTest.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using ks::engine::fileformat::KsAnimEntry;
using ks::engine::fileformat::KsAnimFile;
using ks::engine::fileformat::KsAnimKeyframe;
using ks::engine::fileformat::KsAnimParseResult;

constexpr float kSqrt1Over2 = 0.70710678f;

void putI32(std::vector<std::uint8_t>& out, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 24) & 0xFF));
}

void putF32(std::vector<std::uint8_t>& out, float v) {
    static_assert(sizeof(float) == 4, "ksanim assumes 4-byte float");
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&v);
    out.insert(out.end(), bytes, bytes + 4);
}

void putName(std::vector<std::uint8_t>& out, const std::string& name) {
    putI32(out, static_cast<std::int32_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
}

void putKeyframe(std::vector<std::uint8_t>& out, float qx, float qy, float qz,
                 float qw, float tx, float ty, float tz, float sx, float sy,
                 float sz) {
    const float values[10] = {qx, qy, qz, qw, tx, ty, tz, sx, sy, sz};
    for (float value : values) putF32(out, value);
}

void putMatrix(std::vector<std::uint8_t>& out,
               const std::array<float, 16>& m) {
    for (float value : m) putF32(out, value);
}

std::array<float, 16> identityMatrix() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

std::array<float, 16> translationMatrix(float tx, float ty, float tz) {
    auto m = identityMatrix();
    m[12] = tx;
    m[13] = ty;
    m[14] = tz;
    return m;
}

// version 2, two entries: one animated, one with zero keyframes.
std::vector<std::uint8_t> buildV2() {
    std::vector<std::uint8_t> out;
    putI32(out, 2);
    putI32(out, 2);

    putName(out, "node_a");
    putI32(out, 3);
    putKeyframe(out, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1);      // identity at origin
    putKeyframe(out, 0, 0, 0, 1, 10, 0, 0, 1, 1, 1);     // +10 on X
    putKeyframe(out, 0, 0, kSqrt1Over2, kSqrt1Over2,     // 90 deg about Z
                20, 0, 0, 1, 1, 1);

    putName(out, "node_empty");
    putI32(out, 0);
    return out;
}

// version 1, one entry with two whole matrices.
std::vector<std::uint8_t> buildV1() {
    std::vector<std::uint8_t> out;
    putI32(out, 1);
    putI32(out, 1);
    putName(out, "m1");
    putI32(out, 2);
    putMatrix(out, identityMatrix());
    putMatrix(out, translationMatrix(7.0f, -3.0f, 1.5f));
    return out;
}

std::string asString(const std::vector<std::uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void checkClose(const char* what, float actual, float expected,
                        float eps = 1e-4f) {
    ::ks::test::report(std::fabs(actual - expected) <= eps, what, __FILE__,
                       __LINE__);
}

void testV2() {
    const KsAnimParseResult parsed = ks::engine::fileformat::parseKsAnim(
        asString(buildV2()));
    KS_CHECK(parsed.ok());
    if (!parsed.ok()) {
        std::fprintf(stderr, "v2 parse error: %s\n", parsed.error.c_str());
        return;
    }
    KS_CHECK(parsed.file.version == 2);
    KS_CHECK(parsed.file.entries.size() == 2);
    if (parsed.file.entries.size() != 2) return;

    const KsAnimEntry* entry = parsed.file.find("node_a");
    KS_CHECK(entry != nullptr);
    if (entry == nullptr) return;
    KS_CHECK(entry->keyframeCount() == 3);
    KS_CHECK(entry->keyframes.size() == 3);
    if (entry->keyframes.size() != 3) return;
    checkClose("kf0 scale x", entry->keyframes[0].scale[0], 1.0f);
    checkClose("kf1 tx", entry->keyframes[1].translation[0], 10.0f);
    checkClose("kf2 qw", entry->keyframes[2].rotation[3], kSqrt1Over2);

    // Zero-keyframe entries are kept, not silently dropped.
    const KsAnimEntry* empty = parsed.file.find("node_empty");
    KS_CHECK(empty != nullptr);
    if (empty != nullptr) KS_CHECK(empty->keyframeCount() == 0);

    KS_CHECK(parsed.file.totalKeyframes() == 3);
    KS_CHECK(parsed.file.animatedNodes().size() == 2);

    std::array<float, 16> m{};
    // t = 0: first keyframe.
    KS_CHECK(parsed.file.sample("node_a", 0.0f, m));
    checkClose("t0 tx", m[12], 0.0f);
    checkClose("t0 m0", m[0], 1.0f);

    // t = 0.25: halfway between kf0 and kf1 -> x = 5, rotation still the
    // identity (both keyframes are identity).
    KS_CHECK(parsed.file.sample("node_a", 0.25f, m));
    checkClose("t.25 tx", m[12], 5.0f);
    checkClose("t.25 m0", m[0], 1.0f);
    checkClose("t.25 m1", m[1], 0.0f);
    checkClose("t.25 m4", m[4], 0.0f);
    checkClose("t.25 m5", m[5], 1.0f);
    checkClose("t.25 m15", m[15], 1.0f);
    checkClose("t.25 m3", m[3], 0.0f);
    checkClose("t.25 m7", m[7], 0.0f);
    checkClose("t.25 m11", m[11], 0.0f);

    // t = 0.5 lands exactly on kf1.
    KS_CHECK(parsed.file.sample("node_a", 0.5f, m));
    checkClose("t.5 tx", m[12], 10.0f);
    checkClose("t.5 m0", m[0], 1.0f);

    // t = 0.75: halfway between identity (kf1) and 90 deg about Z (kf2) ->
    // a 45 deg slerp, with translation lerped 10 -> 20.
    KS_CHECK(parsed.file.sample("node_a", 0.75f, m));
    checkClose("t.75 tx", m[12], 15.0f);
    checkClose("t.75 m0", m[0], kSqrt1Over2);
    checkClose("t.75 m1", m[1], -kSqrt1Over2);
    checkClose("t.75 m4", m[4], kSqrt1Over2);
    checkClose("t.75 m5", m[5], kSqrt1Over2);

    // t = 1 clamps to the last keyframe (90 deg about Z, x = 20).
    KS_CHECK(parsed.file.sample("node_a", 1.0f, m));
    checkClose("t1 tx", m[12], 20.0f);
    checkClose("t1 m0", m[0], 0.0f);
    checkClose("t1 m1", m[1], -1.0f);
    checkClose("t1 m4", m[4], 1.0f);

    // Out-of-range and unknown nodes leave the buffer untouched.
    KS_CHECK(parsed.file.sample("node_a", -5.0f, m));
    checkClose("t-5 tx", m[12], 0.0f);
    std::array<float, 16> untouched{};
    untouched[12] = -1.0f;
    KS_CHECK(!parsed.file.sample("nope", 0.0f, untouched));
    KS_CHECK(untouched[12] == -1.0f);
    KS_CHECK(!parsed.file.sample("node_empty", 0.5f, untouched));
}

void testV1() {
    const KsAnimParseResult parsed = ks::engine::fileformat::parseKsAnim(
        asString(buildV1()));
    KS_CHECK(parsed.ok());
    if (!parsed.ok()) return;
    KS_CHECK(parsed.file.version == 1);
    KS_CHECK(parsed.file.entries.size() == 1);
    if (parsed.file.entries.empty()) return;

    const KsAnimEntry* entry = parsed.file.find("m1");
    KS_CHECK(entry != nullptr);
    if (entry == nullptr) return;
    KS_CHECK(entry->keyframeCount() == 2);
    KS_CHECK(entry->matrices.size() == 2);

    std::array<float, 16> m{};
    KS_CHECK(parsed.file.sample("m1", 0.0f, m));
    checkClose("v1 t0 tx", m[12], 0.0f);
    KS_CHECK(parsed.file.sample("m1", 0.4f, m)); // nearest is keyframe 0
    checkClose("v1 t.4 tx", m[12], 0.0f);
    KS_CHECK(parsed.file.sample("m1", 0.6f, m)); // nearest is keyframe 1
    checkClose("v1 t.6 tx", m[12], 7.0f);
    checkClose("v1 t.6 ty", m[13], -3.0f);
    checkClose("v1 t.6 tz", m[14], 1.5f);
    KS_CHECK(parsed.file.sample("m1", 1.0f, m));
    checkClose("v1 t1 tx", m[12], 7.0f);
}

void testKeyframeToMatrix() {
    KsAnimKeyframe kf;
    kf.scale = {2.0f, 3.0f, 4.0f};
    kf.translation = {1.0f, 2.0f, 3.0f};
    const std::array<float, 16> m = kf.toMatrix();
    checkClose("m00", m[0], 2.0f);
    checkClose("m11", m[5], 3.0f);
    checkClose("m22", m[10], 4.0f);
    checkClose("tx", m[12], 1.0f);
    checkClose("ty", m[13], 2.0f);
    checkClose("tz", m[14], 3.0f);
    checkClose("m03", m[3], 0.0f);
    checkClose("m13", m[7], 0.0f);
    checkClose("m23", m[11], 0.0f);
    checkClose("m33", m[15], 1.0f);
}

void testRejection() {
    KS_CHECK(!ks::engine::fileformat::isKsAnim(""));
    KS_CHECK(!ks::engine::fileformat::isKsAnim(std::string("\x05\x00\x00\x00", 4)));
    KS_CHECK(ks::engine::fileformat::isKsAnim(std::string("\x02\x00\x00\x00", 4)));

    KS_CHECK(!ks::engine::fileformat::parseKsAnim("").ok());
    KS_CHECK(!ks::engine::fileformat::parseKsAnim(std::string("\x02\x00", 2)).ok());

    // Unsupported version.
    {
        std::vector<std::uint8_t> bad;
        putI32(bad, 7);
        putI32(bad, 0);
        const auto parsed = ks::engine::fileformat::parseKsAnim(asString(bad));
        KS_CHECK(!parsed.ok());
        KS_CHECK(parsed.error.find("version 7") != std::string::npos);
    }

    // Entry count larger than the buffer can hold.
    {
        std::vector<std::uint8_t> bad;
        putI32(bad, 2);
        putI32(bad, 100000);
        putName(bad, "x");
        putI32(bad, 0);
        KS_CHECK(!ks::engine::fileformat::parseKsAnim(asString(bad)).ok());
    }

    // Implausible node name length.
    {
        std::vector<std::uint8_t> bad;
        putI32(bad, 2);
        putI32(bad, 1);
        putI32(bad, 0x7FFFFFFF);
        const auto parsed = ks::engine::fileformat::parseKsAnim(asString(bad));
        KS_CHECK(!parsed.ok());
        KS_CHECK(parsed.error.find("node name") != std::string::npos);
    }

    // Keyframes that run past the end of the buffer.
    {
        std::vector<std::uint8_t> bad;
        putI32(bad, 2);
        putI32(bad, 1);
        putName(bad, "node");
        putI32(bad, 1000); // 1000 * 40 bytes claimed, none supplied
        const auto parsed = ks::engine::fileformat::parseKsAnim(asString(bad));
        KS_CHECK(!parsed.ok());
        KS_CHECK(parsed.error.find("keyframes") != std::string::npos);
    }

    // Truncated in the middle of a v1 matrix.
    {
        std::vector<std::uint8_t> full = buildV1();
        full.resize(full.size() - 8);
        KS_CHECK(!ks::engine::fileformat::parseKsAnim(asString(full)).ok());
    }
}

void testRealFiles() {
    const std::string base =
        "F:/SteamLibrary/steamapps/common/assettocorsa/content/cars/abarth500/animations/";

    // Real v1: 104 entries of 100 keyframes each, walk must land on EOF.
    const std::string v1_path = base + "car_door_L.ksanim";
    if (fs::exists(v1_path)) {
        const KsAnimParseResult parsed =
            ks::engine::fileformat::parseKsAnimFile(v1_path);
        KS_CHECK(parsed.ok());
        if (parsed.ok()) {
            KS_CHECK(parsed.file.version == 1);
            KS_CHECK(parsed.file.entries.size() == 104);
            KS_CHECK(parsed.file.totalKeyframes() > 0);

            const KsAnimEntry* door = parsed.file.find("DOOR_R");
            KS_CHECK(door != nullptr);
            if (door != nullptr && door->matrices.size() == 100) {
                const auto& m = door->matrices.front();
                checkClose("door tx", m[12], -0.7979f, 1e-3f);
                checkClose("door ty", m[13], 0.6255f, 1e-3f);
                checkClose("door tz", m[14], 0.3868f, 1e-3f);
                checkClose("door m0", m[0], 1.0f);
            }
            std::array<float, 16> sampled{};
            KS_CHECK(parsed.file.sample("DOOR_R", 0.5f, sampled));
            checkClose("door sample ty", sampled[13], 0.6255f, 1e-3f);
            KS_CHECK(!parsed.file.sample("MISSING_NODE", 0.0f, sampled));
        }
    }

    // Real v2: quaternion/translation/scale entries.
    const std::string v2_path = base + "shift.ksanim";
    if (fs::exists(v2_path)) {
        const KsAnimParseResult parsed =
            ks::engine::fileformat::parseKsAnimFile(v2_path);
        KS_CHECK(parsed.ok());
        if (parsed.ok()) {
            KS_CHECK(parsed.file.version == 2);
            KS_CHECK(!parsed.file.entries.empty());
            KS_CHECK(parsed.file.totalKeyframes() > 0);

            // Every sampled matrix must stay affine: last row exactly
            // (tx, ty, tz, 1) with an identity upper-left w column.
            for (const KsAnimEntry& entry : parsed.file.entries) {
                if (entry.keyframeCount() == 0) continue;
                std::array<float, 16> m{};
                KS_CHECK(parsed.file.sample(entry.name, 0.37f, m));
                checkClose("affine w-col 3", m[3], 0.0f);
                checkClose("affine w-col 7", m[7], 0.0f);
                checkClose("affine w-col 11", m[11], 0.0f);
                checkClose("affine w", m[15], 1.0f);
                KS_CHECK(std::isfinite(m[12]) && std::isfinite(m[13]) &&
                         std::isfinite(m[14]));
            }
        }
    }
}

} // namespace

int main() {
    testV2();
    testV1();
    testKeyframeToMatrix();
    testRejection();
    testRealFiles();
    return KS_TEST_RESULT("ksanim_test");
}
