#pragma once
/**
 * Composes overlays; cinematic menu + RaceTelemetryHud + devices/mp.
 * Branding: ksim only.
 */
#include "UiRenderer.h"
#include "IconSet.h"
#include "TextRenderer.h"
#include "UiInput.h"
#include "FontTtfLoader.h"
#include "DeviceSettingsOverlay.h"
#include "KeyRebindOverlay.h"
#include "MultiplayerOverlay.h"
#include "ChatOverlay.h"
#include "RaceTelemetryHud.h"
#include "../GameMenuOverlay.h"
#include "../DashboardOverlay.h"
#include "../TelemetryOverlay.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace ks {
namespace sim {
namespace ui {

class NativeUiHub {
public:
    NativeUiHub()
        : m_text(std::make_shared<FontAtlas>())
    {
        m_menu = std::make_unique<GameMenuOverlay>();
        m_dash = std::make_unique<DashboardOverlay>();
        m_telem = std::make_unique<TelemetryOverlay>();
        m_devices = std::make_unique<DeviceSettingsOverlay>();
        m_mp = std::make_unique<MultiplayerOverlay>();
        m_chat = std::make_unique<ChatOverlay>();
        m_raceHud.setMode(RaceHudMode::Race);
        m_renderer.setFont(m_text.atlasPtr());
        applyConfiguredFont();
    }

    GameMenuOverlay& menu() { return *m_menu; }
    DashboardOverlay& dashboard() { return *m_dash; }
    TelemetryOverlay& telemetry() { return *m_telem; }
    DeviceSettingsOverlay& devices() { return *m_devices; }
    KeyRebindOverlay& keyRebind() { return m_keyRebind; }
    MultiplayerOverlay& multiplayer() { return *m_mp; }
    ChatOverlay& chat() { return *m_chat; }
    RaceTelemetryHud& raceHud() { return m_raceHud; }
    UiRenderer& renderer() { return m_renderer; }
    TextRenderer& text() { return m_text; }
    UiInput& input() { return m_input; }

    void resize(int w, int h) {
        m_viewW = w;
        m_viewH = h;
        m_renderer.setViewport(w, h);
    }

    void pushRaceSample(const RaceHudSample& s) { m_raceHud.push(s); }

    void renderFrame(int width, int height) {
        m_viewW = width;
        m_viewH = height;
        m_renderer.setViewport(width, height);

        m_input.beginFrame();
        m_renderer.beginFrame();
        auto& dl = m_renderer.list();

        // Race HUD first (under modal menus)
        if (!m_menu->isVisible() && m_raceHud.isVisible())
            m_raceHud.build(dl, m_text, width, height);

        if (m_dash->isVisible() && !m_menu->isVisible()) {
            m_dash->render(width, height);
            buildDashboard(dl, width, height);
        }
        if (m_telem->isVisible() && !m_menu->isVisible())
            buildTelemetry(dl, width, height);

        m_menu->render(width, height);
        if (m_menu->isVisible() || m_menu->fadeAlpha() > 0.01f)
            buildCinematicMenu(dl, width, height);

        m_devices->build(dl, width, height, &m_input);
        m_keyRebind.build(dl, width, height, &m_input);
        m_mp->build(dl, width, height, &m_input);
        m_chat->build(dl, width, height, &m_input);

        m_renderer.endFrame();
        m_input.endFrame();
    }

    bool handleKey(int key) {
        // Composer open: everything is chat until Enter/Esc closes it, so a
        // half-typed message never reaches the menu or the server browser.
        if (m_chat->isTyping()) return m_chat->handleKey(key);
        if (m_devices->isVisible() && m_devices->handleKey(key)) return true;
        if (m_keyRebind.isVisible() && m_keyRebind.handleKey(key)) return true;
        if (m_mp->isVisible() && m_mp->handleKey(key)) return true;
        if (m_menu->isVisible() && m_menu->handleKeyPress(key)) return true;
        if (m_chat->handleKey(key)) return true;
        // Y = cycle race HUD mode (T is taken by the telemetry overlay)
        if (key == 'Y' || key == 'y') {
            m_raceHud.cycleMode();
            return true;
        }
        if (key == 112) { m_devices->toggle(); return true; }
        if (key == 113) { m_mp->toggle(); return true; }
        if (key == 27 && !m_menu->isVisible()) {
            // Two overlays share this menu: the one before a session, and the
            // pause screen once in the car. The second is gated (see
            // inCarMenuAllowed).
            openInCarMenu(*m_menu, m_inCarMenuAllowed);
            return true;
        }
        return false;
    }

