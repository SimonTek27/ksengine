#include "KsAnim.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace ks::engine::fileformat {

namespace {

// Count ceilings mirrored from the real corpus (largest observed: 605
// entries, 193 keyframes); anything above these is a corrupt/hostile file.
constexpr std::int32_t kMaxEntries = 100000;
constexpr std::int32_t kMaxKeyframes = 1000000;
constexpr std::int32_t kMaxNameBytes = 4096;
constexpr std::int32_t kQtLatin1Flag = 0x80000000;

// Bounds-checked little-endian cursor. Same contract as Kn5Reader's:
// every read either consumes exactly what it promises or latches the first
// error, and callers unwind with early returns.
class ByteReader {
public:
    explicit ByteReader(std::string_view bytes)
        : data_(reinterpret_cast<const std::uint8_t*>(bytes.data())),
          size_(bytes.size()) {}

    const std::string& error() const { return error_; }
    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return size_ - pos_; }

    bool fail(std::string message) {
        if (error_.empty()) {
            error_ = "offset " + std::to_string(pos_) + ": " + std::move(message);
        }
        return false;
    }

    bool read(void* dst, std::size_t n) {
        if (n > remaining()) return fail("unexpected end of file");
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool readI32(std::int32_t& v) { return read(&v, sizeof(v)); }

    bool readF32(float& v) { return read(&v, sizeof(v)); }

    bool readCount(std::int32_t& count, std::int32_t limit, const char* what) {
        if (!readI32(count)) return false;
        if (count < 0 || count > limit) {
            return fail(std::string("implausible ") + what + " count " +
                        std::to_string(count));
        }
        return true;
    }

    // Reads a node name: u32 byte length followed by that many bytes. The
    // high-bit form is QDataStream's Latin-1 encoding, accepted for files
    // written by the Qt editor; the plain byte form is what real AC content
    // uses. Both are kept as raw bytes here — every name in the corpus is
    // ASCII.
    bool readName(std::string& out) {
        std::uint32_t raw = 0;
        if (!read(&raw, sizeof(raw))) return false;
        const std::uint32_t length =
            raw & ~static_cast<std::uint32_t>(kQtLatin1Flag);
        if (length > static_cast<std::uint32_t>(kMaxNameBytes)) {
            return fail("implausible node name length " + std::to_string(length));
        }
        if (length > remaining()) return fail("node name extends past end of file");
        out.assign(reinterpret_cast<const char*>(data_ + pos_), length);
        pos_ += length;
        return true;
    }

    bool readFloats(float* out, std::size_t n) {
        if (n * sizeof(float) > remaining()) {
            return fail("unexpected end of file");
        }
        return read(out, n * sizeof(float));
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::string error_;
};

float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

std::array<float, 4> normalizeQuat(const std::array<float, 4>& q) {
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] +
                                q[3] * q[3]);
    if (len <= 1e-8f) return {0.0f, 0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / len;
    return {q[0] * inv, q[1] * inv, q[2] * inv, q[3] * inv};
}

// Spherical linear interpolation; the sign of q2 is flipped when the
// quaternions point opposite ways so the shorter arc is taken.
std::array<float, 4> quatSlerp(const std::array<float, 4>& a,
                                const std::array<float, 4>& b, float t) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float sign = 1.0f;
    if (dot < 0.0f) {
        dot = -dot;
        sign = -1.0f;
    }
    if (dot > 0.9999f) {
        const std::array<float, 4> mixed = {
            a[0] + (b[0] * sign - a[0]) * t, a[1] + (b[1] * sign - a[1]) * t,
            a[2] + (b[2] * sign - a[2]) * t, a[3] + (b[3] * sign - a[3]) * t};
        return normalizeQuat(mixed);
    }
    const float angle = std::acos(dot);
    const float sin_angle = std::sin(angle);
    const float w1 = std::sin((1.0f - t) * angle) / sin_angle;
    const float w2 = std::sin(t * angle) / sin_angle * sign;
    return normalizeQuat({a[0] * w1 + b[0] * w2, a[1] * w1 + b[1] * w2,
                          a[2] * w1 + b[2] * w2, a[3] * w1 + b[3] * w2});
}

} // namespace

