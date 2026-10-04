#pragma once
/** Multi-layout catalog (P1.6 helper). */
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>

namespace ks {
namespace sim {

struct TrackLayoutInfo {
    std::string id;
    std::string name;
    std::string folder;
    float lengthM = 0.f;
};

class TrackLayoutCatalog {
public:
    void clear() { m_layouts.clear(); m_active = 0; }
    void add(const TrackLayoutInfo& info) { m_layouts.push_back(info); }
    const std::vector<TrackLayoutInfo>& layouts() const { return m_layouts; }
    int activeIndex() const { return m_active; }
    void setActive(int i) {
        if (i >= 0 && i < (int)m_layouts.size()) m_active = i;
    }
    const TrackLayoutInfo* active() const {
        if (m_layouts.empty()) return nullptr;
        return &m_layouts[static_cast<size_t>(m_active)];
    }
    bool selectById(const std::string& id) {
        for (size_t i = 0; i < m_layouts.size(); ++i) {
            if (m_layouts[i].id == id) { m_active = (int)i; return true; }
        }
        return false;
    }
private:
    std::vector<TrackLayoutInfo> m_layouts;
    int m_active = 0;
};

} // namespace sim
} // namespace ks