    /** WM_CHAR payload, routed straight to the composer while it is open. */
    bool handleChar(int character) { return m_chat->handleChar(character); }

    bool handleMouse(const MouseEvent& e) {
        m_input.inject(e);
        if (e.type == MouseEventType::Down && e.button == MouseButton::Left)
            m_input.notePressPosition();

        if (m_devices->isVisible() && m_devices->handleMouse(e)) return true;
        if (m_keyRebind.isVisible() && m_keyRebind.handleMouse(e)) return true;
        if (m_mp->isVisible() && m_mp->handleMouse(e)) return true;

        if (m_menu->isVisible() || m_menu->fadeAlpha() > 0.5f) {
            updateMenuHover(e.x, e.y, m_viewW, m_viewH);
            if (e.type == MouseEventType::Up && e.button == MouseButton::Left &&
                m_menu->hoverIndex() >= 0) {
                m_menu->setSelectedIndex(m_menu->hoverIndex());
                m_menu->handleClick({(int)e.x, (int)e.y}, m_viewW, m_viewH);
            }
            return true;
        }
        return false;
    }

    bool handleMouseMove(float x, float y) {
        MouseEvent e; e.type = MouseEventType::Move; e.x = x; e.y = y;
        return handleMouse(e);
    }

    bool handleMouseButton(MouseButton b, bool down, float x, float y) {
        MouseEvent e;
        e.type = down ? MouseEventType::Down : MouseEventType::Up;
        e.button = b; e.x = x; e.y = y;
        return handleMouse(e);
    }

    bool handleMouseWheel(float delta, float x, float y) {
        MouseEvent e; e.type = MouseEventType::Wheel; e.wheelDelta = delta; e.x = x; e.y = y;
        return handleMouse(e);
    }

    /** Set once per frame by SimulationLoop: whether the in-car overlay (the
     *  same menu, opened with ESC while driving) is allowed right now. */
    void setInCarMenuAllowed(bool allowed) { m_inCarMenuAllowed = allowed; }

    /** ESC while in the car: opens the overlay only when the gate allows it.
     *  Returns whether it opened; the caller consumes the key either way, so a
     *  refused ESC never falls through to the driver's controls.
     *  Static so the policy can be exercised without a full hub. */
    static bool openInCarMenu(GameMenuOverlay& menu, bool allowed) {
        if (!allowed) return false;
        menu.openPauseMenu();
        return true;
    }

    bool blocksDrivingInput() const {
        return m_menu->isInputBlocked() || m_devices->isVisible() ||
               m_keyRebind.isVisible() || m_mp->isVisible() ||
               m_chat->isTyping();
    }

private:
    static bool fileReadable(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) return false;
        std::fclose(f);
        return true;
    }

