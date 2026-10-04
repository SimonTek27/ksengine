#include "CADOBJParser.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

namespace ks::engine::fileformat {
namespace {

constexpr std::size_t kMaxTokenLen = 64;

struct Token {
    const char* begin = nullptr;
    std::size_t len = 0;

    std::string_view view() const { return std::string_view(begin, len); }
    bool empty() const { return len == 0; }
};

// Splits a line on spaces/tabs. Carriage returns are treated as whitespace so
// CRLF files need no pre-trim.
std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> out;
    const char* p = line.data();
    const char* const end = p + line.size();
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        const char* start = p;
        while (p < end && (*p != ' ' && *p != '\t' && *p != '\r')) ++p;
        if (p > start) out.push_back({start, static_cast<std::size_t>(p - start)});
    }
    return out;
}

bool parseFloat(const Token& token, float& out) {
    if (token.empty() || token.len >= kMaxTokenLen) return false;
    char buf[kMaxTokenLen + 1];
    std::memcpy(buf, token.begin, token.len);
    buf[token.len] = '\0';
    char* end = nullptr;
    const float value = std::strtof(buf, &end);
    if (end != buf + token.len || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parseIndex(const Token& token, std::int64_t& out) {
    if (token.empty() || token.len >= kMaxTokenLen) return false;
    char buf[kMaxTokenLen + 1];
    std::memcpy(buf, token.begin, token.len);
    buf[token.len] = '\0';
    char* end = nullptr;
    const long value = std::strtol(buf, &end, 10);
    if (end != buf + token.len) return false;
    out = static_cast<std::int64_t>(value);
    return true;
}

// OBJ corner: `v`, `v/vt`, `v//vn` or `v/vt/vn`. A field of 0 is absent.
struct Corner {
    std::int64_t v = 0;
    std::int64_t vt = 0;
    std::int64_t vn = 0;
};

bool parseCorner(const Token& token, Corner& out) {
    const char* p = token.begin;
    const char* const end = p + token.len;
    int field = 0;
    while (p <= end) {
        const char* slash = p;
        while (slash < end && *slash != '/') ++slash;
        const Token part{p, static_cast<std::size_t>(slash - p)};
        if (!part.empty()) {
            std::int64_t value = 0;
            if (!parseIndex(part, value)) return false;
            if (field == 0) out.v = value;
            else if (field == 1) out.vt = value;
            else if (field == 2) out.vn = value;
            else return false; // too many fields
        }
        if (slash == end) break;
        p = slash + 1;
        ++field;
    }
    return out.v != 0;
}

// OBJ indices are 1-based; a negative index counts back from the current
// element count. 0 is not a legal OBJ index.
bool resolveIndex(std::int64_t idx, std::size_t count, std::size_t& out) {
    if (idx > 0) {
        out = static_cast<std::size_t>(idx - 1);
        return out < count;
    }
    if (idx < 0) {
        const std::int64_t resolved = static_cast<std::int64_t>(count) + idx;
        if (resolved < 0) return false;
        out = static_cast<std::size_t>(resolved);
        return out < count;
    }
    return false;
}

std::string_view remainder(const std::string& line, const Token& first) {
    const char* p = first.begin + first.len;
    const char* const end = line.data() + line.size();
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    const char* stop = end;
    while (stop > p && (stop[-1] == ' ' || stop[-1] == '\t' || stop[-1] == '\r')) --stop;
    return std::string_view(p, static_cast<std::size_t>(stop - p));
}

std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    return path.substr(0, slash + 1);
}

} // namespace

bool CADOBJParser::loadFromFile(const std::string& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        m_scene = ObjScene();
        m_error = "cannot open " + path;
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    const bool ok = parse(content, directoryOf(path));
    m_scene.sourcePath = path;
    return ok;
}

bool CADOBJParser::loadFromString(const std::string& content, const std::string& basePath) {
    return parse(content, basePath);
}