std::array<float, 16> KsAnimKeyframe::toMatrix() const {
    // M = S * R * T in row-major storage: scale each rotation row by the
    // matching axis scale, then overwrite row 3 with the translation (the
    // rotation block never reaches row 3, so nothing is disturbed).
    const float x = rotation[0];
    const float y = rotation[1];
    const float z = rotation[2];
    const float w = rotation[3];

    const float r[3][3] = {
        {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w),
         2.0f * (x * z + y * w)},
        {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z),
         2.0f * (y * z - x * w)},
        {2.0f * (x * z - y * w), 2.0f * (y * z + x * w),
         1.0f - 2.0f * (x * x + y * y)},
    };

    std::array<float, 16> m{};
    for (int row = 0; row < 3; ++row) {
        const float s = scale[static_cast<std::size_t>(row)];
        for (int col = 0; col < 3; ++col) {
            m[static_cast<std::size_t>(row * 4 + col)] =
                s * r[row][col];
        }
        m[static_cast<std::size_t>(row * 4 + 3)] = 0.0f;
    }
    m[12] = translation[0];
    m[13] = translation[1];
    m[14] = translation[2];
    m[15] = 1.0f;
    return m;
}

bool isKsAnim(std::string_view bytes) {
    if (bytes.size() < 4) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::int32_t version = static_cast<std::int32_t>(p[0]) |
                                 (static_cast<std::int32_t>(p[1]) << 8) |
                                 (static_cast<std::int32_t>(p[2]) << 16) |
                                 (static_cast<std::int32_t>(p[3]) << 24);
    return version >= kKsAnimMinVersion && version <= kKsAnimMaxVersion;
}

KsAnimParseResult parseKsAnim(std::string_view bytes) {
    KsAnimParseResult result;
    ByteReader reader(bytes);

    if (!reader.readI32(result.file.version)) {
        result.error = reader.error();
        return result;
    }
    if (result.file.version < kKsAnimMinVersion ||
        result.file.version > kKsAnimMaxVersion) {
        reader.fail("unsupported ksanim version " +
                    std::to_string(result.file.version));
        result.error = reader.error();
        return result;
    }

    std::int32_t count = 0;
    if (!reader.readCount(count, kMaxEntries, "entry")) {
        result.error = reader.error();
        return result;
    }

    const bool v2 = result.file.version == 2;
    result.file.entries.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        KsAnimEntry entry;
        if (!reader.readName(entry.name)) {
            result.error = reader.error();
            return result;
        }
        std::int32_t keyframes = 0;
        if (!reader.readCount(keyframes, kMaxKeyframes, "keyframe")) {
            result.error = reader.error();
            return result;
        }

        if (v2) {
            if (static_cast<std::uint64_t>(keyframes) *
                    kKsAnimKeyframeFloats * sizeof(float) >
                reader.remaining()) {
                reader.fail("keyframes extend past end of file");
                result.error = reader.error();
                return result;
            }
            entry.keyframes.resize(static_cast<std::size_t>(keyframes));
            for (std::int32_t k = 0; k < keyframes; ++k) {
                float values[kKsAnimKeyframeFloats];
                if (!reader.readFloats(values, kKsAnimKeyframeFloats)) {
                    result.error = reader.error();
                    return result;
                }
                auto& kf = entry.keyframes[static_cast<std::size_t>(k)];
                kf.rotation = {values[0], values[1], values[2], values[3]};
                kf.translation = {values[4], values[5], values[6]};
                kf.scale = {values[7], values[8], values[9]};
            }
        } else {
            if (static_cast<std::uint64_t>(keyframes) *
                    kKsAnimMatrixFloats * sizeof(float) >
                reader.remaining()) {
                reader.fail("keyframes extend past end of file");
                result.error = reader.error();
                return result;
            }
            entry.matrices.resize(static_cast<std::size_t>(keyframes));
            for (std::int32_t k = 0; k < keyframes; ++k) {
                if (!reader.readFloats(
                        entry.matrices[static_cast<std::size_t>(k)].data(),
                        kKsAnimMatrixFloats)) {
                    result.error = reader.error();
                    return result;
                }
            }
        }
        // Zero-keyframe entries are kept (the Qt reader drops them) so the
        // parsed entry list mirrors the file's entry count exactly.
        result.file.entries.push_back(std::move(entry));
    }

    // Trailing bytes after the last entry are tolerated the same way KN5
    // tolerates them — observed on a handful of mod files.
    return result;
}

