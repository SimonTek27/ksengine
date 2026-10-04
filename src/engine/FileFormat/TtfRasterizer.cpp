#include "TtfRasterizer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ks {
namespace engine {
namespace fileformat {
namespace {

bool failWith(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

struct P {
    float x = 0.f;
    float y = 0.f;
};

P midpoint(const P& a, const P& b) { return P{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; }

void flattenQuad(const P& p0, const P& control, const P& p1, float tolerance, int depth,
                 std::vector<P>& out) {
    const float dx = p1.x - p0.x;
    const float dy = p1.y - p0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float dev = std::abs((control.x - p0.x) * dy - (control.y - p0.y) * dx);
    if (depth >= 12 || len < 1e-6f || dev <= tolerance * len) {
        out.push_back(p1);
        return;
    }
    const P p01 = midpoint(p0, control);
    const P c11 = midpoint(control, p1);
    const P mid = midpoint(p01, c11);
    flattenQuad(p0, p01, mid, tolerance, depth + 1, out);
    flattenQuad(mid, c11, p1, tolerance, depth + 1, out);
}

/** TrueType contour (on/off flags) -> closed polyline in pixel space (y down). */
bool contourToPolyline(const std::vector<TtfPoint>& contour, float scale, float tolerance,
                       std::vector<P>& out) {
    const std::size_t n = contour.size();
    out.clear();
    if (n < 2) return false;

    std::vector<P> pts(n);
    std::vector<bool> on(n);
    for (std::size_t i = 0; i < n; ++i) {
        pts[i] = P{contour[i].x * scale, -contour[i].y * scale};
        on[i] = contour[i].onCurve;
    }

    std::size_t firstOn = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (on[i]) {
            firstOn = i;
            break;
        }
    }
    if (firstOn == n) {
        // Malformed (every point off-curve): keep the shape as a straight polygon.
        out = pts;
        return true;
    }

    const P start = pts[firstOn];
    out.push_back(start);
    P current = start;
    const std::size_t steps = n - 1;
    for (std::size_t k = 1; k <= steps; ++k) {
        const std::size_t idx = (firstOn + k) % n;
        if (on[idx]) {
            out.push_back(pts[idx]);
            current = pts[idx];
            continue;
        }
        const std::size_t nextIdx = (idx + 1) % n;
        const bool nextIsOn = on[nextIdx];
        const P end = nextIsOn ? pts[nextIdx] : midpoint(pts[idx], pts[nextIdx]);
        flattenQuad(current, pts[idx], end, tolerance, 0, out);
        current = end;
    }
    return out.size() >= 2;
}

struct Edge {
    float x0 = 0.f;
    float y0 = 0.f;
    float x1 = 0.f;
    float y1 = 0.f;
};

struct Crossing {
    float x = 0.f;
    int direction = 0;
};

}  // namespace

std::size_t TtfGlyphBitmap::ink() const {
    std::size_t count = 0;
    for (std::uint8_t v : pixels) {
        if (v != 0) ++count;
    }
    return count;
}

bool rasterizeGlyph(const TtfReader& font, std::uint16_t gid, const TtfRasterOptions& options,
                    TtfGlyphBitmap& out, std::string* error) {
    out = TtfGlyphBitmap{};
    if (options.pixelSize <= 0.f) return failWith(error, "pixelSize must be positive");
    if (font.unitsPerEm() == 0) return failWith(error, "font has no unitsPerEm");

    TtfGlyphOutline outlineData;
    if (!font.outline(gid, outlineData, error)) return false;

    const float scale = options.pixelSize / static_cast<float>(font.unitsPerEm());
    std::vector<std::vector<P>> polylines;
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    bool haveBounds = false;
    for (const auto& contour : outlineData.contours) {
        std::vector<P> poly;
        if (!contourToPolyline(contour, scale, options.flattenTolerance, poly)) continue;
        for (const P& p : poly) {
            if (!haveBounds) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                haveBounds = true;
            } else {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        }
        polylines.push_back(std::move(poly));
    }
    if (!haveBounds || polylines.empty()) return true;  // blank glyph

    const int pad = std::max(0, options.padding);
    const int left = static_cast<int>(std::floor(minX)) - pad;
    const int top = static_cast<int>(std::floor(minY)) - pad;
    const int right = static_cast<int>(std::ceil(maxX)) + pad;
    const int bottom = static_cast<int>(std::ceil(maxY)) + pad;
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0) return true;
    if (width > 8192 || height > 8192) return failWith(error, "glyph bitmap too large");

    for (auto& poly : polylines) {
        for (P& p : poly) {
            p.x -= static_cast<float>(left);
            p.y -= static_cast<float>(top);
        }
    }

