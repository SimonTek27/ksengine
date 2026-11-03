#pragma once
/**
 * SVG icons for the Qt-free overlays.
 *
 * The overlays could stroke any polyline but had no way to describe a
 * picture: a menu row was a coloured rectangle and a string. Icons are
 * authored as plain SVG (24x24 viewBox, stroke only) and parsed here into
 * polylines, so they go through the same tessellator as the rest of the UI
 * and stay sharp at any size instead of being baked into a bitmap.
 *
 * Supported: <svg viewBox>, <path d>, <circle>, <rect> (with rx), <line>,
 * <polyline>, <polygon>, with `stroke-width` on the root or the element.
 * Path commands: M m L l H h V v C c S s Q q T t A a Z z, including implicit
 * repeats and the "1.5-3" separator style. Curves and elliptical arcs are
 * flattened to polylines.
 *
 * Deliberately not supported: fills, gradients, transforms, <g>, CSS. These
 * are line icons; a filled or transformed shape is skipped rather than drawn
 * wrong, which keeps a concave outline from being filled as a triangle fan.
 */
#include "NativeUiTypes.h"
#include "VectorPath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ks {
namespace sim {
namespace ui {

inline constexpr float kSvgPi = 3.14159265358979f;

/** One drawing element: its flattened subpaths and its line weight (viewBox
 *  units). Subpaths are kept apart because a stroke must not join them. */
struct SvgShape {
    std::vector<std::vector<Vec2>> subpaths;
    float strokeWidth = 2.f;
};

/** Where a viewBox lands on screen, so callers and tests share one mapping. */
struct IconTransform {
    float scale = 1.f;
    float ox = 0.f;
    float oy = 0.f;
    Vec2 apply(const Vec2& p) const { return {ox + p.x * scale, oy + p.y * scale}; }
};

namespace svg_detail {

/** Numbers as SVG writes them: "1.5-3" is two numbers, ".5" is one. */
struct NumberScanner {
    const char* p = nullptr;

    void skip() {
        while (*p != '\0' && (std::isspace(static_cast<unsigned char>(*p)) || *p == ','))
            ++p;
    }

    bool done() const { return *p == '\0'; }

    bool hasCommand() const {
        return std::isalpha(static_cast<unsigned char>(*p)) != 0;
    }

    bool next(float& out) {
        skip();
        if (*p == '\0') return false;
        char* end = nullptr;
        const double value = std::strtod(p, &end);
        if (end == p) return false;
        p = end;
        out = static_cast<float>(value);
        return true;
    }
};

inline void flattenCubic(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3) {
    const float len = (p1 - p0).length() + (p2 - p1).length() + (p3 - p2).length();
    const int steps = std::clamp(static_cast<int>(std::ceil(len / 1.2f)), 6, 48);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float mt = 1.f - t;
        out.push_back(p0 * (mt * mt * mt) + p1 * (3.f * mt * mt * t) +
                      p2 * (3.f * mt * t * t) + p3 * (t * t * t));
    }
}

inline void flattenQuad(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2) {
    const float len = (p1 - p0).length() + (p2 - p1).length();
    const int steps = std::clamp(static_cast<int>(std::ceil(len / 1.2f)), 5, 40);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float mt = 1.f - t;
        out.push_back(p0 * (mt * mt) + p1 * (2.f * mt * t) + p2 * (t * t));
    }
}

/** SVG elliptical arc (endpoint form) sampled as a polyline. */
inline void flattenArc(std::vector<Vec2>& out, Vec2 from, float rx, float ry, float rotDeg,
                       bool largeArc, bool sweep, Vec2 to) {
    if (rx == 0.f || ry == 0.f) {
        out.push_back(to);
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const float phi = rotDeg * (kSvgPi / 180.f);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);

    const float dx2 = (from.x - to.x) * 0.5f;
    const float dy2 = (from.y - to.y) * 0.5f;
    const float x1p = cosPhi * dx2 + sinPhi * dy2;
    const float y1p = -sinPhi * dx2 + cosPhi * dy2;

    // Radii too small for the endpoints: scale them up (SVG spec F.6.6).
    const float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.f) {
        const float grow = std::sqrt(lambda);
        rx *= grow;
        ry *= grow;
    }

    const float rx2 = rx * rx;
    const float ry2 = ry * ry;
    const float numerator = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
    const float denominator = rx2 * y1p * y1p + ry2 * x1p * x1p;
    if (denominator <= 1e-9f) {
        out.push_back(to);
        return;
    }
    float factor = std::sqrt(std::max(0.f, numerator / denominator));
    if (largeArc == sweep) factor = -factor;

