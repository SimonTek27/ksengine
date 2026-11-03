#include "AI/AiFileReader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ks {
namespace ai {
namespace {

constexpr unsigned int kAiMagic = 0x00414900u;
constexpr unsigned int kMaxPoints = 100000u;
// AIController scales target speed by (1 - curvature * aggression * 0.5), so a
// radius under 10 m would drive the target negative. No real corner is that
// tight; this only guards degenerate CSV values.
constexpr float kMaxCurvature = 0.1f;

bool readExact(std::ifstream& in, void* dst, std::size_t bytes)
{
    in.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
    return static_cast<std::size_t>(in.gcount()) == bytes;
}

void finishSpline(AiSpline& spline)
{
    float cum = 0.0f;
    for (std::size_t i = 0; i < spline.points.size(); ++i) {
        if (i > 0) {
            const AiSplinePoint& a = spline.points[i - 1];
            const AiSplinePoint& b = spline.points[i];
            const float dx = b.position.x - a.position.x;
            const float dy = b.position.y - a.position.y;
            const float dz = b.position.z - a.position.z;
            cum += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        spline.points[i].distance = cum;
    }
    spline.totalDistance = cum;

    if (spline.points.size() >= 2) {
        const AiSplinePoint& a = spline.points.front();
        const AiSplinePoint& b = spline.points.back();
        const float dx = b.position.x - a.position.x;
        const float dy = b.position.y - a.position.y;
        const float dz = b.position.z - a.position.z;
        const float gap = std::sqrt(dx * dx + dy * dy + dz * dz);
        spline.closed = spline.totalDistance > 0.0f
                        && gap < spline.totalDistance * 0.05f;
    }
}

AiSpline readTextSpline(const std::string& path)
{
    AiSpline spline;
    std::ifstream in(path);
    if (!in) return spline;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;

        float f[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        int fields = 0;
        const char* p = line.c_str();
        while (fields < 5 && *p) {
            char* end = nullptr;
            const float v = std::strtof(p, &end);
            if (end == p) break;
            f[fields++] = v;
            p = end;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == ',' || *p == ';' || *p == '\t') ++p;
        }
        if (fields < 3) continue;

        AiSplinePoint pt;
        pt.position = {f[0], f[1], f[2]};
        pt.curvature = f[3];
        pt.speed = f[4];
        spline.points.push_back(pt);
        if (spline.points.size() > kMaxPoints) {
            spline.points.clear();
            return spline;
        }
    }

    finishSpline(spline);
    return spline;
}

std::string trimSpace(const std::string& s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r'))
        ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
        --end;
    return s.substr(begin, end - begin);
}

std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c != '"') {
                current.push_back(c);
            } else if (i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"'); // escaped quote inside a field
                ++i;
            } else {
                inQuotes = false;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            fields.push_back(trimSpace(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(trimSpace(current));
    return fields;
}

// Empty cells are normal in lanes.csv (records that shipped with sentinel
// garbage have no speed/radius), so a field must parse cleanly or stay unset.
bool parseOptionalFloat(const std::string& text, float& out)
{
    const std::string trimmed = trimSpace(text);
    if (trimmed.empty()) return false;
    const char* start = trimmed.c_str();
    char* end = nullptr;
    const float value = std::strtof(start, &end);
    if (end == start) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') return false;
    out = value;
    return std::isfinite(value);
}

// Lane grouping of lanes.csv: column indices resolved from the header, so the
// reader survives column reordering and extra columns.
struct LanesColumns {
    int sourceFile = -1;
    int rowIndex = -1;
    int x = -1, y = -1, z = -1;
    int speed = -1, radiusM = -1;
};

bool resolveLanesColumns(const std::vector<std::string>& header, LanesColumns& out)
{
    for (std::size_t i = 0; i < header.size(); ++i) {
        const std::string& name = header[i];
        const int index = static_cast<int>(i);
        if (name == "source_file") out.sourceFile = index;
        else if (name == "row_index") out.rowIndex = index;
        else if (name == "x") out.x = index;
        else if (name == "y") out.y = index;
        else if (name == "z") out.z = index;
        else if (name == "speed") out.speed = index;
        else if (name == "radius_m") out.radiusM = index;
    }
    // source_file is what makes this lanes.csv rather than a plain x,y,z list.
    return out.sourceFile >= 0 && out.x >= 0 && out.y >= 0 && out.z >= 0;
}

// A few records in ideal_line.ai shipped with sentinel values instead of a
// speed; leaving those at 0 makes the AI brake to a stop over a bad point.
// Interpolate across each gap from its neighbours.
void fillMissingSpeeds(std::vector<AiSplinePoint>& points)
{
    std::size_t i = 0;
    while (i < points.size() && points[i].speed <= 0.0f) ++i;
    if (i == points.size()) return; // no reference speed anywhere
    const float lead = points[i].speed;
    for (std::size_t k = 0; k < i; ++k) points[k].speed = lead;

    for (; i < points.size(); ++i) {
        if (points[i].speed > 0.0f) continue;
        std::size_t next = i;
        while (next < points.size() && points[next].speed <= 0.0f) ++next;
        const float from = points[i - 1].speed;
        const float to = next < points.size() ? points[next].speed : from;
        const float span = static_cast<float>(next - i + 1);
        for (std::size_t k = i; k < next; ++k) {
            const float t = static_cast<float>(k - i + 1) / span;
            points[k].speed = from + (to - from) * t;
        }
        if (next == points.size()) return;
        i = next;
    }
}

} // namespace

AiSpline AiFileReader::readSpline(const std::string& path)
{
    AiSpline spline;
    std::ifstream in(path, std::ios::binary);
    if (!in) return spline;

    unsigned int magic = 0;
    if (!readExact(in, &magic, 4) || magic != kAiMagic) {
        in.close();
        // lanes.csv is the world-space dump of the .ai splines; several track
        // folders ship it with no .ai at all, so it carries the racing line.
        AiSpline lanes = readLanesCsv(path);
        if (!lanes.empty()) return lanes;
        return readTextSpline(path);
    }

    unsigned int version = 0;
    unsigned int count = 0;
    if (!readExact(in, &version, 4) || !readExact(in, &count, 4)
        || count == 0 || count > kMaxPoints) {
        return spline;
    }

    spline.points.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        float f[5];
        if (!readExact(in, f, sizeof(f))) {
            spline.points.clear();
            return spline;
        }
        AiSplinePoint pt;
        pt.position = {f[0], f[1], f[2]};
        pt.curvature = f[3];
        pt.speed = f[4];
        spline.points.push_back(pt);
    }

    finishSpline(spline);
    return spline;
}

AiSpline AiFileReader::readLanesCsv(const std::string& path,
                                    const std::string& lineName)
{
    AiSpline spline;
    std::ifstream in(path);
    if (!in) return spline;

    std::string line;
    if (!std::getline(in, line)) return spline;

    LanesColumns columns;
    if (!resolveLanesColumns(splitCsvLine(line), columns)) return spline;

    // Group rows by source_file, keeping first-seen order for the fallbacks.
    std::vector<std::string> groupsInOrder;
    std::map<std::string, std::vector<std::pair<int, AiSplinePoint>>> groups;

    while (std::getline(in, line)) {
        if (trimSpace(line).empty()) continue;
        const std::vector<std::string> fields = splitCsvLine(line);
        const int count = static_cast<int>(fields.size());
        if (count <= columns.sourceFile || count <= columns.x ||
            count <= columns.y || count <= columns.z)
            continue;

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!parseOptionalFloat(fields[columns.x], x) ||
            !parseOptionalFloat(fields[columns.y], y) ||
            !parseOptionalFloat(fields[columns.z], z))
            continue;

        const std::string& group = fields[columns.sourceFile];
        if (group.empty()) continue;

        AiSplinePoint pt;
        pt.position = {x, y, z};

        // speed is m/s as authored; the merged dump leaves it blank on the
        // few records that carried sentinel values upstream.
        float speed = 0.0f;
        if (columns.speed >= 0 && count > columns.speed &&
            parseOptionalFloat(fields[columns.speed], speed) && speed > 0.0f)
            pt.speed = speed;

        float radiusM = 0.0f;
        if (columns.radiusM >= 0 && count > columns.radiusM)
            parseOptionalFloat(fields[columns.radiusM], radiusM);
        if (radiusM > 1.0f) {
            // lanes.csv stores a turning RADIUS in metres, while AIController
            // scales target speed by curvature, so invert it here.
            float curvature = 1.0f / radiusM;
            if (curvature > kMaxCurvature) curvature = kMaxCurvature;
            pt.curvature = curvature;
        }

        // Spline order matters; row_index survives a hand-resorted file.
        int order = static_cast<int>(groups[group].size());
        if (columns.rowIndex >= 0 && count > columns.rowIndex) {
            float parsed = 0.0f;
            if (parseOptionalFloat(fields[columns.rowIndex], parsed))
                order = static_cast<int>(parsed);
        }

        if (std::find(groupsInOrder.begin(), groupsInOrder.end(), group) ==
            groupsInOrder.end())
            groupsInOrder.push_back(group);
        groups[group].emplace_back(order, pt);

        if (groups[group].size() > kMaxPoints) return AiSpline();
    }

