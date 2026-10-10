#pragma once
#include "KsExport.h"

// Qt-free reader for the Assetto Corsa .ksanim node animation container.
//
// The layout below was verified byte-for-byte against ~2000 real AC .ksanim
// files (content/cars/*/animations, content/tracks, objects): the entry walk
// must land exactly on EOF, which it does for every file except two known
// corrupt mods.
//
// Layout (little-endian throughout):
//   int32 version      = 0 | 1 | 2   (0 seen only on an empty stub file)
//   int32 entryCount
//   per entry:
//     int32 nameLen + nameLen bytes   <- UTF-8/ASCII, NOT a Qt UTF-16 string.
//                                       (The Qt reader in the editor plugin
//                                       feeds these through QDataStream's
//                                       QString operator, which expects a
//                                       char count + UTF-16 and therefore
//                                       mis-reads real content; this reader
//                                       uses the byte form the files actually
//                                       contain. nameLen with the high bit set
//                                       is the Qt Latin-1 encoding and is
//                                       accepted as a compatibility path.)
//     int32 keyframeCount
//     version 1/0: keyframeCount * 16 floats, one 4x4 matrix per keyframe
//     version 2  : keyframeCount * 10 floats per keyframe:
//                  quaternion x,y,z,w + translation x,y,z + scale x,y,z
//
// Matrix layout matches KN5 node transforms exactly (Kn5Reader.h): row-major,
// translation in elements 12..14, so an animated matrix can be dropped into a
// node slot without conversion. Verified against real content: the first
// keyframe of `DOOR_R` in abarth500's car_door_L.ksanim is an identity
// rotation with the hinge offset at [12..14].
//
// v2 sampling builds M = S * R * T (scale, then rotate, then translate — the
// order implied by translation living in row 3). Note the Qt helper
// `KSAnimKeyframe::toMatrix()` composes R * T * S instead, which only differs
// for keyframes whose scale is not (1,1,1).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

inline constexpr std::int32_t kKsAnimMinVersion = 0;
inline constexpr std::int32_t kKsAnimMaxVersion = 2;
inline constexpr std::size_t kKsAnimMatrixFloats = 16;
inline constexpr std::size_t kKsAnimKeyframeFloats = 10;

struct KSENGINE_API KsAnimKeyframe {
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
    std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};

    // Row-major, translation in [12..14] — the KN5 node matrix layout.
    std::array<float, 16> toMatrix() const;
};

struct KsAnimEntry {
    std::string name;
    std::vector<std::array<float, kKsAnimMatrixFloats>> matrices; // version 1
    std::vector<KsAnimKeyframe> keyframes;                        // version 2

    int keyframeCount() const {
        return static_cast<int>(keyframes.empty() ? matrices.size()
                                                  : keyframes.size());
    }
};

struct KSENGINE_API KsAnimFile {
    std::int32_t version = 0;
    std::vector<KsAnimEntry> entries;

    const KsAnimEntry* find(const std::string& name) const;
    int totalKeyframes() const;
    std::vector<std::string> animatedNodes() const;

    // Samples `name` at normalized time t in [0, 1] into a row-major matrix.
    // Version 2 slerps rotation and lerps translation/scale; version 1 snaps
    // to the nearest keyframe (a 4x4 matrix has no slerp). Returns false when
    // the node is unknown or has no keyframes, leaving `out` untouched.
    bool sample(const std::string& name, float t,
                std::array<float, kKsAnimMatrixFloats>& out) const;
};

struct KsAnimParseResult {
    KsAnimFile file;
    std::string error; // empty on success, "offset N: ..." on failure

    bool ok() const { return error.empty(); }
};

// True when the first 4 bytes are a plausible ksanim version (0..2).
KSENGINE_API bool isKsAnim(std::string_view bytes);

// Parses an in-memory ksanim image (binary-safe).
KSENGINE_API KsAnimParseResult parseKsAnim(std::string_view bytes);

// Reads the whole file into memory, then parses it.
KSENGINE_API KsAnimParseResult parseKsAnimFile(const std::string& path);

} // namespace ks::engine::fileformat