    const float cxp = factor * (rx * y1p / ry);
    const float cyp = factor * (-ry * x1p / rx);
    const float cx = cosPhi * cxp - sinPhi * cyp + (from.x + to.x) * 0.5f;
    const float cy = sinPhi * cxp + cosPhi * cyp + (from.y + to.y) * 0.5f;

    const auto signedAngle = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float lengths = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        const float ratio = lengths > 1e-9f ? dot / lengths : 1.f;
        float angle = std::acos(std::clamp(ratio, -1.f, 1.f));
        if (ux * vy - uy * vx < 0.f) angle = -angle;
        return angle;
    };

    const float ux = (x1p - cxp) / rx;
    const float uy = (y1p - cyp) / ry;
    const float vx = (-x1p - cxp) / rx;
    const float vy = (-y1p - cyp) / ry;
    const float startAngle = signedAngle(1.f, 0.f, ux, uy);
    float delta = signedAngle(ux, uy, vx, vy);
    if (!sweep && delta > 0.f) delta -= 2.f * kSvgPi;
    else if (sweep && delta < 0.f) delta += 2.f * kSvgPi;

    const int steps = std::max(4, static_cast<int>(std::ceil(std::fabs(delta) / (kSvgPi / 20.f))));
    for (int i = 1; i <= steps; ++i) {
        const float theta = startAngle + delta * (static_cast<float>(i) / static_cast<float>(steps));
        out.push_back({cx + cosPhi * rx * std::cos(theta) - sinPhi * ry * std::sin(theta),
                       cy + sinPhi * rx * std::cos(theta) + cosPhi * ry * std::sin(theta)});
    }
}