KsAnimParseResult parseKsAnimFile(const std::string& path) {
    // Sized read: istreambuf_iterator walks the stream byte-by-byte, which
    // costs tens of seconds on multi-megabyte animation files.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        KsAnimParseResult result;
        result.error = "cannot open file: " + path;
        return result;
    }
    const std::streamoff size = input.tellg();
    if (size < 0) {
        KsAnimParseResult result;
        result.error = "cannot determine file size: " + path;
        return result;
    }
    input.seekg(0, std::ios::beg);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), size);
    if (!input && size > 0) {
        KsAnimParseResult result;
        result.error = "read error: " + path;
        return result;
    }
    return parseKsAnim(bytes);
}

const KsAnimEntry* KsAnimFile::find(const std::string& name) const {
    for (const KsAnimEntry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

int KsAnimFile::totalKeyframes() const {
    int total = 0;
    for (const KsAnimEntry& entry : entries) total += entry.keyframeCount();
    return total;
}

std::vector<std::string> KsAnimFile::animatedNodes() const {
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const KsAnimEntry& entry : entries) names.push_back(entry.name);
    return names;
}

bool KsAnimFile::sample(const std::string& name, float t,
                        std::array<float, kKsAnimMatrixFloats>& out) const {
    const KsAnimEntry* entry = find(name);
    if (entry == nullptr) return false;

    if (!entry->keyframes.empty()) {
        const auto count = static_cast<float>(entry->keyframes.size());
        if (entry->keyframes.size() == 1) {
            out = entry->keyframes.front().toMatrix();
            return true;
        }
        const float clamped = clamp01(t);
        const float raw = clamped * (count - 1.0f);
        const auto i0 = static_cast<std::size_t>(std::floor(raw));
        const auto i1 = std::min(i0 + 1, entry->keyframes.size() - 1);
        const float frac = raw - static_cast<float>(i0);

        const KsAnimKeyframe& a = entry->keyframes[i0];
        const KsAnimKeyframe& b = entry->keyframes[i1];
        KsAnimKeyframe blended;
        blended.rotation = quatSlerp(a.rotation, b.rotation, frac);
        for (int axis = 0; axis < 3; ++axis) {
            blended.translation[static_cast<std::size_t>(axis)] =
                a.translation[static_cast<std::size_t>(axis)] +
                (b.translation[static_cast<std::size_t>(axis)] -
                 a.translation[static_cast<std::size_t>(axis)]) *
                    frac;
            blended.scale[static_cast<std::size_t>(axis)] =
                a.scale[static_cast<std::size_t>(axis)] +
                (b.scale[static_cast<std::size_t>(axis)] -
                 a.scale[static_cast<std::size_t>(axis)]) *
                    frac;
        }
        out = blended.toMatrix();
        return true;
    }

    if (entry->matrices.empty()) return false;
    // Version 1 stores whole matrices: no quaternion to slerp, so snap to the
    // nearest keyframe instead of blending element-wise (which would shear).
    const float clamped = clamp01(t);
    const auto last = static_cast<float>(entry->matrices.size() - 1);
    auto index = static_cast<std::size_t>(std::lround(clamped * last));
    if (index >= entry->matrices.size()) index = entry->matrices.size() - 1;
    out = entry->matrices[index];
    return true;
}

} // namespace ks::engine::fileformat
