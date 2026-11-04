/**
 * SVG menu icons.
 *
 * The parser is the only thing between an icon document and the vertex
 * buffer, so it is exercised directly: numbers as SVG writes them, every path
 * command the icon set uses, subpath separation (a stroke must never join two
 * contours), and the viewBox -> pixel mapping the menu relies on.
 */
#include "KsTest.h"
#include "simulator/ui/IconSet.h"
#include "simulator/ui/SvgIcon.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using ks::sim::ui::Color;
using ks::sim::ui::DrawCmdType;
using ks::sim::ui::DrawList;
using ks::sim::ui::IconTransform;
using ks::sim::ui::SvgIcon;
using ks::sim::ui::Vec2;

namespace {

using Subpaths = std::vector<std::vector<Vec2>>;

Subpaths parsePath(const char* d) {
    Subpaths out;
    ks::sim::ui::svg_detail::parsePathData(d, out);
    return out;
}

int strokeCommands(const DrawList& dl) {
    int count = 0;
    for (const auto& cmd : dl.commands())
        if (cmd.type == DrawCmdType::VectorStroke) ++count;
    return count;
}

float lastStrokeWidth(const DrawList& dl) {
    float width = 0.f;
    for (const auto& cmd : dl.commands())
        if (cmd.type == DrawCmdType::VectorStroke) width = cmd.thickness;
    return width;
}

float lastStrokePoints(const DrawList& dl) {
    float count = 0.f;
    for (const auto& cmd : dl.commands())
        if (cmd.type == DrawCmdType::VectorStroke)
            count = static_cast<float>(cmd.pathPoints.size() / 2);
    return count;
}

} // namespace

