#include "AI/AiFileWriter.h"
#include "AI/AiFileReader.h"

#include <cstring>
#include <fstream>

namespace ks {
namespace ai {
namespace {

constexpr unsigned int kAiMagic = 0x00414900u;
constexpr unsigned int kAiVersion = 1u;
constexpr unsigned int kMaxPoints = 100000u;

void writeU32(std::ofstream& out, unsigned int value)
{
    const char bytes[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8) & 0xffu),
        static_cast<char>((value >> 16) & 0xffu),
        static_cast<char>((value >> 24) & 0xffu),
    };
    out.write(bytes, sizeof(bytes));
}

void writeF32(std::ofstream& out, float value)
{
    static_assert(sizeof(float) == sizeof(unsigned int),
                  "AI spline writer assumes 32-bit float");
    unsigned int bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(out, bits);
}

} // namespace

bool AiFileWriter::writeSpline(const std::string& path, const AiSpline& spline)
{
    if (spline.points.empty() || spline.points.size() > kMaxPoints)
        return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    writeU32(out, kAiMagic);
    writeU32(out, kAiVersion);
    writeU32(out, static_cast<unsigned int>(spline.points.size()));
    for (const AiSplinePoint& point : spline.points) {
        writeF32(out, point.position.x);
        writeF32(out, point.position.y);
        writeF32(out, point.position.z);
        writeF32(out, point.curvature);
        writeF32(out, point.speed);
    }

    return static_cast<bool>(out);
}

} // namespace ai
} // namespace ks