    /** Elegant, readable UI face. KS_FONT_TTF picks one explicitly; otherwise
     *  the platform UI font is used (Segoe UI on Windows — clean at menu sizes,
     *  unlike the baked 8x8 bitmap, which stays as the last resort). */
    void applyConfiguredFont() {
        float pixelSize = 17.f;
        if (const char* raw = std::getenv("KS_FONT_PX"); raw != nullptr && *raw != '\0') {
            const float parsed = std::strtof(raw, nullptr);
            if (parsed > 0.f && parsed <= 512.f) pixelSize = parsed;
        }
        std::string chosen;
        if (const char* path = std::getenv("KS_FONT_TTF"); path != nullptr && *path != '\0') {
            chosen = path;
        } else {
            // A project-local face wins, so a build can ship its own type.
            static const char* kCandidates[] = {
                "system/fonts/ui.ttf",
                "assets/ui/ui.ttf",
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/seguisb.ttf",
            };
            for (const char* candidate : kCandidates) {
                if (fileReadable(candidate)) { chosen = candidate; break; }
            }
        }
        if (chosen.empty()) {
            std::printf("[ui] no UI font found, keeping the baked 8x8 set\n");
            return;
        }
        std::string error;
        FontAtlas& font = *m_text.atlasPtr();
        if (!loadTtfFont(font, chosen, pixelSize, &error)) {
            std::printf("[ui] font %s ignored: %s\n", chosen.c_str(), error.c_str());
            return;
        }
        std::printf("[ui] font %s @ %.1f px -> %dx%d atlas\n", chosen.c_str(), pixelSize,
                    font.width(), font.height());
    }

    static TextStyle sty(const Color& c, float scale, bool boldFeel = false) {
        TextStyle s;
        s.color = c;
        s.scale = scale;
        s.align = TextAlign::Left;
        s.shadow = true;
        s.outline = boldFeel;
        return s;
    }

    struct MenuLayout {
        float left = 72.f;
        float top = 140.f;
        float rowH = 44.f;
        float listW = 420.f;
        float accentW = 4.f;
    };

    /** Horizontal in-car bar: one cell per action, icon above its label.
     *  Painted and hit-tested from the same layout, so a click always lands on
     *  the cell the player sees. */
    struct PauseBar {
        static constexpr int kMaxItems = 8;
        Rect panel{};
        Rect flag{};
        Rect item[kMaxItems]{};
        int menuIndex[kMaxItems]{};
        int count = 0;
    };

    PauseBar pauseBar(int w, int h) const {
        PauseBar bar;
        const auto& items = m_menu->items();
        int visible = 0;
        int gaps = 0;
        for (const auto& item : items) {
            if (item.isSeparator) {
                ++gaps;
            } else if (visible < PauseBar::kMaxItems) {
                ++visible;
            }
        }
        if (visible == 0) return bar;

        const float itemW = 150.f;
        const float flagW = 46.f;
        const float gapW = 18.f;
        const float barH = 84.f;
        const float barW = flagW + itemW * static_cast<float>(visible) +
                           gapW * static_cast<float>(gaps);
        const float x = (w - barW) * 0.5f;
        // A little above centre: the car and the road ahead stay visible.
        const float y = h * 0.40f + (1.f - m_menu->transitionProgress()) * 14.f;

        bar.panel = {x, y, barW, barH};
        bar.flag = {x + 14.f, y + (barH - 30.f) * 0.5f, 26.f, 30.f};

        float cursor = x + flagW;
        for (int i = 0; i < static_cast<int>(items.size()) && bar.count < visible; ++i) {
            if (items[i].isSeparator) {
                cursor += gapW;
                continue;
            }
            bar.item[bar.count] = {cursor, y, itemW, barH};
            bar.menuIndex[bar.count] = i;
            cursor += itemW;
            ++bar.count;
        }
        return bar;
    }

    void buildPauseBar(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        if (a <= 0.01f) return;
        const PauseBar bar = pauseBar(w, h);
        if (bar.count == 0) return;

        dl.addRectFilled({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)},
                         Color::rgba(0.f, 0.f, 0.f, 0.30f * a));
        dl.addRectFilled(bar.panel, Color::rgba(0.06f, 0.06f, 0.07f, 0.94f * a));
        dl.addRect({bar.panel.x, bar.panel.y, bar.panel.w, 1.f},
                   Color::rgba(1.f, 1.f, 1.f, 0.10f * a));

