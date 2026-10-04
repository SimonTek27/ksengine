#include "TrackToolCore.h"

#include "TrackLoader.h"
#include "engine/AI/AiFileWriter.h"
#include "engine/FileFormat/Kn5Reader.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ks {
namespace tracktool {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

void putI32(std::string& out, std::int32_t value)
{
    const char bytes[4] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    out.append(bytes, 4);
}

void putF32(std::string& out, float value)
{
    std::int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putI32(out, bits);
}

// Minimal KN5 the engine's full reader accepts: magic + v5 header, zero
// textures/materials, single Base root node with identity transform.
std::string minimalKn5Image()
{
    std::string image;
    image.append("sc6969", 6);
    putI32(image, 5); // version 5 (depthMode present in materials)
    putI32(image, 0); // textureCount
    putI32(image, 0); // materialCount
    // root node: Base class, name "root", no children, active, transform
    putI32(image,
           static_cast<std::int32_t>(engine::fileformat::Kn5NodeClass::base));
    putI32(image, 4);
    image.append("root", 4);
    putI32(image, 0);
    image.push_back(1);
    for (float v : engine::fileformat::kn5IdentityMatrix())
        putF32(image, v);
    return image;
}

const char kSurfacesIni[] =
    "; ks_tracktool scaffold — baseline surface friction map\n"
    "[ROAD]\n"
    "KEY=ROAD\n"
    "FRICTION=1.0\n"
    "[KERB]\n"
    "KEY=KERB\n"
    "FRICTION=0.8\n"
    "[GRASS]\n"
    "KEY=GRASS\n"
    "FRICTION=0.6\n";

bool writeFile(const fs::path& path, const char* data, std::size_t bytes,
               std::string& errorOut)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        errorOut = "cannot write " + path.string();
        return false;
    }
    out.write(data, static_cast<std::streamsize>(bytes));
    if (!out) {
        errorOut = "failed writing " + path.string();
        return false;
    }
    return true;
}

} // namespace

ai::AiSpline generateOvalSpline(float radius, unsigned int points, float speed)
{
    ai::AiSpline spline;
    if (radius <= 0.0f || points < 8 || points > 100000u)
        return spline;

    spline.points.reserve(points);
    const float curvature = 1.0f / radius;
    for (unsigned int i = 0; i < points; ++i) {
        const float t = kTwoPi * static_cast<float>(i) /
                        static_cast<float>(points);
        ai::AiSplinePoint point;
        point.position = {radius * std::cos(t), 0.0f, radius * std::sin(t)};
        point.curvature = curvature;
        point.speed = speed;
        spline.points.push_back(point);
    }
    return spline;
}

bool scaffoldTrack(const std::string& trackDirectory, std::string& errorOut)
{
    errorOut.clear();
    if (trackDirectory.empty()) {
        errorOut = "empty track directory";
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(trackDirectory) / "data", ec);
    if (ec) {
        errorOut = "cannot create " + fs::path(trackDirectory).string() +
                   "/data: " + ec.message();
        return false;
    }
    fs::create_directories(fs::path(trackDirectory) / "ai", ec);
    if (ec) {
        errorOut = "cannot create " + fs::path(trackDirectory).string() +
                   "/ai: " + ec.message();
        return false;
    }

    const std::string kn5 = minimalKn5Image();
    if (!writeFile(fs::path(trackDirectory) / "model.kn5", kn5.data(),
                   kn5.size(), errorOut))
        return false;

    if (!writeFile(fs::path(trackDirectory) / "data" / "surfaces.ini",
                   kSurfacesIni, std::strlen(kSurfacesIni), errorOut))
        return false;

    const ai::AiSpline oval = generateOvalSpline();
    if (!ai::AiFileWriter::writeSpline(
             (fs::path(trackDirectory) / "ai" / "fast_lane.ai").string(), oval)) {
        errorOut = "cannot write AI spline under " + trackDirectory;
        return false;
    }
    return true;
}

TrackReport validateTrack(const std::string& trackDirectory)
{
    TrackReport report;
    if (!fs::is_directory(trackDirectory)) {
        report.errors.push_back("not a directory: " + trackDirectory);
        return report;
    }

    sim::TrackLoader loader;
    const sim::TrackData track = loader.loadTrackFolder(trackDirectory);
    report.name = track.name;

    if (!track.kn5HeaderValidated) {
        report.errors.push_back(loader.lastError().empty()
                                    ? "no KN5 file found in " + trackDirectory
                                    : loader.lastError());
    } else {
        report.kn5Path = track.kn5Path;
    }

    if (!track.aiSplineLoaded) {
        report.errors.push_back(
            "AI spline missing or invalid (ai/fast_lane.ai)");
    } else {
        report.aiSplinePath = track.aiSplinePath;
        if (track.aiSpline) {
            report.splinePoints = track.aiSpline->size();
            report.trackLength = track.aiSpline->totalDistance;
            report.splineClosed = track.aiSpline->closed;
            if (!report.splineClosed)
                report.warnings.push_back(
                    "AI spline does not close (loop gap > 5% of length)");
            if (report.splinePoints < 2)
                report.errors.push_back("AI spline has fewer than 2 points");
        }
    }

    if (!track.surfacesLoaded) {
        report.warnings.push_back(
            "data/surfaces.ini missing or empty — default grip 1.0");
    } else {
        report.surfaceCount = track.surfaceFriction.size();
        report.baseGrip = track.baseGrip;
    }

    report.ok = report.errors.empty();
    return report;
}

std::string formatReport(const TrackReport& report)
{
    std::string out;
    out += "track: " + (report.name.empty() ? std::string("<unnamed>")
                                             : report.name) +
           "\n";
    out += "  kn5:       ";
    if (!report.kn5Path.empty())
        out += "OK " + report.kn5Path + "\n";
    else
        out += "MISSING/INVALID\n";

    out += "  ai spline: ";
    if (!report.aiSplinePath.empty()) {
        out += "OK " + std::to_string(report.splinePoints) + " points, " +
               std::to_string(report.trackLength) + " m, " +
               (report.splineClosed ? "closed" : "open") + "\n";
    } else {
        out += "MISSING\n";
    }

    out += "  surfaces:  ";
    if (report.surfaceCount > 0) {
        out += "OK " + std::to_string(report.surfaceCount) +
               " surfaces, base grip " + std::to_string(report.baseGrip) +
               "\n";
    } else {
        out += "MISSING\n";
    }

    for (const std::string& warning : report.warnings)
        out += "  warning:   " + warning + "\n";
    for (const std::string& error : report.errors)
        out += "  error:     " + error + "\n";
    out += "result: ";
    if (report.ok)
        out += "OK\n";
    else
        out += "FAILED (" + std::to_string(report.errors.size()) + " errors)\n";
    return out;
}

} // namespace tracktool
} // namespace ks