bool CADOBJParser::parse(const std::string& content, const std::string& basePath) {
    m_scene = ObjScene();
    m_error.clear();
    m_positions.clear();
    m_normals.clear();
    m_texCoords.clear();
    m_weld.clear();
    m_current = nullptr;
    m_faceProblems = 0;

    auto fail = [this](const char* what, std::size_t lineNo) {
        if (m_error.empty()) {
            m_error = std::string(what) + " at line " + std::to_string(lineNo);
        }
    };

    std::size_t lineNo = 0;
    std::size_t pos = 0;
    while (pos <= content.size()) {
        const std::size_t eol = content.find('\n', pos);
        const std::size_t len =
            (eol == std::string::npos ? content.size() : eol) - pos;
        const std::string line = content.substr(pos, len);
        pos += len + 1;
        ++lineNo;
        if (eol == std::string::npos && len == 0) break;

        if (line.empty() || line[0] == '#') {
            if (eol == std::string::npos) break;
            continue;
        }

        const std::vector<Token> tokens = tokenize(line);
        if (tokens.empty()) {
            if (eol == std::string::npos) break;
            continue;
        }

        const std::string_view prefix = tokens[0].view();

        if (prefix == "v" || prefix == "vn" || prefix == "vt") {
            Vec3 v;
            Vec2 t;
            bool good = false;
            if (prefix == "vt") {
                good = tokens.size() >= 2 && parseFloat(tokens[1], t.x);
                if (good && tokens.size() >= 3) parseFloat(tokens[2], t.y);
            } else {
                good = tokens.size() >= 4 && parseFloat(tokens[1], v.x) &&
                       parseFloat(tokens[2], v.y) && parseFloat(tokens[3], v.z);
            }
            if (!good) {
                fail("malformed element", lineNo);
            } else if (prefix == "v") {
                m_positions.push_back(v);
            } else if (prefix == "vn") {
                m_normals.push_back(v);
            } else {
                m_texCoords.push_back(t);
            }
        } else if (prefix == "f") {
            if (tokens.size() < 4) {
                fail("face needs at least 3 corners", lineNo);
                ++m_faceProblems;
                continue;
            }

            std::vector<Corner> corners;
            corners.reserve(tokens.size() - 1);
            bool good = true;
            for (std::size_t i = 1; i < tokens.size() && good; ++i) {
                Corner c;
                if (!parseCorner(tokens[i], c)) {
                    fail("bad face corner", lineNo);
                    good = false;
                }
                corners.push_back(c);
            }

            // Resolve every corner before touching the mesh, so a face with a
            // bad index leaves no half-welded corners behind.
            struct Resolved {
                std::size_t v = 0;
                std::size_t t = 0;
                std::size_t n = 0;
                bool hasT = false;
                bool hasN = false;
            };
            std::vector<Resolved> resolved;
            resolved.reserve(corners.size());

            for (const Corner& c : corners) {
                if (!good) break;
                Resolved r;
                if (!resolveIndex(c.v, m_positions.size(), r.v)) {
                    fail("position index out of range", lineNo);
                    good = false;
                    break;
                }
                r.hasT = c.vt != 0;
                r.hasN = c.vn != 0;
                if (r.hasT && !resolveIndex(c.vt, m_texCoords.size(), r.t)) {
                    fail("texcoord index out of range", lineNo);
                    good = false;
                    break;
                }
                if (r.hasN && !resolveIndex(c.vn, m_normals.size(), r.n)) {
                    fail("normal index out of range", lineNo);
                    good = false;
                    break;
                }
                resolved.push_back(r);
            }

            if (!good || resolved.size() != corners.size()) {
                ++m_faceProblems;
                continue;
            }

            if (m_current == nullptr) {
                m_scene.meshes.emplace_back();
                m_current = &m_scene.meshes.back();
                m_current->name = "default";
            }

            std::vector<std::uint32_t> loop;
            loop.reserve(resolved.size());
            for (const Resolved& r : resolved) {
                const std::tuple<std::int64_t, std::int64_t, std::int64_t> key{
                    static_cast<std::int64_t>(r.v),
                    r.hasT ? static_cast<std::int64_t>(r.t) : static_cast<std::int64_t>(-1),
                    r.hasN ? static_cast<std::int64_t>(r.n) : static_cast<std::int64_t>(-1)};

                auto it = m_weld.find(key);
                if (it == m_weld.end()) {
                    const auto local = static_cast<std::uint32_t>(m_current->vertices.size());
                    m_current->vertices.push_back(m_positions[r.v]);
                    m_current->normals.push_back(r.hasN ? m_normals[r.n] : Vec3());
                    m_current->texCoords.push_back(r.hasT ? m_texCoords[r.t] : Vec2());
                    m_current->hasNormals = m_current->hasNormals || r.hasN;
                    m_current->hasTexCoords = m_current->hasTexCoords || r.hasT;
                    it = m_weld.emplace(key, local).first;
                }
                loop.push_back(it->second);
            }

            for (std::size_t i = 1; i + 1 < loop.size(); ++i) {
                m_current->indices.push_back(loop[0]);
                m_current->indices.push_back(loop[i]);
                m_current->indices.push_back(loop[i + 1]);
            }
            ++m_scene.faceCount;
        } else if (prefix == "o" || prefix == "g") {
            const std::string_view name = remainder(line, tokens[0]);
            if (!name.empty()) {
                m_scene.meshes.emplace_back();
                m_current = &m_scene.meshes.back();
                m_current->name.assign(name);
                m_weld.clear();
            }
        } else if (prefix == "usemtl") {
            const std::string_view name = remainder(line, tokens[0]);
            if (m_current == nullptr) {
                m_scene.meshes.emplace_back();
                m_current = &m_scene.meshes.back();
                m_current->name = "default";
            }
            m_current->materialName.assign(name);
        } else if (prefix == "mtllib") {
            const std::string_view name = remainder(line, tokens[0]);
            if (!name.empty()) loadMTL(basePath + std::string(name));
        }
        // `s`, `l`, `p`, `vp` and anything else are intentionally ignored.

        if (eol == std::string::npos) break;
    }

    m_scene.positionCount = m_positions.size();
    m_scene.texCoordCount = m_texCoords.size();
    m_scene.normalCount = m_normals.size();

    // `o`/`g` statements that carry no geometry leave an empty shell behind.
    m_current = nullptr;
    m_scene.meshes.erase(
        std::remove_if(m_scene.meshes.begin(), m_scene.meshes.end(),
                       [](const ObjMesh& mesh) { return mesh.indices.empty(); }),
        m_scene.meshes.end());

    for (const ObjMesh& mesh : m_scene.meshes) m_scene.triangleCount += mesh.triangleCount();

    if (m_scene.triangleCount == 0) {
        if (m_error.empty()) m_error = "no faces";
        return false;
    }
    // Any recorded problem — malformed element, bad corner, index out of
    // range — makes the parse unclean even when some faces did survive.
    return m_error.empty();
}

