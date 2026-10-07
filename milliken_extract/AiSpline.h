#pragma once

/**
 * AI spline / fast_lane data — std only (no QString/QVector/QObject).
 */

#include <string>
#include <vector>
#include <cstdio>
#include <fstream>
#include <cmath>
#include <cstdint>

namespace ks {
namespace ai {

struct AiVec3 {
    float x = 0, y = 0, z = 0;
};

struct AiPoint {
    AiVec3 position;
    float curvature = 0.0f;
    float speed = 0.0f;
    int lap = 0;
    float distance = 0.0f;
};

struct AiPointExtra {
    AiVec3 normal;
    float width = 8.0f;
    float comfort = 0.5f;
    std::string tag;
};

struct AiSpline {
    std::string name;
    std::vector<AiPoint> points;
    std::vector<AiPointExtra> extras;
    float totalDistance = 0.0f;
    int laps = 1;

    bool isValid() const { return !points.empty(); }
    int pointCount() const { return static_cast<int>(points.size()); }
};

struct AiSplineGrid {
    std::string trackName;
    std::vector<AiSpline> splines;

    bool isValid() const { return !splines.empty(); }
    int splineCount() const { return static_cast<int>(splines.size()); }
};

/**
 * Minimal readers:
 * - Text lines: "x y z speed curvature [lap]"
 * - Binary AC-style is not fully reimplemented here; invalid file → empty spline.
 */
class AiFileReader {
public:
    static AiSpline readSpline(const std::string& filename) {
        AiSpline s;
        s.name = filename;
        std::ifstream in(filename);
        if (!in) {
            // try alternate path
            return s;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            AiPoint p;
            int lap = 0;
            int n = std::sscanf(line.c_str(), "%f %f %f %f %f %d",
                                &p.position.x, &p.position.y, &p.position.z,
                                &p.speed, &p.curvature, &lap);
            if (n >= 3) {
                if (n >= 6) p.lap = lap;
                if (n < 4) p.speed = 40.0f;
                s.points.push_back(p);
            }
        }
        // cumulative distance
        float dist = 0;
        for (size_t i = 0; i < s.points.size(); ++i) {
            if (i > 0) {
                float dx = s.points[i].position.x - s.points[i-1].position.x;
                float dy = s.points[i].position.y - s.points[i-1].position.y;
                float dz = s.points[i].position.z - s.points[i-1].position.z;
                dist += std::sqrt(dx*dx+dy*dy+dz*dz);
            }
            s.points[i].distance = dist;
        }
        s.totalDistance = dist;
        return s;
    }

    static AiSplineGrid readGrid(const std::string& filename) {
        AiSplineGrid g;
        g.trackName = filename;
        auto s = readSpline(filename);
        if (s.isValid()) g.splines.push_back(std::move(s));
        return g;
    }

    static bool read(const std::string& filename, AiSplineGrid& outGrid) {
        outGrid = readGrid(filename);
        return outGrid.isValid();
    }
};

class AiFileWriter {
public:
    static bool writeSpline(const std::string& filename, const AiSpline& spline) {
        std::ofstream out(filename);
        if (!out) return false;
        for (const auto& p : spline.points) {
            out << p.position.x << ' ' << p.position.y << ' ' << p.position.z
                << ' ' << p.speed << ' ' << p.curvature << ' ' << p.lap << '\n';
        }
        return true;
    }

    static bool writeGrid(const std::string& filename, const AiSplineGrid& grid) {
        if (grid.splines.empty()) return false;
        return writeSpline(filename, grid.splines.front());
    }

    static bool write(const std::string& filename, const AiSplineGrid& grid) {
        return writeGrid(filename, grid);
    }
};

} // namespace ai
} // namespace ks
