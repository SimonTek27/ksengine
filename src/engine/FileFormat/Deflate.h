#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ks::engine::fileformat {

// Decompresses one RFC 1950 zlib stream or one raw RFC 1951 DEFLATE stream
// into `out` (which is cleared first). Real FBX files store every array with
// `encoding == 1`, i.e. a zlib stream, and no zlib is available to this
// build, so the decoder lives here.
//
// Returns false and points `error` at a reason when the stream is truncated,
// corrupt, or not DEFLATE at all.
bool inflateZlib(std::string_view src, std::vector<std::uint8_t>& out,
                 std::string* error = nullptr);

// Same, for a stream with no 2-byte zlib header and no trailing adler32.
bool inflateRaw(std::string_view src, std::vector<std::uint8_t>& out,
                std::string* error = nullptr);

} // namespace ks::engine::fileformat
