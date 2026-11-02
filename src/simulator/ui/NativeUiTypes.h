#pragma once
/** Immediate-mode UI primitives for Qt-free simulator overlays. */
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ks {
namespace sim {
namespace ui {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    static Color rgba(float r, float g, float b, float a = 1.f) { return {r, g, b, a}; }
    static Color rgb(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255) {
        return {R / 255.f, G / 255.f, B / 255.f, A / 255.f};
    }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

enum class DrawCmdType : uint8_t {
    RectFilled = 0,
    RectOutline,
    Line,
    Text,
    ProgressBar,
    VectorStroke, // polyline stored in pathPoints
    VectorFill    // convex polygon in pathPoints
};

struct DrawCmd {
    DrawCmdType type = DrawCmdType::RectFilled;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float thickness = 1.f;
    Color color;
    float value = 0.f;
    std::string text;
    float fontScale = 1.f;
    float maxWidth = 0.f; // > 0 wraps the text into lines of at most this width
    // Vector path flattened points (x,y pairs)
    std::vector<float> pathPoints;
    bool pathClosed = false;
};

struct UiVertex {
    float x, y;
    float r, g, b, a;
    float u, v;
};

class DrawList {
public:
    void clear() { m_cmds.clear(); }

    void addRectFilled(const Rect& r, const Color& c) {
        DrawCmd d;
        d.type = DrawCmdType::RectFilled;
        d.x0 = r.x; d.y0 = r.y; d.x1 = r.x + r.w; d.y1 = r.y + r.h;
        d.color = c;
        m_cmds.push_back(std::move(d));
    }

    void addRect(const Rect& r, const Color& c, float thickness = 1.f) {
        DrawCmd d;
        d.type = DrawCmdType::RectOutline;
        d.x0 = r.x; d.y0 = r.y; d.x1 = r.x + r.w; d.y1 = r.y + r.h;
        d.color = c;
        d.thickness = thickness;
        m_cmds.push_back(std::move(d));
    }

    void addLine(float x0, float y0, float x1, float y1, const Color& c, float thickness = 1.f) {
        DrawCmd d;
        d.type = DrawCmdType::Line;
        d.x0 = x0; d.y0 = y0; d.x1 = x1; d.y1 = y1;
        d.color = c;
        d.thickness = thickness;
        m_cmds.push_back(std::move(d));
    }

    void addText(float x, float y, const std::string& text, const Color& c, float scale = 1.f) {
        DrawCmd d;
        d.type = DrawCmdType::Text;
        d.x0 = x; d.y0 = y;
        d.color = c;
        d.text = text;
        d.fontScale = scale;
        m_cmds.push_back(std::move(d));
    }

    /** Text that wraps at `maxWidth` (pixels, 0 = single line). The wrapping
     *  itself is BitmapText's, so a long description cannot run out of the
     *  panel that holds it. */
    void addTextWrapped(float x, float y, const std::string& text, const Color& c,
                        float scale, float maxWidth) {
        DrawCmd d;
        d.type = DrawCmdType::Text;
        d.x0 = x; d.y0 = y;
        d.color = c;
        d.text = text;
        d.fontScale = scale;
        d.maxWidth = maxWidth;
        m_cmds.push_back(std::move(d));
    }

    void addProgressBar(const Rect& r, float value01, const Color& fill, const Color& bg) {
        addRectFilled(r, bg);
        Rect f = r;
        f.w = r.w * std::clamp(value01, 0.f, 1.f);
        DrawCmd d;
        d.type = DrawCmdType::ProgressBar;
        d.x0 = f.x; d.y0 = f.y; d.x1 = f.x + f.w; d.y1 = f.y + f.h;
        d.color = fill;
        d.value = value01;
        m_cmds.push_back(std::move(d));
    }

    /** Append stroked path (already flattened x,y pairs). */
    void addPolyline(const std::vector<float>& xy, const Color& c, float width, bool closed = false) {
        DrawCmd d;
        d.type = DrawCmdType::VectorStroke;
        d.color = c;
        d.thickness = width;
        d.pathPoints = xy;
        d.pathClosed = closed;
        m_cmds.push_back(std::move(d));
    }

    void addPolygonFill(const std::vector<float>& xy, const Color& c) {
        DrawCmd d;
        d.type = DrawCmdType::VectorFill;
        d.color = c;
        d.pathPoints = xy;
        d.pathClosed = true;
        m_cmds.push_back(std::move(d));
    }

    const std::vector<DrawCmd>& commands() const { return m_cmds; }

private:
    std::vector<DrawCmd> m_cmds;
};

} // namespace ui
} // namespace sim
} // namespace ks
