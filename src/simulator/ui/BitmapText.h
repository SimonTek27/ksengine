#pragma once
/**
 * Bitmap text layout & emission into DrawList / vertex stream.
 * Uses FontAtlas 8×8 monospace glyphs (ASCII 32–126).
 */
#include "FontAtlas.h"
#include "NativeUiTypes.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ks {
namespace sim {
namespace ui {

enum class TextAlign : uint8_t {
    Left = 0,
    Center,
    Right
};

struct TextStyle {
    Color color = Color::rgb(255, 255, 255);
    float scale = 1.f;
    TextAlign align = TextAlign::Left;
    float lineSpacing = 1.25f; // multiplier on glyph height

    bool shadow = false;
    Color shadowColor = Color::rgba(0, 0, 0, 0.75f);
    float shadowOx = 1.f;
    float shadowOy = 1.f;

    bool outline = false;
    Color outlineColor = Color::rgba(0, 0, 0, 0.9f);
    float outlinePx = 1.f; // at scale 1

    float maxWidth = 0.f; // 0 = no wrap
};

struct TextMetrics {
    float width = 0.f;
    float height = 0.f;
    int lineCount = 0;
};

class BitmapText {
public:
    explicit BitmapText(const FontAtlas& font) : m_font(font) {}

    float lineHeight(float scale, float spacing = 1.25f) const {
        return m_font.baseLineHeight() * scale * spacing;
    }

    /** Width of a single line (no newlines). */
    float measureLine(const std::string& line, float scale) const {
        float w = 0.f;
        for (unsigned char ch : line) {
            if (ch == '\n') break;
            w += m_font.glyph(ch).advance * scale;
        }
        return w;
    }

    TextMetrics measure(const std::string& text, const TextStyle& style) const {
        const auto lines = wrapLines(text, style);
        TextMetrics m;
        m.lineCount = static_cast<int>(lines.size());
        for (const auto& ln : lines)
            m.width = std::max(m.width, measureLine(ln, style.scale));
        m.height = m.lineCount > 0
            ? lineHeight(style.scale, style.lineSpacing) * static_cast<float>(m.lineCount)
            : 0.f;
        return m;
    }

    /** Emit glyph quads into vertex/index buffers (same layout as UiRenderer). */
    void emit(const std::string& text, float x, float y, const TextStyle& style,
              std::vector<UiVertex>& outVerts, std::vector<uint32_t>& outIdx) const {
        auto pushQuad = [&](float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1, const Color& c) {
            const uint32_t base = static_cast<uint32_t>(outVerts.size());
            outVerts.push_back({x0, y0, c.r, c.g, c.b, c.a, u0, v0});
            outVerts.push_back({x1, y0, c.r, c.g, c.b, c.a, u1, v0});
            outVerts.push_back({x1, y1, c.r, c.g, c.b, c.a, u1, v1});
            outVerts.push_back({x0, y1, c.r, c.g, c.b, c.a, u0, v1});
            outIdx.push_back(base + 0);
            outIdx.push_back(base + 1);
            outIdx.push_back(base + 2);
            outIdx.push_back(base + 0);
            outIdx.push_back(base + 2);
            outIdx.push_back(base + 3);
        };

        auto emitRun = [&](float ox, float oy, const Color& col) {
            const auto lines = wrapLines(text, style);
            float cy = y + oy;
            for (const auto& line : lines) {
                float lineW = measureLine(line, style.scale);
                float cx = x + ox;
                if (style.align == TextAlign::Center)
                    cx -= lineW * 0.5f;
                else if (style.align == TextAlign::Right)
                    cx -= lineW;

                for (unsigned char ch : line) {
                    const GlyphInfo g = m_font.glyph(ch);
                    const float x0 = cx + g.xoff * style.scale;
                    const float y0 = cy + g.yoff * style.scale;
                    const float x1 = x0 + g.width * style.scale;
                    const float y1 = y0 + g.height * style.scale;
                    if (ch != ' ' && ch != '\t')
                        pushQuad(x0, y0, x1, y1, g.u0, g.v0, g.u1, g.v1, col);
                    cx += g.advance * style.scale;
                }
                cy += lineHeight(style.scale, style.lineSpacing);
            }
        };

        // Order: outline → shadow → fill (so fill is on top)
        if (style.outline) {
            const float o = style.outlinePx * style.scale;
            const float offs[8][2] = {
                {-o, 0}, {o, 0}, {0, -o}, {0, o},
                {-o, -o}, {o, -o}, {-o, o}, {o, o}
            };
            for (auto& d : offs)
                emitRun(d[0], d[1], style.outlineColor);
        }
        if (style.shadow)
            emitRun(style.shadowOx * style.scale, style.shadowOy * style.scale, style.shadowColor);

        emitRun(0.f, 0.f, style.color);
    }

    /** Convenience: emit into DrawList as a single Text cmd (no shadow/outline — use emit for full style). */
    void addToDrawList(DrawList& dl, float x, float y, const std::string& text,
                       const Color& color, float scale = 1.f) const {
        dl.addText(x, y, text, color, scale);
    }

    void addStyledToDrawList(DrawList& dl, float x, float y, const std::string& text,
                             const TextStyle& style) const {
        // Expand styles into multiple text cmds for outline/shadow via DrawList path
        if (style.outline) {
            const float o = style.outlinePx * style.scale;
            const float offs[8][2] = {
                {-o, 0}, {o, 0}, {0, -o}, {0, o},
                {-o, -o}, {o, -o}, {-o, o}, {o, o}
            };
            for (auto& d : offs)
                dl.addText(x + d[0], y + d[1], text, style.outlineColor, style.scale);
        }
        if (style.shadow)
            dl.addText(x + style.shadowOx * style.scale, y + style.shadowOy * style.scale,
                       text, style.shadowColor, style.scale);
        dl.addText(x, y, text, style.color, style.scale);
    }

private:
    std::vector<std::string> wrapLines(const std::string& text, const TextStyle& style) const {
        std::vector<std::string> lines;
        std::string cur;
        auto flush = [&]() {
            lines.push_back(cur);
            cur.clear();
        };

        for (size_t i = 0; i < text.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if (ch == '\n') {
                flush();
                continue;
            }
            if (ch == '\t') {
                cur.push_back(' ');
                cur.push_back(' ');
                cur.push_back(' ');
                cur.push_back(' ');
                continue;
            }
            cur.push_back(static_cast<char>(ch));
            if (style.maxWidth > 0.f && measureLine(cur, style.scale) > style.maxWidth) {
                // break at last space if any
                const auto sp = cur.find_last_of(' ');
                if (sp != std::string::npos && sp > 0) {
                    lines.push_back(cur.substr(0, sp));
                    cur = cur.substr(sp + 1);
                } else if (cur.size() > 1) {
                    char last = cur.back();
                    cur.pop_back();
                    flush();
                    cur.push_back(last);
                }
            }
        }
        if (!cur.empty() || lines.empty())
            flush();
        return lines;
    }

    const FontAtlas& m_font;
};

} // namespace ui
} // namespace sim
} // namespace ks