    std::vector<Edge> edges;
    {
        std::size_t segmentCount = 0;
        for (const auto& poly : polylines) segmentCount += poly.size();
        edges.reserve(segmentCount);
    }
    for (const auto& poly : polylines) {
        const std::size_t n = poly.size();
        if (n < 2) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const P& a = poly[i];
            const P& b = poly[(i + 1) % n];
            if (std::abs(a.y - b.y) < 1e-9f) continue;
            edges.push_back(Edge{a.x, a.y, b.x, b.y});
        }
    }
    if (edges.empty()) return true;

    const int superSample = std::max(1, options.supersample);
    std::vector<float> coverage(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                0.f);
    std::vector<Crossing> crossings;
    crossings.reserve(edges.size());

    for (int row = 0; row < height; ++row) {
        for (int s = 0; s < superSample; ++s) {
            const float y =
                static_cast<float>(row) + (static_cast<float>(s) + 0.5f) / static_cast<float>(superSample);
            crossings.clear();
            for (const Edge& e : edges) {
                const bool up = e.y0 <= y && y < e.y1;
                const bool down = e.y1 <= y && y < e.y0;
                if (!up && !down) continue;
                const float t = (y - e.y0) / (e.y1 - e.y0);
                Crossing c;
                c.x = e.x0 + t * (e.x1 - e.x0);
                c.direction = e.y1 > e.y0 ? 1 : -1;
                crossings.push_back(c);
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            int winding = 0;
            for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
                winding += crossings[i].direction;
                if (winding == 0) continue;
                const float spanA = crossings[i].x;
                const float spanB = crossings[i + 1].x;
                if (spanB <= 0.f || spanA >= static_cast<float>(width) || spanB <= spanA) {
                    continue;
                }
                const float clipA = std::max(spanA, 0.f);
                const float clipB = std::min(spanB, static_cast<float>(width));
                int px = static_cast<int>(std::floor(clipA));
                const int pxEnd = static_cast<int>(std::ceil(clipB));
                if (px < 0) px = 0;
                for (; px < pxEnd && px < width; ++px) {
                    const float lo = std::max(clipA, static_cast<float>(px));
                    const float hi = std::min(clipB, static_cast<float>(px + 1));
                    if (hi > lo) {
                        coverage[static_cast<std::size_t>(row) * width + px] +=
                            (hi - lo) / static_cast<float>(superSample);
                    }
                }
            }
        }
    }

    out.pixels.resize(coverage.size());
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        const float value = std::min(1.f, std::max(0.f, coverage[i])) * 255.f + 0.5f;
        out.pixels[i] = static_cast<std::uint8_t>(value);
    }
    out.width = width;
    out.height = height;
    out.left = left;
    out.top = top;
    return true;
}