/** Parse a `d` attribute into disjoint subpaths (a Z closes and starts over). */
inline void parsePathData(const char* d, std::vector<std::vector<Vec2>>& out) {
    NumberScanner scan;
    scan.p = d;

    Vec2 cur{0.f, 0.f};
    Vec2 start{0.f, 0.f};
    Vec2 lastCubic{0.f, 0.f};
    Vec2 lastQuad{0.f, 0.f};
    char command = 0;
    char previous = 0;

    std::vector<Vec2> contour;
    const auto flush = [&out, &contour]() {
        if (contour.size() >= 2) out.push_back(contour);
        contour.clear();
    };
    const auto begin = [&contour](Vec2 p) { contour.push_back(p); };

    while (true) {
        scan.skip();
        if (scan.done()) break;

        if (scan.hasCommand()) {
            command = *scan.p;
            ++scan.p;
        } else if (command == 0) {
            break; // numbers before any command: nothing to draw
        } else if (command == 'M') {
            command = 'L'; // implicit repeats of moveto are linetos
        } else if (command == 'm') {
            command = 'l';
        }

        float a = 0, b = 0, c = 0, e = 0, f = 0, g = 0, h = 0;
        switch (command) {
        case 'M': case 'm': {
            if (!scan.next(a) || !scan.next(b)) { scan.p += std::strlen(scan.p); return; }
            flush();
            cur = (command == 'm') ? Vec2{cur.x + a, cur.y + b} : Vec2{a, b};
            start = cur;
            begin(cur);
            break;
        }
        case 'L': case 'l': {
            if (!scan.next(a) || !scan.next(b)) { flush(); return; }
            cur = (command == 'l') ? Vec2{cur.x + a, cur.y + b} : Vec2{a, b};
            if (contour.empty()) begin(start);
            contour.push_back(cur);
            break;
        }
        case 'H': case 'h': {
            if (!scan.next(a)) { flush(); return; }
            cur = (command == 'h') ? Vec2{cur.x + a, cur.y} : Vec2{a, cur.y};
            if (contour.empty()) begin(start);
            contour.push_back(cur);
            break;
        }
        case 'V': case 'v': {
            if (!scan.next(a)) { flush(); return; }
            cur = (command == 'v') ? Vec2{cur.x, cur.y + a} : Vec2{cur.x, a};
            if (contour.empty()) begin(start);
            contour.push_back(cur);
            break;
        }
        case 'C': case 'c': {
            if (!scan.next(a) || !scan.next(b) || !scan.next(c) || !scan.next(e) ||
                !scan.next(f) || !scan.next(g)) { flush(); return; }
            Vec2 c1{(command == 'c') ? cur.x + a : a, (command == 'c') ? cur.y + b : b};
            Vec2 c2{(command == 'c') ? cur.x + c : c, (command == 'c') ? cur.y + e : e};
            Vec2 end{(command == 'c') ? cur.x + f : f, (command == 'c') ? cur.y + g : g};
            if (contour.empty()) begin(cur);
            flattenCubic(contour, cur, c1, c2, end);
            lastCubic = c2;
            cur = end;
            break;
        }
        case 'S': case 's': {
            if (!scan.next(a) || !scan.next(b) || !scan.next(c) || !scan.next(e)) { flush(); return; }
            const bool smooth = (previous == 'C' || previous == 'c' || previous == 'S' || previous == 's');
            Vec2 c1 = smooth ? Vec2{2.f * cur.x - lastCubic.x, 2.f * cur.y - lastCubic.y} : cur;
            Vec2 c2{(command == 's') ? cur.x + a : a, (command == 's') ? cur.y + b : b};
            Vec2 end{(command == 's') ? cur.x + c : c, (command == 's') ? cur.y + e : e};
            if (contour.empty()) begin(cur);
            flattenCubic(contour, cur, c1, c2, end);
            lastCubic = c2;
            cur = end;
            break;
        }
        case 'Q': case 'q': {
            if (!scan.next(a) || !scan.next(b) || !scan.next(c) || !scan.next(e)) { flush(); return; }
            Vec2 c1{(command == 'q') ? cur.x + a : a, (command == 'q') ? cur.y + b : b};
            Vec2 end{(command == 'q') ? cur.x + c : c, (command == 'q') ? cur.y + e : e};
            if (contour.empty()) begin(cur);
            flattenQuad(contour, cur, c1, end);
            lastQuad = c1;
            cur = end;
            break;
        }
        case 'T': case 't': {
            if (!scan.next(a) || !scan.next(b)) { flush(); return; }
            const bool smooth = (previous == 'Q' || previous == 'q' || previous == 'T' || previous == 't');
            Vec2 c1 = smooth ? Vec2{2.f * cur.x - lastQuad.x, 2.f * cur.y - lastQuad.y} : cur;
            Vec2 end{(command == 't') ? cur.x + a : a, (command == 't') ? cur.y + b : b};
            if (contour.empty()) begin(cur);
            flattenQuad(contour, cur, c1, end);
            lastQuad = c1;
            cur = end;
            break;
        }
        case 'A': case 'a': {
            if (!scan.next(a) || !scan.next(b) || !scan.next(c) || !scan.next(e) ||
                !scan.next(f) || !scan.next(g) || !scan.next(h)) { flush(); return; }
            Vec2 end{(command == 'a') ? cur.x + g : g, (command == 'a') ? cur.y + h : h};
            if (contour.empty()) begin(cur);
            flattenArc(contour, cur, a, b, c, e != 0.f, f != 0.f, end);
            cur = end;
            break;
        }
        case 'Z': case 'z': {
            if (!contour.empty()) {
                if (std::fabs(contour.back().x - start.x) > 1e-4f ||
                    std::fabs(contour.back().y - start.y) > 1e-4f)
                    contour.push_back(start);
                flush();
            }
            cur = start;
            break;
        }
        default:
            // Unknown command: skip its numbers so a stray token cannot loop.
            while (scan.next(a)) { }
            flush();
            return;
        }
        if (command != 'Z' && command != 'z') previous = command;
    }
    flush();
}

/** Value of `name` in a raw start tag (`d="..."` / `cx='12'`). */
inline std::string attrText(const std::string& tag, const char* name) {
    const std::string key = name;
    size_t pos = 0;
    while ((pos = tag.find(key, pos)) != std::string::npos) {
        const size_t after = pos + key.size();
        // Must be a whole attribute name, not a suffix of a longer one.
        const bool startsWord = pos == 0 ||
            std::isspace(static_cast<unsigned char>(tag[pos - 1])) || tag[pos - 1] == '<';
        size_t cursor = after;
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])))
            ++cursor;
        if (startsWord && cursor < tag.size() && tag[cursor] == '=') {
            ++cursor;
            while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])))
                ++cursor;
            if (cursor >= tag.size()) return {};
            const char quote = tag[cursor];
            if (quote == '"' || quote == '\'') {
                const size_t end = tag.find(quote, cursor + 1);
                if (end == std::string::npos) return {};
                return tag.substr(cursor + 1, end - cursor - 1);
            }
            const size_t end = tag.find_first_of(" \t\r\n>", cursor);
            return tag.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
        }
        pos = after;
    }
    return {};
}

inline float attrNumber(const std::string& tag, const char* name, float fallback) {
    const std::string text = svg_detail::attrText(tag, name);
    if (text.empty()) return fallback;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? fallback : static_cast<float>(value);
}

inline bool hasAttr(const std::string& tag, const char* name) {
    return !svg_detail::attrText(tag, name).empty();
}