int main() {
    // --- every icon in the set is a document that parses and stays in frame --
    {
        // Icons that are more than one stroke: the chevrons and the refresh
        // ring are deliberately single paths, so they are not in this list.
        static const char* ids[] = {"drive", "restart", "setup", "settings",
                                    "exit",  "home",    "bell"};
        for (const char* id : ids) {
            const SvgIcon* icon = ks::sim::ui::icons::get(id);
            KS_CHECK(icon != nullptr);
            if (icon == nullptr) continue;
            KS_CHECK(icon->valid());
            KS_CHECK(icon->viewBoxWidth() > 0.f && icon->viewBoxHeight() > 0.f);
            KS_CHECK(icon->shapes().size() >= 2); // an icon is never one line
            bool inside = true;
            for (const auto& shape : icon->shapes()) {
                KS_CHECK(shape.strokeWidth > 0.f);
                KS_CHECK(!shape.subpaths.empty());
                for (const auto& subpath : shape.subpaths) {
                    KS_CHECK(subpath.size() >= 2);
                    for (const auto& p : subpath) {
                        if (p.x < -0.01f || p.y < -0.01f ||
                            p.x > icon->viewBoxWidth() + 0.01f ||
                            p.y > icon->viewBoxHeight() + 0.01f)
                            inside = false;
                    }
                }
            }
            KS_CHECK(inside);

            // The icon also has to *use* its frame: a parser that silently
            // kept only the first element would still pass everything above.
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (const auto& shape : icon->shapes()) {
                for (const auto& subpath : shape.subpaths) {
                    for (const auto& p : subpath) {
                        minX = std::min(minX, p.x);
                        minY = std::min(minY, p.y);
                        maxX = std::max(maxX, p.x);
                        maxY = std::max(maxY, p.y);
                    }
                }
            }
            KS_CHECK(maxX - minX >= 8.f);
            KS_CHECK(maxY - minY >= 8.f);
        }
        // Unknown ids are simply absent - a row without an icon, not a crash.
        KS_CHECK(ks::sim::ui::icons::source("no-such-icon") == nullptr);
        KS_CHECK(ks::sim::ui::icons::get("no-such-icon") == nullptr);
        KS_CHECK(ks::sim::ui::icons::get(nullptr) == nullptr);
        // Cached: the same pointer twice, so parsing happens once.
        KS_CHECK(ks::sim::ui::icons::get("drive") == ks::sim::ui::icons::get("drive"));
    }

    // --- path data -----------------------------------------------------------
    {
        // A closed square: one subpath, and Z repeats the first point.
        const Subpaths square = parsePath("M2 2 L10 2 L10 10 L2 10 Z");
        KS_CHECK(square.size() == 1);
        KS_CHECK(square[0].size() == 5);
        KS_CHECK_NEAR(square[0].back().x, 2.f, 1e-4f);
        KS_CHECK_NEAR(square[0].back().y, 2.f, 1e-4f);
    }
    {
        // Relative commands start from the current point, not the origin.
        const Subpaths rel = parsePath("m4 4 l6 0 l0 6 z");
        KS_CHECK(rel.size() == 1);
        KS_CHECK_NEAR(rel[0][0].x, 4.f, 1e-4f);
        KS_CHECK_NEAR(rel[0][1].x, 10.f, 1e-4f);
        KS_CHECK_NEAR(rel[0][2].y, 10.f, 1e-4f);
        KS_CHECK_NEAR(rel[0].back().x, 4.f, 1e-4f);
    }
    {
        // Horizontal and vertical shorthands.
        const Subpaths hv = parsePath("M1 1 H5 V9");
        KS_CHECK(hv.size() == 1);
        KS_CHECK(hv[0].size() == 3);
        KS_CHECK_NEAR(hv[0][1].x, 5.f, 1e-4f);
        KS_CHECK_NEAR(hv[0][1].y, 1.f, 1e-4f);
        KS_CHECK_NEAR(hv[0][2].y, 9.f, 1e-4f);
    }
    {
        // Implicit repeats: extra coordinate pairs continue the last command.
        const Subpaths rep = parsePath("M0 0 L2 0 4 2");
        KS_CHECK(rep.size() == 1);
        KS_CHECK(rep[0].size() == 3);
        KS_CHECK_NEAR(rep[0][2].x, 4.f, 1e-4f);
        KS_CHECK_NEAR(rep[0][2].y, 2.f, 1e-4f);
    }
    {
        // "1.5-3" is two numbers: a sign starts a new one.
        const Subpaths tight = parsePath("M10-3L-4.5.5");
        KS_CHECK(tight.size() == 1);
        KS_CHECK_NEAR(tight[0][0].x, 10.f, 1e-4f);
        KS_CHECK_NEAR(tight[0][0].y, -3.f, 1e-4f);
        KS_CHECK_NEAR(tight[0][1].x, -4.5f, 1e-4f);
        KS_CHECK_NEAR(tight[0][1].y, 0.5f, 1e-4f);
    }
    {
        // Separate subpaths stay separate: a stroke must not join contours.
        const Subpaths apart = parsePath("M2 2h4M8 8h4");
        KS_CHECK(apart.size() == 2);
        KS_CHECK(apart[0].size() == 2);
        KS_CHECK(apart[1].size() == 2);
    }
    {
        // Cubic: flattened into several segments, arriving exactly at the end.
        const Subpaths curve = parsePath("M0 0 C0 10 10 10 10 0");
        KS_CHECK(curve.size() == 1);
        KS_CHECK(curve[0].size() > 4);
        KS_CHECK_NEAR(curve[0].back().x, 10.f, 1e-3f);
        KS_CHECK_NEAR(curve[0].back().y, 0.f, 1e-3f);
        bool inBand = true;
        for (const auto& p : curve[0])
            if (p.y < -1e-3f || p.y > 10.001f) inBand = false;
        KS_CHECK(inBand);
    }
    {
        // Elliptical arc by endpoint: the samples sit on the circle itself.
        const Subpaths arc = parsePath("M2 12 A10 10 0 0 1 22 12");
        KS_CHECK(arc.size() == 1);
        KS_CHECK(arc[0].size() > 8);
        KS_CHECK_NEAR(arc[0].back().x, 22.f, 1e-2f);
        KS_CHECK_NEAR(arc[0].back().y, 12.f, 1e-2f);
        bool onCircle = true;
        for (const auto& p : arc[0]) {
            const float dx = p.x - 12.f;
            const float dy = p.y - 12.f;
            if (std::fabs(std::sqrt(dx * dx + dy * dy) - 10.f) > 0.2f) onCircle = false;
        }
        KS_CHECK(onCircle);
    }
    {
        // A move without any drawing leaves nothing behind.
        KS_CHECK(parsePath("M4 4").empty());
        KS_CHECK(parsePath("").empty());
        KS_CHECK(parsePath("garbage").empty());
    }

    // --- document: the non-path elements ------------------------------------
    {
        SvgIcon icon;
        const char* doc =
            "<svg viewBox=\"0 0 24 24\" stroke-width=\"1.5\">"
            "<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
            "<rect x=\"2\" y=\"4\" width=\"8\" height=\"6\" rx=\"2\"/>"
            "<line x1=\"0\" y1=\"0\" x2=\"24\" y2=\"0\"/>"
            "<polygon points=\"2,2 8,2 5,8\"/>"
            "</svg>";
        KS_CHECK(icon.parse(doc));
        KS_CHECK(icon.shapes().size() == 4);
        KS_CHECK_NEAR(icon.viewBoxWidth(), 24.f, 1e-4f);
        for (const auto& shape : icon.shapes()) KS_CHECK_NEAR(shape.strokeWidth, 1.5f, 1e-4f);
        // polygon closes itself
        const auto& poly = icon.shapes()[3].subpaths[0];
        KS_CHECK(poly.size() == 4);
        KS_CHECK_NEAR(poly.back().x, poly.front().x, 1e-4f);
        // rect with rx is a rounded outline, not four corners
        KS_CHECK(icon.shapes()[1].subpaths[0].size() > 5);
    }
    {
        // An element's own stroke-width wins over the root's.
        SvgIcon icon;
        KS_CHECK(icon.parse("<svg viewBox=\"0 0 10 10\" stroke-width=\"1\">"
                            "<path d=\"M0 0L5 5\" stroke-width=\"3\"/></svg>"));
        KS_CHECK(icon.shapes().size() == 1);
        KS_CHECK_NEAR(icon.shapes()[0].strokeWidth, 3.f, 1e-4f);
    }
    {
        // Nothing drawable is a false parse, not an empty icon on screen.
        SvgIcon empty;
        KS_CHECK(!empty.parse(""));
        KS_CHECK(!empty.parse(nullptr));
        KS_CHECK(!empty.parse("<svg></svg>"));
        KS_CHECK(!empty.parse("not an svg"));
        KS_CHECK(!empty.valid());
    }

    // --- mapping and drawing -------------------------------------------------
    {
        SvgIcon icon;
        KS_CHECK(icon.parse("<svg viewBox=\"0 0 24 24\"><path d=\"M0 0L24 24\"/></svg>"));
        const IconTransform t = icon.transformTo(100.f, 50.f, 24.f);
        KS_CHECK_NEAR(t.scale, 1.f, 1e-4f);
        const Vec2 topLeft = t.apply({0.f, 0.f});
        const Vec2 bottomRight = t.apply({24.f, 24.f});
        KS_CHECK_NEAR(topLeft.x, 100.f, 1e-3f);
        KS_CHECK_NEAR(topLeft.y, 50.f, 1e-3f);
        KS_CHECK_NEAR(bottomRight.x, 124.f, 1e-3f);
        KS_CHECK_NEAR(bottomRight.y, 74.f, 1e-3f);
    }
    {
        // A wide viewBox is letterboxed inside the square, not stretched.
        SvgIcon icon;
        KS_CHECK(icon.parse("<svg viewBox=\"0 0 48 12\"><path d=\"M0 0L48 12\"/></svg>"));
        const IconTransform t = icon.transformTo(100.f, 50.f, 24.f);
        KS_CHECK_NEAR(t.scale, 0.5f, 1e-4f);
        const Vec2 topLeft = t.apply({0.f, 0.f});
        KS_CHECK_NEAR(topLeft.x, 100.f, 1e-3f);
        KS_CHECK_NEAR(topLeft.y, 59.f, 1e-3f); // (24 - 6) / 2 of empty band
    }
    {
        // draw() emits one stroke per subpath, at the scaled line weight.
        SvgIcon icon;
        KS_CHECK(icon.parse("<svg viewBox=\"0 0 24 24\" stroke-width=\"2\">"
                            "<path d=\"M2 2h4M8 8h4\"/></svg>"));
        DrawList dl;
        icon.draw(dl, 10.f, 10.f, 24.f, Color::rgb(255, 255, 255));
        KS_CHECK(strokeCommands(dl) == 2);
        KS_CHECK_NEAR(lastStrokeWidth(dl), 2.f, 1e-4f);
        KS_CHECK_NEAR(lastStrokePoints(dl), 2.f, 1e-4f);
        // Half the size halves the line, and weightScale thickens it back.
        DrawList small;
        icon.draw(small, 0.f, 0.f, 12.f, Color::rgb(255, 255, 255));
        KS_CHECK_NEAR(lastStrokeWidth(small), 1.f, 1e-4f);
        DrawList thick;
        icon.draw(thick, 0.f, 0.f, 24.f, Color::rgb(255, 255, 255), 2.f);
        KS_CHECK_NEAR(lastStrokeWidth(thick), 4.f, 1e-4f);
    }
    {
        // A degenerate size draws nothing instead of collapsing to zero.
        SvgIcon icon;
        KS_CHECK(icon.parse("<svg viewBox=\"0 0 24 24\"><path d=\"M0 0L24 24\"/></svg>"));
        DrawList dl;
        icon.draw(dl, 0.f, 0.f, 0.f, Color::rgb(255, 255, 255));
        KS_CHECK(dl.commands().empty());
    }

    return KS_TEST_RESULT("ui_svg_icon_test");
}
