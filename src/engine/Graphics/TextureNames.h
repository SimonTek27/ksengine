#pragma once
/**
 * The name ksim gives a texture, as opposed to the one the content happens to
 * carry.
 *
 * A KN5 refers to its textures by whatever the original author typed, and those
 * names leak into things we own: the renderer cache key, the material listing,
 * logs. One of them is the car body diffuse, which ships as `vrc-car-body.dds`
 * (and its normal map as `vrc-car-body-map.dds`) in every AC skin. That name is
 * not ours — it comes from a third-party toolchain — so ksim calls the body
 * `CARBODY` and the map `CARBODY_MAP`, whatever the file on disk is called.
 *
 * The mapping is by name only: nothing is renamed on disk, no content is
 * touched, and the pixels still come from the file the KN5 points at. If the
 * reference is not a body texture, it is returned unchanged (extension
 * included), so everything else keeps the identity it was authored with.
 */
#include <cctype>
#include <cstddef>
#include <string>

namespace ks {
namespace graphics {

/** `vrc-car-body` -> `CARBODY`, `vrc-car-body-map` -> `CARBODY_MAP`, anything
 *  else unchanged. Case-insensitive on the way in. */
inline std::string canonicalTextureName(const std::string& reference) {
    if (reference.empty()) return reference;

    std::string base = reference;
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);

    std::string folded;
    folded.reserve(base.size());
    for (char c : base)
        folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    static const char kBody[] = "vrc-car-body";
    const std::size_t prefixLength = sizeof(kBody) - 1;
    if (folded.size() < prefixLength || folded.compare(0, prefixLength, kBody) != 0)
        return reference;

    std::string out = "CARBODY";
    for (std::size_t i = prefixLength; i < base.size(); ++i) {
        const char c = base[i];
        if (c == '-' || c == '_')
            out.push_back('_');
        else
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace graphics
} // namespace ks
