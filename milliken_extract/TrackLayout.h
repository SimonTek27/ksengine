#pragma once
#include "TrackLoader.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>

namespace ks {
namespace sim {

struct TrackLayoutInfo {
    std::string id;
    std::string name;
    std::string kn5File;
    std::string aiSpline;
    bool reverse = false;
    float lengthM = 0.f;
};

class TrackLayoutCatalog {
public:
    const std::vector<TrackLayoutInfo>& layouts() const { return m_layouts; }
    int selected() const { return m_selected; }
    bool select(int index) {
        if (index < 0 || index >= (int)m_layouts.size()) return false;
        m_selected = index; return true;
    }
    bool selectById(const std::string& id) {
        for (int i = 0; i < (int)m_layouts.size(); ++i)
            if (m_layouts[(size_t)i].id == id) { m_selected = i; return true; }
        return false;
    }
    const TrackLayoutInfo* current() const {
        if (m_layouts.empty() || m_selected < 0 || m_selected >= (int)m_layouts.size()) return nullptr;
        return &m_layouts[(size_t)m_selected];
    }
    void loadFromTrackDir(const std::string& trackDir) {
        m_layouts.clear(); m_selected = 0; m_trackDir = trackDir;
        if (loadIni(trackDir + "/layouts.ini")) return;
        TrackLayoutInfo def;
        def.id = "default";
        def.name = TrackLoader::trackNameFromDirectory(trackDir);
        def.kn5File = TrackLoader::findKn5File(trackDir);
        auto slash = def.kn5File.find_last_of("/\\");
        if (slash != std::string::npos) def.kn5File = def.kn5File.substr(slash + 1);
        def.aiSpline = "ai/fast_lane.ai";
        m_layouts.push_back(def);
        std::ifstream rev(trackDir + "/ai/fast_lane_reverse.ai");
        if (rev) {
            TrackLayoutInfo r = def;
            r.id = "reverse"; r.name = def.name + " Reverse";
            r.aiSpline = "ai/fast_lane_reverse.ai"; r.reverse = true;
            m_layouts.push_back(r);
        }
    }
    TrackData apply(TrackLoader& loader) const {
        const auto* L = current();
        if (!L) return loader.loadTrackFolder(m_trackDir);
        TrackData td = loader.loadTrackFolder(m_trackDir);
        if (!L->kn5File.empty()) {
            auto alt = loader.loadKn5File(m_trackDir + "/" + L->kn5File);
            if (alt.kn5HeaderValidated) { td.kn5Path = alt.kn5Path; td.kn5HeaderValidated = true; }
        }
        if (!L->aiSpline.empty()) td.aiSplinePath = m_trackDir + "/" + L->aiSpline;
        if (!L->name.empty()) td.name = L->name;
        return td;
    }
private:
    bool loadIni(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        auto trim = [](std::string s) {
            while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(s.begin());
            while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
            return s;
        };
        std::string line; TrackLayoutInfo* cur = nullptr;
        while (std::getline(in, line)) {
            auto sc = line.find(';'); if (sc != std::string::npos) line = line.substr(0, sc);
            line = trim(line); if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') {
                m_layouts.push_back({}); cur = &m_layouts.back();
                cur->id = line.substr(1, line.size()-2); cur->name = cur->id; continue;
            }
            if (!cur) continue;
            auto eq = line.find('='); if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq+1));
            if (key == "Name") cur->name = val;
            else if (key == "Kn5" || key == "Model") cur->kn5File = val;
            else if (key == "Ai" || key == "Spline") cur->aiSpline = val;
            else if (key == "Reverse") cur->reverse = (val == "1" || val == "true");
            else if (key == "Length") cur->lengthM = std::strtof(val.c_str(), nullptr);
        }
        return !m_layouts.empty();
    }
    std::vector<TrackLayoutInfo> m_layouts;
    int m_selected = 0;
    std::string m_trackDir;
};

} // namespace sim
} // namespace ks
