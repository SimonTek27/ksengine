#include "TrackLayout.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace ks {
namespace sim {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

// Minimal JSON helpers (same spirit as TrackDataJsonLoader — no deps).
std::string jsonTextValue(const std::string& text, size_t from, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t pos = text.find(pat, from);
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos + pat.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] != '"') return {};
    ++pos;
    std::string out;
    while (pos < text.size() && text[pos] != '"') {
        if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
        out.push_back(text[pos++]);
    }
    return out;
}

float jsonNumberValue(const std::string& text, size_t from, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t pos = text.find(pat, from);
    if (pos == std::string::npos) return 0.f;
    pos = text.find(':', pos + pat.size());
    if (pos == std::string::npos) return 0.f;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size()) return 0.f;
    char* end = nullptr;
    const float v = std::strtof(text.c_str() + pos, &end);
    if (end == text.c_str() + pos) return 0.f;
    return v > 0.f ? v : 0.f;
}

bool jsonBoolValue(const std::string& text, size_t from, const std::string& key, bool def = false) {
    const std::string pat = "\"" + key + "\"";
    size_t pos = text.find(pat, from);
    if (pos == std::string::npos) return def;
    pos = text.find(':', pos + pat.size());
    if (pos == std::string::npos) return def;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos + 4 <= text.size() && text.compare(pos, 4, "true") == 0) return true;
    if (pos + 5 <= text.size() && text.compare(pos, 5, "false") == 0) return false;
    return def;
}

} // namespace

void TrackLayoutCatalog::clear() {
    m_layouts.clear();
    m_trackRoot.clear();
    m_active = 0;
}

void TrackLayoutCatalog::add(const TrackLayoutInfo& info) {
    for (auto& existing : m_layouts) {
        if (existing.id == info.id) {
            existing = info;
            return;
        }
    }
    m_layouts.push_back(info);
}

bool TrackLayoutCatalog::looksLikeLayoutDir(const std::string& path) {
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return false;
    if (fs::exists(fs::path(path) / "data" / "data.json", ec)) return true;
    if (fs::exists(fs::path(path) / "data" / "surfaces.ini", ec)) return true;
    if (fs::exists(fs::path(path) / "surfaces.ini", ec)) return true;
    if (fs::is_directory(fs::path(path) / "ai", ec)) return true;
    for (auto& e : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (e.path().extension() == ".kn5") return true;
    }
    return false;
}

bool TrackLayoutCatalog::rootHasContent(const std::string& trackRoot) {
    return looksLikeLayoutDir(trackRoot);
}

bool TrackLayoutCatalog::loadManifest(const std::string& trackRoot) {
    const fs::path manifest = fs::path(trackRoot) / "layouts.json";
    std::error_code ec;
    if (!fs::exists(manifest, ec)) return false;

    std::ifstream in(manifest);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    size_t arr = text.find("\"layouts\"");
    if (arr == std::string::npos) return false;
    size_t bracket = text.find('[', arr);
    if (bracket == std::string::npos) return false;

    size_t pos = bracket + 1;
    int found = 0;
    while (pos < text.size()) {
        size_t obj = text.find('{', pos);
        if (obj == std::string::npos) break;
        size_t end = text.find('}', obj);
        if (end == std::string::npos) break;

        TrackLayoutInfo info;
        info.id = jsonTextValue(text, obj, "id");
        info.name = jsonTextValue(text, obj, "name");
        std::string folder = jsonTextValue(text, obj, "folder");
        info.lengthM = jsonNumberValue(text, obj, "length_m");
        if (info.lengthM <= 0.f) info.lengthM = jsonNumberValue(text, obj, "length");
        info.isDefault = jsonBoolValue(text, obj, "default", false);

        if (info.id.empty() && !folder.empty()) info.id = folder;
        if (info.id.empty()) {
            pos = end + 1;
            continue;
        }
        if (info.name.empty()) info.name = info.id;

        if (folder.empty() || folder == "." || folder == "./") {
            info.folder = trackRoot;
            info.isDefault = true;
        } else if (fs::path(folder).is_absolute()) {
            info.folder = folder;
        } else {
            info.folder = (fs::path(trackRoot) / folder).lexically_normal().string();
        }

        add(info);
        ++found;
        pos = end + 1;
    }
    return found > 0;
}

