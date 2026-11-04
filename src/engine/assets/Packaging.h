#pragma once
#include <string>

namespace ks {
namespace engine {
namespace assets {

/** Content packaging helpers. ksim prefers reading folders (and zip entries
 *  at load time) in place; unpack is available for tools and optional extract. */
class Packaging {
public:
    static bool pack(const std::string& srcDir, const std::string& outFile);
    static bool unpack(const std::string& inFile, const std::string& dstDir);
};

} // namespace assets
} // namespace engine
} // namespace ks
