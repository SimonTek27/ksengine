#pragma once
/**
 * Batches DrawList into a dynamic vertex buffer.
 * Text → BitmapText; vectors → VectorPath tessellation.
 */
#include "NativeUiTypes.h"
#include "FontAtlas.h"
#include "BitmapText.h"
#include "VectorPath.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace ks {
namespace sim {
namespace ui {

class UiRenderer {
public:
    UiRenderer() : m_font(std::make_shared<FontAtlas>()) {}

    void setFont(std::shared_ptr<FontAtlas> font) {
        if (font) { m_font = std::move(font); m_dirty = true; }
    }
    const FontAtlas& font() const { return *m_font; }
    FontAtlas& font() { return *m_font; }

    void setViewport(int width, int height) {
        if (width != m_w || height != m_h) {
            m_w = width; m_h = height; m_dirty = true;
        }
    }

    void beginFrame() { m_list.clear(); m_dirty = true; }
    DrawList& list() { return m_list; }
    const DrawList& list() const { return m_list; }

    void addTextStyled(float x, float y, const std::string& text, const TextStyle& style) {
        BitmapText bt(*m_font);
        bt.addStyledToDrawList(m_list, x, y, text, style);
        m_dirty = true;
    }

    /** Stroke a vector path into this frame's draw list. */
    void addPathStroke(const VectorPath& path, const StrokeStyle& style) {
        auto pts = path.flatten();
        std::vector<float> xy;
        xy.reserve(pts.size() * 2);
        for (auto& p : pts) { xy.push_back(p.x); xy.push_back(p.y); }
        m_list.addPolyline(xy, style.color, style.width, style.closed);
        m_dirty = true;
    }

    void addPathFill(const VectorPath& path, const FillStyle& style) {
        auto pts = path.flatten();
        std::vector<float> xy;
        xy.reserve(pts.size() * 2);
        for (auto& p : pts) { xy.push_back(p.x); xy.push_back(p.y); }
        m_list.addPolygonFill(xy, style.color);
        m_dirty = true;
    }

    void endFrame() {
        if (m_dirty) rebuildVertices();
        m_dirty = false;
    }

    const std::vector<UiVertex>& vertices() const { return m_vertices; }
    const std::vector<uint32_t>& indices() const { return m_indices; }
    int viewportWidth() const { return m_w; }
    int viewportHeight() const { return m_h; }
    const std::vector<uint8_t>& atlasPixels() const { return m_font->pixelsR8(); }
    int atlasWidth() const { return m_font->width(); }
    int atlasHeight() const { return m_font->height(); }

    TextMetrics measure(const std::string& text, const TextStyle& style) const {
        return BitmapText(*m_font).measure(text, style);
    }

private:
    void pushQuadUV(float x0, float y0, float x1, float y1,
                    float u0, float v0, float u1, float v1, const Color& c) {
        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        m_vertices.push_back({x0, y0, c.r, c.g, c.b, c.a, u0, v0});
        m_vertices.push_back({x1, y0, c.r, c.g, c.b, c.a, u1, v0});
        m_vertices.push_back({x1, y1, c.r, c.g, c.b, c.a, u1, v1});
        m_vertices.push_back({x0, y1, c.r, c.g, c.b, c.a, u0, v1});
        m_indices.push_back(base + 0); m_indices.push_back(base + 1); m_indices.push_back(base + 2);
        m_indices.push_back(base + 0); m_indices.push_back(base + 2); m_indices.push_back(base + 3);
    }

    /** A UV that is guaranteed to land inside the atlas' white block. Path
     *  geometry gives every vertex the same texel, and the block's corner is
     *  not a safe sample under nearest sampling (it can fall on the edge
     *  texel), so strokes and fills point at the middle of the block. */
    void whiteCenterUV(float& u, float& v) const {
        float u0, v0, u1, v1;
        m_font->whiteUV(u0, v0, u1, v1);
        u = (u0 + u1) * 0.5f;
        v = (v0 + v1) * 0.5f;
    }