void TrackLayoutCatalog::scanSubdirs(const std::string& trackRoot) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(trackRoot, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        if (name == "data" || name == "ai" || name == "texture" || name == "textures" ||
            name == "ui" || name == "sound" || name == "sfx" || name == "baked" ||
            name == "." || name == "..")
            continue;
        if (!looksLikeLayoutDir(e.path().string())) continue;

        TrackLayoutInfo info;
        info.id = name;
        info.name = name;
        info.folder = e.path().string();
        info.isDefault = false;
        const fs::path dj = e.path() / "data" / "data.json";
        if (fs::exists(dj, ec)) {
            std::ifstream in(dj);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                const std::string t = ss.str();
                info.lengthM = jsonNumberValue(t, 0, "length_m");
                if (info.lengthM <= 0.f) info.lengthM = jsonNumberValue(t, 0, "length");
                std::string n = jsonTextValue(t, 0, "name");
                if (!n.empty()) info.name = n;
            }
        }
        add(info);
    }
}

void TrackLayoutCatalog::ensureDefault(const std::string& trackRoot) {
    if (!rootHasContent(trackRoot)) {
        if (m_layouts.empty()) {
            TrackLayoutInfo info;
            info.id = "default";
            info.name = fs::path(trackRoot).filename().string();
            info.folder = trackRoot;
            info.isDefault = true;
            add(info);
        }
        return;
    }
    bool hasDefault = false;
    for (const auto& l : m_layouts) {
        if (l.isDefault || l.id == "default" || l.folder == trackRoot) {
            hasDefault = true;
            break;
        }
    }
    if (!hasDefault) {
        TrackLayoutInfo info;
        info.id = "default";
        info.name = fs::path(trackRoot).filename().string();
        info.folder = trackRoot;
        info.isDefault = true;
        const fs::path dj = fs::path(trackRoot) / "data" / "data.json";
        std::error_code ec;
        if (fs::exists(dj, ec)) {
            std::ifstream in(dj);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                const std::string t = ss.str();
                info.lengthM = jsonNumberValue(t, 0, "length_m");
                if (info.lengthM <= 0.f) info.lengthM = jsonNumberValue(t, 0, "length");
                std::string n = jsonTextValue(t, 0, "name");
                if (!n.empty()) info.name = n;
            }
        }
        m_layouts.insert(m_layouts.begin(), info);
    }
}

int TrackLayoutCatalog::discover(const std::string& trackRoot) {
    clear();
    if (trackRoot.empty()) return 0;
    std::error_code ec;
    fs::path root = fs::path(trackRoot).lexically_normal();
    if (!fs::is_directory(root, ec)) return 0;
    m_trackRoot = root.string();

    const bool fromManifest = loadManifest(m_trackRoot);
    if (!fromManifest) scanSubdirs(m_trackRoot);
    ensureDefault(m_trackRoot);

    m_active = 0;
    for (size_t i = 0; i < m_layouts.size(); ++i) {
        if (m_layouts[i].isDefault || m_layouts[i].id == "default") {
            m_active = static_cast<int>(i);
            break;
        }
    }
    return count();
}

void TrackLayoutCatalog::setActive(int i) {
    if (i >= 0 && i < count()) m_active = i;
}

bool TrackLayoutCatalog::selectByIndex(int i) {
    if (i < 0 || i >= count()) return false;
    m_active = i;
    return true;
}

bool TrackLayoutCatalog::selectById(const std::string& id) {
    for (size_t i = 0; i < m_layouts.size(); ++i) {
        if (m_layouts[i].id == id) {
            m_active = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

const TrackLayoutInfo* TrackLayoutCatalog::active() const {
    if (m_layouts.empty()) return nullptr;
    return &m_layouts[static_cast<size_t>(m_active)];
}

const TrackLayoutInfo* TrackLayoutCatalog::findById(const std::string& id) const {
    for (const auto& l : m_layouts)
        if (l.id == id) return &l;
    return nullptr;
}

std::string TrackLayoutCatalog::activeFolder() const {
    const TrackLayoutInfo* a = active();
    return a ? a->folder : std::string{};
}

} // namespace sim
} // namespace ks