bool CADOBJParser::loadMTL(const std::string& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) return false;

    ObjMaterial* current = nullptr;
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<Token> tokens = tokenize(line);
        if (tokens.empty()) continue;

        const std::string_view prefix = tokens[0].view();
        if (prefix == "newmtl") {
            const std::string_view name = remainder(line, tokens[0]);
            if (name.empty()) continue;
            ObjMaterial mat;
            mat.name.assign(name);
            const std::string key(mat.name);
            const auto ins = m_scene.materials.emplace(key, std::move(mat));
            current = &ins.first->second;
            continue;
        }
        if (current == nullptr) continue;

        auto readVec3 = [&](Vec3& out) {
            return tokens.size() >= 4 && parseFloat(tokens[1], out.x) &&
                   parseFloat(tokens[2], out.y) && parseFloat(tokens[3], out.z);
        };
        auto readRest = [&](std::string& out) {
            out.assign(remainder(line, tokens[0]));
        };

        if (prefix == "Ka") readVec3(current->ka);
        else if (prefix == "Kd") readVec3(current->kd);
        else if (prefix == "Ks") readVec3(current->ks);
        else if (prefix == "Ns") {
            float v = 0.0f;
            if (tokens.size() >= 2 && parseFloat(tokens[1], v)) current->ns = v;
        } else if (prefix == "d" || prefix == "Tr") {
            float v = 0.0f;
            if (tokens.size() >= 2 && parseFloat(tokens[1], v)) current->d = v;
        } else if (prefix == "illum") {
            std::int64_t v = 0;
            if (tokens.size() >= 2 && parseIndex(tokens[1], v)) current->illum = static_cast<int>(v);
        } else if (prefix == "map_Kd") readRest(current->mapKd);
        else if (prefix == "map_Ks") readRest(current->mapKs);
        else if (prefix == "map_Bump" || prefix == "bump") readRest(current->mapBump);
    }
    return true;
}

} // namespace ks::engine::fileformat
