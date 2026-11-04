#include "Packaging.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace ks {
namespace engine {
namespace assets {
namespace fs = std::filesystem;

bool Packaging::pack(const std::string& srcDir, const std::string& outFile) {
    if (!fs::is_directory(srcDir) || outFile.empty()) return false;
#if defined(_WIN32)
    // Prefer tar.exe (Win10+) over PowerShell for a quiet exit code.
    const std::string cmd =
        "tar -a -cf \"" + outFile + "\" -C \"" + srcDir + "\" . >nul 2>nul";
#else
    const std::string cmd =
        "tar -czf \"" + outFile + "\" -C \"" + srcDir + "\" . >/dev/null 2>&1";
#endif
    const int rc = std::system(cmd.c_str());
    return rc == 0 && fs::is_regular_file(outFile);
}

bool Packaging::unpack(const std::string& inFile, const std::string& dstDir) {
    if (!fs::is_regular_file(inFile) || dstDir.empty()) return false;
    std::error_code ec;
    fs::create_directories(dstDir, ec);
#if defined(_WIN32)
    const std::string cmd =
        "tar -xf \"" + inFile + "\" -C \"" + dstDir + "\" >nul 2>nul";
#else
    const std::string cmd =
        "tar -xf \"" + inFile + "\" -C \"" + dstDir + "\" >/dev/null 2>&1";
#endif
    const int rc = std::system(cmd.c_str());
    return rc == 0;
}

} // namespace assets
} // namespace engine
} // namespace ks
