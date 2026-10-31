#pragma once
/**
 * Multi-layout track catalog (Phase 3.3).
 *
 * A track folder may expose several race layouts that share the same base
 * assets (textures, shared kn5) but differ in AI line, length, pit/start
 * metadata and optionally their own model / data tree.
 *
 * Discovery order:
 *   1. layouts.json at the track root (authoritative when present)
 *   2. Immediate subdirectories that look like layouts (data/, ai/, *.kn5)
 *   3. Always include a "default" entry pointing at the track root when the
 *      root itself has usable content
 *
 * switchLayout() on SimulationLoop rebinds layout-specific paths without
 * tearing down cars / renderer / session stack.
 */

#include <string>
#include <vector>

namespace ks {
namespace sim {

struct TrackLayoutInfo {
    std::string id;       // stable key ("default", "gp", "national", ...)
    std::string name;     // display name
    std::string folder;   // absolute or track-relative path to layout root
    float lengthM = 0.f;  // 0 = unknown / use AI spline later
    bool isDefault = false;
};

class TrackLayoutCatalog {
public:
    void clear();

    /** Scan track root and populate the catalog. Returns number of layouts. */
    int discover(const std::string& trackRoot);

    void add(const TrackLayoutInfo& info);
    const std::vector<TrackLayoutInfo>& layouts() const { return m_layouts; }
    bool empty() const { return m_layouts.empty(); }
    int count() const { return static_cast<int>(m_layouts.size()); }

    int activeIndex() const { return m_active; }
    void setActive(int i);
    bool selectById(const std::string& id);
    bool selectByIndex(int i);

    const TrackLayoutInfo* active() const;
    const TrackLayoutInfo* findById(const std::string& id) const;

    /** Resolved absolute folder for the active layout (or empty). */
    std::string activeFolder() const;

    /** Track root used at last discover(). */
    const std::string& trackRoot() const { return m_trackRoot; }

private:
    bool loadManifest(const std::string& trackRoot);
    void scanSubdirs(const std::string& trackRoot);
    void ensureDefault(const std::string& trackRoot);
    static bool looksLikeLayoutDir(const std::string& path);
    static bool rootHasContent(const std::string& trackRoot);

    std::vector<TrackLayoutInfo> m_layouts;
    std::string m_trackRoot;
    int m_active = 0;
};

} // namespace sim
} // namespace ks