    const std::string* chosen = nullptr;
    if (!lineName.empty()) {
        auto it = groups.find(lineName);
        if (it == groups.end()) return spline; // caller asked for a lane with no rows
        chosen = &it->first;
    } else {
        const char* preferred[] = {"fast_lane.ai", "ideal_line.ai"};
        for (const char* candidate : preferred) {
            auto it = groups.find(candidate);
            if (it != groups.end()) { chosen = &it->first; break; }
        }
        if (!chosen) {
            for (const std::string& candidate : groupsInOrder) {
                if (candidate == "pit_lane.ai") continue; // last resort only
                chosen = &candidate;
                break;
            }
        }
        if (!chosen && !groupsInOrder.empty())
            chosen = &groupsInOrder.front();
    }
    if (!chosen) return spline;

    auto& lane = groups[*chosen];
    std::stable_sort(lane.begin(), lane.end(),
                     [](const std::pair<int, AiSplinePoint>& a,
                        const std::pair<int, AiSplinePoint>& b) {
                         return a.first < b.first;
                     });

    spline.points.reserve(lane.size());
    for (const auto& entry : lane)
        spline.points.push_back(entry.second);
    fillMissingSpeeds(spline.points);
    finishSpline(spline);
    return spline;
}

} // namespace ai
} // namespace ks