bool buildTtfAtlas(const TtfReader& font, const TtfRasterOptions& options, TtfAtlas& out,
                   std::string* error) {
    out = TtfAtlas{};
    if (options.pixelSize <= 0.f) return failWith(error, "pixelSize must be positive");
    if (font.unitsPerEm() == 0) return failWith(error, "font has no unitsPerEm");

    const float scale = options.pixelSize / static_cast<float>(font.unitsPerEm());
    const float ascent = static_cast<float>(font.ascender()) * scale;
    const float descent = static_cast<float>(-font.descender()) * scale;
    const float lineGap = static_cast<float>(font.lineGap()) * scale;
    out.pixelSize = options.pixelSize;
    out.ascent = ascent;
    out.descent = descent;
    out.lineHeight = ascent + descent + lineGap;
    if (out.lineHeight <= 0.f) out.lineHeight = options.pixelSize * 1.25f;

    const int first = out.firstChar;
    const int count = out.glyphCount();
    const std::uint16_t fallback =
        static_cast<std::uint16_t>(font.glyphIndex(static_cast<std::uint32_t>('?')));

    struct Slot {
        TtfGlyphBitmap bitmap;
        float advance = 0.f;
        float xoff = 0.f;
        float yoff = 0.f;
    };
    std::vector<Slot> slots;
    slots.reserve(static_cast<std::size_t>(count));

    std::size_t area = 0;
    int maxWidth = 0;
    for (int i = 0; i < count; ++i) {
        const int ch = first + i;
        const std::uint32_t mapped = font.glyphIndex(static_cast<std::uint32_t>(ch));
        std::uint16_t gid = static_cast<std::uint16_t>(mapped);
        Slot slot;
        const bool isBlank = (ch == ' ' || ch == '\t');
        if (mapped != 0) {
            slot.advance = static_cast<float>(font.advanceWidth(gid)) * scale;
        } else if (isBlank) {
            gid = 0;
            slot.advance = options.pixelSize * 0.5f;
        } else if (fallback != 0) {
            gid = fallback;
            slot.advance = static_cast<float>(font.advanceWidth(gid)) * scale;
        } else {
            gid = 0;
            slot.advance = 0.f;
        }
        if (gid != 0) {
            if (!rasterizeGlyph(font, gid, options, slot.bitmap, error)) return false;
        }
        slot.xoff = static_cast<float>(slot.bitmap.left);
        slot.yoff = static_cast<float>(slot.bitmap.top);
        area += std::max<std::size_t>(1, slot.bitmap.pixels.size());
        maxWidth = std::max(maxWidth, slot.bitmap.width);
        slots.push_back(std::move(slot));
    }

    int targetWidth = 128;
    while (targetWidth < maxWidth ||
           static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetWidth) < area) {
        targetWidth *= 2;
        if (targetWidth >= 8192) break;
    }
    if (options.maxAtlasWidth > 0) {
        targetWidth = std::min(targetWidth, std::max(options.maxAtlasWidth, maxWidth));
    }
    targetWidth = std::max(targetWidth, 2);

    std::vector<std::pair<int, int>> origins(slots.size(), std::pair<int, int>{0, 0});
    int cursorX = 0;
    int rowY = 0;
    int rowHeight = 0;
    int usedWidth = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const int w = slots[i].bitmap.width;
        const int h = slots[i].bitmap.height;
        if (w > 0 && cursorX + w > targetWidth) {
            rowY += rowHeight;
            cursorX = 0;
            rowHeight = 0;
        }
        origins[i] = std::make_pair(cursorX, rowY);
        usedWidth = std::max(usedWidth, cursorX + w);
        cursorX += w;
        rowHeight = std::max(rowHeight, h);
    }
    rowY += rowHeight;

    const int atlasWidth = std::max(usedWidth, 2);
    const int glyphHeight = rowY;
    const int atlasHeight = glyphHeight + 2;  // last two rows: white texel for solid fills
    const std::size_t pixelCount =
        static_cast<std::size_t>(atlasWidth) * static_cast<std::size_t>(atlasHeight);
    if (pixelCount > (4096u * 4096u)) return failWith(error, "atlas too large");

    out.width = atlasWidth;
    out.height = atlasHeight;
    out.pixels.assign(pixelCount, 0);
    out.glyphs.resize(slots.size());

    for (std::size_t i = 0; i < slots.size(); ++i) {
        const Slot& slot = slots[i];
        const auto& origin = origins[i];
        TtfAtlasGlyph& glyph = out.glyphs[i];
        glyph.xoff = slot.xoff;
        glyph.yoff = slot.yoff;
        glyph.advance = slot.advance;
        glyph.width = static_cast<float>(slot.bitmap.width);
        glyph.height = static_cast<float>(slot.bitmap.height);
        if (slot.bitmap.width > 0 && slot.bitmap.height > 0) {
            const int ox = origin.first;
            const int oy = origin.second;
            for (int y = 0; y < slot.bitmap.height; ++y) {
                const std::size_t dst =
                    static_cast<std::size_t>(oy + y) * static_cast<std::size_t>(atlasWidth) +
                    static_cast<std::size_t>(ox);
                const std::size_t src = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(slot.bitmap.width);
                std::copy_n(slot.bitmap.pixels.data() + src,
                            static_cast<std::size_t>(slot.bitmap.width),
                            out.pixels.data() + dst);
            }
            glyph.u0 = static_cast<float>(ox) / static_cast<float>(atlasWidth);
            glyph.v0 = static_cast<float>(oy) / static_cast<float>(atlasHeight);
            glyph.u1 = static_cast<float>(ox + slot.bitmap.width) / static_cast<float>(atlasWidth);
            glyph.v1 = static_cast<float>(oy + slot.bitmap.height) / static_cast<float>(atlasHeight);
        }
    }

    for (int y = glyphHeight; y < atlasHeight; ++y) {
        for (int x = 0; x < 2; ++x) {
            out.pixels[static_cast<std::size_t>(y) * atlasWidth + x] = 255;
        }
    }
    out.whiteU0 = 0.f;
    out.whiteV0 = static_cast<float>(glyphHeight) / static_cast<float>(atlasHeight);
    out.whiteU1 = 2.f / static_cast<float>(atlasWidth);
    out.whiteV1 = 1.f;

    if (!out.valid()) return failWith(error, "atlas failed validation");
    return true;
}

}  // namespace fileformat
}  // namespace engine
}  // namespace ks