    void pushSolid(float x0, float y0, float x1, float y1, const Color& c) {
        float u0, v0, u1, v1;
        m_font->whiteUV(u0, v0, u1, v1);
        pushQuadUV(x0, y0, x1, y1, u0, v0, u1, v1, c);
    }

    void emitStrokeFromPoints(const std::vector<float>& xy, float width, const Color& c, bool closed) {
        VectorPath vp;
        if (xy.size() < 4) return;
        vp.moveTo(xy[0], xy[1]);
        for (size_t i = 2; i + 1 < xy.size(); i += 2)
            vp.lineTo(xy[i], xy[i + 1]);
        if (closed) vp.close();
        float u0 = 0.f, v0 = 0.f;
        whiteCenterUV(u0, v0);
        StrokeStyle st;
        st.color = c;
        st.width = width;
        st.closed = closed;
        vp.stroke(st, m_vertices, m_indices, u0, v0, u0, v0);
    }

    void emitFillFromPoints(const std::vector<float>& xy, const Color& c) {
        VectorPath vp;
        if (xy.size() < 6) return;
        vp.moveTo(xy[0], xy[1]);
        for (size_t i = 2; i + 1 < xy.size(); i += 2)
            vp.lineTo(xy[i], xy[i + 1]);
        vp.close();
        float u0 = 0.f, v0 = 0.f;
        whiteCenterUV(u0, v0);
        FillStyle fs;
        fs.color = c;
        vp.fillConvex(fs, m_vertices, m_indices, u0, v0, u0, v0);
    }

    void rebuildVertices() {
        m_vertices.clear();
        m_indices.clear();
        BitmapText bt(*m_font);

        for (const auto& cmd : m_list.commands()) {
            switch (cmd.type) {
            case DrawCmdType::RectFilled:
            case DrawCmdType::ProgressBar:
                pushSolid(cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.color);
                break;
            case DrawCmdType::RectOutline: {
                const float t = cmd.thickness;
                pushSolid(cmd.x0, cmd.y0, cmd.x1, cmd.y0 + t, cmd.color);
                pushSolid(cmd.x0, cmd.y1 - t, cmd.x1, cmd.y1, cmd.color);
                pushSolid(cmd.x0, cmd.y0, cmd.x0 + t, cmd.y1, cmd.color);
                pushSolid(cmd.x1 - t, cmd.y0, cmd.x1, cmd.y1, cmd.color);
                break;
            }
            case DrawCmdType::Line: {
                VectorPath vp;
                vp.moveTo(cmd.x0, cmd.y0);
                vp.lineTo(cmd.x1, cmd.y1);
                float u0 = 0.f, v0 = 0.f;
                whiteCenterUV(u0, v0);
                StrokeStyle st;
                st.color = cmd.color;
                st.width = cmd.thickness;
                vp.stroke(st, m_vertices, m_indices, u0, v0, u0, v0);
                break;
            }
            case DrawCmdType::Text: {
                TextStyle st;
                st.color = cmd.color;
                st.scale = cmd.fontScale;
                st.maxWidth = cmd.maxWidth;
                bt.emit(cmd.text, cmd.x0, cmd.y0, st, m_vertices, m_indices);
                break;
            }
            case DrawCmdType::VectorStroke:
                emitStrokeFromPoints(cmd.pathPoints, cmd.thickness, cmd.color, cmd.pathClosed);
                break;
            case DrawCmdType::VectorFill:
                emitFillFromPoints(cmd.pathPoints, cmd.color);
                break;
            }
        }
    }

    std::shared_ptr<FontAtlas> m_font;
    DrawList m_list;
    std::vector<UiVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    int m_w = 1280;
    int m_h = 720;
    bool m_dirty = true;
};

} // namespace ui
} // namespace sim
} // namespace ks