        // Session flag: a checkered block. Drawn as squares because the icon
        // set is stroke-only and a checkered flag is solid colour.
        const float squareW = bar.flag.w / 3.f;
        const float squareH = bar.flag.h / 3.f;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const bool dark = ((row + col) % 2) == 0;
                dl.addRectFilled({bar.flag.x + static_cast<float>(col) * squareW,
                                  bar.flag.y + static_cast<float>(row) * squareH,
                                  squareW, squareH},
                                 dark ? Color::rgba(0.78f, 0.12f, 0.12f, a)
                                      : Color::rgba(0.93f, 0.93f, 0.90f, a));
            }
        }

        const auto& items = m_menu->items();
        const int selected = m_menu->selectedIndex();
        for (int i = 0; i < bar.count; ++i) {
            const Rect& cell = bar.item[i];
            const MenuItem& item = items[bar.menuIndex[i]];
            const bool isSelected = bar.menuIndex[i] == selected;
            const bool isHover = bar.menuIndex[i] == m_menu->hoverIndex();

            if (isSelected || isHover) {
                dl.addRectFilled({cell.x + 5.f, cell.y + 9.f, cell.w - 10.f, cell.h - 18.f},
                                 Color::rgba(1.f, 1.f, 1.f, (isSelected ? 0.10f : 0.05f) * a));
            }
            if (isSelected) {
                dl.addRectFilled({cell.x + 5.f, cell.y + cell.h - 11.f, cell.w - 10.f, 2.f},
                                 Color::rgba(0.86f, 0.13f, 0.13f, a));
            }

            const float centreX = cell.x + cell.w * 0.5f;
            const Color tint = isSelected ? Color::rgba(1.f, 1.f, 1.f, a)
                                          : Color::rgba(0.70f, 0.70f, 0.74f, a);
            if (const SvgIcon* icon = icons::get(item.icon)) {
                const float size = 26.f;
                icon->draw(dl, centreX - size * 0.5f, cell.y + 17.f, size, tint,
                           isSelected ? 1.15f : 1.f);
            }
            // DrawList text is left-aligned, so the centring happens here.
            const float labelScale = 0.92f;
            const float labelW = m_text.measureWidth(item.text, labelScale);
            m_text.draw(dl, centreX - labelW * 0.5f, cell.y + 51.f, item.text,
                        sty(tint, labelScale, isSelected));
        }

        const char* hint = "LEFT/RIGHT  select     ENTER  confirm     ESC  back";
        const float hintScale = 0.85f;
        const float hintW = m_text.measureWidth(hint, hintScale);
        m_text.draw(dl, static_cast<float>(w) * 0.5f - hintW * 0.5f,
                    bar.panel.y + bar.panel.h + 16.f, hint,
                    sty(Color::rgba(0.55f, 0.55f, 0.60f, a), hintScale));
    }

    void updateMenuHover(float mx, float my, int w, int h) {
        if (m_menu->state() == MenuState::Pause) {
            const PauseBar bar = pauseBar(w, h);
            int hover = -1;
            for (int i = 0; i < bar.count; ++i)
                if (bar.item[i].contains(mx, my)) hover = bar.menuIndex[i];
            m_menu->setHoverIndex(hover);
            if (hover >= 0) m_menu->setSelectedIndex(hover);
            return;
        }

        MenuLayout L;
        const auto& items = m_menu->items();
        int hover = -1;
        float iy = L.top;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].isSeparator) {
                iy += L.rowH * 0.45f;
                continue;
            }
            Rect rr{L.left, iy, L.listW, L.rowH - 4.f};
            if (rr.contains(mx, my))
                hover = i;
            iy += L.rowH;
        }
        m_menu->setHoverIndex(hover);
        if (hover >= 0)
            m_menu->setSelectedIndex(hover);
    }

    void buildCinematicMenu(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        if (a <= 0.01f) return;

        // The in-car overlay is a horizontal bar, not the vertical list the
        // pre-session screens use: it sits over a view the player still wants
        // to see, and it is navigated left/right.
        if (m_menu->state() == MenuState::Pause) {
            buildPauseBar(dl, w, h);
            return;
        }

        const float t = m_menu->transitionProgress();
        const float slide = (1.f - t) * 40.f;

        dl.addRectFilled({0, 0, (float)w, (float)h}, Color::rgba(0.f, 0.f, 0.f, 0.72f * a));
        dl.addRectFilled({0, 0, 8.f, (float)h}, Color::rgba(0.85f, 0.12f, 0.12f, 0.9f * a));

        m_text.draw(dl, 72.f + slide, 48.f, "KSIM",
                    sty(Color::rgba(1, 1, 1, a), 2.4f, true));
        m_text.draw(dl, 72.f + slide, 88.f, m_menu->sectionTitle(),
                    sty(Color::rgba(0.75f, 0.75f, 0.78f, a), 1.1f));

        char ctx[128];
        // ASCII only: the atlas covers 32..126, so a middle dot rendered as??.
        std::snprintf(ctx, sizeof(ctx), "%s   |   %s",
                      m_menu->carName().empty() ? "Vehicle" : m_menu->carName().c_str(),
                      m_menu->trackName().empty() ? "Circuit" : m_menu->trackName().c_str());
        m_text.draw(dl, 72.f + slide, static_cast<float>(h) - 48.f, ctx,
                    sty(Color::rgba(0.55f, 0.55f, 0.6f, a), 1.f));

        MenuLayout L;
        L.left += slide;
        const auto& items = m_menu->items();
        const int sel = m_menu->selectedIndex();
        float iy = L.top;
        std::string desc;

        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const auto& it = items[i];
            if (it.isSeparator) {
                dl.addRectFilled({L.left, iy + L.rowH * 0.15f, L.listW * 0.55f, 1.f},
                                 Color::rgba(1, 1, 1, 0.12f * a));
                iy += L.rowH * 0.45f;
                continue;
            }

            const bool selected = (i == sel);
            const bool hover = (i == m_menu->hoverIndex());

            if (selected || hover) {
                dl.addRectFilled({L.left - 12.f, iy - 2.f, L.listW + 24.f, L.rowH - 2.f},
                                 Color::rgba(1.f, 1.f, 1.f, 0.06f * a));
                dl.addRectFilled({L.left - 12.f, iy - 2.f, L.accentW, L.rowH - 2.f},
                                 Color::rgba(0.9f, 0.15f, 0.15f, a));
            }

            Color titleCol = selected
                ? Color::rgba(1.f, 1.f, 1.f, a)
                : Color::rgba(0.72f, 0.72f, 0.75f, a);
            // The row's icon, when it names one: an SVG parsed once and
            // stroked through the same tessellator as the rest of the UI, so
            // it stays sharp at any size and tints with the row. The label
            // moves right to leave it room.
            float labelX = L.left + 8.f;
            if (const SvgIcon* rowIcon = icons::get(it.icon)) {
                const float iconSize = 20.f;
                const Color iconCol = selected
                    ? Color::rgba(1.f, 1.f, 1.f, a)
                    : Color::rgba(0.66f, 0.66f, 0.70f, a);
                rowIcon->draw(dl, labelX, iy + (L.rowH - 6.f - iconSize) * 0.5f + 1.f,
                              iconSize, iconCol, selected ? 1.15f : 1.f);
                labelX += iconSize + 12.f;
            }
            m_text.draw(dl, labelX, iy + 10.f, it.text,
                        sty(titleCol, selected ? 1.35f : 1.2f, selected));

            if (selected)
                desc = it.description;
            iy += L.rowH;
        }

        if (!desc.empty()) {
            const float px = static_cast<float>(w) * 0.52f;
            const float py = L.top;
            const float pw = static_cast<float>(w) - px - 64.f;
            if (pw > 120.f) {
                dl.addRectFilled({px, py, pw, 160.f}, Color::rgba(0.05f, 0.05f, 0.07f, 0.85f * a));
                dl.addRectFilled({px, py, 3.f, 160.f}, Color::rgba(0.9f, 0.15f, 0.15f, 0.7f * a));
                m_text.draw(dl, px + 20.f, py + 24.f, "INFO",
                            sty(Color::rgba(0.9f, 0.25f, 0.25f, a), 1.f, true));
                // Wrapped inside the panel: a long description used to run
                // past its right edge.
                dl.addTextWrapped(px + 20.f, py + 56.f, desc,
                                  Color::rgba(0.85f, 0.85f, 0.88f, a), 1.15f,
                                  pw - 40.f);
            }
        }

        m_text.draw(dl, static_cast<float>(w) - 280.f, static_cast<float>(h) - 48.f,
                    "ENTER  select    ESC  back",
                    sty(Color::rgba(0.5f, 0.5f, 0.55f, a), 0.95f));
    }

    void buildDashboard(DrawList& dl, int /*w*/, int h) {
        const float barW = 240.f;
        const float x = 24.f;
        const float y = static_cast<float>(h) - 130.f;

        dl.addRectFilled({x, y, barW, 100}, Color::rgba(0, 0, 0, 0.6f));
        dl.addRect({x, y, barW, 100}, Color::rgb(60, 100, 160), 1.f);
        m_text.draw(dl, x + 12, y + 8, "DASH", TextRenderer::hudLabel());

        dl.addProgressBar({x + 12, y + 36, barW - 24, 14}, m_telem->m_throttle,
                          Color::rgb(80, 210, 100), Color::rgba(0.15f, 0.15f, 0.15f, 1.f));
        dl.addProgressBar({x + 12, y + 54, barW - 24, 14}, m_telem->m_brake,
                          Color::rgb(230, 70, 70), Color::rgba(0.15f, 0.15f, 0.15f, 1.f));

        char line[64];
        std::snprintf(line, sizeof(line), "SPD %d  RPM %d",
                      static_cast<int>(m_telem->m_speed * 3.6f),
                      static_cast<int>(m_telem->m_rpm));
        m_text.draw(dl, x + 12, y + 74, line, TextRenderer::hudValue());
    }

    void buildTelemetry(DrawList& dl, int w, int /*h*/) {
        const float panelW = 300.f;
        const float x = static_cast<float>(w) - panelW - 16.f;
        const float y = 16.f;

        dl.addRectFilled({x, y, panelW, 170.f}, Color::rgba(0, 0, 0, 0.55f));
        dl.addRect({x, y, panelW, 170.f}, Color::rgb(80, 120, 200), 1.f);
        m_text.draw(dl, x + 12, y + 10, "TELEMETRY", TextRenderer::hudLabel());

        char buf[64];
        std::snprintf(buf, sizeof(buf), "Lat G  %.2f", m_telem->m_lateralG);
        m_text.draw(dl, x + 12, y + 36, buf, TextRenderer::hudValue());
        std::snprintf(buf, sizeof(buf), "Lon G  %.2f", m_telem->m_longitudinalG);
        m_text.draw(dl, x + 12, y + 56, buf, TextRenderer::hudValue());

        dl.addProgressBar({x + 12, y + 88, panelW - 24, 12},
                          (m_telem->m_steering + 1.f) * 0.5f,
                          Color::rgb(100, 170, 255), Color::rgba(0.12f, 0.12f, 0.12f, 1.f));
        m_text.draw(dl, x + 12, y + 110, "STEER", TextRenderer::hudLabel());
        std::snprintf(buf, sizeof(buf), "THR %.0f%%  BRK %.0f%%",
                      m_telem->m_throttle * 100.f, m_telem->m_brake * 100.f);
        m_text.draw(dl, x + 12, y + 140, buf, TextRenderer::hudLabel());
    }

    UiRenderer m_renderer;
    TextRenderer m_text;
    UiInput m_input;
    RaceTelemetryHud m_raceHud;
    KeyRebindOverlay m_keyRebind;
    int m_viewW = 1280, m_viewH = 720;
    bool m_inCarMenuAllowed = true;   // pre-session: always

    std::unique_ptr<GameMenuOverlay> m_menu;
    std::unique_ptr<DashboardOverlay> m_dash;
    std::unique_ptr<TelemetryOverlay> m_telem;
    std::unique_ptr<DeviceSettingsOverlay> m_devices;
    std::unique_ptr<MultiplayerOverlay> m_mp;
    std::unique_ptr<ChatOverlay> m_chat;
};

} // namespace ui
} // namespace sim
} // namespace ks