/** Points of a `points="x,y x,y"` attribute. */
inline std::vector<Vec2> parsePoints(const std::string& text) {
    std::vector<Vec2> points;
    NumberScanner scan;
    scan.p = text.c_str();
    while (true) {
        float x = 0, y = 0;
        if (!scan.next(x) || !scan.next(y)) break;
        points.push_back({x, y});
    }
    return points;
}

/** One circle as a closed polyline.</br>
 *  A stroked circle is a dozen segments at icon size; 48 is smooth at 64 px. */
inline void addCircle(std::vector<Vec2>& out, float cx, float cy, float r, int segments = 48) {
    for (int i = 0; i <= segments; ++i) {
        const float t = (2.f * kSvgPi) * (static_cast<float>(i) / static_cast<float>(segments));
        out.push_back({cx + r * std::cos(t), cy + r * std::sin(t)});
    }
}

/** Rounded rectangle as a closed polyline (rx clamped to half the smaller side). */
inline void addRoundedRect(std::vector<Vec2>& out, float x, float y, float w, float h, float rx) {
    const float r = std::clamp(rx, 0.f, std::min(w, h) * 0.5f);
    if (r <= 1e-4f) {
        out.push_back({x, y});
        out.push_back({x + w, y});
        out.push_back({x + w, y + h});
        out.push_back({x, y + h});
        out.push_back({x, y});
        return;
    }
    const int steps = 8;
    const auto corner = [&out, steps](float cx, float cy, float fromDeg, float toDeg, float radius) {
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float angle = (fromDeg + (toDeg - fromDeg) * t) * (kSvgPi / 180.f);
            out.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
        }
    };
    corner(x + w - r, y + r, -90.f, 0.f, r);
    corner(x + w - r, y + h - r, 0.f, 90.f, r);
    corner(x + r, y + h - r, 90.f, 180.f, r);
    corner(x + r, y + r, 180.f, 270.f, r);
    out.push_back(out.front());
}

} // namespace svg_detail

class SvgIcon {
public:
    /** Parse one SVG document. Returns false and keeps the icon empty when
     *  nothing drawable is found, so a broken icon never draws garbage. */
    bool parse(const char* svgText) {
        m_shapes.clear();
        m_viewBoxX = 0.f;
        m_viewBoxY = 0.f;
        m_viewBoxW = 24.f;
        m_viewBoxH = 24.f;
        m_rootStroke = 2.f;
        if (svgText == nullptr || *svgText == '\0') return false;

        const std::string text = svgText;
        readViewBox(text);
        readRootStroke(text);

        size_t cursor = 0;
        while ((cursor = text.find('<', cursor)) != std::string::npos) {
            const size_t close = text.find('>', cursor);
            if (close == std::string::npos) break;
            const std::string tag = text.substr(cursor, close - cursor + 1);
            cursor = close + 1;
            if (tag.size() < 3 || tag[1] == '/' || tag[1] == '?' || tag[1] == '!') continue;

            const auto isTag = [&tag](const char* name) {
                const size_t len = std::strlen(name);
                return tag.compare(1, len, name) == 0 &&
                       (tag.size() > 1 + len) &&
                       (std::isspace(static_cast<unsigned char>(tag[1 + len])) ||
                        tag[1 + len] == '/' || tag[1 + len] == '>');
            };

            SvgShape shape;
            shape.strokeWidth = svg_detail::attrNumber(tag, "stroke-width", m_rootStroke);
            if (isTag("path")) {
                const std::string d = svg_detail::attrText(tag, "d");
                if (!d.empty()) svg_detail::parsePathData(d.c_str(), shape.subpaths);
            } else if (isTag("circle")) {
                const float cx = svg_detail::attrNumber(tag, "cx", 0.f);
                const float cy = svg_detail::attrNumber(tag, "cy", 0.f);
                const float r = svg_detail::attrNumber(tag, "r", 0.f);
                if (r > 0.f) {
                    std::vector<Vec2> ring;
                    svg_detail::addCircle(ring, cx, cy, r);
                    shape.subpaths.push_back(std::move(ring));
                }
            } else if (isTag("rect")) {
                const float x = svg_detail::attrNumber(tag, "x", 0.f);
                const float y = svg_detail::attrNumber(tag, "y", 0.f);
                const float w = svg_detail::attrNumber(tag, "width", 0.f);
                const float h = svg_detail::attrNumber(tag, "height", 0.f);
                const float rx = svg_detail::attrNumber(tag, "rx", svg_detail::attrNumber(tag, "ry", 0.f));
                if (w > 0.f && h > 0.f) {
                    std::vector<Vec2> box;
                    svg_detail::addRoundedRect(box, x, y, w, h, rx);
                    shape.subpaths.push_back(std::move(box));
                }
            } else if (isTag("line")) {
                std::vector<Vec2> segment;
                segment.push_back({svg_detail::attrNumber(tag, "x1", 0.f), svg_detail::attrNumber(tag, "y1", 0.f)});
                segment.push_back({svg_detail::attrNumber(tag, "x2", 0.f), svg_detail::attrNumber(tag, "y2", 0.f)});
                shape.subpaths.push_back(std::move(segment));
            } else if (isTag("polyline") || isTag("polygon")) {
                std::vector<Vec2> points = svg_detail::parsePoints(svg_detail::attrText(tag, "points"));
                const bool closed = isTag("polygon");
                if (closed && points.size() >= 2)
                    points.push_back(points.front());
                if (points.size() >= 2) shape.subpaths.push_back(std::move(points));
            } else {
                continue; // <svg>, <title>, <g>, <defs>... nothing to draw here
            }

            if (!shape.subpaths.empty()) m_shapes.push_back(std::move(shape));
        }
        return !m_shapes.empty();
    }

