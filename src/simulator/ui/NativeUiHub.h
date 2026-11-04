#pragma once
/**
 * Composes overlays; cinematic menu + RaceTelemetryHud + devices/mp.
 * Branding: ksim only.
 */
#include "UiRenderer.h"
#include "IconSet.h"
#include "NotificationCenter.h"
#include "MainMenu.h"
#include "TextRenderer.h"
#include "UiInput.h"
#include "FontTtfLoader.h"
#include "DeviceSettingsOverlay.h"
#include "KeyRebindOverlay.h"
#include "MultiplayerOverlay.h"
#include "ChatOverlay.h"
#include "ConsoleOverlay.h"
#include "RaceTelemetryHud.h"
#include "../GameMenuOverlay.h"
#include "../DashboardOverlay.h"
#include "../TelemetryOverlay.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <ctime>

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
        m_console = std::make_unique<ConsoleOverlay>();
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
    ConsoleOverlay& console() { return *m_console; }
    RaceTelemetryHud& raceHud() { return m_raceHud; }
    UiRenderer& renderer() { return m_renderer; }
    TextRenderer& text() { return m_text; }
    UiInput& input() { return m_input; }
    NotificationCenter& notifications() { return m_notices; }

    /** Report one thing worth telling the player about. Anything the loop or
     *  the app already logs as a real event goes through here, so the bell
     *  reports what happened rather than a decorative count. */
    void notify(std::string text, NoticeKind kind = NoticeKind::Info) {
        m_notices.push(std::move(text), kind);
    }

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

        // Whatever moved the menu - a key, a tile, a callback from the loop -
        // the trail the bar's arrows walk follows it.
        syncNavHistory();

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
        m_console->build(dl, width, height, &m_input);
        // The notice list is painted after every screen: it is a panel over
        // whatever the player is looking at, not a part of it.
        buildNotifications(dl, width, height);

        m_renderer.endFrame();
        m_input.endFrame();
    }

    bool handleKey(int key) {
        // Composer open: everything is chat until Enter/Esc closes it, so a
        // half-typed message never reaches the menu or the server browser.
        if (m_chat->isTyping()) return m_chat->handleKey(key);
        // Roadmap 4.x — developer console: INSERT stays with SimulatorApp
        // (CTRL+INSERT opens it, INSERT alone closes it); every other key goes
        // to the console while it is up, so typing never drives the car.
        if (key == 0x2D) return false;
        if (m_console->isVisible()) {
            // Forward keys to the console (it handles its own typing state)
            return m_console->handleKey(key);
        }
        if (m_devices->isVisible() && m_devices->handleKey(key)) return true;
        if (m_keyRebind.isVisible() && m_keyRebind.handleKey(key)) return true;
        if (m_mp->isVisible() && m_mp->handleKey(key)) return true;
        // The startup screen is a horizontal row of tiles: the arrows walk
        // the row (DOWN/UP included, so a pad mapped either way still moves),
        // ENTER opens the section, and ESC offers to leave the game instead of
        // hiding the menu the game opened on.
        if (m_menu->isVisible() && m_menu->state() == MenuState::Main) {
            const std::vector<int> rows = mainRows();
            const int count = static_cast<int>(rows.size());
            const int slot = mainSlot(rows, m_menu->selectedIndex());
            if (count > 0) {
                if (key == 0x27 || key == 0x28) { // RIGHT / DOWN
                    m_menu->setSelectedIndex(rows[mainMenuStep(slot, +1, count)]);
                    return true;
                }
                if (key == 0x25 || key == 0x26) { // LEFT / UP
                    m_menu->setSelectedIndex(rows[mainMenuStep(slot, -1, count)]);
                    return true;
                }
            }
            if (key == 0x0D) { m_menu->activateSelection(); return true; }
            if (key == 0x1B) { m_menu->openScreen(MenuState::QuitConfirm); return true; }
        }
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

    /** WM_CHAR payload: the console takes it while it is up, the chat otherwise. */
    bool handleChar(int character) {
        if (m_console->isVisible()) return m_console->handleChar(character);
        return m_chat->handleChar(character);
    }

    bool handleMouse(const MouseEvent& e) {
        m_input.inject(e);
        if (e.type == MouseEventType::Down && e.button == MouseButton::Left)
            m_input.notePressPosition();

        if (m_devices->isVisible() && m_devices->handleMouse(e)) return true;
        if (m_keyRebind.isVisible() && m_keyRebind.handleMouse(e)) return true;
        if (m_mp->isVisible() && m_mp->handleMouse(e)) return true;

        if (m_menu->isVisible() || m_menu->fadeAlpha() > 0.5f) {
            syncNavHistory();
            // The notice list covers part of the screen: a press inside it
            // closes it rather than pressing whatever sits underneath.
            if (m_notices.open() && e.type == MouseEventType::Up &&
                e.button == MouseButton::Left &&
                notificationPanel(m_viewW, m_viewH).contains(e.x, e.y)) {
                m_notices.setOpen(false);
                return true;
            }
            updateMenuHover(e.x, e.y, m_viewW, m_viewH);
            if (e.type == MouseEventType::Up && e.button == MouseButton::Left) {
                if (m_navHover >= 0) {
                    activateNav(m_navHover);
                    return true;
                }
                if (m_quickHover >= 0) {
                    // A header quick action is a switch, not a section.
                    activateQuick(m_quickHover);
                    return true;
                }
                if (m_menu->hoverIndex() >= 0) {
                    m_menu->setSelectedIndex(m_menu->hoverIndex());
                    m_menu->handleClick({(int)e.x, (int)e.y}, m_viewW, m_viewH);
                }
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
               m_chat->isTyping() || m_console->isVisible();
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
        // Same card language as the arrival screen: live corners, a border
        // that follows them, cells that light up as rounded plates.
        dl.addRoundedRectFilled(bar.panel, Color::rgba(0.06f, 0.06f, 0.07f, 0.94f * a), 16.f);
        dl.addRoundedRect(bar.panel, Color::rgba(1.f, 1.f, 1.f, 0.12f * a), 16.f, 1.f);

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
                dl.addRoundedRectFilled({cell.x + 5.f, cell.y + 9.f, cell.w - 10.f, cell.h - 18.f},
                                        Color::rgba(1.f, 1.f, 1.f,
                                                    (isSelected ? 0.10f : 0.05f) * a), 12.f);
            }
            if (isSelected) {
                dl.addRoundedRectFilled({cell.x + 5.f, cell.y + cell.h - 11.f, cell.w - 10.f, 2.f},
                                        Color::rgba(0.86f, 0.13f, 0.13f, a), 1.f);
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

    // ---- startup screen: the tile row -----------------------------------

    /** Main-screen rows with the separators dropped: those are the tiles. */
    std::vector<int> mainRows() const {
        std::vector<int> rows;
        const auto& items = m_menu->items();
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (!items[i].isSeparator) rows.push_back(i);
        }
        return rows;
    }

    /** Slot of a row inside that list (0 when it is not there). */
    static int mainSlot(const std::vector<int>& rows, int row) {
        for (int s = 0; s < static_cast<int>(rows.size()); ++s) {
            if (rows[s] == row) return s;
        }
        return 0;
    }

    MainMenuLayout mainLayout(int w, int h) const {
        const std::vector<int> rows = mainRows();
        return layoutMainMenu(w, h, static_cast<int>(rows.size()),
                              mainSlot(rows, m_menu->selectedIndex()));
    }

    /** A key cap and its label; returns where the next one starts. */
    float keyHint(DrawList& dl, float x, const Rect& bar, const char* key,
                  const char* label, float a) {
        const float scale = 0.95f;
        const float pad = 9.f;
        const float capW = m_text.measureWidth(key, scale) + pad * 2.f;
        const float capH = bar.h * 0.46f;
        const float y = bar.y + (bar.h - capH) * 0.5f;
        dl.addRectFilled({x, y, capW, capH}, Color::rgba(1.f, 1.f, 1.f, 0.14f * a));
        dl.addRect({x, y, capW, capH}, Color::rgba(1.f, 1.f, 1.f, 0.22f * a), 1.f);
        m_text.draw(dl, x + pad, y + capH * 0.24f, key,
                    sty(Color::rgba(0.95f, 0.95f, 0.96f, a), scale));
        m_text.draw(dl, x + capW + 9.f, y + capH * 0.24f, label,
                    sty(Color::rgba(0.72f, 0.72f, 0.76f, a), scale));
        return x + capW + 9.f + m_text.measureWidth(label, scale);
    }

    /** The screen the game opens on: one tile per section, the selected one
     *  named large on the right with what it is for, the scene behind. */
    void buildMainTiles(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        if (a <= 0.01f) return;
        const float W = static_cast<float>(w);
        const float H = static_cast<float>(h);
        const MainMenuLayout L = mainLayout(w, h);
        const std::vector<int> rows = mainRows();
        const auto& items = m_menu->items();
        const int selSlot = mainSlot(rows, m_menu->selectedIndex());

        // The showroom is behind this screen, so it is dimmed rather than
        // covered: the hall the game just loaded is part of the menu.
        // Enough shade for the labels to read, not enough to turn midday into
        // dusk: the car sits in the middle of the screen and has to stay lit.
        dl.addRectFilled({0.f, 0.f, W, H * 0.40f}, Color::rgba(0.f, 0.f, 0.f, 0.12f * a));
        dl.addRectFilled({0.f, H * 0.30f, W, H * 0.36f}, Color::rgba(0.f, 0.f, 0.f, 0.06f * a));

        drawHeader(dl, w, h);

        for (int i = 0; i < L.count; ++i) {
            // The strip counts slots, the menu counts rows, and the separators
            // sit between them: a slot points at the next row that is a tile.
            const int slot = L.first + i;
            if (slot < 0 || slot >= static_cast<int>(rows.size())) continue;
            const int row = rows[slot];
            const MenuItem& item = items[row];
            const Rect r = L.tile[i];
            const bool selected = (i == selSlot);
            const bool hover = (slot == m_menu->hoverIndex());

            // A card, not a plate: live corners, the glyph as its artwork and
            // the label on the card itself. The dark strip that used to sit
            // under the label cut the card into two rectangles and hid the
            // bottom of the glyph, which is what made the strip read square.
            const float radius = std::clamp(r.h * 0.11f, 8.f, 18.f);
            dl.addRoundedRectFilled(r, Color::rgba(0.05f, 0.05f, 0.07f,
                                                   (selected ? 0.92f
                                                             : (hover ? 0.80f : 0.68f)) * a),
                                    radius);
            if (const SvgIcon* icon = icons::get(item.icon)) {
                const float size = std::min(r.w, r.h) * 0.46f;
                icon->draw(dl, r.x + r.w - size - 22.f,
                           r.y + (r.h - size) * 0.5f - 8.f, size,
                           Color::rgba(1.f, 1.f, 1.f,
                                       (selected ? 0.15f : (hover ? 0.11f : 0.075f)) * a),
                           1.f);
            }
            m_text.draw(dl, r.x + 20.f, r.y + r.h - 38.f, item.text,
                        sty(selected ? Color::rgba(1.f, 1.f, 1.f, a)
                                     : Color::rgba(0.82f, 0.82f, 0.86f, a),
                            1.15f, selected));
            if (selected) {
                dl.addRoundedRectFilled({r.x + 20.f, r.y + r.h - 16.f, 44.f, 3.f},
                                        Color::rgba(0.86f, 0.13f, 0.13f, a), 1.5f);
                dl.addRoundedRect(r, Color::rgba(1.f, 1.f, 1.f, 0.20f * a), radius, 1.f);
            } else if (hover) {
                dl.addRoundedRect(r, Color::rgba(1.f, 1.f, 1.f, 0.12f * a), radius, 1.f);
            }
        }

        if (selSlot >= 0 && selSlot < L.count) {
            const int slot = L.first + selSlot;
            const int row =
                (slot >= 0 && slot < static_cast<int>(rows.size())) ? rows[slot] : -1;
            if (row >= 0) {
                const MenuItem& item = items[row];
                const float slide = (1.f - m_menu->transitionProgress()) * 26.f;
                const float titleScale = 2.5f;
                // The name and its line sit over the scene, and the showroom
                // behind them is bright: without a plate the text washed out
                // into the marble. Same plate for both, so the block reads as
                // one caption and travels with the fade.
                const float plateY = L.title.y - 26.f;
                const float plateH = (L.body.y + L.body.h * 0.62f) - plateY + 18.f;
                dl.addRectFilled({L.title.x - 18.f, plateY, L.title.w + 36.f, plateH},
                                 Color::rgba(0.03f, 0.03f, 0.05f, 0.55f * a));
                dl.addRectFilled({L.title.x - 18.f, plateY, 3.f, plateH},
                                 Color::rgba(0.86f, 0.13f, 0.13f, 0.85f * a));
                dl.addRectFilled({L.title.x, L.title.y - 18.f, 56.f, 3.f},
                                 Color::rgba(0.86f, 0.13f, 0.13f, a));
                m_text.draw(dl, L.title.x + slide, L.title.y, item.text,
                            sty(Color::rgba(1.f, 1.f, 1.f, a), titleScale, true));
                if (!item.description.empty()) {
                    dl.addTextWrapped(L.body.x + slide, L.body.y, item.description,
                                      Color::rgba(0.87f, 0.87f, 0.90f, a), 1.3f, L.body.w);
                }
            }
        }

        dl.addRectFilled(L.bottomBar, Color::rgba(0.03f, 0.03f, 0.04f, 0.80f * a));
        dl.addLine(0.f, L.bottomBar.y, W, L.bottomBar.y,
                   Color::rgba(1.f, 1.f, 1.f, 0.10f * a), 1.f);
        const float afterEsc = keyHint(dl, L.margin, L.bottomBar, "ESC", "EXIT", a);
        keyHint(dl, afterEsc + 28.f, L.bottomBar, "ENTER", "CONFIRM", a);
        const char* nav = "LEFT/RIGHT   select";
        const float navW = m_text.measureWidth(nav, 0.95f);
        m_text.draw(dl, W - L.home.x - navW, L.bottomBar.y + L.bottomBar.h * 0.30f, nav,
                    sty(Color::rgba(0.55f, 0.55f, 0.60f, a), 0.95f));
    }

    /** The notice list, hanging from the bell that opens it: right-aligned
     *  with the bar, so it opens under the glyph the player pressed. */
    Rect notificationPanel(int w, int h) const {
        const MainMenuLayout L = mainLayout(w, h);
        const float width = std::min(380.f, static_cast<float>(w) * 0.32f);
        const float rows = static_cast<float>(
            std::min<std::size_t>(m_notices.items().size(), 5));
        const Rect bell = L.quick[MainMenuLayout::kClockSlot];
        return {bell.x + bell.w - width, L.topBar.y + L.topBar.h + 8.f, width,
                54.f + rows * 30.f};
    }

    void buildNotifications(DrawList& dl, int w, int h) {
        if (!m_notices.open()) return;
        const float fade = m_menu->fadeAlpha();
        const float a = fade > 0.01f ? fade : 1.f;
        const Rect panel = notificationPanel(w, h);
        dl.addRoundedRectFilled(panel, Color::rgba(0.04f, 0.04f, 0.05f, 0.96f * a), 12.f);
        dl.addRoundedRect(panel, Color::rgba(1.f, 1.f, 1.f, 0.14f * a), 12.f, 1.f);
        m_text.draw(dl, panel.x + 16.f, panel.y + 14.f, "NOTIFICATIONS",
                    sty(Color::rgba(0.86f, 0.25f, 0.25f, a), 0.95f, true));
        if (m_notices.empty()) {
            m_text.draw(dl, panel.x + 16.f, panel.y + 40.f, "Nothing to report",
                        sty(Color::rgba(0.62f, 0.62f, 0.66f, a), 1.f));
            return;
        }
        const auto& items = m_notices.items();
        const std::size_t first = items.size() > 5 ? items.size() - 5 : 0;
        float y = panel.y + 40.f;
        for (std::size_t i = items.size(); i > first; --i) {
            const Notice& notice = items[i - 1];
            const Color dot = notice.kind == NoticeKind::Warning
                ? Color::rgba(0.86f, 0.55f, 0.20f, a)
                : Color::rgba(0.45f, 0.70f, 0.95f, a);
            dl.addRoundedRectFilled({panel.x + 16.f, y + 6.f, 6.f, 6.f}, dot, 3.f);
            m_text.draw(dl, panel.x + 30.f, y, notice.text,
                        sty(Color::rgba(0.86f, 0.86f, 0.89f, a), 1.f));
            y += 30.f;
        }
    }

    /** The bar every screen wears: navigation and the section's name on the
     *  left, the driver's plate and the quick actions on the right, the clock
     *  closing it. Painted from the layout that hit-tests it, so a button the
     *  player sees is a button they can press. */
    void drawHeader(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        const MainMenuLayout L = mainLayout(w, h);
        dl.addRectFilled(L.topBar, Color::rgba(0.03f, 0.03f, 0.04f, 0.60f * a));
        dl.addLine(0.f, L.topBar.h, static_cast<float>(w), L.topBar.h,
                   Color::rgba(1.f, 1.f, 1.f, 0.10f * a), 1.f);

        // Home / back / forward / refresh, flat and unlabelled, with the
        // screen's name after them: the bar reads left to right like a
        // window's, and the first button always has somewhere to go.
        static const char* kNavIcons[MainMenuLayout::kNavCount] = {
            "home", "back", "forward", "refresh"};
        for (int i = 0; i < MainMenuLayout::kNavCount; ++i) {
            const Rect& r = L.nav[i];
            const bool live = navEnabled(i);
            const bool hot = live && (i == m_navHover);
            if (hot) dl.addRectFilled(r, Color::rgba(1.f, 1.f, 1.f, 0.14f * a));
            const float alpha = live ? (hot ? 1.f : 0.70f) : 0.20f;
            if (const SvgIcon* icon = icons::get(kNavIcons[i])) {
                const float inset = r.w * 0.18f;
                icon->draw(dl, r.x + inset, r.y + inset, r.w - inset * 2.f,
                           Color::rgba(0.92f, 0.92f, 0.95f, alpha * a), 1.f);
            }
        }

        const std::string label = (m_menu->state() == MenuState::Main)
            ? std::string("HOME") : m_menu->sectionTitle();
        m_text.draw(dl, L.home.x, L.home.y, label,
                    sty(Color::rgba(0.90f, 0.90f, 0.92f, a), 1.f, true));

        // The driver's plate: the name and an avatar carrying the initial,
        // which is all the profile art there is.
        const std::string driver = m_menu->profile().name;
        if (!driver.empty()) {
            char initial = driver[0];
            if (initial >= 'a' && initial <= 'z') initial = static_cast<char>(initial - 32);
            const float nameScale = 1.05f;
            const float nameW = m_text.measureWidth(driver, nameScale);
            m_text.draw(dl, L.avatar.x - nameW - 16.f, L.avatar.y + L.avatar.h * 0.34f, driver,
                        sty(Color::rgba(0.96f, 0.96f, 0.97f, a), nameScale));
            dl.addRectFilled(L.avatar, Color::rgba(1.f, 1.f, 1.f, 0.12f * a));
            dl.addRect(L.avatar, Color::rgba(1.f, 1.f, 1.f, 0.24f * a), 1.f);
            const std::string initialText(1, initial);
            const float iw = m_text.measureWidth(initialText, 1.35f);
            m_text.draw(dl, L.avatar.x + (L.avatar.w - iw) * 0.5f, L.avatar.y + L.avatar.h * 0.30f,
                        initialText, sty(Color::rgba(0.96f, 0.96f, 0.97f, a), 1.35f, true));
        }

        // The quick actions, then the time, then the notification glyph that
        // closes the bar. The slot used to be a second clock face of its own,
        // which read as an unlabelled button next to the time it duplicated.
        static const char* kQuickIcons[MainMenuLayout::kQuickCount] = {
            "speaker", "vr", "pad", "bell"};
        for (int i = 0; i < MainMenuLayout::kQuickCount; ++i) {
            const Rect& r = L.quick[i];
            const bool hot = (i == m_quickHover);
            if (hot) dl.addRectFilled(r, Color::rgba(1.f, 1.f, 1.f, 0.14f * a));
            const bool active = (i == MainMenuLayout::kClockSlot) && m_notices.open();
            const Color tint = (hot || active) ? Color::rgba(1.f, 1.f, 1.f, a)
                                               : Color::rgba(0.74f, 0.74f, 0.78f, a);
            if (const SvgIcon* icon = icons::get(kQuickIcons[i])) {
                const float inset = r.w * 0.16f;
                icon->draw(dl, r.x + inset, r.y + inset, r.w - inset * 2.f, tint, 1.f);
            }
            // The badge counts what arrived while the player was not looking;
            // opening the list is what clears it.
            if (i == MainMenuLayout::kClockSlot && m_notices.unread() > 0) {
                const float dotH = 15.f;
                const Rect dot{r.x + r.w - 14.f, r.y + 1.f, 20.f, dotH};
                dl.addRoundedRectFilled(dot, Color::rgba(0.86f, 0.13f, 0.13f, a), dotH * 0.5f);
                char count[8];
                const std::size_t unread = m_notices.unread();
                if (unread > 9) std::snprintf(count, sizeof(count), "9+");
                else std::snprintf(count, sizeof(count), "%d", static_cast<int>(unread));
                const float cw = m_text.measureWidth(count, 0.62f);
                m_text.draw(dl, dot.x + (dot.w - cw) * 0.5f, dot.y + 2.f, count,
                            sty(Color::rgba(1.f, 1.f, 1.f, a), 0.62f, true));
            }
        }
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        if (const std::tm* fromClock = std::localtime(&now)) local = *fromClock;
        char timeText[16];
        std::snprintf(timeText, sizeof(timeText), "%02d:%02d", local.tm_hour, local.tm_min);
        const float tw = m_text.measureWidth(timeText, 1.f);
        const Rect& clock = L.quick[MainMenuLayout::kClockSlot];
        m_text.draw(dl, clock.x - tw - 10.f, clock.y + clock.h * 0.22f, timeText,
                    sty(Color::rgba(0.88f, 0.88f, 0.90f, a), 1.f));
    }

    /** Header quick action under the cursor, or -1. */
    int quickIconAt(float mx, float my, int w, int h) const {
        const MainMenuLayout L = mainLayout(w, h);
        for (int i = 0; i < MainMenuLayout::kQuickCount; ++i) {
            if (L.quick[i].contains(mx, my)) return i;
        }
        return -1;
    }

    /** The bar's moves, in layout order. */
    enum class NavAction { Home = 0, Back = 1, Forward = 2, Refresh = 3 };

    bool navEnabled(int index) const {
        if (index == static_cast<int>(NavAction::Home))
            return m_menu->state() != MenuState::Main &&
                   m_menu->state() != MenuState::Pause;
        if (index == static_cast<int>(NavAction::Back)) return m_navPos > 0;
        if (index == static_cast<int>(NavAction::Forward))
            return m_navPos + 1 < m_navHistory.size();
        return true; // refresh always has a screen to rebuild
    }

    void openHistoryState() {
        const MenuState target = m_navHistory[m_navPos];
        if (target == m_menu->state()) return;
        m_menu->openScreen(target);
        // This move retraces a step of the trail, it does not add one.
        m_navLast = target;
    }

    void activateNav(int index) {
        switch (static_cast<NavAction>(index)) {
        case NavAction::Home:
            // The first screen is one step of the trail like any other move:
            // the back chevron then returns to the page the player left.
            if (m_menu->state() != MenuState::Main) m_menu->openScreen(MenuState::Main);
            break;
        case NavAction::Back:
            if (m_navPos == 0) return;
            --m_navPos;
            openHistoryState();
            break;
        case NavAction::Forward:
            if (m_navPos + 1 >= m_navHistory.size()) return;
            ++m_navPos;
            openHistoryState();
            break;
        case NavAction::Refresh:
            // The rows come from the menu's own builders, so re-opening the
            // screen is what picking up whatever changed behind it means.
            m_menu->openScreen(m_menu->state());
            break;
        }
    }

    /** Records the state the player just entered: a new step drops whatever
     *  was left ahead of it, the way a browser's trail does. */
    void syncNavHistory() {
        const MenuState now = m_menu->state();
        if (now == m_navLast) return;
        m_navLast = now;
        if (m_navHistory.empty()) {
            m_navHistory.push_back(now);
            m_navPos = 0;
            return;
        }
        if (m_navPos + 1 < m_navHistory.size())
            m_navHistory.resize(m_navPos + 1);
        m_navHistory.push_back(now);
        m_navPos = m_navHistory.size() - 1;
        if (m_navHistory.size() > 64) {
            m_navHistory.erase(m_navHistory.begin());
            m_navPos -= 1;
        }
    }

    /** The bar's four icons: sound, the VR screen, the controller, then the
     *  bell, which opens the list of what just happened. */
    void activateQuick(int index) {
        switch (index) {
        case 0: // sound: the audio page of the settings panel
            if (m_menu->onOpenSettingsPanelRequested)
                m_menu->onOpenSettingsPanelRequested("audio");
            break;
        case 1: // the VR screen
            if (m_menu->onToggleFullscreenRequested) m_menu->onToggleFullscreenRequested();
            break;
        case 2: // controller: the bindings screen
            m_menu->openScreen(MenuState::Controls);
            break;
        default: // the bell: open or close the notice list
            m_notices.toggle();
            break;
        }
    }

    void updateMenuHover(float mx, float my, int w, int h) {
        m_quickHover = -1;
        m_navHover = -1;
        // The bar belongs to every screen but the in-car pause overlay, which
        // brings a bar of its own.
        if (m_menu->state() != MenuState::Pause) {
            const MainMenuLayout bar = mainLayout(w, h);
            for (int i = 0; i < MainMenuLayout::kNavCount; ++i) {
                if (bar.nav[i].contains(mx, my)) m_navHover = i;
            }
            if (m_navHover >= 0) {
                m_menu->setHoverIndex(-1);
                return;
            }
        }
        // The quick actions belong to the bar, so they are live on every
        // screen that wears it — the bell used to answer only on the tiles.
        if (m_menu->state() != MenuState::Pause) {
            const int quick = quickIconAt(mx, my, w, h);
            if (quick >= 0) {
                m_quickHover = quick;
                m_menu->setHoverIndex(-1); // the bar is not a tile
                return;
            }
        }
        if (m_menu->state() == MenuState::Main) {
            const int hover = mainMenuTileAt(mainLayout(w, h), mx, my);
            m_menu->setHoverIndex(hover);
            if (hover >= 0) m_menu->setSelectedIndex(hover);
            return;
        }
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
        // The arrival screen is the tile row, not the list: it is the first
        // thing the player sees and it has to read as one.
        if (m_menu->state() == MenuState::Main) {
            buildMainTiles(dl, w, h);
            return;
        }

        const float t = m_menu->transitionProgress();
        const float slide = (1.f - t) * 40.f;

        dl.addRectFilled({0, 0, (float)w, (float)h}, Color::rgba(0.f, 0.f, 0.f, 0.72f * a));
        dl.addRectFilled({0, 0, 8.f, (float)h}, Color::rgba(0.85f, 0.12f, 0.12f, 0.9f * a));

        // Every screen keeps the bar: the section's name is in it, so the
        // wordmark that used to sit here is gone.
        drawHeader(dl, w, h);

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
                // A row is a card too: the same rounded plate the tiles wear,
                // sized to the row, with the accent inside its corner.
                dl.addRoundedRectFilled({L.left - 12.f, iy - 2.f, L.listW + 24.f, L.rowH - 2.f},
                                        Color::rgba(1.f, 1.f, 1.f,
                                                    (selected ? 0.09f : 0.06f) * a), 8.f);
                dl.addRoundedRectFilled({L.left - 12.f, iy + 4.f, L.accentW, L.rowH - 14.f},
                                        Color::rgba(0.9f, 0.15f, 0.15f, a), 1.f);
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
                dl.addRoundedRectFilled({px, py, pw, 160.f},
                                        Color::rgba(0.05f, 0.05f, 0.07f, 0.85f * a), 12.f);
                dl.addRoundedRectFilled({px, py + 10.f, 3.f, 140.f},
                                        Color::rgba(0.9f, 0.15f, 0.15f, 0.7f * a), 1.5f);
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
    NotificationCenter m_notices;     // what the bell reports
    int m_quickHover = -1;            // header quick action under the cursor
    int m_navHover = -1;              // bar navigation button under the cursor
    // The trail the bar's arrows walk: the states the player went through, in
    // order, and where in it they are now.
    std::vector<MenuState> m_navHistory{MenuState::Main};
    size_t m_navPos = 0;
    MenuState m_navLast = MenuState::Main;

    std::unique_ptr<GameMenuOverlay> m_menu;
    std::unique_ptr<DashboardOverlay> m_dash;
    std::unique_ptr<TelemetryOverlay> m_telem;
    std::unique_ptr<DeviceSettingsOverlay> m_devices;
    std::unique_ptr<MultiplayerOverlay> m_mp;
    std::unique_ptr<ChatOverlay> m_chat;
    std::unique_ptr<ConsoleOverlay> m_console;
};

} // namespace ui
} // namespace sim
} // namespace ks
