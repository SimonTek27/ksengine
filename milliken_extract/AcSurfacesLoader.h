#pragma once
/**
 * Load Assetto Corsa surfaces.ini → TrackSurface grip multipliers.
 * Example keys (AC-style):
 *   [SURFACE_0]
 *   KEY=ROAD
 *   FRICTION=1.0
 *   DIRT_ADDITIVE=0
 */
#include "../../engine/physics/TrackSurface.h"
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace ks {
namespace ac {

struct AcSurfaceDef {
    std::string key;
    float friction = 1.f;
    float damping = 0.f;
    float dirtAdditive = 0.f;
};

class AcSurfacesLoader {
public:
    bool load(const std::string& surfacesIniPath) {
        m_defs.clear();
        std::ifstream in(surfacesIniPath);
        if (!in) {
            std::fprintf(stderr, "AcSurfacesLoader: cannot open %s\n", surfacesIniPath.c_str());
            return false;
        }
        std::string line, section;
        AcSurfaceDef cur;
        auto flush = [&]() {
            if (!cur.key.empty())
                m_defs[upper(cur.key)] = cur;
            cur = {};
        };
        while (std::getline(in, line)) {
            // strip comments
            auto sc = line.find(';');
            if (sc != std::string::npos) line = line.substr(0, sc);
            auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            trim(line);
            if (line.empty()) continue;
            if (line.front() == '[') {
                flush();
                section = line;
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            trim(k); trim(v);
            std::string ku = upper(k);
            if (ku == "KEY" || ku == "NAME") cur.key = v;
            else if (ku == "FRICTION" || ku == "GRIP") cur.friction = std::stof(v);
            else if (ku == "DAMPING") cur.damping = std::stof(v);
            else if (ku == "DIRT_ADDITIVE" || ku == "DIRT") cur.dirtAdditive = std::stof(v);
        }
        flush();
        // Apply ROAD as base grip if present
        auto it = m_defs.find("ROAD");
        if (it == m_defs.end()) it = m_defs.find("ASPHALT");
        // Register every material
        for (const auto& kv : m_defs)
            ks::physics::TrackSurface::instance().setMaterialGrip(kv.first, kv.second.friction);
        auto grass = m_defs.find("GRASS");
        if (grass != m_defs.end())
            ks::physics::TrackSurface::instance().setOffTrackGrip(grass->second.friction);
        auto kerb = m_defs.find("KERB");
        if (kerb == m_defs.end()) kerb = m_defs.find("CURB");
        if (kerb != m_defs.end())
            ks::physics::TrackSurface::instance().setKerbGrip(kerb->second.friction);
        if (it != m_defs.end()) {
            ks::physics::TrackSurface::instance().setBaseGrip(it->second.friction);
            std::fprintf(stderr, "AcSurfacesLoader: base grip %.3f from %s\n",
                         it->second.friction, it->second.key.c_str());
        }
        return !m_defs.empty();
    }

    float frictionFor(const std::string& key) const {
        auto it = m_defs.find(upper(key));
        return it != m_defs.end() ? it->second.friction : 1.f;
    }

    const std::map<std::string, AcSurfaceDef>& defs() const { return m_defs; }

private:
    static void trim(std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    }
    static std::string upper(std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    std::map<std::string, AcSurfaceDef> m_defs;
};

} // namespace ac
} // namespace ks