    bool valid() const { return !m_shapes.empty() && m_viewBoxW > 0.f && m_viewBoxH > 0.f; }
    float viewBoxWidth() const { return m_viewBoxW; }
    float viewBoxHeight() const { return m_viewBoxH; }
    const std::vector<SvgShape>& shapes() const { return m_shapes; }

    /** Where the viewBox lands inside a `size` x `size` box at (x, y):
     *  aspect preserved, centred (SVG's default preserveAspectRatio). */
    IconTransform transformTo(float x, float y, float size) const {
        IconTransform t;
        if (!valid() || size <= 0.f) return t;
        t.scale = size / std::max(m_viewBoxW, m_viewBoxH);
        t.ox = x + (size - m_viewBoxW * t.scale) * 0.5f - m_viewBoxX * t.scale;
        t.oy = y + (size - m_viewBoxH * t.scale) * 0.5f - m_viewBoxY * t.scale;
        return t;
    }

    /** Stroke the icon into a draw list. `weightScale` thickens the line on
     *  the selected row without touching the geometry. */
    void draw(DrawList& dl, float x, float y, float size, const Color& colour,
              float weightScale = 1.f) const {
        if (!valid() || size <= 1.f) return;
        const IconTransform t = transformTo(x, y, size);
        for (const auto& shape : m_shapes) {
            const float width = std::max(0.75f, shape.strokeWidth * t.scale * weightScale);
            for (const auto& subpath : shape.subpaths) {
                if (subpath.size() < 2) continue;
                std::vector<float> xy;
                xy.reserve(subpath.size() * 2);
                for (const auto& point : subpath) {
                    const Vec2 mapped = t.apply(point);
                    xy.push_back(mapped.x);
                    xy.push_back(mapped.y);
                }
                dl.addPolyline(xy, colour, width);
            }
        }
    }

private:
    void readViewBox(const std::string& text) {
        const size_t svgTag = text.find("<svg");
        if (svgTag == std::string::npos) return;
        const size_t end = text.find('>', svgTag);
        const std::string tag = text.substr(svgTag, end == std::string::npos ? std::string::npos
                                                                           : end - svgTag);
        const std::string box = svg_detail::attrText(tag, "viewBox");
        if (box.empty()) return;
        float values[4] = {0.f, 0.f, 24.f, 24.f};
        svg_detail::NumberScanner scan;
        scan.p = box.c_str();
        for (float& value : values) {
            if (!scan.next(value)) return;
        }
        if (values[2] <= 0.f || values[3] <= 0.f) return;
        m_viewBoxX = values[0];
        m_viewBoxY = values[1];
        m_viewBoxW = values[2];
        m_viewBoxH = values[3];
    }

    void readRootStroke(const std::string& text) {
        const size_t svgTag = text.find("<svg");
        if (svgTag == std::string::npos) return;
        const size_t end = text.find('>', svgTag);
        const std::string tag = text.substr(svgTag, end == std::string::npos ? std::string::npos
                                                                           : end - svgTag);
        m_rootStroke = svg_detail::attrNumber(tag, "stroke-width", 2.f);
    }

    float m_viewBoxX = 0.f;
    float m_viewBoxY = 0.f;
    float m_viewBoxW = 24.f;
    float m_viewBoxH = 24.f;
    float m_rootStroke = 2.f;
    std::vector<SvgShape> m_shapes;
};

} // namespace ui
} // namespace sim
} // namespace ks
