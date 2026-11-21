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
#include "ProfileSettingsOverlay.h"
#include "KeyRebindOverlay.h"
#include "PadRebindOverlay.h"
#include "ControllerSettingsOverlay.h"
#include "MultiplayerOverlay.h"
#include "MenuRowShape.h"
#include "MenuBackdrop.h"
#include "ChatOverlay.h"
#include "AudioSettingsOverlay.h"
#include "GameplaySettingsOverlay.h"
#include "UiSettingsOverlay.h"
#include "SpectatorScoreboard.h"
#include "ConsoleOverlay.h"
#include "engine/sim/SpectatorMode.h"
#include "CountryFlag.h"
#include "InspectorPanel.h"
#include "UiImages.h"
#include "UiSounds.h"
#include "RaceTelemetryHud.h"
#include "KinetikaAppsOverlay.h"
#include "DigitalDashPanel.h"
#include "../GameMenuOverlay.h"
#include "../CarSpecSheet.h"
#include "../DashboardOverlay.h"
#include "../TelemetryOverlay.h"
#include "../PhysicsGarage.h"
#include "../SetupGarage.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace ks {
namespace sim {
namespace ui {

class NativeUiHub {
public:
    // Theme overrides loaded from theme.json (system/cfg or KS_THEME_JSON).
    inline static std::string themeFontPath;
    inline static float themeFontPx = 0.f;
    inline static std::string themeBackgroundImage;
    /** A surface the theme paints itself: the header bar and the pause screen
     *  have their own ground instead of the shared procedural backdrop. Two
     *  colours, top and bottom, and the alpha the whole thing is drawn with.
     *  `used` false means the theme said nothing and the built-in ground stays. */
    struct ThemeSurface {
        bool used = false;
        float top[4] = {0.f, 0.f, 0.f, 0.f};
        float bottom[4] = {0.f, 0.f, 0.f, 0.f};
        /** Named by the theme and logged, but not drawn: the UI draw list has
         *  no textured quads yet, so an image here would stay invisible. */
        std::string image;
        float imageOpacity = 1.f;
    };
    inline static ThemeSurface themeHeader;
    inline static ThemeSurface themePause;
    /** The decoded textures the theme names, and which slot each one landed in
     *  (-1 when the path was empty or the file could not be read). The quads
     *  point at these slots; the renderer uploads the same store. */
    inline static UiImageStore themeImages;
    inline static int themeHeaderImage = -1;
    inline static int themePauseImage = -1;
    inline static int themePageImage = -1;
    inline static float themeHeaderImageAlpha = 1.f;
    inline static float themePauseImageAlpha = 1.f;
    inline static float themePageImageAlpha = 1.f;
    /** The clicks the theme names: one wav per UiSound, empty when the theme
     *  said nothing for that moment, and the level the whole set plays at. The
     *  hub only carries the names; the app decodes and plays them. */
    inline static std::string themeSound[kUiSoundCount];
    inline static float themeSoundGain = 1.f;
    /** A pointer image the theme names; the app installs it on the window. */
    inline static std::string themeCursorImage;
    inline static float themeCursorHotX = 0.f, themeCursorHotY = 0.f;
    /** The theme the player picked on SETTINGS -> UI, when not empty. It beats
     *  the pointer in system/cfg/theme.json but loses to KS_THEME, so a debug
     *  run still chooses its theme from the environment. */
    inline static std::string themeOverride;
    static void loadThemeJson();
    /** Read the theme again from disk and rebuild the font: how a change made
     *  on the UI page lands in the running menu. */
    void refreshTheme() {
        loadThemeJson();
        applyConfiguredFont();
    }

    NativeUiHub()
        : m_text(std::make_shared<FontAtlas>())
    {
        m_menu = std::make_unique<GameMenuOverlay>();
        m_dash = std::make_unique<DashboardOverlay>();
        m_dash->setVisible(false);
        m_telem = std::make_unique<TelemetryOverlay>();
        m_devices = std::make_unique<DeviceSettingsOverlay>();
m_profiles = std::make_unique<ProfileSettingsOverlay>();
        m_mp = std::make_unique<MultiplayerOverlay>();
        m_chat = std::make_unique<ChatOverlay>();
        m_kinetikaApps.setChat(m_chat.get());
        m_console = std::make_unique<ConsoleOverlay>();
        m_raceHud.setMode(RaceHudMode::Off);
        // The car dash blinks at 8 Hz like the strip does: the clock starts with
        // the UI so a capture is not the only thing that sees it moving.
        m_carDashClock = std::chrono::steady_clock::now();
        m_renderer.setFont(m_text.atlasPtr());
        m_renderer.setImageStore(&themeImages);
        loadThemeJson();
        applyConfiguredFont();
    }

    GameMenuOverlay& menu() { return *m_menu; }
    /** The clicks the menu asked for since the last drain. The app calls this
     *  once a frame and plays what comes out; a theme with no sounds block and
     *  a menu nobody is touching both yield nothing here. */
    int drainSounds(UiSound* out, int maxSounds) { return m_menu->drainSounds(out, maxSounds); }
    DashboardOverlay& dashboard() { return *m_dash; }
    /** Where the two in-race panels are drawn: default corner plus wherever the
     *  driver dragged them, kept on screen. Public because the drag is the
     *  feature and a test has to be able to see where it landed. */
    Rect hdashRect(int w, int h) const {
        // Taller than INPUTS: the aids row that the console changes sits under
        // the warning lights, and the driving warnings (fade, ABS, TC) have a row
        // of their own between them.
        return overlayRect(w, h, 300.f, 24.f, m_hdashDX, m_hdashDY, 200.f);
    }
    Rect inputsRect(int w, int h) const {
        return overlayRect(w, h, 240.f, 24.f + 300.f + 12.f, m_inputsDX, m_inputsDY);
    }

    /** Where the driver left the two in-race panels, as offsets from their
     *  default corner: the loop persists them so the next session opens with the
     *  panels where he put them. */
    struct OverlayOffsets {
        float hdashX = 0.f, hdashY = 0.f;
        float inputsX = 0.f, inputsY = 0.f;
    };
    OverlayOffsets overlayOffsets() const {
        return {m_hdashDX, m_hdashDY, m_inputsDX, m_inputsDY};
    }
    void setOverlayOffsets(const OverlayOffsets& o) {
        m_hdashDX = o.hdashX;
        m_hdashDY = o.hdashY;
        m_inputsDX = o.inputsX;
        m_inputsDY = o.inputsY;
    }
    TelemetryOverlay& telemetry() { return *m_telem; }
    DeviceSettingsOverlay& devices() { return *m_devices; }
ProfileSettingsOverlay& profiles() { return *m_profiles; }
    KeyRebindOverlay& keyRebind() { return m_keyRebind; }
    PadRebindOverlay& padRebind() { return m_padRebind; }
    ControllerSettingsOverlay& controllerSettings() { return m_controllerSettings; }
    MultiplayerOverlay& multiplayer() { return *m_mp; }
    ChatOverlay& chat() { return *m_chat; }
    /** The classification a spectator asked for with TAB: filled by the loop
     *  from the session's own standings and painted over the race. */
    SpectatorScoreboard& spectatorScoreboard() { return m_scoreboard; }
    /** The SETTINGS -> AUDIO page: two columns, the car's channels and the rest. */
    AudioSettingsOverlay& audioSettings() { return m_audioSettings; }
    /** The SETTINGS -> UI page: language, menu theme and the default overlays. */
    UiSettingsOverlay& uiSettings() { return m_uiSettings; }
    /** The SETTINGS -> GAMEPLAY page: difficulty, rules, assists and spotter. */
    GameplaySettingsOverlay& gameplaySettings() { return m_gameplaySettings; }
    /** Which overlays a session starts with. The dashboard's own visibility
     *  turns the H-DASH panel on; the trace and the car display have their own
     *  flags because they share that panel's corner. */
    void setInputsVisible(bool v) { m_inputsVisible = v; }
    bool inputsVisible() const { return m_inputsVisible; }
    void setCarDashVisible(bool v) { m_carDashVisible = v; }
    bool carDashVisible() const { return m_carDashVisible; }
    ConsoleOverlay& console() { return *m_console; }
    RaceTelemetryHud& raceHud() { return m_raceHud; }
    /** The Kinetika apps shelf: F8 opens the manager, F9 hides every app, F10
     *  toggles DEV MODE. The apps read the same sample as the race HUD. */
    KinetikaAppsOverlay& kinetikaApps() { return m_kinetikaApps; }
    /** The display the loaded car declares in its data/data.json (CSP-3). The
     *  loop hands it the car folder; the panel reads it once and draws it. */
    DigitalDashPanel& carDash() { return m_carDash; }
    UiRenderer& renderer() { return m_renderer; }
    TextRenderer& text() { return m_text; }
    UiInput& input() { return m_input; }
    NotificationCenter& notifications() { return m_notices; }
    /** The showroom's part inspector: filled by the loop when a component of
     *  the car is clicked, painted over the room while something is selected. */
    InspectorPanel& inspector() { return m_inspector; }
    /** ENTER on a focused inspector row commits through here: the loop writes
     *  the model document and reloads the car, like the console's MAT. By
     *  value, for the same reason as the loop's commit: the reload replaces
     *  the strings a reference would alias. */
    using InspectorCommitHook = std::function<std::string(std::string material, std::string key,
                                                           std::string value)>;
    void setInspectorCommitHook(InspectorCommitHook hook) {
        m_inspectorCommit = std::move(hook);
    }
    InspectorPanel m_inspector;
    InspectorCommitHook m_inspectorCommit;

    void commitInspectorEdit() {
        if (!m_inspectorCommit || !m_inspector.hasFocus()) return;
        const std::string result =
            m_inspectorCommit(m_inspector.materialName(), m_inspector.pendingKey(),
                              m_inspector.pendingValueText());
        // The loop re-showed the panel with the reloaded values; the result
        // (or the reason it failed) is the footer now. The focus is spent.
        m_inspector.clearFocus();
        m_inspector.setFooter(result);
    }

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

    void pushRaceSample(const RaceHudSample& s) {
        m_raceHud.push(s);
        // One frame, two readers: the HUD and the car's own display are fed the
        // same sample, so the two cannot show different numbers.
        m_carDash.setRaceSample(s);
    }
    /// The setup screen lives in the loop (it reads the loaded car's own
    /// setup.ini); the hub only draws it.
    void setSetupScreen(SetupGarage* screen) {
        m_setupScreen = screen;
        if (m_setupScreen != nullptr) m_setupScreen->setLocale(m_locale);
    }
    /// The physics screen lives in the loop next to the setup screen (it
    /// reads the live vehicle); the hub only draws it.
    void setPhysicsScreen(PhysicsGarage* screen) {
        m_physicsScreen = screen;
        if (m_physicsScreen != nullptr) m_physicsScreen->setLocale(m_locale);
    }

    /** One table for the menu and every technical panel it opens. */
    void setLocale(const ks::engine::config::Locale* locale) {
        m_locale = locale;
        m_menu->setLocale(locale);
        m_keyRebind.setLocale(locale);
        m_devices->setLocale(locale);
        m_controllerSettings.setLocale(locale);
        if (m_setupScreen != nullptr) m_setupScreen->setLocale(locale);
        if (m_physicsScreen != nullptr) m_physicsScreen->setLocale(locale);
    }

    /** The welcome mark, used as a cover while a track or a session is being
     *  prepared. The app calls beginLoading() before a blocking load and
     *  endLoading() once the first frame of what it loaded is about to run, so
     *  the player sees the mark instead of a frozen menu or a black frame. */
    void beginLoading(const std::string& label) {
        m_loading = true;
        m_loadingEndRequested = false;
        m_loadingStart = std::chrono::steady_clock::now();
        m_loadingLabel = label;
    }
    void endLoading() { m_loadingEndRequested = true; }
    bool isLoading() const { return m_loading; }

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

        // Race HUD first (under modal menus). The console is the calibration
        // stage: its panel is the only thing over the room, so the lap counter
        // and the position do not sit on top of the backdrop being tuned.
        // KS_HIDE_MENU is a capture: the room and the developer tools, not the
        // game interface. The loop still decides whether the UI layer runs at
        // all (it does when the console or the inspector is up).
        const bool captureMode = std::getenv("KS_HIDE_MENU") != nullptr;
        if (!captureMode && !m_menu->isVisible() && m_raceHud.isVisible() &&
            !m_console->isVisible())
            m_raceHud.build(dl, m_text, width, height);

        // Kinetika-style apps: independent windows over the scene, under the menus
        // and the developer console. A capture may keep the menu object
        // "visible" while hiding its pixels (KS_HIDE_MENU), so the app hooks are
        // what let a screenshot show the shelf without the start screen.
        const bool captureApps = captureMode &&
            (std::getenv("KS_APPS") != nullptr || std::getenv("KS_APPS_VISIBLE") != nullptr);
        if ((!m_menu->isVisible() || captureApps) && !m_console->isVisible()) {
            m_kinetikaApps.setTrackMap(m_menu->selectedTrackMap());
            m_kinetikaApps.build(dl, m_text, width, height, m_raceHud.sample());
        }

        if (!captureMode && m_carDashVisible && !m_menu->isVisible()) {
            buildCarDash(dl, width, height);
        }
        if (!captureMode && m_telem->isVisible() && !m_menu->isVisible())
            buildTelemetry(dl, width, height);
        // Spectator classification: under the menu (a full-screen page covers
        // it) and over the race, the same layer as the HUD it belongs to.
        if (!captureMode && !m_menu->isVisible())
            m_scoreboard.build(dl, m_text, width, height);

        m_menu->render(width, height);
        if (m_menu->isVisible() || m_menu->fadeAlpha() > 0.01f)
            buildCinematicMenu(dl, width, height);

        m_devices->build(dl, width, height, &m_input);
m_profiles->build(dl, width, height, &m_input);
        m_keyRebind.build(dl, width, height, &m_input);
        m_padRebind.build(dl, width, height, &m_input);
        m_controllerSettings.build(dl, width, height, &m_input);
        m_mp->build(dl, width, height, &m_input);
        m_chat->build(dl, width, height, &m_input);
        // The setup screen is the loop's (it owns the car's document), so the
        // hub is handed the pointer instead of a copy.
        if (m_setupScreen != nullptr && m_setupScreen->isVisible())
            m_setupScreen->build(dl, m_text, width, height, &m_input);
        if (m_physicsScreen != nullptr && m_physicsScreen->isVisible())
            m_physicsScreen->build(dl, m_text, width, height, &m_input);
        m_console->build(dl, width, height, &m_input);
        m_audioSettings.build(dl, m_text, width, height);
        m_uiSettings.build(dl, m_text, width, height);
        m_gameplaySettings.build(dl, m_text, width, height);
        m_inspector.build(dl, width, height);
        // The notice list is painted after every screen: it is a panel over
        // whatever the player is looking at, not a part of it.
        buildNotifications(dl, width, height);
        buildSessionPlanner(dl, width, height);
        // The loading cover goes over everything: it is the screen the player
        // sees while a track or a session is being prepared, so it has to own
        // the frame (menus and HUDs included).
        buildLoadingScreen(dl, width, height);

        m_renderer.endFrame();
        m_input.endFrame();
    }

    bool handleKey(int key) {
        // The loading cover owns input: a key pressed while it is up must not
        // reach the menu or the car it covers.
        if (m_loading) return true;
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
        // The UI and audio pages are modal over the settings screen: they own
        // every key while they are up, and closing one gives SETTINGS back.
        if (m_uiSettings.isVisible()) {
            m_uiSettings.handleKey(key);
            if (!m_uiSettings.isVisible() && !m_menu->isVisible())
                m_menu->setVisible(true);
            return true;
        }
        if (m_gameplaySettings.isVisible()) {
            m_gameplaySettings.handleKey(key);
            if (!m_gameplaySettings.isVisible() && !m_menu->isVisible())
                m_menu->setVisible(true);
            return true;
        }
        if (m_audioSettings.isVisible()) {
            m_audioSettings.handleKey(key);
            if (!m_audioSettings.isVisible() && !m_menu->isVisible())
                m_menu->setVisible(true);
            return true;
        }
        // TAB is the spectator's classification: the state and its flag live in
        // SpectatorMode (the SPECTATE control verb owns entering and leaving),
        // so the key only flips what the panel draws. Outside a spectated
        // session TAB stays free for whoever else wants it.
        if (key == 0x09) {
            auto& spectator = ks::engine::sim::SpectatorMode::instance();
            if (spectator.isSpectating()) {
                spectator.showScoreboard(!spectator.scoreboardVisible());
                return true;
            }
        }
        // A panel opened over the menu gives the menu back when it closes: the
        // first-run wizard opens CONTROLS from here, and being left behind a
        // hidden menu would strand it.
        const auto giveMenuBack = [this](bool panelOpen) {
            if (!panelOpen && !m_menu->isVisible()) m_menu->setVisible(true);
        };
        if (m_devices->isVisible()) {
            const bool handled = m_devices->handleKey(key);
            giveMenuBack(m_devices->isVisible());
            if (handled) return true;
        }
        if (m_profiles->isVisible()) {
            const bool handled = m_profiles->handleKey(key);
            giveMenuBack(m_profiles->isVisible());
            if (handled) return true;
        }
        if (m_keyRebind.isVisible()) {
            const bool handled = m_keyRebind.handleKey(key);
            giveMenuBack(m_keyRebind.isVisible());
            if (handled) return true;
        }
        if (m_padRebind.isVisible()) {
            const bool handled = m_padRebind.handleKey(key);
            giveMenuBack(m_padRebind.isVisible());
            if (handled) return true;
        }
        if (m_controllerSettings.isVisible()) {
            const bool handled = m_controllerSettings.handleKey(key);
            giveMenuBack(m_controllerSettings.isVisible());
            if (handled) return true;
        }
        if (m_mp->isVisible() && m_mp->handleKey(key)) return true;
        // A focused inspector row owns the arrows, ENTER and ESC: the panel
        // is the editor and the frame is what it edits, so typing there never
        // reaches the menu or the chat underneath.
        if (m_inspector.isVisible() && m_inspector.hasFocus()) {
            if (key == 0x25 || key == 0x26) { // LEFT / UP
                m_inspector.adjustFocus(-1);
                return true;
            }
            if (key == 0x27 || key == 0x28) { // RIGHT / DOWN
                m_inspector.adjustFocus(+1);
                return true;
            }
            if (key == 0x0D) { // ENTER: write model.json, like MAT
                commitInspectorEdit();
                return true;
            }
                            if (key == 0x1B) { // ESC: a drag goes back to the pressed
                                   // value, otherwise the focus is dropped
                    if (m_inspector.isDragging()) m_inspector.cancelDrag();
                    else m_inspector.clearFocus();
                    return true;
                }
        }
        // ESC over the session card closes it, not the page behind it - and
        // on the tiles that page would otherwise offer to leave the game.
        if (m_menu->sessionPlannerOpen() && key == 0x1B) {
            m_menu->closeSessionPlanner();
            return true;
        }
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
        // The event-type page wears the same tile row as the main menu, so it
        // borrows its arrow handling: LEFT/RIGHT/UP/DOWN walk the tiles, ENTER
        // opens the mode's config, ESC goes back.
        if (m_menu->isVisible() && m_menu->state() == MenuState::EventSelect) {
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
            if (key == 0x1B) { m_menu->handleKeyPress(0x1B); return true; }
        }
        if (m_menu->isVisible() && m_menu->handleKeyPress(key)) return true;
        if (m_chat->handleKey(key)) return true;
        // F8 app shelf / F9 hide all / F10 DEV MODE. The manager is modal while
        // it is up, so its keys never reach the driving bindings.
        if (m_kinetikaApps.handleKey(key)) return true;
        // Y = cycle race HUD mode (T is taken by the telemetry overlay)
        if (key == 'Y' || key == 'y') {
            m_raceHud.cycleMode();
            return true;
        }
        if (key == 112) { m_devices->toggle(); return true; }
        if (key == 113) { m_mp->toggle(); return true; }
        if (key == 114) { m_profiles->toggle(); return true; }
        if (key == 27 && !m_menu->isVisible()) {
            // Two overlays share this menu: the one before a session, and the
            // pause screen once in the car. The second is gated (see
            // inCarMenuAllowed).
            openInCarMenu(*m_menu, m_inCarMenuAllowed);
            return true;
        }
        return false;
    }

    /** WM_CHAR payload: the console takes it while it is up, the inspector's
     *  focused row next (+/- adjust), the chat otherwise. */
    bool handleChar(int character) {
        if (m_console->isVisible()) return m_console->handleChar(character);
        if (m_menu->isVisible() && m_menu->isTextEditing()) return m_menu->handleChar(character);
        if (m_inspector.isVisible() && m_inspector.handleChar(character)) return true;
        return m_chat->handleChar(character);
    }

    bool handleMouse(const MouseEvent& e) {
        m_input.inject(e);
        if (e.type == MouseEventType::Down && e.button == MouseButton::Left)
            m_input.notePressPosition();
        // The loading cover owns the pointer too: keep the input state coherent
        // (the event was injected above) but let no click reach what it covers.
        if (m_loading) return true;

        if (m_uiSettings.isVisible()) {
            m_uiSettings.handleMouse(e, m_viewW, m_viewH);
            return true;
        }
        if (m_gameplaySettings.isVisible()) {
            m_gameplaySettings.handleMouse(e, m_viewW, m_viewH);
            return true;
        }
        if (m_audioSettings.isVisible()) {
            m_audioSettings.handleMouse(e, m_viewW, m_viewH);
            return true;
        }

        if (m_devices->isVisible() && m_devices->handleMouse(e)) return true;
        if (m_profiles->isVisible() && m_profiles->handleMouse(e)) return true;
        if (m_keyRebind.isVisible() && m_keyRebind.handleMouse(e)) return true;
        if (m_padRebind.isVisible() && m_padRebind.handleMouse(e)) return true;
        if (m_controllerSettings.isVisible() && m_controllerSettings.handleMouse(e)) return true;
        if (m_mp->isVisible() && m_mp->handleMouse(e)) return true;

        // The inspector panel owns its own presses: a click on a row focuses
        // it, a drag moves the pending value and releasing saves it, like
        // ENTER. Hover only lights the row up and never consumes the move.
        if (m_inspector.isVisible()) {
            if (e.type == MouseEventType::Move) {
                if (m_inspector.isDragging()) {
                    m_inspector.dragTo(e.x);
                    return true;
                }
                m_inspector.setHover(m_inspector.hitRow(e.x, e.y, m_viewW));
            } else if (e.type == MouseEventType::Down && e.button == MouseButton::Left &&
                       m_inspector.panelRect(m_viewW).contains(e.x, e.y)) {
                m_inspector.beginDrag(m_inspector.hitRow(e.x, e.y, m_viewW), e.x);
                return true;
            } else if (e.type == MouseEventType::Up && e.button == MouseButton::Left &&
                       m_inspector.isDragging()) {
                // Up anywhere ends the drag: the pointer may have left the
                // panel mid-drag. A press without a drag only focused.
                if (m_inspector.endDrag()) commitInspectorEdit();
                return true;
            }
        }

        // The Kinetika app shelf owns its windows and its manager. It is below the
        // menu (a menu that is up hides the apps) but above the scene drag.
        if (!m_menu->isVisible() && m_kinetikaApps.handleMouse(e, m_viewW, m_viewH)) return true;

        // The in-race panels belong to the driver, like the app windows in AC: a
        // press anywhere on one grabs it and it follows the pointer until the
        // button comes up. Only while they are on screen, and only when no menu
        // is over them.
        if (dragOverlays(e)) return true;

        // A menu that is not up owns no clicks, whatever its fade state says:
        // with KS_HIDE_MENU=1 the start screen never fades out, and its stale
        // hover tables used to swallow every press meant for the room.
        if (m_menu->isVisible()) {
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
                // The session card hangs over the page: a row moves its detail
                // on, a press anywhere else puts the card away rather than
                // pressing whatever it covered.
                if (m_menu->sessionPlannerOpen()) {
                    const SessionPlannerLayout S = plannerLayout(m_viewW, m_viewH);
                    const std::vector<GameMenuOverlay::SessionPlanRow> plan =
                        m_menu->sessionPlanRows();
                    const int row = sessionPlannerRowAt(S, e.x, e.y);
                    if (row >= 0 && row < static_cast<int>(plan.size())) {
                        m_menu->cycleSessionPlanField(plan[row].field);
                        return true;
                    }
                    if (!S.card.contains(e.x, e.y)) {
                        m_menu->closeSessionPlanner();
                        return true;
                    }
                }
                // The walk's two buttons are painted on the bottom bar; a
                // press there moves a step instead of pressing a row.
                if (isStepPage(m_menu->state())) {
                    const StepPage sp = stepPage(m_viewW, m_viewH);
                    if (sp.back.contains(e.x, e.y)) {
                        m_menu->handleKeyPress(0x1B);
                        return true;
                    }
                    if (sp.next.contains(e.x, e.y)) {
                        // The weather page edits the sky in place, so it has no
                        // row whose action would move the walk on: NEXT is the
                        // step itself. Every other page picks its row.
                        if (stepIndex(m_menu->state()) == 2)
                            m_menu->openSessionEvent();
                        else
                            m_menu->activateSelection();
                        return true;
                    }
                }
                if (isStepPage(m_menu->state()) && stepIndex(m_menu->state()) == 0 &&
                    carPageClick(e.x, e.y))
                    return true;
                if (isStepPage(m_menu->state()) && stepIndex(m_menu->state()) == 1 &&
                    trackPageClick(e.x, e.y))
                    return true;
                if (isStepPage(m_menu->state()) && stepIndex(m_menu->state()) == 2 &&
                    weatherPageClick(e.x, e.y))
                    return true;
                if (isStepPage(m_menu->state()) && stepIndex(m_menu->state()) == 3 &&
                    eventPageClick(e.x, e.y))
                    return true;
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
               m_profiles->isVisible() ||
               m_keyRebind.isVisible() || m_padRebind.isVisible() ||
               m_controllerSettings.isVisible() ||
               m_mp->isVisible() ||
               m_audioSettings.isVisible() || m_uiSettings.isVisible() ||
               m_gameplaySettings.isVisible() ||
               m_chat->isTyping() || m_console->isVisible() ||
               m_kinetikaApps.blocksDriving();
    }

private:
    static bool fileReadable(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) return false;
        std::fclose(f);
        return true;
    }

    /** Monospace UI face for the menu and overlays. KS_FONT_TTF picks one
     *  explicitly; otherwise the first monospace candidate available is used
     *  (Consolas on Windows — even widths keep table-like overlays aligned,
     *  unlike the baked 8x8 bitmap, which stays as the last resort). */
    void applyConfiguredFont() {
        float pixelSize = 17.f;
        if (themeFontPx > 0.f && themeFontPx <= 512.f) pixelSize = themeFontPx;
        if (const char* raw = std::getenv("KS_FONT_PX"); raw != nullptr && *raw != '\0') {
            const float parsed = std::strtof(raw, nullptr);
            if (parsed > 0.f && parsed <= 512.f) pixelSize = parsed;
        }
        std::string chosen;
        if (!themeFontPath.empty()) {
            chosen = themeFontPath;
        } else if (const char* path = std::getenv("KS_FONT_TTF"); path != nullptr && *path != '\0') {
            chosen = path;
        } else {
            // A project-local monospace face wins, so a build can ship its own type.
            static const char* kCandidates[] = {
                "system/fonts/ui.ttf",
                "assets/ui/ui.ttf",
                "C:/Windows/Fonts/consola.ttf",
                "C:/Windows/Fonts/CascadiaMono.ttf",
                "C:/Windows/Fonts/lucon.ttf",
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
        std::printf("[ui] font %s @ %.1f px (%.0f px raster) -> %dx%d atlas\n",
                    chosen.c_str(), pixelSize,
                    std::min(pixelSize * kUIFontRasterSupersample, kUIFontRasterMaxPx),
                    font.width(), font.height());
    }

    static TextStyle sty(const Color& c, float scale, bool boldFeel = false,
                         bool shadow = true) {
        TextStyle s;
        s.color = c;
        s.scale = scale;
        s.align = TextAlign::Left;
        // Ink on the light plate carries no shadow: a dark drop shadow under
        // dark letters on a white row reads as a smudge, not as weight.
        // Bold is simulated by thickening the glyph in its own colour, so there
        // is no black outer border around menu/overlay text.
        s.shadow = shadow;
        // No simulated outline: it read as a smudged contour. Bold is carried
        // by the caller's scale/colour instead, so the text stays crisp.
        s.outline = false;
        return s;
    }

    /** RaceRoom's interface, reduced to what this menu needs: near-black flats
     *  with square corners, one red accent, hairline rules instead of plates,
     *  and a light plate under the row the cursor is on (the launcher marks its
     *  current tab the same way: white block, dark text). Every page reads its
     *  colours from here, so the theme is one place and stays consistent. */
public:
    struct Theme {
        inline static float inkR = 0.93f;
        inline static float inkG = 0.93f;
        inline static float inkB = 0.94f;
        static Color ink(float a) { return Color::rgba(inkR, inkG, inkB, a); }
        inline static float inkDimR = 0.60f;
        inline static float inkDimG = 0.60f;
        inline static float inkDimB = 0.63f;
        static Color inkDim(float a) { return Color::rgba(inkDimR, inkDimG, inkDimB, a); }
        /** The one accent: RaceRoom's red. */
        inline static float accentR = 0.95f;
        inline static float accentG = 0.68f;
        inline static float accentB = 0.20f;
        static Color accent(float a) { return Color::rgba(accentR, accentG, accentB, a); }
        /** The plate under the selected row. */
        static Color plate(float a) { return Color::rgba(0.93f, 0.93f, 0.945f, a); }
        /** Ink on that plate: the values keep the red, darkened for contrast. */
        static Color onPlate(float a) { return Color::rgba(0.05f, 0.05f, 0.06f, a); }
        inline static float onPlateAccentR = 0.78f;
        inline static float onPlateAccentG = 0.52f;
        inline static float onPlateAccentB = 0.10f;
        static Color onPlateAccent(float a) { return Color::rgba(onPlateAccentR, onPlateAccentG, onPlateAccentB, a); }
        /** Panels: the INFO box, the pause bar, anything that is a surface. */
        static Color surface(float a) { return Color::rgba(0.055f, 0.055f, 0.062f, a); }
        /** 1px rules: separators, panel borders, the bar's bottom edge. */
        static Color hairline(float a) { return Color::rgba(1.f, 1.f, 1.f, 0.10f * a); }
        /** Hover wash on a row that is not the selected one. */
        static Color hover(float a) { return Color::rgba(1.f, 1.f, 1.f, 0.055f * a); }
    };

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

        if (themePauseImage >= 0) {
            // The pause screen's own picture, full frame, under the dim and the
            // bar: the theme's ground for the screen the player paused onto.
            dl.addImage({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)}, themePauseImage,
                        Color::rgba(1.f, 1.f, 1.f, a * themePauseImageAlpha));
        } else if (themePause.used) {
            // A theme that names a ground for the pause screen gets its own
            // full-frame wash; the dim below is what keeps the session readable
            // over it. Without one, the flat dim is the whole ground.
            fillVerticalGradient(dl, {0.f, 0.f, static_cast<float>(w), static_cast<float>(h)},
                                 themePause.top, themePause.bottom, a, 96);
        }
        dl.addRectFilled({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)},
                         Color::rgba(0.f, 0.f, 0.f, 0.30f * a));
        // The same surface language as the list pages: a flat panel with a
        // hairline border, and the cell under the cursor on the light plate.
        dl.addRectFilled(bar.panel, Theme::surface(0.94f * a));
        dl.addRect(bar.panel, Theme::hairline(a), 1.f);

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
                                 isSelected ? Theme::plate(a) : Theme::hover(a));
            }
            if (isSelected) {
                dl.addRectFilled({cell.x + 5.f, cell.y + cell.h - 11.f, cell.w - 10.f, 2.f},
                                 Theme::accent(a));
            }

            const float centreX = cell.x + cell.w * 0.5f;
            const Color tint = isSelected ? Theme::onPlate(a) : Theme::inkDim(a);
            if (const SvgIcon* icon = icons::get(item.icon)) {
                const float size = 26.f;
                icon->draw(dl, centreX - size * 0.5f, cell.y + 17.f, size, tint,
                           isSelected ? 1.15f : 1.f);
            }
            // DrawList text is left-aligned, so the centring happens here.
            const float labelScale = 0.92f;
            const float labelW = m_text.measureWidth(item.text, labelScale);
            m_text.draw(dl, centreX - labelW * 0.5f, cell.y + 51.f, item.text,
                        sty(tint, labelScale, isSelected, !isSelected));
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

    // ---- the pre-session walk: CARS -> TRACKS -> WEATHER -> SESSION -------
    // The EVENT TYPE page that starts the walk is outside this rail: it picks
    // the session kind before any content, then the four steps run in order.
    // Laid out like rFactor's session screens: the step rail under the header,
    // the current choice named large in a card on the left, the list it is
    // chosen from on the right, BACK and NEXT on the bottom bar. Painted and
    // hit-tested from the same rectangles, so a click lands on what is drawn.

    /** Step of the walk a page is, or -1 when the page is not one of them. */
    static int stepIndex(MenuState state) {
        switch (state) {
        case MenuState::CarSelect: return 0;
        case MenuState::TrackSelect: return 1;
        case MenuState::TrackWeather: return 2;
        case MenuState::Singleplayer: return 3;
        default: return -1;
        }
    }

    static bool isStepPage(MenuState state) { return stepIndex(state) >= 0; }

    struct StepPage {
        static constexpr int kSteps = 4;
        Rect rail{};
        Rect tab[kSteps]{};
        Rect card{};
        Rect list{};
        Rect back{};
        Rect next{};
        float rowH = 44.f;
        int visible = 1;
    };

    /** The CARS page is its own layout: year/class/search over a class rail,
     *  a brand grid, and the selected car's showroom, specs and dyno. */
    struct CarPageLayout {
        Rect filter[3]{};
        Rect series{};
        Rect brands{};
        Rect showcase{};
        Rect modelBar{};
        Rect preview{};
        Rect specs{};
        Rect dyno{};
        Rect paintPrev{};
        Rect paintNext{};
        Rect reset{};
        std::vector<Rect> seriesRows;
        std::vector<Rect> brandTiles;
        std::vector<Rect> modelTabs;
        std::vector<Rect> paintSwatches;
    };


    StepPage stepPage(int w, int h) const {
        StepPage p;
        const float W = static_cast<float>(w);
        const float H = static_cast<float>(h);
        const MainMenuLayout bar = mainLayout(w, h);
        const float topBar = bar.topBar.y + bar.topBar.h;
        p.rail = {72.f, topBar + 18.f, W - 144.f, 30.f};
        const float tabW = std::min(240.f, p.rail.w / static_cast<float>(StepPage::kSteps));
        for (int i = 0; i < StepPage::kSteps; ++i)
            p.tab[i] = {p.rail.x + static_cast<float>(i) * (tabW + 12.f), p.rail.y,
                        tabW, p.rail.h};
        const float top = p.rail.y + p.rail.h + 34.f;
        const float bottom = H - 118.f;
        const float cardW = std::clamp(W * 0.42f, 320.f, 720.f);
        p.card = {72.f, top, cardW, bottom - top};
        const float listX = p.card.x + p.card.w + 40.f;
        p.list = {listX, top, W - listX - 72.f, bottom - top};
        p.rowH = 44.f;
        p.visible = std::max(1, static_cast<int>((p.list.h - 12.f) / p.rowH));
        p.back = {72.f, H - 92.f, 150.f, 44.f};
        p.next = {W - 72.f - 170.f, H - 92.f, 170.f, 44.f};
        return p;
    }

    /** WEATHER page: the sky the session will start with, its source and the
     *  controls that edit it. CUSTOM owns six steppers; TODAY/REAL DATE own a
     *  date and a service request; DRY is a fixed preset. */
    struct WeatherPageLayout {
        Rect modeBar{};
        Rect modeChip[4]{};
        Rect conditions{};
        Rect controls{};
        Rect timeMinus{};
        Rect timePlus{};
        Rect dateMinus{};
        Rect datePlus{};
        Rect ask{};
        Rect fieldMinus[6]{};
        Rect fieldValue[6]{};
        Rect fieldPlus[6]{};
    };

    /** EVENT page: the session's car/track/weather on the left, one tile per
     *  event type on the right. A tile opens that event's MODE SETUP. */
    struct EventPageLayout {
        Rect summary{};
        Rect events{};
        Rect modeCards[8]{};
    };

    /** TRACKS page: country/type/search over a category rail, circuit tiles,
     *  and the selected circuit's layout, map, facts and sector ribbon. */
    struct TrackPageLayout {
        Rect filter[3]{};
        Rect reset{};
        Rect categories{};
        Rect cards{};
        Rect showcase{};
        Rect layoutBar{};
        Rect map{};
        Rect specs{};
        Rect telemetry{};
        std::vector<Rect> categoryRows;
        std::vector<Rect> cardTiles;
        std::vector<Rect> layoutTabs;
    };

    CarPageLayout carPageLayout(int w, int h) const {
        CarPageLayout L;
        const StepPage p = stepPage(w, h);
        const float left = p.rail.x;
        const float right = p.rail.x + p.rail.w;
        const float top = p.rail.y + p.rail.h + 20.f;
        const float bottom = p.back.y - 12.f;
        const float width = right - left;
        const float gap = 12.f;
        const float filterH = 56.f;
        const float resetW = 82.f;
        L.filter[0] = {left, top, width * 0.20f, filterH};
        L.filter[1] = {L.filter[0].x + L.filter[0].w + gap, top, width * 0.28f, filterH};
        L.filter[2] = {L.filter[1].x + L.filter[1].w + gap, top,
                       right - resetW - gap - (L.filter[1].x + L.filter[1].w + gap), filterH};
        L.reset = {right - resetW, top + 18.f, resetW, filterH - 18.f};

        const float midTop = top + filterH + 14.f;
        const float midH = std::clamp((bottom - midTop) * 0.20f, 112.f, 190.f);
        L.series = {left, midTop, width * 0.30f, midH};
        L.brands = {L.series.x + L.series.w + gap, midTop,
                    right - (L.series.x + L.series.w + gap), midH};
        const float showTop = midTop + midH + 14.f;
        L.showcase = {left, showTop, width, std::max(200.f, bottom - showTop)};
        L.modelBar = {L.showcase.x, L.showcase.y, L.showcase.w, 44.f};
        // The studio takes the left of the lower showcase; specs and the dyno
        // stack in a column beside it. That keeps the car large on a 1280-wide
        // logical page instead of squeezing it above two rows of cards.
        L.preview = {L.showcase.x, L.modelBar.y + L.modelBar.h,
                     L.showcase.w * 0.56f, std::max(120.f, L.showcase.h - L.modelBar.h)};
        L.specs = {L.preview.x + L.preview.w + gap, L.preview.y,
                   L.showcase.w - L.preview.w - gap,
                   std::max(80.f, (L.preview.h - 8.f) * 0.57f)};
        L.dyno = {L.specs.x, L.specs.y + L.specs.h + 8.f, L.specs.w,
                  std::max(60.f, L.preview.y + L.preview.h - (L.specs.y + L.specs.h + 8.f))};

        const float seriesTop = L.series.y + 38.f;
        for (int i = 0; i <= static_cast<int>(m_menu->carClasses().size()); ++i) {
            const float y = seriesTop + static_cast<float>(i) * 26.f;
            if (y + 24.f > L.series.y + L.series.h - 8.f) break;
            L.seriesRows.push_back({L.series.x + 10.f, y, L.series.w - 20.f, 24.f});
        }
        const int brandCount = static_cast<int>(m_menu->carBrands().size()) + 1;
        const int cols = std::max(2, static_cast<int>(L.brands.w / 145.f));
        const float tileW = (L.brands.w - 20.f - static_cast<float>(cols - 1) * 8.f) /
                            static_cast<float>(cols);
        const int maxTiles = std::max(0, static_cast<int>((L.brands.h - 46.f) / 48.f)) * cols;
        for (int i = 0; i < std::min(maxTiles, brandCount); ++i) {
            const int col = i % cols;
            const int row = i / cols;
            L.brandTiles.push_back({L.brands.x + 10.f + static_cast<float>(col) * (tileW + 8.f),
                                    L.brands.y + 38.f + static_cast<float>(row) * 48.f,
                                    tileW, 42.f});
        }

        const auto& entries = m_menu->carEntries();
        const float modelX = L.modelBar.x + 92.f;
        const float paintX = L.modelBar.x + L.modelBar.w - 360.f;
        const float tabW = 142.f;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const float x = modelX + static_cast<float>(i) * (tabW + 6.f);
            if (x + tabW > paintX - 10.f) break;
            L.modelTabs.push_back({x, L.modelBar.y + 7.f, tabW, 30.f});
        }
        L.paintPrev = {paintX, L.modelBar.y + 8.f, 22.f, 28.f};
        L.paintNext = {L.modelBar.x + L.modelBar.w - 28.f, L.modelBar.y + 8.f, 22.f, 28.f};
        const auto* entry = m_menu->selectedCarEntry();
        const int paints = entry ? static_cast<int>(entry->paints.size()) : 0;
        const int swatches = std::min(5, paints);
        const float swatchX = L.paintPrev.x + 34.f;
        for (int i = 0; i < swatches; ++i) {
            L.paintSwatches.push_back({swatchX + static_cast<float>(i) * 24.f,
                                       L.modelBar.y + 12.f, 18.f, 18.f});
        }
        return L;
    }

    TrackPageLayout trackPageLayout(int w, int h) const {
        TrackPageLayout L;
        const StepPage p = stepPage(w, h);
        const float left = p.rail.x;
        const float right = p.rail.x + p.rail.w;
        const float width = right - left;
        const float top = p.rail.y + p.rail.h + 20.f;
        const float bottom = p.back.y - 12.f;
        const float gap = 12.f;
        const float filterH = 54.f;
        const float resetW = 82.f;
        L.filter[0] = {left, top, width * 0.23f, filterH};
        L.filter[1] = {L.filter[0].x + L.filter[0].w + gap, top, width * 0.30f, filterH};
        L.filter[2] = {L.filter[1].x + L.filter[1].w + gap, top,
                       right - resetW - gap - (L.filter[1].x + L.filter[1].w + gap), filterH};
        L.reset = {right - resetW, top + 16.f, resetW, filterH - 16.f};

        const float midTop = top + filterH + 10.f;
        const float midH = std::clamp((bottom - midTop) * 0.23f, 116.f, 190.f);
        L.categories = {left, midTop, width * 0.26f, midH};
        L.cards = {L.categories.x + L.categories.w + gap, midTop,
                   right - (L.categories.x + L.categories.w + gap), midH};
        const float showTop = midTop + midH + 10.f;
        L.showcase = {left, showTop, width, std::max(160.f, bottom - showTop)};
        L.layoutBar = {L.showcase.x, L.showcase.y, L.showcase.w, 30.f};
        L.map = {L.showcase.x, L.layoutBar.y + L.layoutBar.h, L.showcase.w * 0.58f,
                 std::max(100.f, L.showcase.h - L.layoutBar.h)};
        const float sideX = L.map.x + L.map.w + gap;
        const float sideW = L.showcase.w - L.map.w - gap;
        const float sideH = std::max(70.f, L.showcase.h - L.layoutBar.h);
        L.specs = {sideX, L.map.y, sideW, std::max(64.f, (sideH - 8.f) * 0.56f)};
        L.telemetry = {sideX, L.specs.y + L.specs.h + 8.f, sideW,
                       std::max(54.f, L.map.y + sideH - (L.specs.y + L.specs.h + 8.f))};

        const float catTop = L.categories.y + 34.f;
        for (int i = 0; i <= static_cast<int>(m_menu->trackCircuitTypes().size()); ++i) {
            const float y = catTop + static_cast<float>(i) * 24.f;
            if (y + 20.f > L.categories.y + L.categories.h - 6.f) break;
            L.categoryRows.push_back({L.categories.x + 9.f, y, L.categories.w - 18.f, 20.f});
        }

        const auto& tracks = m_menu->trackEntries();
        const int cols = std::max(2, static_cast<int>(L.cards.w / 230.f));
        const float tileW = (L.cards.w - 18.f - static_cast<float>(cols - 1) * 8.f) /
                            static_cast<float>(cols);
        const int rows = std::max(1, static_cast<int>((L.cards.h - 38.f) / 44.f));
        const int maxTiles = rows * cols;
        for (int i = 0; i < std::min(maxTiles, static_cast<int>(tracks.size())); ++i) {
            const int col = i % cols;
            const int row = i / cols;
            L.cardTiles.push_back({L.cards.x + 9.f + static_cast<float>(col) * (tileW + 8.f),
                                    L.cards.y + 36.f + static_cast<float>(row) * 44.f,
                                    tileW, 40.f});
        }

        const ContentEntry* track = m_menu->selectedTrackEntry();
        const int layoutCount = track ? std::max(1, static_cast<int>(track->layouts.size())) : 1;
        for (int i = 0; i < std::min(4, layoutCount); ++i) {
            L.layoutTabs.push_back({L.layoutBar.x + 174.f + static_cast<float>(i) * 154.f,
                                    L.layoutBar.y + 5.f, 146.f, 20.f});
        }
        return L;
    }

    WeatherPageLayout weatherPageLayout(int w, int h) const {
        WeatherPageLayout L;
        const StepPage p = stepPage(w, h);
        const float left = p.rail.x;
        const float right = p.rail.x + p.rail.w;
        const float width = right - left;
        const float top = p.rail.y + p.rail.h + 20.f;
        const float bottom = p.back.y - 12.f;
        const float gap = 12.f;
        L.modeBar = {left, top, width, 38.f};
        float chipX = left;
        for (int i = 0; i < 4; ++i) {
            L.modeChip[i] = {chipX, top + 5.f, i == 3 ? 112.f : 96.f, 28.f};
            chipX += L.modeChip[i].w + 7.f;
        }
        const float contentTop = top + 50.f;
        const float contentH = std::max(180.f, bottom - contentTop);
        const float leftW = width * 0.52f;
        L.conditions = {left, contentTop, leftW, contentH};
        L.controls = {left + leftW + gap, contentTop, right - (left + leftW + gap), contentH};

        const float cx = L.controls.x + L.controls.w - 14.f;
        L.timeMinus = {cx - 112.f, L.controls.y + 48.f, 24.f, 24.f};
        L.timePlus = {cx - 24.f, L.controls.y + 48.f, 24.f, 24.f};
        L.dateMinus = {L.controls.x + 12.f, L.controls.y + 82.f, 24.f, 24.f};
        L.datePlus = {L.dateMinus.x + 142.f, L.controls.y + 82.f, 24.f, 24.f};
        L.ask = {L.controls.x + 12.f, L.controls.y + 116.f, L.controls.w - 24.f, 26.f};

        const float fieldTop = L.controls.y + 84.f;
        const float colW = (L.controls.w - 24.f) * 0.5f;
        for (int i = 0; i < 6; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const float x = L.controls.x + 12.f + static_cast<float>(col) * (colW + 8.f);
            const float y = fieldTop + static_cast<float>(row) * 38.f;
            L.fieldMinus[i] = {x + colW - 78.f, y, 22.f, 24.f};
            L.fieldValue[i] = {x + colW - 54.f, y + 2.f, 30.f, 20.f};
            L.fieldPlus[i] = {x + colW - 22.f, y, 22.f, 24.f};
        }
        return L;
    }

    EventPageLayout eventPageLayout(int w, int h) const {
        EventPageLayout L;
        const StepPage p = stepPage(w, h);
        const float left = p.rail.x;
        const float right = p.rail.x + p.rail.w;
        const float width = right - left;
        const float top = p.rail.y + p.rail.h + 20.f;
        const float bottom = p.back.y - 12.f;
        const float gap = 12.f;
        const float summaryW = width * 0.37f;
        L.summary = {left, top, summaryW, std::max(190.f, bottom - top)};
        L.events = {left + summaryW + gap, top, right - (left + summaryW + gap),
                    std::max(190.f, bottom - top)};

        const float gridTop = L.events.y + 44.f;
        const float gridH = std::max(120.f, L.events.y + L.events.h - gridTop - 10.f);
        const float cardGap = 8.f;
        const float cardW = (L.events.w - 20.f - cardGap) * 0.5f;
        const float cardH = (gridH - cardGap * 3.f) * 0.25f;
        for (int i = 0; i < 8; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            L.modeCards[i] = {L.events.x + 10.f + static_cast<float>(col) * (cardW + cardGap),
                              gridTop + static_cast<float>(row) * (cardH + cardGap),
                              cardW, cardH};
        }
        return L;
    }

    /** Menu rows the step pages list: separators are spacing, not choices. */
    std::vector<int> stepRows() const {
        std::vector<int> rows;
        const auto& items = m_menu->items();
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (!items[i].isSeparator) rows.push_back(i);
        return rows;
    }

    static int stepSlot(const std::vector<int>& rows, int row) {
        for (int s = 0; s < static_cast<int>(rows.size()); ++s)
            if (rows[s] == row) return s;
        return 0;
    }

    /** First row drawn, so the selection is always inside the window. */
    static int stepFirst(const std::vector<int>& rows, int sel, int visible) {
        const int count = static_cast<int>(rows.size());
        if (count <= visible) return 0;
        const int slot = stepSlot(rows, sel);
        return std::clamp(slot - visible + 1, 0, count - visible);
    }

    /** Menu row under the pointer on a step page, or -1. */
    void updateCarPageHover(float mx, float my) {
        const CarPageLayout L = carPageLayout(m_viewW, m_viewH);
        m_carFilterHover = -1;
        m_carSeriesHover = -1;
        m_carBrandHover = -1;
        m_carModelHover = -1;
        m_carPaintHover = -1;
        for (int i = 0; i < 3; ++i)
            if (L.filter[i].contains(mx, my)) m_carFilterHover = i;
        if (L.reset.contains(mx, my)) m_carFilterHover = 3;
        for (int i = 0; i < static_cast<int>(L.seriesRows.size()); ++i)
            if (L.seriesRows[static_cast<std::size_t>(i)].contains(mx, my)) m_carSeriesHover = i;
        for (int i = 0; i < static_cast<int>(L.brandTiles.size()); ++i)
            if (L.brandTiles[static_cast<std::size_t>(i)].contains(mx, my)) m_carBrandHover = i;
        for (int i = 0; i < static_cast<int>(L.modelTabs.size()); ++i)
            if (L.modelTabs[static_cast<std::size_t>(i)].contains(mx, my)) m_carModelHover = i;
        for (int i = 0; i < static_cast<int>(L.paintSwatches.size()); ++i)
            if (L.paintSwatches[static_cast<std::size_t>(i)].contains(mx, my)) m_carPaintHover = i;
    }

    void updateTrackPageHover(float mx, float my) {
        const TrackPageLayout L = trackPageLayout(m_viewW, m_viewH);
        m_trackFilterHover = -1;
        m_trackCategoryHover = -1;
        m_trackCardHover = -1;
        m_trackLayoutHover = -1;
        for (int i = 0; i < 3; ++i)
            if (L.filter[i].contains(mx, my)) m_trackFilterHover = i;
        if (L.reset.contains(mx, my)) m_trackFilterHover = 3;
        for (int i = 0; i < static_cast<int>(L.categoryRows.size()); ++i)
            if (L.categoryRows[static_cast<std::size_t>(i)].contains(mx, my))
                m_trackCategoryHover = i;
        for (int i = 0; i < static_cast<int>(L.cardTiles.size()); ++i)
            if (L.cardTiles[static_cast<std::size_t>(i)].contains(mx, my)) m_trackCardHover = i;
        for (int i = 0; i < static_cast<int>(L.layoutTabs.size()); ++i)
            if (L.layoutTabs[static_cast<std::size_t>(i)].contains(mx, my)) m_trackLayoutHover = i;
    }

    void updateEventPageHover(float mx, float my) {
        const EventPageLayout L = eventPageLayout(m_viewW, m_viewH);
        m_eventModeHover = -1;
        const auto modes = m_menu->effectiveSingleplayerModes();
        for (int i = 0; i < std::min(8, static_cast<int>(modes.size())); ++i)
            if (L.modeCards[i].contains(mx, my)) m_eventModeHover = i;
    }

    bool eventPageClick(float x, float y) {
        if (!isStepPage(m_menu->state()) || stepIndex(m_menu->state()) != 3) return false;
        const EventPageLayout L = eventPageLayout(m_viewW, m_viewH);
        const auto modes = m_menu->effectiveSingleplayerModes();
        for (int i = 0; i < std::min(8, static_cast<int>(modes.size())); ++i) {
            if (!L.modeCards[i].contains(x, y)) continue;
            m_menu->setSelectedIndex(m_menu->firstEventModeRow() + i);
            m_menu->activateSelection();
            return true;
        }
        return false;
    }

    void updateWeatherPageHover(float mx, float my) {
        const WeatherPageLayout L = weatherPageLayout(m_viewW, m_viewH);
        m_weatherModeHover = -1;
        m_weatherTimeHover = -1;
        m_weatherDateHover = -1;
        m_weatherAskHover = -1;
        m_weatherFieldHover = -1;
        for (int i = 0; i < 4; ++i)
            if (L.modeChip[i].contains(mx, my)) m_weatherModeHover = i;
        if (L.timeMinus.contains(mx, my)) m_weatherTimeHover = 0;
        if (L.timePlus.contains(mx, my)) m_weatherTimeHover = 1;
        if (L.dateMinus.contains(mx, my)) m_weatherDateHover = 0;
        if (L.datePlus.contains(mx, my)) m_weatherDateHover = 1;
        if (L.ask.contains(mx, my)) m_weatherAskHover = 1;
        for (int i = 0; i < 6; ++i)
            if (L.fieldMinus[i].contains(mx, my) || L.fieldValue[i].contains(mx, my) ||
                L.fieldPlus[i].contains(mx, my))
                m_weatherFieldHover = i;
    }

    bool weatherPageClick(float x, float y) {
        if (!isStepPage(m_menu->state()) || stepIndex(m_menu->state()) != 2) return false;
        const WeatherPageLayout L = weatherPageLayout(m_viewW, m_viewH);
        static const WeatherSource kModes[] = {WeatherSource::Dry, WeatherSource::Custom,
                                               WeatherSource::Today, WeatherSource::RealDate};
        for (int i = 0; i < 4; ++i) {
            if (!L.modeChip[i].contains(x, y)) continue;
            m_menu->chooseWeatherMode(kModes[i]);
            return true;
        }
        if (L.timeMinus.contains(x, y)) { m_menu->stepWeatherHour(-1); return true; }
        if (L.timePlus.contains(x, y)) { m_menu->stepWeatherHour(1); return true; }
        const WeatherSource mode = m_menu->weatherMode();
        if (mode == WeatherSource::Today || mode == WeatherSource::RealDate) {
            if (L.dateMinus.contains(x, y)) { m_menu->stepWeatherDate(-1); return true; }
            if (L.datePlus.contains(x, y)) { m_menu->stepWeatherDate(1); return true; }
            if (L.ask.contains(x, y)) { m_menu->refreshWeather(); return true; }
        }
        if (mode == WeatherSource::Custom) {
            for (int i = 0; i < 6; ++i) {
                if (L.fieldMinus[i].contains(x, y)) {
                    m_menu->adjustWeatherField(static_cast<GameMenuOverlay::WeatherField>(i), -1);
                    return true;
                }
                if (L.fieldPlus[i].contains(x, y)) {
                    m_menu->adjustWeatherField(static_cast<GameMenuOverlay::WeatherField>(i), 1);
                    return true;
                }
            }
        }
        return false;
    }

    bool trackPageClick(float x, float y) {
        if (!isStepPage(m_menu->state()) || stepIndex(m_menu->state()) != 1) return false;
        const TrackPageLayout L = trackPageLayout(m_viewW, m_viewH);
        const auto leaveSearch = [&]() {
            if (m_menu->trackSearchEditing()) m_menu->endTrackSearch();
        };
        if (L.reset.contains(x, y)) {
            m_menu->clearTrackFilters();
            return true;
        }
        if (L.filter[0].contains(x, y)) {
            leaveSearch();
            const auto& countries = m_menu->trackCountries();
            int at = -1;
            for (int i = 0; i < static_cast<int>(countries.size()); ++i)
                if (countries[static_cast<std::size_t>(i)] == m_menu->trackCountryFilter()) at = i;
            const int next = at + 1;
            if (next >= static_cast<int>(countries.size())) m_menu->setTrackCountryFilter(std::string());
            else m_menu->setTrackCountryFilter(countries[static_cast<std::size_t>(next)]);
            return true;
        }
        if (L.filter[1].contains(x, y)) {
            leaveSearch();
            const auto& types = m_menu->trackCircuitTypes();
            int at = -1;
            for (int i = 0; i < static_cast<int>(types.size()); ++i)
                if (types[static_cast<std::size_t>(i)] == m_menu->trackCircuitTypeFilter()) at = i;
            const int next = at + 1;
            if (next >= static_cast<int>(types.size()))
                m_menu->setTrackCircuitTypeFilter(std::string());
            else
                m_menu->setTrackCircuitTypeFilter(types[static_cast<std::size_t>(next)]);
            return true;
        }
        if (L.filter[2].contains(x, y)) {
            m_menu->beginTrackSearch();
            return true;
        }
        for (int i = 0; i < static_cast<int>(L.categoryRows.size()); ++i) {
            if (!L.categoryRows[static_cast<std::size_t>(i)].contains(x, y)) continue;
            leaveSearch();
            if (i == 0) m_menu->setTrackCircuitTypeFilter(std::string());
            else m_menu->setTrackCircuitTypeFilter(
                m_menu->trackCircuitTypes()[static_cast<std::size_t>(i - 1)]);
            return true;
        }
        for (int i = 0; i < static_cast<int>(L.cardTiles.size()); ++i) {
            if (!L.cardTiles[static_cast<std::size_t>(i)].contains(x, y)) continue;
            leaveSearch();
            m_menu->setSelectedIndex(m_menu->trackRowForEntry(i));
            m_menu->setTrackLayoutChoice(std::string());
            return true;
        }
        for (int i = 0; i < static_cast<int>(L.layoutTabs.size()); ++i) {
            if (!L.layoutTabs[static_cast<std::size_t>(i)].contains(x, y)) continue;
            const ContentEntry* selected = m_menu->selectedTrackEntry();
            // With one layout the pill is information, not a fake control.
            if (!selected || selected->layouts.size() <= 1 ||
                i >= static_cast<int>(selected->layouts.size()))
                return false;
            leaveSearch();
            const TrackLayoutInfo& layout = selected->layouts[static_cast<std::size_t>(i)];
            m_menu->setTrackLayoutChoice(layout.id.empty() ? layout.name : layout.id);
            return true;
        }
        return false;
    }

    bool carPageClick(float x, float y) {
        if (!isStepPage(m_menu->state()) || stepIndex(m_menu->state()) != 0) return false;
        const CarPageLayout L = carPageLayout(m_viewW, m_viewH);
        const ContentEntry* entry = m_menu->selectedCarEntry();
        const auto leaveSearch = [&]() {
            if (m_menu->carSearchEditing()) m_menu->endCarSearch();
        };

        if (L.reset.contains(x, y)) {
            m_menu->clearCarFilters();
            return true;
        }
        if (L.filter[0].contains(x, y)) {
            leaveSearch();
            const auto& years = m_menu->carYears();
            if (!years.empty()) {
                int at = -1;
                for (int i = 0; i < static_cast<int>(years.size()); ++i)
                    if (years[static_cast<std::size_t>(i)] == m_menu->carYearFilter()) at = i;
                const int next = (at + 1) % static_cast<int>(years.size());
                m_menu->setCarYearFilter(years[static_cast<std::size_t>(next)]);
            }
            return true;
        }
        if (L.filter[1].contains(x, y)) {
            leaveSearch();
            const auto& classes = m_menu->carClasses();
            int at = -1;
            for (int i = 0; i < static_cast<int>(classes.size()); ++i)
                if (classes[static_cast<std::size_t>(i)] == m_menu->carClassFilter()) at = i;
            const int next = at + 1;
            if (next >= static_cast<int>(classes.size())) m_menu->setCarClassFilter(std::string());
            else m_menu->setCarClassFilter(classes[static_cast<std::size_t>(next)]);
            return true;
        }
        if (L.filter[2].contains(x, y)) {
            m_menu->beginCarSearch();
            return true;
        }
        for (int i = 0; i < static_cast<int>(L.seriesRows.size()); ++i) {
            if (!L.seriesRows[static_cast<std::size_t>(i)].contains(x, y)) continue;
            leaveSearch();
            if (i == 0) m_menu->setCarClassFilter(std::string());
            else m_menu->setCarClassFilter(m_menu->carClasses()[static_cast<std::size_t>(i - 1)]);
            return true;
        }
        for (int i = 0; i < static_cast<int>(L.brandTiles.size()); ++i) {
            if (!L.brandTiles[static_cast<std::size_t>(i)].contains(x, y)) continue;
            leaveSearch();
            if (i == 0) m_menu->setCarBrandFilter(std::string());
            else m_menu->setCarBrandFilter(m_menu->carBrands()[static_cast<std::size_t>(i - 1)]);
            return true;
        }
        for (int i = 0; i < static_cast<int>(L.modelTabs.size()); ++i) {
            if (!L.modelTabs[static_cast<std::size_t>(i)].contains(x, y)) continue;
            leaveSearch();
            m_menu->setSelectedIndex(m_menu->carRowForEntry(i));
            m_menu->activateSelection();
            m_carPaintPick = 0;
            return true;
        }

        const auto& paints = entry ? entry->paints : std::vector<CarPaint>{};
        if (!paints.empty()) {
            leaveSearch();
            int nextPick = m_carPaintPick;
            if (L.paintPrev.contains(x, y)) nextPick = (m_carPaintPick + static_cast<int>(paints.size()) - 1) % static_cast<int>(paints.size());
            else if (L.paintNext.contains(x, y)) nextPick = (m_carPaintPick + 1) % static_cast<int>(paints.size());
            else {
                bool hit = false;
                for (int i = 0; i < static_cast<int>(L.paintSwatches.size()); ++i) {
                    if (!L.paintSwatches[static_cast<std::size_t>(i)].contains(x, y)) continue;
                    nextPick = i;
                    hit = true;
                    break;
                }
                if (!hit) return false;
            }
            m_carPaintPick = nextPick;
            if (m_menu->loadedCarDir() == entry->path && m_menu->onPaintChosen) {
                const CarPaint& p = paints[static_cast<std::size_t>(nextPick)];
                m_menu->onPaintChosen(p.r, p.g, p.b);
            }
            return true;
        }
        return false;
    }

    int stepRowAt(const StepPage& p, float mx, float my) const {
        if (!p.list.contains(mx, my)) return -1;
        const std::vector<int> rows = stepRows();
        const int first = stepFirst(rows, m_menu->selectedIndex(), p.visible);
        const int offset = static_cast<int>((my - p.list.y - 6.f) / p.rowH);
        if (offset < 0) return -1;
        const int slot = first + offset;
        if (slot < 0 || slot >= static_cast<int>(rows.size())) return -1;
        return rows[slot];
    }

    /** Cells of a track map, one rectangle per run of equal coverage (a
     *  384-wide map costs a few hundred commands, not one per cell). The
     *  loader keeps coverage, not a yes/no mask, so the edge fades instead of
     *  stepping. */
    static void drawTrackMap(DrawList& dl, const TrackMapPreview& map, const Rect& area,
                             float a) {
        if (area.h <= 4.f || map.empty()) return;
        const float cell = std::min(area.w / static_cast<float>(map.cols),
                                    area.h / static_cast<float>(map.rows));
        if (cell <= 0.5f) return;
        const float originX = area.x + (area.w - cell * static_cast<float>(map.cols)) * 0.5f;
        const float originY = area.y + (area.h - cell * static_cast<float>(map.rows)) * 0.5f;
        // A fraction of a cell of overlap hides the seams between adjacent
        // rows without making a fine map bolder than it is. It follows the
        // cell, with a floor for sub-pixel cells and a cap for a large card.
        const float overlap = std::clamp(cell * 0.35f, 0.12f, 0.5f);
        for (int r = 0; r < map.rows; ++r) {
            int runStart = -1;
            int runAlpha = 0;
            for (int c = 0; c <= map.cols; ++c) {
                const int alpha = c < map.cols ? map.at(c, r) : 0;
                if (runStart >= 0 && alpha != runAlpha) {
                    dl.addRectFilled(
                        {originX + static_cast<float>(runStart) * cell,
                         originY + static_cast<float>(r) * cell,
                         static_cast<float>(c - runStart) * cell + overlap, cell + overlap},
                        Color::rgba(0.72f, 0.73f, 0.77f,
                                    a * std::pow(static_cast<float>(runAlpha) / 255.f, 0.35f)));
                    runStart = -1;
                }
                if (alpha > 0 && runStart < 0) {
                    runStart = c;
                    runAlpha = alpha;
                }
            }
        }
    }

    static void addLogoRun(DrawList& dl, float originX, float originY, float cell, int from,
                           int to, int row, std::uint32_t colour, float a) {
        if (to <= from) return;
        const float coverage = static_cast<float>((colour >> 24) & 0xFFu) / 255.f;
        dl.addRectFilled({originX + static_cast<float>(from) * cell,
                          originY + static_cast<float>(row) * cell,
                          static_cast<float>(to - from) * cell + 0.6f, cell + 0.6f},
                         Color::rgb(static_cast<std::uint8_t>((colour >> 16) & 0xFFu),
                                    static_cast<std::uint8_t>((colour >> 8) & 0xFFu),
                                    static_cast<std::uint8_t>(colour & 0xFFu),
                                    static_cast<std::uint8_t>(
                                        std::lround(255.f * coverage * a))));
    }

    /** A logo reduced to colour cells, merged the same way as the map. */
    static void drawColourCells(DrawList& dl, const TrackLogoPreview& logo, const Rect& area,
                                float a) {
        if (logo.empty() || area.w <= 4.f || area.h <= 4.f) return;
        const float cell = std::min(area.w / static_cast<float>(logo.cols),
                                    area.h / static_cast<float>(logo.rows));
        if (cell <= 0.5f) return;
        const float originX = area.x + (area.w - cell * static_cast<float>(logo.cols)) * 0.5f;
        const float originY = area.y + (area.h - cell * static_cast<float>(logo.rows)) * 0.5f;
        for (int r = 0; r < logo.rows; ++r) {
            int runStart = -1;
            std::uint32_t runColour = 0u;
            for (int c = 0; c <= logo.cols; ++c) {
                const std::uint32_t value = c < logo.cols ? logo.at(c, r) : 0u;
                const bool ink = (value >> 24) >= 32u;
                if (runStart >= 0 && (!ink || value != runColour)) {
                    addLogoRun(dl, originX, originY, cell, runStart, c, r, runColour, a);
                    runStart = -1;
                }
                if (ink && runStart < 0) {
                    runStart = c;
                    runColour = value;
                }
            }
        }
    }

    /** "MU" for Mugello, "MC" for Mugello Circuit: the plate a track without a
     *  logo file gets, like the initials a list shows for a missing picture. */
    static std::string trackInitials(const std::string& name) {
        std::string initials;
        bool newWord = true;
        for (char c : name) {
            if (c == ' ' || c == '-' || c == '_') {
                newWord = true;
                continue;
            }
            if (!newWord || initials.size() >= 2) continue;
            initials.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            newWord = false;
        }
        while (initials.size() < 2 && initials.size() < name.size()) {
            initials.push_back(
                static_cast<char>(std::toupper(static_cast<unsigned char>(name[initials.size()]))));
        }
        return initials;
    }

    /** The bundled artwork for a country (content/gui/flags/countries), reduced
     *  to cells on first use. Null when the country or its file is unknown. */
    const TrackLogoPreview* flagImage(const std::string& country) const {
        const std::string code = countryCode(country);
        if (code.empty()) return nullptr;
        const auto found = m_flagImages.find(code);
        if (found != m_flagImages.end())
            return found->second.empty() ? nullptr : &found->second;
        TrackLogoPreview cells;
        loadFlagPreview(contentAssetPath("gui/flags/countries/" + code + ".png"), cells);
        const auto inserted = m_flagImages.emplace(code, std::move(cells));
        return inserted.first->second.empty() ? nullptr : &inserted.first->second;
    }

    /** A country's flag: the crisp banded design where the table has one, the
     *  bundled artwork otherwise, nothing when neither knows the country. */
    void drawCountryFlag(DrawList& dl, const Rect& r, const std::string& country, float a) {
        CountryFlagSpec spec;
        if (!countryFlagSpec(country, spec)) {
            if (const TrackLogoPreview* image = flagImage(country))
                drawColourCells(dl, *image, r, a);
            return;
        }
        float total = 0.f;
        for (int i = 0; i < spec.bands; ++i) total += spec.weight[i];
        if (total <= 0.f) return;
        float pos = spec.vertical ? r.x : r.y;
        for (int i = 0; i < spec.bands; ++i) {
            const float length = (spec.vertical ? r.w : r.h) * spec.weight[i] / total;
            if (length <= 0.f) continue;
            const std::uint32_t rgb = spec.rgb[i];
            const Color colour =
                Color::rgb(static_cast<std::uint8_t>((rgb >> 16) & 0xFFu),
                           static_cast<std::uint8_t>((rgb >> 8) & 0xFFu),
                           static_cast<std::uint8_t>(rgb & 0xFFu),
                           static_cast<std::uint8_t>(std::lround(255.f * a)));
            if (spec.vertical)
                dl.addRectFilled({pos, r.y, length + 0.6f, r.h}, colour);
            else
                dl.addRectFilled({r.x, pos, r.w, length + 0.6f}, colour);
            pos += length;
        }
        dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.22f * a), 1.f);
    }

    /** TRACKS card: logo, name and country flag above the circuit, and the
     *  circuit's own facts (coordinates, length, kind) below it. Everything
     *  comes from the folder; what it does not declare is left out instead of
     *  being filled in with invented values. */
    void buildTrackCard(DrawList& dl, const StepPage& p, float a) {
        const ContentEntry* track = m_menu->selectedTrackEntry();
        std::string name = track ? track->label : m_menu->trackName();
        if (name.empty()) name = "NO TRACK SELECTED";
        const float pad = 24.f;
        const float x = p.card.x + pad;
        const float right = p.card.x + p.card.w - pad;
        const float logoSize = 58.f;
        const float logoY = p.card.y + 58.f;

        if (const TrackLogoPreview* logo = m_menu->selectedTrackLogo()) {
            drawColourCells(dl, *logo, {x, logoY, logoSize, logoSize}, a);
        } else {
            dl.addRectFilled({x, logoY, logoSize, logoSize}, Theme::surface(0.92f * a));
            dl.addRect({x, logoY, logoSize, logoSize}, Theme::hairline(a), 1.f);
            m_text.draw(dl, x + 14.f, logoY + 19.f, trackInitials(name),
                        sty(Theme::ink(a), 1.7f, true));
        }

        const bool hasFlag = track != nullptr && !track->country.empty();
        const float flagW = 44.f;
        const float nameX = x + logoSize + 16.f;
        const float nameW = right - (hasFlag ? flagW + 14.f : 0.f) - nameX;
        const float headScale = 2.0f;
        m_text.draw(dl, nameX, logoY + 2.f, elideToWidth(name, headScale, nameW),
                    sty(Theme::ink(a), headScale, true));

        // City and country under the name; the flag repeats the country.
        std::string place;
        if (track) {
            if (!track->city.empty()) place = track->city;
            if (!track->country.empty())
                place = place.empty() ? track->country : place + ", " + track->country;
        }
        if (!place.empty())
            m_text.draw(dl, nameX, logoY + 40.f, elideToWidth(place, 1.05f, nameW),
                        sty(Theme::inkDim(a), 1.05f));
        if (hasFlag)
            drawCountryFlag(dl, {right - flagW, logoY + 12.f, flagW, 32.f}, track->country, a);

        // The circuit itself, then its facts underneath.
        const float mapTop = logoY + logoSize + 22.f;
        const float factsTop = p.card.y + p.card.h - 92.f;
        if (const TrackMapPreview* map = m_menu->selectedTrackMap()) {
            drawTrackMap(dl, *map, {x, mapTop, right - x, std::max(0.f, factsTop - mapTop - 12.f)},
                         a);
        }

        float lineY = factsTop;
        const std::string coords = track ? trackCoordinateText(*track) : std::string();
        if (!coords.empty()) {
            m_text.draw(dl, x, lineY, coords, sty(Theme::ink(0.9f * a), 1.05f));
            lineY += 22.f;
        }
        std::string facts;
        if (track && track->lengthM > 0.0) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.3f km", track->lengthM / 1000.0);
            facts = text;
        }
        if (track && !track->circuitType.empty()) {
            if (!facts.empty()) facts += "   ";
            facts += track->circuitType == "permanent" ? "PERMANENT CIRCUIT"
                                                       : "TEMPORARY CIRCUIT";
        }
        if (!facts.empty())
            m_text.draw(dl, x, lineY, facts, sty(Theme::ink(0.88f * a), 1.05f));
        else if (coords.empty())
            m_text.draw(dl, x, lineY, "THE FOLDER DECLARES NO TRACK DETAILS",
                        sty(Theme::inkDim(0.9f * a), 0.95f));
    }

    static void fillCircle(DrawList& dl, float cx, float cy, float radius, const Color& colour) {
        std::vector<float> points;
        const int segments = 20;
        points.reserve(segments * 2);
        for (int i = 0; i < segments; ++i) {
            const float angle = 6.2831853f * static_cast<float>(i) / segments;
            points.push_back(cx + radius * std::cos(angle));
            points.push_back(cy + radius * std::sin(angle));
        }
        dl.addPolygonFill(points, colour);
    }

    /** Weather drawn from the same primitives as everything else: a disc with
     *  rays for the sun, lumps for a cloud, slanted strokes for rain, asterisks
     *  for snow. No artwork to ship, and it tints with the card. */
    static void drawWeatherIcon(DrawList& dl, const Rect& box, WeatherKind kind, float a) {
        if (kind == WeatherKind::Unknown || box.w <= 8.f || box.h <= 8.f) return;
        const float cx = box.x + box.w * 0.5f;
        const float cy = box.y + box.h * 0.5f;
        const float s = std::min(box.w, box.h);
        const Color sun = Color::rgba(0.98f, 0.79f, 0.28f, a);
        const Color light = Color::rgba(0.88f, 0.90f, 0.94f, a);
        const Color shadow = Color::rgba(0.60f, 0.64f, 0.70f, a);
        const Color wet = Color::rgba(0.45f, 0.68f, 0.95f, a);

        const auto drawSun = [&](float radius, float ox, float oy) {
            fillCircle(dl, cx + ox, cy + oy, radius, sun);
            for (int i = 0; i < 8; ++i) {
                const float angle = 6.2831853f * static_cast<float>(i) / 8.f;
                dl.addLine(cx + ox + std::cos(angle) * radius * 1.35f,
                           cy + oy + std::sin(angle) * radius * 1.35f,
                           cx + ox + std::cos(angle) * radius * 1.85f,
                           cy + oy + std::sin(angle) * radius * 1.85f, sun,
                           std::max(1.5f, s * 0.035f));
            }
        };
        const auto drawCloud = [&](float width, float height, float ox, float oy,
                                    const Color& colour) {
            fillCircle(dl, cx + ox - width * 0.24f, cy + oy + height * 0.08f, height * 0.30f,
                       colour);
            fillCircle(dl, cx + ox, cy + oy - height * 0.08f, height * 0.40f, colour);
            fillCircle(dl, cx + ox + width * 0.24f, cy + oy + height * 0.08f, height * 0.28f,
                       colour);
            dl.addRectFilled({cx + ox - width * 0.30f, cy + oy + height * 0.02f,
                              width * 0.60f, height * 0.32f},
                             colour);
        };
        const auto drawDrops = [&](int count, float length, float thickness) {
            for (int i = 0; i < count; ++i) {
                const float t = (count == 1) ? 0.5f : static_cast<float>(i) / (count - 1);
                const float x = cx - s * 0.26f + s * 0.52f * t;
                dl.addLine(x, cy + s * 0.16f, x - length * 0.35f, cy + s * 0.16f + length, wet,
                           thickness);
            }
        };
        const auto drawFlakes = [&](int count) {
            for (int i = 0; i < count; ++i) {
                const float t = (count == 1) ? 0.5f : static_cast<float>(i) / (count - 1);
                const float x = cx - s * 0.26f + s * 0.52f * t;
                const float y = cy + s * 0.26f;
                const float r = s * 0.06f;
                dl.addLine(x - r, y, x + r, y, light, 1.5f);
                dl.addLine(x, y - r, x, y + r, light, 1.5f);
                dl.addLine(x - r * 0.7f, y - r * 0.7f, x + r * 0.7f, y + r * 0.7f, light, 1.2f);
                dl.addLine(x - r * 0.7f, y + r * 0.7f, x + r * 0.7f, y - r * 0.7f, light, 1.2f);
            }
        };

        switch (kind) {
        case WeatherKind::Clear:
            drawSun(s * 0.22f, 0.f, 0.f);
            break;
        case WeatherKind::PartlyCloudy:
            drawSun(s * 0.16f, -s * 0.16f, -s * 0.16f);
            drawCloud(s * 0.80f, s * 0.42f, s * 0.06f, s * 0.12f, light);
            break;
        case WeatherKind::Overcast:
            drawCloud(s * 0.78f, s * 0.38f, -s * 0.08f, -s * 0.14f, shadow);
            drawCloud(s * 0.86f, s * 0.44f, s * 0.04f, s * 0.08f, light);
            break;
        case WeatherKind::Fog:
            drawCloud(s * 0.80f, s * 0.38f, 0.f, -s * 0.14f, light);
            for (int i = 0; i < 3; ++i) {
                const float y = cy + s * (0.16f + 0.10f * static_cast<float>(i));
                const float width = s * (0.56f - 0.10f * static_cast<float>(i));
                dl.addLine(cx - width * 0.5f, y, cx + width * 0.5f, y, light,
                           std::max(1.5f, s * 0.03f));
            }
            break;
        case WeatherKind::Drizzle:
            drawCloud(s * 0.86f, s * 0.44f, 0.f, -s * 0.08f, light);
            drawDrops(3, s * 0.10f, 1.5f);
            break;
        case WeatherKind::Rain:
            drawCloud(s * 0.86f, s * 0.44f, 0.f, -s * 0.10f, light);
            drawDrops(4, s * 0.18f, 2.f);
            break;
        case WeatherKind::HeavyRain:
            drawCloud(s * 0.90f, s * 0.46f, 0.f, -s * 0.12f, shadow);
            drawDrops(6, s * 0.24f, 2.5f);
            break;
        case WeatherKind::Snow:
            drawCloud(s * 0.86f, s * 0.44f, 0.f, -s * 0.10f, light);
            drawFlakes(3);
            break;
        case WeatherKind::Thunderstorm:
            drawCloud(s * 0.88f, s * 0.44f, 0.f, -s * 0.14f, shadow);
            {
                const Color bolt = Color::rgba(1.f, 0.86f, 0.30f, a);
                std::vector<float> points = {
                    cx - s * 0.05f, cy + s * 0.04f, cx + s * 0.10f, cy + s * 0.04f,
                    cx + s * 0.02f, cy + s * 0.18f, cx + s * 0.12f, cy + s * 0.18f,
                    cx - s * 0.06f, cy + s * 0.38f, cx + s * 0.01f, cy + s * 0.24f,
                    cx - s * 0.08f, cy + s * 0.24f,
                };
                dl.addPolygonFill(points, bolt);
            }
            break;
        case WeatherKind::Unknown:
            break;
        }
    }

    /** WEATHER card: the track it belongs to, the sky as an icon and a name,
     *  the numbers the session will start with, and where they came from. */
    void buildWeatherCard(DrawList& dl, const StepPage& p, float a) {
        const WeatherOutlook& w = m_menu->weather();
        const ContentEntry* track = m_menu->chosenTrackEntry();
        const float pad = 24.f;
        const float x = p.card.x + pad;
        const float width = p.card.w - pad * 2.f;

        const std::string trackName = track ? track->label : m_menu->trackName();
        m_text.draw(dl, x, p.card.y + 58.f,
                    elideToWidth(trackName.empty() ? "NO TRACK SELECTED" : trackName, 1.5f,
                                 width),
                    sty(Theme::ink(a), 1.5f, true));
        std::string place;
        if (track) {
            if (!track->city.empty()) place = track->city;
            if (!track->country.empty())
                place = place.empty() ? track->country : place + ", " + track->country;
        }
        if (!place.empty())
            m_text.draw(dl, x, p.card.y + 84.f, elideToWidth(place, 1.f, width),
                        sty(Theme::inkDim(a), 1.f));

        const Rect iconBox{x + width * 0.5f - 52.f, p.card.y + 112.f, 104.f, 104.f};
        drawWeatherIcon(dl, iconBox, w.kind, a);

        m_text.draw(dl, x, p.card.y + 228.f, weatherKindName(w.kind),
                    sty(Theme::ink(a), 1.7f, true));

        char line[192];
        std::snprintf(line, sizeof(line), "AIR %.0f C    TRACK %.0f C    WIND %.1f m/s",
                      w.ambientC, w.trackC, w.windMs);
        m_text.draw(dl, x, p.card.y + 270.f, line, sty(Theme::ink(0.88f * a), 1.05f));
        std::snprintf(line, sizeof(line), "RAIN %.2f    WET %.2f    CLOUD %.2f    FOG %.2f",
                      w.rain, w.wetness, w.cloud, w.fog);
        m_text.draw(dl, x, p.card.y + 292.f, line, sty(Theme::ink(0.88f * a), 1.05f));

        // Where the numbers came from: the mode, the day and the hour.
        std::snprintf(line, sizeof(line), "%s%s%s   %02d:%02d", weatherSourceName(w.source),
                      w.date.empty() ? "" : "  ", w.date.c_str(),
                      static_cast<int>(w.hourOfDay),
                      static_cast<int>((w.hourOfDay - static_cast<int>(w.hourOfDay)) * 60.f));
        m_text.draw(dl, x, p.card.y + 322.f, line, sty(Theme::accent(a), 1.05f, true));
        if (!w.note.empty())
            dl.addTextWrapped(x, p.card.y + 346.f, w.note, Theme::inkDim(a), 0.95f, width);
    }

    void ensureCarSpec() {
        const ContentEntry* entry = m_menu->selectedCarEntry();
        const std::string path = entry ? entry->path : std::string();
        if (path == m_carSpecPath) return;
        m_carSpecPath = path;
        m_carSpec = CarSpecSheet{};
        m_carPaintPick = 0;
        if (!path.empty()) loadCarSpecSheet(path, m_carSpec);
    }

    void drawCarPreview(DrawList& dl, const Rect& area, const ContentEntry& entry,
                        const CarPaint* paint, float a) {
        const Color primary = paint ? Color::rgba(paint->r, paint->g, paint->b, a)
                                    : Theme::accent(a);
        const Color dark = Color::rgba(0.025f, 0.020f, 0.035f, 0.94f * a);
        const Color line = Color::rgba(0.82f, 0.84f, 0.88f, 0.55f * a);
        const float cx = area.x + area.w * 0.54f;
        const float cy = area.y + area.h * 0.52f;
        const float s = std::min(area.w * 0.92f, area.h * 1.55f);
        const bool formula = entry.carClass.find("Formula") != std::string::npos ||
                             entry.carClass.find("formula") != std::string::npos ||
                             entry.carClass.find("Open Wheel") != std::string::npos;

        // Studio floor: the same perspective rule the page uses elsewhere,
        // drawn faintly so the vector car sits on something.
        for (int i = 0; i < 4; ++i) {
            const float y = cy + s * 0.29f + static_cast<float>(i) * s * 0.035f;
            dl.addLine(cx - s * 0.52f, y, cx + s * 0.52f, y,
                       Color::rgba(0.90f, 0.68f, 0.20f, (0.12f - 0.02f * i) * a), 1.f);
        }
        dl.addRectFilled({cx - s * 0.42f, cy + s * 0.27f, s * 0.84f, 5.f},
                         Color::rgba(0.f, 0.f, 0.f, 0.55f * a));

        if (formula) {
            // Open-wheel profile: exposed wheels, low tub, front and rear wing.
            fillCircle(dl, cx - s * 0.34f, cy + s * 0.17f, s * 0.105f, dark);
            fillCircle(dl, cx + s * 0.34f, cy + s * 0.17f, s * 0.105f, dark);
            fillCircle(dl, cx - s * 0.34f, cy + s * 0.17f, s * 0.055f, line);
            fillCircle(dl, cx + s * 0.34f, cy + s * 0.17f, s * 0.055f, line);
            std::vector<float> body = {
                cx - s * 0.42f, cy + s * 0.06f, cx - s * 0.18f, cy + s * 0.01f,
                cx - s * 0.08f, cy - s * 0.10f, cx + s * 0.10f, cy - s * 0.10f,
                cx + s * 0.24f, cy + s * 0.02f, cx + s * 0.39f, cy + s * 0.07f,
                cx + s * 0.30f, cy + s * 0.14f, cx - s * 0.30f, cy + s * 0.14f,
            };
            dl.addPolygonFill(body, primary);
            dl.addPolygonFill({cx - s * 0.12f, cy - s * 0.20f, cx + s * 0.08f, cy - s * 0.20f,
                               cx + s * 0.18f, cy - s * 0.09f, cx - s * 0.02f, cy - s * 0.09f},
                              dark);
            dl.addLine(cx - s * 0.48f, cy + s * 0.20f, cx - s * 0.20f, cy + s * 0.20f, line, 2.f);
            dl.addLine(cx + s * 0.20f, cy + s * 0.20f, cx + s * 0.48f, cy + s * 0.20f, line, 2.f);
            dl.addLine(cx - s * 0.47f, cy - s * 0.04f, cx - s * 0.28f, cy + s * 0.03f, line, 2.f);
            dl.addLine(cx + s * 0.26f, cy + s * 0.02f, cx + s * 0.48f, cy - s * 0.08f, line, 2.f);
            dl.addRectFilled({cx - s * 0.49f, cy - s * 0.09f, s * 0.20f, 4.f}, line);
            dl.addRectFilled({cx + s * 0.28f, cy - s * 0.13f, s * 0.23f, 5.f}, line);
            dl.addRectFilled({cx - s * 0.14f, cy - s * 0.22f, s * 0.28f, 4.f}, primary);
        } else {
            // Closed-wheel profile: a long body, cabin and two arches.
            fillCircle(dl, cx - s * 0.32f, cy + s * 0.17f, s * 0.115f, dark);
            fillCircle(dl, cx + s * 0.32f, cy + s * 0.17f, s * 0.115f, dark);
            fillCircle(dl, cx - s * 0.32f, cy + s * 0.17f, s * 0.060f, line);
            fillCircle(dl, cx + s * 0.32f, cy + s * 0.17f, s * 0.060f, line);
            std::vector<float> body = {
                cx - s * 0.48f, cy + s * 0.10f, cx - s * 0.40f, cy - s * 0.04f,
                cx - s * 0.22f, cy - s * 0.09f, cx - s * 0.12f, cy - s * 0.20f,
                cx + s * 0.12f, cy - s * 0.20f, cx + s * 0.28f, cy - s * 0.08f,
                cx + s * 0.47f, cy + s * 0.01f, cx + s * 0.45f, cy + s * 0.13f,
                cx - s * 0.47f, cy + s * 0.13f,
            };
            dl.addPolygonFill(body, primary);
            dl.addPolygonFill({cx - s * 0.13f, cy - s * 0.19f, cx + s * 0.12f, cy - s * 0.19f,
                               cx + s * 0.22f, cy - s * 0.08f, cx - s * 0.18f, cy - s * 0.08f},
                              Color::rgba(0.06f, 0.07f, 0.10f, 0.92f * a));
            dl.addLine(cx - s * 0.42f, cy - s * 0.02f, cx + s * 0.38f, cy - s * 0.02f,
                       Color::rgba(1.f, 1.f, 1.f, 0.22f * a), 2.f);
            dl.addRectFilled({cx + s * 0.28f, cy - s * 0.15f, s * 0.22f, 5.f}, line);
        }
        // A small wing/light signature gives the silhouette its own colour.
        dl.addRectFilled({cx - s * 0.46f, cy + s * 0.08f, s * 0.10f, 5.f}, primary);
        dl.addRectFilled({cx + s * 0.36f, cy + s * 0.08f, s * 0.10f, 5.f}, primary);
    }

    void drawDyno(DrawList& dl, const Rect& area, const CarSpecSheet& sheet, float a) {
        const float pad = 12.f;
        const Rect plot{area.x + pad, area.y + 38.f, area.w - pad * 2.f,
                        std::max(24.f, area.h - 44.f)};
        dl.addRectFilled(plot, Color::rgba(0.f, 0.f, 0.f, 0.24f * a));
        for (int i = 0; i <= 4; ++i) {
            const float y = plot.y + plot.h * static_cast<float>(i) / 4.f;
            dl.addLine(plot.x, y, plot.x + plot.w, y,
                       Color::rgba(1.f, 1.f, 1.f, 0.07f * a), 1.f);
        }
        if (sheet.curve.size() < 2) {
            m_text.draw(dl, plot.x + 8.f, plot.y + plot.h * 0.45f,
                        "NO POWER CURVE DECLARED IN THIS CAR FOLDER",
                        sty(Theme::inkDim(a), 0.86f));
            return;
        }
        const auto peak = [](const CarSpecSheet& s) { return std::max(1.0, s.peakPowerHp); };
        const double maxRpm = std::max(1.0, sheet.curve.back().first);
        const double maxHp = peak(sheet);
        const double maxNm = std::max(1.0, sheet.peakTorqueNm);
        auto xOf = [&](double rpm) {
            return plot.x + static_cast<float>(rpm / maxRpm) * plot.w;
        };
        auto yOf = [&](double value) {
            return plot.y + plot.h - static_cast<float>(value) * plot.h * 0.88f;
        };
        float prevHpX = 0.f, prevHpY = 0.f, prevNmX = 0.f, prevNmY = 0.f;
        for (std::size_t i = 0; i < sheet.curve.size(); ++i) {
            const double rpm = sheet.curve[i].first;
            const double nM = sheet.curve[i].second;
            const double hp = nM * rpm / 5252.0;
            const float x = xOf(rpm), yHp = yOf(hp / maxHp), yNm = yOf(nM / maxNm);
            if (i > 0) {
                dl.addLine(prevHpX, prevHpY, x, yHp, Theme::accent(a), 2.f);
                dl.addLine(prevNmX, prevNmY, x, yNm, Color::rgba(0.86f, 0.20f, 0.28f, a), 2.f);
            }
            prevHpX = x; prevHpY = yHp; prevNmX = x; prevNmY = yNm;
        }
        char line[96];
        std::snprintf(line, sizeof(line), "%.0f BHP @ %d", sheet.peakPowerHp, sheet.peakPowerRpm);
        m_text.draw(dl, plot.x, area.y + 22.f, line, sty(Theme::accent(a), 0.78f, true));
        std::snprintf(line, sizeof(line), "%.0f Nm @ %d", sheet.peakTorqueNm, sheet.peakTorqueRpm);
        m_text.draw(dl, plot.x + plot.w * 0.65f, area.y + 22.f, line,
                    sty(Color::rgba(0.90f, 0.32f, 0.38f, a), 0.82f, true));
    }

    void buildCarPage(DrawList& dl, int w, int h, float a) {
        (void)w; (void)h;
        ensureCarSpec();
        const CarPageLayout L = carPageLayout(w, h);
        const auto& classes = m_menu->carClasses();
        const auto& brands = m_menu->carBrands();
        const auto& entries = m_menu->carEntries();
        const ContentEntry* entry = m_menu->selectedCarEntry();
        const auto& paints = entry ? entry->paints : std::vector<CarPaint>{};
        const int paintIndex = paints.empty()
            ? 0 : std::clamp(m_carPaintPick, 0, static_cast<int>(paints.size()) - 1);
        const CarPaint* paint = paints.empty() ? nullptr : &paints[static_cast<std::size_t>(paintIndex)];

        const auto number1 = [](const char* pattern, double value) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), pattern, value);
            return std::string(buffer);
        };
        const auto card = [&](const Rect& r, const char* title) {
            dl.addRectFilled(r, Color::rgba(0.045f, 0.022f, 0.065f, 0.94f * a));
            dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.12f * a), 1.f);
            if (title) m_text.draw(dl, r.x + 14.f, r.y + 10.f, title,
                                   sty(Theme::accent(a), 0.78f, true));
        };
        const auto filterCard = [&](const Rect& r, const char* label, const std::string& value,
                                    bool active) {
            dl.addRectFilled(r, Color::rgba(0.035f, 0.018f, 0.052f, 0.96f * a));
            dl.addRect(r, active ? Theme::accent(a) : Color::rgba(1.f, 1.f, 1.f, 0.14f * a), 1.f);
            m_text.draw(dl, r.x + 12.f, r.y + 8.f, label, sty(Theme::accent(a), 0.68f, true));
            m_text.draw(dl, r.x + 12.f, r.y + 31.f, elideToWidth(value, 1.05f, r.w - 24.f),
                        sty(Theme::ink(a), 1.05f, true));
        };

        filterCard(L.filter[0], "ANNO VETTURA",
                   m_menu->carYearFilter() > 0 ? std::to_string(m_menu->carYearFilter())
                                               : "TUTTI GLI ANNI",
                   m_carFilterHover == 0);
        filterCard(L.filter[1], "TIPOLOGIA",
                   m_menu->carClassFilter().empty() ? "TUTTE LE CLASSI"
                                                    : m_menu->carClassFilter(),
                   m_carFilterHover == 1);
        const std::string query = m_menu->carSearchQuery();
        filterCard(L.filter[2], "CERCA MODELLO O MARCHIO",
                   m_menu->carSearchEditing() ? (query + "_") : (query.empty() ? "MODELLO O MARCHIO" : query),
                   m_carFilterHover == 2);
        dl.addRectFilled(L.reset, Color::rgba(m_carFilterHover == 3 ? 0.30f : 0.20f, 0.025f, 0.04f,
                                             0.70f * a));
        dl.addRect(L.reset, Color::rgba(0.95f, 0.30f, 0.36f, 0.55f * a), 1.f);
        m_text.draw(dl, L.reset.x + 16.f, L.reset.y + 8.f, "RESET",
                    sty(Color::rgba(1.f, 0.74f, 0.74f, a), 0.74f, true));

        // Series/classes rail.
        card(L.series, nullptr);
        m_text.draw(dl, L.series.x + 14.f, L.series.y + 10.f, "SERIE / CLASSI",
                    sty(Theme::accent(a), 0.78f, true));
        m_text.draw(dl, L.series.x + L.series.w - 76.f, L.series.y + 10.f,
                    std::to_string(classes.size()) + " CLASSI", sty(Theme::inkDim(a), 0.68f));
        dl.addLine(L.series.x + 10.f, L.series.y + 33.f, L.series.x + L.series.w - 10.f,
                   L.series.y + 33.f, Theme::hairline(a), 1.f);
        for (int i = 0; i < static_cast<int>(L.seriesRows.size()); ++i) {
            const Rect& rr = L.seriesRows[static_cast<std::size_t>(i)];
            const bool all = i == 0;
            const std::string name = all ? "TUTTE LE SERIE"
                : classes[static_cast<std::size_t>(i - 1)];
            const bool active = all ? m_menu->carClassFilter().empty()
                                    : m_menu->carClassFilter() == name;
            if (active)
                dl.addRectFilled(rr, Color::rgba(0.96f, 0.66f, 0.18f, 0.18f * a));
            else if (m_carSeriesHover == i)
                dl.addRectFilled(rr, Color::rgba(1.f, 1.f, 1.f, 0.05f * a));
            m_text.draw(dl, rr.x + 10.f, rr.y + 4.f, elideToWidth(name, 0.86f, rr.w - 20.f),
                        sty(active ? Theme::accent(a) : Theme::ink(0.88f * a), 0.86f, active));
        }

        // Manufacturer grid.
        card(L.brands, nullptr);
        m_text.draw(dl, L.brands.x + 14.f, L.brands.y + 10.f, "SELEZIONA MARCHIO",
                    sty(Theme::accent(a), 0.78f, true));
        m_text.draw(dl, L.brands.x + L.brands.w - 170.f, L.brands.y + 10.f,
                    std::to_string(entries.size()) + " VETTURE FILTRATE",
                    sty(Theme::inkDim(a), 0.68f));
        dl.addLine(L.brands.x + 10.f, L.brands.y + 33.f, L.brands.x + L.brands.w - 10.f,
                   L.brands.y + 33.f, Theme::hairline(a), 1.f);
        for (int i = 0; i < static_cast<int>(L.brandTiles.size()); ++i) {
            const Rect& rr = L.brandTiles[static_cast<std::size_t>(i)];
            const bool all = i == 0;
            const std::string name = all ? "TUTTI" : brands[static_cast<std::size_t>(i - 1)];
            const bool active = all ? m_menu->carBrandFilter().empty()
                                    : m_menu->carBrandFilter() == name;
            int count = 0;
            for (const ContentEntry& e : entries)
                if (all || e.brand == name) ++count;
            dl.addRectFilled(rr, active ? Color::rgba(0.46f, 0.24f, 0.04f, 0.82f * a)
                                        : (m_carBrandHover == i
                                               ? Color::rgba(1.f, 1.f, 1.f, 0.08f * a)
                                               : Color::rgba(0.09f, 0.045f, 0.12f, 0.92f * a)));
            dl.addRect(rr, active ? Theme::accent(a) : Theme::hairline(a), 1.f);
            std::string initials = name.substr(0, 1);
            for (char& c : initials) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            m_text.draw(dl, rr.x + 10.f, rr.y + 7.f, initials,
                        sty(active ? Theme::accent(a) : Theme::ink(a), 1.05f, true));
            m_text.draw(dl, rr.x + 34.f, rr.y + 8.f, elideToWidth(name, 0.78f, rr.w - 42.f),
                        sty(Theme::ink(a), 0.78f, true));
            m_text.draw(dl, rr.x + 34.f, rr.y + 25.f, std::to_string(count) + " CAR",
                        sty(Theme::inkDim(a), 0.66f));
        }
        if (brands.empty()) {
            m_text.draw(dl, L.brands.x + 14.f, L.brands.y + L.brands.h - 20.f,
                        "BRAND NOT DECLARED IN CONTENT", sty(Theme::inkDim(a), 0.62f));
        }

        // Showcase: model strip, studio, specs and dyno.
        card(L.showcase, nullptr);
        dl.addRectFilled(L.modelBar, Color::rgba(0.025f, 0.012f, 0.035f, 0.96f * a));
        m_text.draw(dl, L.modelBar.x + 14.f, L.modelBar.y + 16.f, "MODELLO",
                    sty(Theme::inkDim(a), 0.70f, true));
        for (int i = 0; i < static_cast<int>(L.modelTabs.size()); ++i) {
            const Rect& rr = L.modelTabs[static_cast<std::size_t>(i)];
            const ContentEntry& e = entries[static_cast<std::size_t>(i)];
            const bool active = entry && entry->path == e.path;
            dl.addRectFilled(rr, active ? Theme::accent(0.90f * a)
                                        : Color::rgba(1.f, 1.f, 1.f,
                                                      (m_carModelHover == i ? 0.13f : 0.06f) * a));
            dl.addRect(rr, active ? Theme::accent(a) : Theme::hairline(a), 1.f);
            m_text.draw(dl, rr.x + 8.f, rr.y + 7.f,
                        elideToWidth(e.label, 0.76f, rr.w - 16.f),
                        sty(active ? Color::rgba(0.06f, 0.02f, 0.03f, a) : Theme::ink(a),
                            0.76f, active));
        }
        m_text.draw(dl, L.paintPrev.x - 8.f, L.modelBar.y + 15.f, "SKIN",
                    sty(Theme::accent(a), 0.68f, true));
        m_text.draw(dl, L.paintPrev.x + 4.f, L.modelBar.y + 14.f, "<",
                    sty(Theme::ink(a), 0.90f, true));
        m_text.draw(dl, L.paintNext.x + 4.f, L.modelBar.y + 14.f, ">",
                    sty(Theme::ink(a), 0.90f, true));
        for (int i = 0; i < static_cast<int>(L.paintSwatches.size()); ++i) {
            const Rect& rr = L.paintSwatches[static_cast<std::size_t>(i)];
            const CarPaint& p = paints[static_cast<std::size_t>(i)];
            dl.addRectFilled(rr, Color::rgba(p.r, p.g, p.b, a));
            dl.addRect(rr, i == paintIndex || m_carPaintHover == i ? Theme::accent(a) : Theme::hairline(a),
                       1.f);
        }
        if (paint)
            m_text.draw(dl, L.paintPrev.x + 34.f + static_cast<float>(L.paintSwatches.size()) * 24.f + 8.f,
                        L.modelBar.y + 18.f, elideToWidth(paint->name, 0.68f, 90.f),
                        sty(Theme::ink(a), 0.68f));
        else
            m_text.draw(dl, L.paintPrev.x + 34.f, L.modelBar.y + 18.f, "BAKED LIVERY",
                        sty(Theme::inkDim(a), 0.68f));

        dl.addRectFilled(L.preview, Color::rgba(0.018f, 0.010f, 0.028f, 0.72f * a));
        dl.addRect(L.preview, Color::rgba(1.f, 1.f, 1.f, 0.08f * a), 1.f);
        dl.addRectFilled({L.preview.x, L.preview.y, 3.f, std::min(100.f, L.preview.h)},
                         Theme::accent(a));
        if (entry) {
            m_text.draw(dl, L.preview.x + 20.f, L.preview.y + 12.f, "SELECT CAR // SHOWROOM",
                        sty(Theme::accent(a), 0.68f, true));
            m_text.draw(dl, L.preview.x + 20.f, L.preview.y + 42.f,
                        elideToWidth(entry->label, 1.75f, L.preview.w - 40.f),
                        sty(Theme::ink(a), 1.75f, true));
            const std::string sub = (entry->brand.empty() ? "NO BRAND" : entry->brand) +
                (entry->carClass.empty() ? "" : " / " + entry->carClass) +
                (entry->year > 0 ? " / " + std::to_string(entry->year) : "");
            m_text.draw(dl, L.preview.x + 20.f, L.preview.y + 76.f,
                        elideToWidth(sub, 0.82f, L.preview.w - 40.f), sty(Theme::inkDim(a), 0.82f));
            const std::string code = "DATA: " + entry->id;
            const float cw = m_text.measureWidth(code, 0.68f);
            m_text.draw(dl, L.preview.x + L.preview.w - 20.f - cw, L.preview.y + 14.f, code,
                        sty(Theme::inkDim(a), 0.68f));
            drawCarPreview(dl, {L.preview.x, L.preview.y + 88.f, L.preview.w,
                                L.preview.h - 88.f}, *entry, paint, a);
        } else {
            m_text.draw(dl, L.preview.x + 20.f, L.preview.y + 50.f, "NO CAR SELECTED",
                        sty(Theme::inkDim(a), 1.4f, true));
        }

        card(L.specs, "INFORMAZIONI & SPECIFICHE");
        const auto stat = [&](const Rect& r, const char* label, const std::string& value,
                              const Color& colour) {
            dl.addRectFilled(r, Color::rgba(0.10f, 0.05f, 0.13f, 0.86f * a));
            dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.09f * a), 1.f);
            m_text.draw(dl, r.x + 9.f, r.y + 4.f, label, sty(Theme::inkDim(a), 0.55f));
            m_text.draw(dl, r.x + 9.f, r.y + 17.f, value, sty(colour, 0.78f, true));
        };
        const float sx = L.specs.x + 12.f;
        const float sy = L.specs.y + 32.f;
        const float sw = (L.specs.w - 36.f) / 3.f;
        const float sh = (L.specs.h - 38.f) / 2.f;
        stat({sx, sy, sw, sh - 4.f}, "POTENZA MAX",
             m_carSpec.peakPowerHp > 0.0 ? number1("%.0f BHP", m_carSpec.peakPowerHp) : "--",
             Theme::accent(a));
        stat({sx + sw + 6.f, sy, sw, sh - 4.f}, "COPPIA MAX",
             m_carSpec.peakTorqueNm > 0.0 ? number1("%.0f Nm", m_carSpec.peakTorqueNm) : "--",
             Color::rgba(0.92f, 0.36f, 0.40f, a));
        stat({sx + (sw + 6.f) * 2.f, sy, sw, sh - 4.f}, "PESO A VUOTO",
             m_carSpec.massKg > 0.0 ? number1("%.0f kg", m_carSpec.massKg) : "--",
             Theme::ink(a));
        stat({sx, sy + sh, sw, sh - 4.f}, "PESO / POTENZA",
             (m_carSpec.massKg > 0.0 && m_carSpec.peakPowerHp > 0.0)
                 ? number1("%.2f kg/hp", m_carSpec.massKg / m_carSpec.peakPowerHp) : "--",
             Color::rgba(0.50f, 0.88f, 0.62f, a));
        stat({sx + sw + 6.f, sy + sh, sw, sh - 4.f}, "TRAZIONE",
             m_carSpec.drivetrain.empty() ? "--" : m_carSpec.drivetrain, Theme::ink(a));
        stat({sx + (sw + 6.f) * 2.f, sy + sh, sw, sh - 4.f}, "RPM LIMIT",
             m_carSpec.rpmLimit > 0 ? std::to_string(m_carSpec.rpmLimit) : "--",
             Theme::ink(a));

        card(L.dyno, "GRAFICO POTENZA & COPPIA");
        drawDyno(dl, L.dyno, m_carSpec, a);
    }

    void buildTrackPage(DrawList& dl, int w, int h, float a) {
        (void)w; (void)h;
        const TrackPageLayout L = trackPageLayout(w, h);
        const ContentEntry* track = m_menu->selectedTrackEntry();
        const auto& tracks = m_menu->trackEntries();
        const auto& types = m_menu->trackCircuitTypes();
        const auto& countries = m_menu->trackCountries();
        const auto typeLabel = [](const std::string& type) {
            if (type == "permanent") return std::string("PERMANENT CIRCUIT");
            if (type == "temporary") return std::string("TEMPORARY CIRCUIT");
            return type.empty() ? std::string("NON DICHIARATA") : type;
        };
        const auto number = [](const char* pattern, double value) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), pattern, value);
            return std::string(buffer);
        };
        const auto card = [&](const Rect& r, const char* title) {
            dl.addRectFilled(r, Color::rgba(0.045f, 0.022f, 0.065f, 0.94f * a));
            dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.12f * a), 1.f);
            if (title) m_text.draw(dl, r.x + 14.f, r.y + 9.f, title,
                                   sty(Theme::accent(a), 0.76f, true));
        };
        const auto filterCard = [&](const Rect& r, const char* label, const std::string& value,
                                    bool active) {
            dl.addRectFilled(r, Color::rgba(0.035f, 0.018f, 0.052f, 0.96f * a));
            dl.addRect(r, active ? Theme::accent(a) : Color::rgba(1.f, 1.f, 1.f, 0.14f * a), 1.f);
            m_text.draw(dl, r.x + 12.f, r.y + 7.f, label, sty(Theme::accent(a), 0.66f, true));
            m_text.draw(dl, r.x + 12.f, r.y + 27.f, elideToWidth(value, 0.95f, r.w - 24.f),
                        sty(Theme::ink(a), 0.95f, true));
        };

        filterCard(L.filter[0], "NAZIONE / REGIONE",
                   m_menu->trackCountryFilter().empty() ? "TUTTE LE NAZIONI"
                                                        : m_menu->trackCountryFilter(),
                   m_trackFilterHover == 0);
        filterCard(L.filter[1], "TIPOLOGIA / SUPERFICIE",
                   m_menu->trackCircuitTypeFilter().empty()
                       ? "TUTTI I CIRCUITI" : typeLabel(m_menu->trackCircuitTypeFilter()),
                   m_trackFilterHover == 1);
        const std::string query = m_menu->trackSearchQuery();
        filterCard(L.filter[2], "CERCA CIRCUITO O LOCALITA'",
                   m_menu->trackSearchEditing()
                       ? (query + "_")
                       : (query.empty() ? "MODELLO, CITTA O NAZIONE" : query),
                   m_trackFilterHover == 2);
        dl.addRectFilled(L.reset, Color::rgba(m_trackFilterHover == 3 ? 0.30f : 0.20f, 0.025f, 0.04f,
                                             0.70f * a));
        dl.addRect(L.reset, Color::rgba(0.95f, 0.30f, 0.36f, 0.55f * a), 1.f);
        m_text.draw(dl, L.reset.x + 16.f, L.reset.y + 7.f, "RESET",
                    sty(Color::rgba(1.f, 0.74f, 0.74f, a), 0.72f, true));

        // Category rail: circuit types actually declared by the folders.
        card(L.categories, "CATEGORIE & CIRCUITI");
        m_text.draw(dl, L.categories.x + L.categories.w - 82.f, L.categories.y + 10.f,
                    std::to_string(types.size()) + (types.size() == 1 ? " TIPO" : " TIPI"),
                    sty(Theme::inkDim(a), 0.62f));
        for (int i = 0; i < static_cast<int>(L.categoryRows.size()); ++i) {
            const Rect& rr = L.categoryRows[static_cast<std::size_t>(i)];
            const bool all = i == 0;
            const std::string name = all ? "TUTTI I CIRCUITI" : typeLabel(types[static_cast<std::size_t>(i - 1)]);
            const bool active = all ? m_menu->trackCircuitTypeFilter().empty()
                                    : m_menu->trackCircuitTypeFilter() == types[static_cast<std::size_t>(i - 1)];
            if (active)
                dl.addRectFilled(rr, Color::rgba(0.96f, 0.66f, 0.18f, 0.18f * a));
            else if (m_trackCategoryHover == i)
                dl.addRectFilled(rr, Color::rgba(1.f, 1.f, 1.f, 0.05f * a));
            m_text.draw(dl, rr.x + 9.f, rr.y + 2.f, elideToWidth(name, 0.76f, rr.w - 18.f),
                        sty(active ? Theme::accent(a) : Theme::ink(0.88f * a), 0.76f, active));
        }

        // Circuit cards.
        card(L.cards, "SELEZIONA CIRCUITO");
        m_text.draw(dl, L.cards.x + L.cards.w - 220.f, L.cards.y + 10.f,
                    std::to_string(tracks.size()) + (tracks.size() == 1 ? " CIRCUITO" : " CIRCUITI"),
                    sty(Theme::inkDim(a), 0.62f));
        for (int i = 0; i < static_cast<int>(L.cardTiles.size()); ++i) {
            const Rect& rr = L.cardTiles[static_cast<std::size_t>(i)];
            const ContentEntry& e = tracks[static_cast<std::size_t>(i)];
            const bool active = track && track->path == e.path;
            dl.addRectFilled(rr, active ? Color::rgba(0.30f, 0.14f, 0.02f, 0.92f * a)
                                        : (m_trackCardHover == i
                                               ? Color::rgba(1.f, 1.f, 1.f, 0.08f * a)
                                               : Color::rgba(0.09f, 0.045f, 0.12f, 0.92f * a)));
            dl.addRect(rr, active ? Theme::accent(a) : Theme::hairline(a), 1.f);
            m_text.draw(dl, rr.x + 9.f, rr.y + 5.f, trackInitials(e.label),
                        sty(active ? Theme::accent(a) : Theme::ink(a), 0.86f, true));
            m_text.draw(dl, rr.x + 42.f, rr.y + 5.f, elideToWidth(e.label, 0.78f, rr.w - 84.f),
                        sty(Theme::ink(a), 0.78f, true));
            if (!e.country.empty())
                drawCountryFlag(dl, {rr.x + rr.w - 30.f, rr.y + 6.f, 20.f, 13.f}, e.country, a);
            std::string sub = e.country;
            if (!e.city.empty()) sub = (sub.empty() ? "" : sub + " / ") + e.city;
            if (e.lengthM > 0.0)
                sub += (sub.empty() ? "" : " / ") + number("%.3f km", e.lengthM / 1000.0);
            m_text.draw(dl, rr.x + 42.f, rr.y + 21.f, elideToWidth(sub, 0.62f, rr.w - 84.f),
                        sty(Theme::inkDim(a), 0.62f));
        }
        if (tracks.empty())
            m_text.draw(dl, L.cards.x + 14.f, L.cards.y + 50.f,
                        "NESSUNA VETTURA O CIRCUITO NEL CONTENUTO", sty(Theme::inkDim(a), 0.72f));

        // Bottom showcase.
        card(L.showcase, nullptr);
        dl.addRectFilled(L.layoutBar, Color::rgba(0.025f, 0.012f, 0.035f, 0.96f * a));
        m_text.draw(dl, L.layoutBar.x + 12.f, L.layoutBar.y + 9.f, "CONFIGURAZIONE LAYOUT:",
                    sty(Theme::accent(a), 0.64f, true));
        const std::string rawLayoutChoice = m_menu->trackLayoutChoice();
        bool layoutChoiceValid = false;
        if (track) {
            for (const TrackLayoutInfo& info : track->layouts)
                if ((info.id.empty() ? info.name : info.id) == rawLayoutChoice)
                    layoutChoiceValid = true;
        }
        const std::string layoutChoice = layoutChoiceValid ? rawLayoutChoice : std::string();
        for (int i = 0; i < static_cast<int>(L.layoutTabs.size()); ++i) {
            const Rect& rr = L.layoutTabs[static_cast<std::size_t>(i)];
            std::string label = "DEFAULT";
            bool active = layoutChoice.empty() && i == 0;
            if (track && i < static_cast<int>(track->layouts.size())) {
                const TrackLayoutInfo& info = track->layouts[static_cast<std::size_t>(i)];
                label = info.name.empty() ? info.id : info.name;
                if (layoutChoice.empty()) active = info.isDefault || i == 0;
                else active = (info.id.empty() ? info.name : info.id) == layoutChoice;
            }
            const bool hot = m_trackLayoutHover == i && track && track->layouts.size() > 1;
            dl.addRectFilled(rr, active ? Theme::accent(0.88f * a)
                                        : Color::rgba(1.f, 1.f, 1.f,
                                                      (hot ? 0.13f : 0.05f) * a));
            dl.addRect(rr, active || hot ? Theme::accent(a) : Theme::hairline(a), 1.f);
            m_text.draw(dl, rr.x + 7.f, rr.y + 2.f, elideToWidth(label, 0.62f, rr.w - 14.f),
                        sty(active ? Color::rgba(0.06f, 0.02f, 0.03f, a) : Theme::ink(a), 0.62f, active));
        }
        if (track) {
            const char* note = track->layouts.size() > 1
                ? "SCEGLI E CONFERMA CON NEXT"
                : (track->layouts.empty() ? "LAYOUT DEFAULT" : "UNICO LAYOUT DISPONIBILE");
            m_text.draw(dl, L.layoutBar.x + L.layoutBar.w - m_text.measureWidth(note, 0.54f) - 10.f,
                        L.layoutBar.y + 9.f, note, sty(Theme::inkDim(a), 0.54f));
        }

        dl.addRectFilled(L.map, Color::rgba(0.018f, 0.010f, 0.028f, 0.80f * a));
        dl.addRect(L.map, Color::rgba(1.f, 1.f, 1.f, 0.08f * a), 1.f);
        for (int gx = 1; gx < 8; ++gx) {
            const float x = L.map.x + L.map.w * static_cast<float>(gx) / 8.f;
            dl.addLine(x, L.map.y + 1.f, x, L.map.y + L.map.h - 1.f,
                       Color::rgba(0.95f, 0.66f, 0.18f, 0.055f * a), 1.f);
        }
        for (int gy = 1; gy < 6; ++gy) {
            const float y = L.map.y + L.map.h * static_cast<float>(gy) / 6.f;
            dl.addLine(L.map.x + 1.f, y, L.map.x + L.map.w - 1.f, y,
                       Color::rgba(0.95f, 0.66f, 0.18f, 0.055f * a), 1.f);
        }
        if (track) {
            m_text.draw(dl, L.map.x + 12.f, L.map.y + 7.f, "SELECT TRACK // SATELLITE TELEMETRY",
                        sty(Theme::accent(a), 0.62f, true));
            m_text.draw(dl, L.map.x + 12.f, L.map.y + 20.f,
                        elideToWidth(track->label, 1.35f, L.map.w - 24.f),
                        sty(Theme::ink(a), 1.35f, true));
            std::string sub = track->city;
            if (!track->country.empty()) sub += (sub.empty() ? "" : ", ") + track->country;
            if (!track->circuitType.empty()) sub += " / " + typeLabel(track->circuitType);
            if (track->lengthM > 0.0)
                sub += " (" + number("%.3f km", track->lengthM / 1000.0) + ")";
            m_text.draw(dl, L.map.x + 12.f, L.map.y + 36.f,
                        elideToWidth(sub, 0.66f, L.map.w - 24.f), sty(Theme::inkDim(a), 0.66f));
            const std::string grade = "FIA GRADE " +
                (track->fiaGrade.empty() ? std::string("N/D") : track->fiaGrade) +
                " // " + typeLabel(track->circuitType);
            const float gw = m_text.measureWidth(grade, 0.60f);
            dl.addRectFilled({L.map.x + L.map.w - gw - 22.f, L.map.y + 8.f, gw + 12.f, 14.f},
                             Color::rgba(0.90f, 0.56f, 0.10f, 0.18f * a));
            m_text.draw(dl, L.map.x + L.map.w - gw - 16.f, L.map.y + 10.f, grade,
                        sty(Theme::accent(a), 0.60f, true));

            const Rect mapArea{L.map.x + 8.f, L.map.y + 52.f, L.map.w - 16.f,
                               std::max(20.f, L.map.h - 84.f)};
            if (const TrackMapPreview* map = m_menu->selectedTrackMap()) {
                drawTrackMap(dl, *map, mapArea, a);
            } else {
                fillCircle(dl, mapArea.x + mapArea.w * 0.5f, mapArea.y + mapArea.h * 0.5f,
                           std::min(mapArea.w, mapArea.h) * 0.28f,
                           Color::rgba(0.95f, 0.66f, 0.18f, 0.06f * a));
                fillCircle(dl, mapArea.x + mapArea.w * 0.5f, mapArea.y + mapArea.h * 0.5f,
                           std::min(mapArea.w, mapArea.h) * 0.17f,
                           Color::rgba(0.95f, 0.66f, 0.18f, 0.10f * a));
                m_text.draw(dl, mapArea.x + mapArea.w * 0.5f - 44.f,
                            mapArea.y + mapArea.h * 0.5f - 24.f,
                            trackInitials(track->label), sty(Theme::accent(0.55f * a), 2.4f, true));
                m_text.draw(dl, mapArea.x + 8.f, mapArea.y + mapArea.h - 14.f,
                            "MAPPA CIRCUITO NON PRESENTE NEL CONTENUTO",
                            sty(Theme::inkDim(0.78f * a), 0.60f, true));
            }

            const std::string direction = content_detail::lowerText(track->runDirection) == "anticlockwise"
                ? "ANTIORARIO" : (track->runDirection.empty() ? "N/D" : "ORARIO");
            char footer[160];
            std::snprintf(footer, sizeof(footer), "SENSO: %s   /   SEZIONI: %d   /   DRS: %d",
                          direction.c_str(), static_cast<int>(track->sections.size()),
                          track->drsZones);
            dl.addRectFilled({L.map.x + 8.f, L.map.y + L.map.h - 22.f, L.map.w - 16.f, 16.f},
                             Color::rgba(0.02f, 0.01f, 0.03f, 0.86f * a));
            m_text.draw(dl, L.map.x + 14.f, L.map.y + L.map.h - 20.f,
                        elideToWidth(footer, 0.58f, L.map.w - 24.f), sty(Theme::ink(a), 0.58f));
        } else {
            m_text.draw(dl, L.map.x + 14.f, L.map.y + 22.f, "NO TRACK SELECTED",
                        sty(Theme::inkDim(a), 1.2f, true));
        }

        // Street-circuit facts, all from data.json / layouts.json.
        card(L.specs, "INFORMAZIONI & SPECIFICHE");
        const auto stat = [&](const Rect& r, const char* label, const std::string& value,
                              const Color& colour) {
            dl.addRectFilled(r, Color::rgba(0.10f, 0.05f, 0.13f, 0.86f * a));
            dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.09f * a), 1.f);
            m_text.draw(dl, r.x + 7.f, r.y + 3.f, label, sty(Theme::inkDim(a), 0.48f));
            m_text.draw(dl, r.x + 7.f, r.y + 14.f, elideToWidth(value, 0.64f, r.w - 14.f),
                        sty(colour, 0.64f, true));
        };
        const float sx = L.specs.x + 10.f;
        const float sy = L.specs.y + 30.f;
        const float sw = (L.specs.w - 32.f) / 3.f;
        const float sh = std::max(20.f, (L.specs.h - 36.f) / 2.f);
        const std::string direction = track ? (content_detail::lowerText(track->runDirection) == "anticlockwise"
            ? "ANTIORARIO" : (track->runDirection.empty() ? "--" : "ORARIO")) : "--";
        stat({sx, sy, sw, sh - 3.f}, "LUNGHEZZA",
             track && track->lengthM > 0.0 ? number("%.3f km", track->lengthM / 1000.0) : "--",
             Theme::accent(a));
        stat({sx + sw + 5.f, sy, sw, sh - 3.f}, "SEZIONI",
             track && !track->sections.empty() ? std::to_string(track->sections.size()) : "--",
             Theme::ink(a));
        stat({sx + (sw + 5.f) * 2.f, sy, sw, sh - 3.f}, "PIT BOXES",
             track && track->pitBoxes > 0 ? std::to_string(track->pitBoxes) : "--", Theme::ink(a));
        stat({sx, sy + sh, sw, sh - 3.f}, "FIA GRADE",
             track && !track->fiaGrade.empty() ? "GRADE " + track->fiaGrade : "--",
             Color::rgba(0.92f, 0.36f, 0.40f, a));
        stat({sx + sw + 5.f, sy + sh, sw, sh - 3.f}, "SENSO", direction, Theme::ink(a));
        stat({sx + (sw + 5.f) * 2.f, sy + sh, sw, sh - 3.f}, "LARGHEZZA",
             track && track->widthM > 0.0 ? number("%.1f m", track->widthM) : "--",
             Color::rgba(0.50f, 0.88f, 0.62f, a));
        if (track && !track->description.empty()) {
            dl.addTextWrapped(L.specs.x + 10.f, L.specs.y + L.specs.h - 31.f, track->description,
                              Theme::inkDim(0.74f * a), 0.56f, L.specs.w - 20.f);
        }

        // Sector ribbon and declared DRS zones. No elevation/speed curve is
        // invented when the content does not carry one.
        card(L.telemetry, "SETTORI & ZONE DRS");
        const float stripX = L.telemetry.x + 10.f;
        const float stripY = L.telemetry.y + 28.f;
        const float stripW = L.telemetry.w - 20.f;
        if (track && !track->sections.empty()) {
            const float stripH = 10.f;
            dl.addRectFilled({stripX, stripY, stripW, stripH},
                             Color::rgba(0.02f, 0.01f, 0.03f, 0.85f * a));
            for (std::size_t i = 0; i < track->sections.size(); ++i) {
                const TrackSectionInfo& section = track->sections[i];
                const float x0 = stripX + static_cast<float>(std::clamp(section.in, 0.0, 1.0)) * stripW;
                const float x1 = stripX + static_cast<float>(std::clamp(section.out, 0.0, 1.0)) * stripW;
                const Color colour = (i % 3) == 0 ? Color::rgba(0.86f, 0.20f, 0.28f, a)
                                    : (i % 3) == 1
                                          ? Color::rgba(0.96f, 0.66f, 0.18f, a)
                                          : Color::rgba(0.20f, 0.76f, 0.78f, a);
                dl.addRectFilled({x0, stripY, std::max(1.f, x1 - x0), stripH}, colour);
                dl.addLine(x0, stripY + stripH, x0, stripY + stripH + 5.f, Theme::hairline(a), 1.f);
            }
            m_text.draw(dl, stripX, stripY + 16.f,
                        std::to_string(track->sections.size()) + " SEZIONI DICHIARATE",
                        sty(Theme::inkDim(a), 0.58f));
            if (track->drsZones > 0)
                m_text.draw(dl, stripX + stripW - 76.f, stripY + 16.f,
                            std::to_string(track->drsZones) + " DRS",
                            sty(Color::rgba(0.50f, 0.88f, 0.62f, a), 0.58f, true));
            m_text.draw(dl, stripX, stripY + 30.f,
                        "PROFILO ALTIMETRICO NON DICHIARATO NEL CONTENUTO",
                        sty(Theme::inkDim(0.60f * a), 0.54f));
        } else {
            m_text.draw(dl, stripX, stripY + 2.f, "NESSUNA SEZIONE DICHIARATA",
                        sty(Theme::inkDim(a), 0.60f));
            m_text.draw(dl, stripX, stripY + 18.f, "IL CONTENUTO NON FORNISCE QUOTE O SETTORI",
                        sty(Theme::inkDim(0.58f * a), 0.54f));
        }
    }

    void buildEventPage(DrawList& dl, int w, int h, float a) {
        (void)w; (void)h;
        const EventPageLayout L = eventPageLayout(w, h);
        const auto modes = m_menu->effectiveSingleplayerModes();
        const int modeCount = std::min(8, static_cast<int>(modes.size()));
        const int firstMode = m_menu->firstEventModeRow();
        const int selected = m_menu->selectedIndex();
        const ContentEntry* track = m_menu->chosenTrackEntry();
        const WeatherOutlook& wx = m_menu->weather();
        const bool carOk = !m_menu->carName().empty();
        const bool trackOk = track != nullptr || !m_menu->trackName().empty();
        const bool ready = carOk && trackOk;
        const auto& items = m_menu->items();
        const auto card = [&](const Rect& r, const char* title, const char* right) {
            dl.addRectFilled(r, Color::rgba(0.045f, 0.022f, 0.065f, 0.94f * a));
            dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.12f * a), 1.f);
            if (title) m_text.draw(dl, r.x + 14.f, r.y + 9.f, title,
                                   sty(Theme::accent(a), 0.76f, true));
            if (right)
                m_text.draw(dl, r.x + r.w - 96.f, r.y + 10.f, right,
                            sty(Theme::inkDim(0.78f * a), 0.60f, true));
        };

        // Left: the three things the event will actually use. This replaces the
        // generic panel that used to show the selected menu row ("CAR") as if
        // it were the event type.
        card(L.summary, "SESSIONE", ready ? "PRONTA" : "INCOMPLETA");
        const auto field = [&](float y, const char* label, const std::string& value,
                               const Color& colour) {
            m_text.draw(dl, L.summary.x + 14.f, y, label,
                        sty(Theme::inkDim(0.78f * a), 0.62f, true));
            m_text.draw(dl, L.summary.x + 14.f, y + 15.f,
                        elideToWidth(value, 0.96f, L.summary.w - 28.f),
                        sty(colour, 0.96f, true));
        };
        const std::string trackText = track ? track->label
                                            : (m_menu->trackName().empty()
                                                   ? std::string("NON SELEZIONATO")
                                                   : m_menu->trackName());
        field(L.summary.y + 34.f, "AUTO",
              carOk ? m_menu->carName() : std::string("NON SELEZIONATA"),
              carOk ? Theme::ink(a) : Color::rgba(0.95f, 0.58f, 0.42f, a));
        field(L.summary.y + 82.f, "CIRCUITO", trackText,
              trackOk ? Theme::ink(a) : Color::rgba(0.95f, 0.58f, 0.42f, a));
        field(L.summary.y + 130.f, "METEO",
              std::string(weatherKindName(wx.kind)) + "  /  " + weatherSourceName(wx.source),
              wx.kind == WeatherKind::Unknown ? Color::rgba(0.95f, 0.58f, 0.42f, a)
                                              : Theme::ink(a));
        char weatherLine[96];
        std::snprintf(weatherLine, sizeof(weatherLine), "%.0f C ARIA  /  %.2f WETNESS",
                      wx.ambientC, wx.wetness);
        m_text.draw(dl, L.summary.x + 14.f, L.summary.y + 160.f, weatherLine,
                    sty(Theme::inkDim(0.80f * a), 0.66f));

        dl.addLine(L.summary.x + 14.f, L.summary.y + 190.f,
                   L.summary.x + L.summary.w - 14.f, L.summary.y + 190.f,
                   Theme::hairline(a), 1.f);
        m_text.draw(dl, L.summary.x + 14.f, L.summary.y + 204.f, "LA SESSIONE USA",
                    sty(Theme::accent(a), 0.64f, true));
        m_text.draw(dl, L.summary.x + 14.f, L.summary.y + 224.f, "AUTO + CIRCUITO + METEO",
                    sty(Theme::ink(0.88f * a), 0.78f, true));
        m_text.draw(dl, L.summary.x + 14.f, L.summary.y + 242.f, "SCEGLI IL TIPO A DESTRA",
                    sty(Theme::inkDim(0.76f * a), 0.66f));
        if (!ready) {
            dl.addTextWrapped(L.summary.x + 14.f, L.summary.y + L.summary.h - 54.f,
                              "TORNA A CARS/TRACKS PER COMPLETARE LA SESSIONE",
                              Color::rgba(0.95f, 0.58f, 0.42f, a), 0.60f,
                              L.summary.w - 28.f);
        } else {
            m_text.draw(dl, L.summary.x + 14.f, L.summary.y + L.summary.h - 30.f,
                        "PRONTA PER LA CONFIGURAZIONE",
                        sty(Color::rgba(0.50f, 0.88f, 0.62f, a), 0.60f, true));
        }

        // Right: modes are buttons, not a hidden menu list. A click opens the
        // same MODE SETUP the keyboard ENTER opens.
        card(L.events, "TIPO EVENTO",
             (std::to_string(modeCount) + (modeCount == 1 ? " EVENTO" : " EVENTI")).c_str());
        if (modeCount == 0) {
            m_text.draw(dl, L.events.x + 14.f, L.events.y + 48.f,
                        "NESSUN TIPO DI EVENTO CONFIGURATO",
                        sty(Theme::inkDim(a), 0.78f));
        }
        for (int i = 0; i < modeCount; ++i) {
            const Rect& r = L.modeCards[i];
            const int row = firstMode + i;
            const bool enabled = row >= 0 && row < static_cast<int>(items.size()) &&
                                 items[row].enabled && !items[row].isSeparator;
            const bool active = selected == row;
            const bool hot = m_eventModeHover == i && enabled;
            dl.addRectFilled(r, active ? Color::rgba(0.30f, 0.14f, 0.02f, 0.92f * a)
                                       : (hot ? Color::rgba(1.f, 1.f, 1.f, 0.09f * a)
                                              : Color::rgba(0.09f, 0.045f, 0.12f, 0.92f * a)));
            dl.addRect(r, active ? Theme::accent(a) : Theme::hairline(a), 1.f);
            if (active)
                dl.addRectFilled({r.x, r.y, 3.f, r.h}, Theme::accent(a));
            const Color labelColour = !enabled ? Theme::inkDim(0.55f * a)
                                              : (active ? Theme::accent(a) : Theme::ink(a));
            m_text.draw(dl, r.x + 12.f, r.y + 8.f,
                        elideToWidth(m_menu->eventModeLabel(modes[static_cast<std::size_t>(i)]),
                                     0.90f, r.w - 24.f),
                        sty(labelColour, 0.90f, true));
            dl.addTextWrapped(r.x + 12.f, r.y + 29.f,
                              m_menu->eventModeHint(modes[static_cast<std::size_t>(i)]),
                              Theme::inkDim((enabled ? 0.82f : 0.55f) * a), 0.56f,
                              r.w - 24.f);
            m_text.draw(dl, r.x + r.w - 92.f, r.y + r.h - 20.f,
                        enabled ? "CONFIGURA  >" : "INCOMPLETA",
                        sty(enabled ? (active ? Theme::accent(a) : Theme::inkDim(0.78f * a))
                                    : Color::rgba(0.95f, 0.58f, 0.42f, 0.72f * a),
                            0.56f, true));
        }
    }

    void buildWeatherPage(DrawList& dl, int w, int h, float a) {
        (void)w; (void)h;
        const WeatherPageLayout L = weatherPageLayout(w, h);
        const WeatherOutlook& wx = m_menu->weather();
        const ContentEntry* track = m_menu->chosenTrackEntry();
        const WeatherSource mode = m_menu->weatherMode();
        const auto card = [&](const Rect& r, const char* title) {
            dl.addRectFilled(r, Color::rgba(0.045f, 0.022f, 0.065f, 0.94f * a));
            dl.addRect(r, Color::rgba(1.f, 1.f, 1.f, 0.12f * a), 1.f);
            if (title) m_text.draw(dl, r.x + 14.f, r.y + 9.f, title,
                                   sty(Theme::accent(a), 0.76f, true));
        };
        const auto metric = [&](float x, float y, const char* label, const std::string& value,
                                const Color& colour) {
            m_text.draw(dl, x, y, label, sty(Theme::inkDim(0.78f * a), 0.62f, true));
            m_text.draw(dl, x, y + 14.f, value, sty(colour, 0.98f, true));
        };
        const auto number = [](const char* pattern, double value) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), pattern, value);
            return std::string(buffer);
        };

        // Mode bar: the four sources are alternatives, not a list that happens
        // to contain the current one. The chosen circuit stays named at right.
        dl.addRectFilled(L.modeBar, Color::rgba(0.02f, 0.012f, 0.03f, 0.82f * a));
        dl.addLine(L.modeBar.x, L.modeBar.y + L.modeBar.h, L.modeBar.x + L.modeBar.w,
                   L.modeBar.y + L.modeBar.h, Theme::hairline(a), 1.f);
        static const char* kModeLabels[4] = {"DRY", "CUSTOM", "TODAY", "REAL DATE"};
        static const WeatherSource kModes[4] = {WeatherSource::Dry, WeatherSource::Custom,
                                                WeatherSource::Today, WeatherSource::RealDate};
        for (int i = 0; i < 4; ++i) {
            const Rect& r = L.modeChip[i];
            const bool active = mode == kModes[i];
            if (active)
                dl.addRectFilled(r, Color::rgba(0.96f, 0.66f, 0.18f, 0.22f * a));
            else if (m_weatherModeHover == i)
                dl.addRectFilled(r, Color::rgba(1.f, 1.f, 1.f, 0.07f * a));
            dl.addRect(r, active ? Theme::accent(a) : Theme::hairline(a), 1.f);
            m_text.draw(dl, r.x + 12.f, r.y + 7.f, kModeLabels[i],
                        sty(active ? Theme::accent(a) : Theme::ink(0.85f * a), 0.72f, active));
        }
        const std::string trackName = track ? track->label : std::string("NESSUN CIRCUITO");
        m_text.draw(dl, L.modeBar.x + L.modeBar.w - 260.f, L.modeBar.y + 13.f,
                    elideToWidth(trackName, 0.74f, 260.f),
                    sty(track ? Theme::ink(0.92f * a) : Theme::inkDim(0.85f * a), 0.74f, true));

        // Left: the resulting sky, with the same fields the simulation gets.
        card(L.conditions, "CONDIZIONI ATTUALI");
        m_text.draw(dl, L.conditions.x + L.conditions.w - 110.f, L.conditions.y + 10.f,
                    weatherSourceName(wx.source), sty(Theme::accent(a), 0.66f, true));
        m_text.draw(dl, L.conditions.x + 14.f, L.conditions.y + 32.f,
                    elideToWidth(trackName, 1.35f, L.conditions.w - 28.f),
                    sty(track ? Theme::ink(a) : Theme::inkDim(a), 1.35f, true));
        std::string place;
        if (track) {
            if (!track->city.empty()) place = track->city;
            if (!track->country.empty())
                place = place.empty() ? track->country : place + ", " + track->country;
        }
        m_text.draw(dl, L.conditions.x + 14.f, L.conditions.y + 54.f,
                    elideToWidth(place.empty() ? "SELEZIONA UN CIRCUITO PER LA LOCALITA" : place,
                                 0.78f, L.conditions.w - 28.f),
                    sty(Theme::inkDim(0.82f * a), 0.78f));

        const Rect iconBox{L.conditions.x + 20.f, L.conditions.y + 76.f, 98.f, 98.f};
        drawWeatherIcon(dl, iconBox, wx.kind, a);
        const bool unknown = wx.kind == WeatherKind::Unknown;
        m_text.draw(dl, L.conditions.x + 136.f, L.conditions.y + 78.f,
                    weatherKindName(wx.kind), sty(unknown ? Theme::inkDim(a) : Theme::ink(a),
                                                  1.30f, true));
        char time[24];
        std::snprintf(time, sizeof(time), "%02d:%02d", static_cast<int>(wx.hourOfDay),
                      static_cast<int>((wx.hourOfDay - static_cast<int>(wx.hourOfDay)) * 60.f));
        m_text.draw(dl, L.conditions.x + 136.f, L.conditions.y + 104.f, time,
                    sty(Theme::accent(a), 1.15f, true));
        m_text.draw(dl, L.conditions.x + 136.f, L.conditions.y + 128.f,
                    wx.source == WeatherSource::Today || wx.source == WeatherSource::RealDate
                        ? (m_menu->weatherDate().empty() ? "DATA ODIERNA" : m_menu->weatherDate())
                        : "ORARIO DI SESSIONE",
                    sty(Theme::inkDim(0.82f * a), 0.72f));

        const float mx0 = L.conditions.x + 14.f;
        const float my0 = L.conditions.y + 190.f;
        const float colW = (L.conditions.w - 28.f) / 3.f;
        metric(mx0, my0, "ARIA", number("%.0f C", wx.ambientC), Theme::ink(a));
        metric(mx0 + colW, my0, "ASFALTO", number("%.0f C", wx.trackC), Theme::ink(a));
        metric(mx0 + colW * 2.f, my0, "VENTO", number("%.1f m/s", wx.windMs), Theme::ink(a));
        metric(mx0, my0 + 44.f, "PIOGGIA", number("%.2f", wx.rain),
               wx.rain > 0.05f ? Color::rgba(0.46f, 0.76f, 0.98f, a) : Theme::inkDim(a));
        metric(mx0 + colW, my0 + 44.f, "WETNESS", number("%.2f", wx.wetness),
               wx.wetness > 0.05f ? Color::rgba(0.46f, 0.76f, 0.98f, a) : Theme::inkDim(a));
        metric(mx0 + colW * 2.f, my0 + 44.f, "NUVOLE", number("%.2f", wx.cloud),
               Theme::ink(a));
        m_text.draw(dl, mx0, my0 + 78.f, "NEBBIA  " + number("%.2f", wx.fog),
                    sty(Theme::inkDim(0.82f * a), 0.66f));
        if (!wx.note.empty()) {
            dl.addTextWrapped(mx0, L.conditions.y + L.conditions.h - 32.f, wx.note,
                              Color::rgba(0.95f, 0.58f, 0.42f, a), 0.62f,
                              L.conditions.w - 28.f);
        }

        // Right: the controls that change that sky. CUSTOM has one row per
        // value; TODAY/REAL DATE have the date and the service request.
        card(L.controls, "GESTISCI METEO");
        const char* modeHint = mode == WeatherSource::Dry
            ? "PRESET ASCIUTTO: NESSUN PARAMETRO DA CAMBIARE"
            : (mode == WeatherSource::Custom
                   ? "IMPOSTA LE CONDIZIONI DELLA SESSIONE"
                   : (mode == WeatherSource::Today
                          ? "METEO REALE DI OGGI SUL CIRCUITO"
                          : "METEO REALE DI UNA DATA SCELTA"));
        m_text.draw(dl, L.controls.x + 14.f, L.controls.y + 28.f, modeHint,
                    sty(Theme::inkDim(0.86f * a), 0.64f));

        m_text.draw(dl, L.controls.x + 14.f, L.controls.y + 53.f, "ORARIO",
                    sty(Theme::inkDim(0.82f * a), 0.68f, true));
        const auto stepButton = [&](const Rect& r, const char* label, bool hot) {
            dl.addRectFilled(r, hot ? Color::rgba(0.96f, 0.66f, 0.18f, 0.24f * a)
                                    : Color::rgba(1.f, 1.f, 1.f, 0.05f * a));
            dl.addRect(r, hot ? Theme::accent(a) : Theme::hairline(a), 1.f);
            m_text.draw(dl, r.x + 8.f, r.y + 5.f, label,
                        sty(hot ? Theme::accent(a) : Theme::ink(0.9f * a), 0.74f, true));
        };
        stepButton(L.timeMinus, "-", m_weatherTimeHover == 0);
        stepButton(L.timePlus, "+", m_weatherTimeHover == 1);
        m_text.draw(dl, L.timeMinus.x + 30.f, L.controls.y + 53.f, time,
                    sty(Theme::ink(a), 0.96f, true));

        if (mode == WeatherSource::Today || mode == WeatherSource::RealDate) {
            m_text.draw(dl, L.controls.x + 42.f, L.controls.y + 86.f, "DATA",
                        sty(Theme::inkDim(0.82f * a), 0.68f, true));
            stepButton(L.dateMinus, "-", m_weatherDateHover == 0);
            stepButton(L.datePlus, "+", m_weatherDateHover == 1);
            m_text.draw(dl, L.dateMinus.x + 28.f, L.controls.y + 87.f, m_menu->weatherDate(),
                        sty(Theme::ink(a), 0.92f, true));
            const bool fetched = m_menu->weatherFetched();
            dl.addRectFilled(L.ask, Color::rgba(m_weatherAskHover ? 0.34f : 0.22f, 0.09f, 0.025f,
                                                0.88f * a));
            dl.addRect(L.ask, Color::rgba(0.96f, 0.66f, 0.18f, 0.64f * a), 1.f);
            m_text.draw(dl, L.ask.x + 12.f, L.ask.y + 7.f,
                        fetched ? "RICHIEDI METEO  /  ULTIMO DATO OK"
                                : "RICHIEDI METEO  /  NESSUN DATO",
                        sty(Theme::accent(a), 0.70f, true));
        } else if (mode == WeatherSource::Custom) {
            static const char* kFieldLabels[6] = {"ARIA", "PIOGGIA", "WETNESS",
                                                  "VENTO", "NEBBIA", "NUVOLE"};
            const std::string values[6] = {
                number("%.0f C", wx.ambientC), number("%.2f", wx.rain),
                number("%.2f", wx.wetness), number("%.1f", wx.windMs),
                number("%.2f", wx.fog), number("%.2f", wx.cloud)};
            for (int i = 0; i < 6; ++i) {
                const Rect& r = L.fieldMinus[i];
                const Rect& v = L.fieldValue[i];
                const Rect& plus = L.fieldPlus[i];
                m_text.draw(dl, L.controls.x + 14.f + static_cast<float>(i % 2) *
                                     ((L.controls.w - 24.f) * 0.5f + 8.f),
                            r.y + 7.f, kFieldLabels[i],
                            sty(m_weatherFieldHover == i ? Theme::accent(a)
                                                         : Theme::inkDim(0.86f * a),
                                0.64f, true));
                stepButton(r, "-", m_weatherFieldHover == i);
                stepButton(plus, "+", m_weatherFieldHover == i);
                m_text.draw(dl, v.x + 2.f, v.y + 5.f, values[i],
                            sty(Theme::ink(a), 0.72f, true));
            }
        } else {
            m_text.draw(dl, L.controls.x + 14.f, L.controls.y + 92.f,
                        "CIELO SERENO", sty(Theme::accent(a), 1.10f, true));
            m_text.draw(dl, L.controls.x + 14.f, L.controls.y + 114.f,
                        "PASSA A CUSTOM PER MODIFICARE",
                        sty(Theme::inkDim(0.86f * a), 0.66f));
            m_text.draw(dl, L.controls.x + 14.f, L.controls.y + 132.f,
                        "I VALORI RESTANO ASCIUTTI",
                        sty(Theme::inkDim(0.74f * a), 0.62f));
        }

        // The numbers above are already the session's; this lower half reads
        // them back as states instead of repeating another set of fields.
        const float summaryY = L.controls.y + 196.f;
        dl.addLine(L.controls.x + 14.f, summaryY, L.controls.x + L.controls.w - 14.f, summaryY,
                   Theme::hairline(a), 1.f);
        m_text.draw(dl, L.controls.x + 14.f, summaryY + 8.f, "RIEPILOGO SESSIONE",
                    sty(Theme::accent(a), 0.72f, true));
        const auto stateText = [&](const char* label, const std::string& value, float x,
                                   const Color& colour) {
            m_text.draw(dl, x, summaryY + 30.f, label, sty(Theme::inkDim(0.80f * a), 0.60f, true));
            m_text.draw(dl, x, summaryY + 44.f, value, sty(colour, 0.82f, true));
        };
        const float statW = (L.controls.w - 28.f) / 3.f;
        const std::string surface = wx.wetness > 0.60f ? "BAGNATA"
                                  : (wx.wetness > 0.08f ? "UMIDA" : "ASCIUTTA");
        const std::string precipitation = wx.rain > 0.60f ? "INTENSA"
                                        : (wx.rain > 0.05f ? "LEGGERA" : "ASSENTE");
        const std::string visibility = wx.fog > 0.60f ? "MINIMA"
                                     : (wx.fog > 0.25f ? "RIDOTTA" : "BUONA");
        stateText("SUPERFICIE", surface,
                  L.controls.x + 14.f,
                  wx.wetness > 0.08f ? Color::rgba(0.46f, 0.76f, 0.98f, a)
                                     : Color::rgba(0.50f, 0.88f, 0.62f, a));
        stateText("PRECIPITAZIONE", precipitation, L.controls.x + 14.f + statW,
                  wx.rain > 0.05f ? Color::rgba(0.46f, 0.76f, 0.98f, a) : Theme::ink(a));
        stateText("VISIBILITA", visibility, L.controls.x + 14.f + statW * 2.f,
                  wx.fog > 0.25f ? Color::rgba(0.95f, 0.58f, 0.42f, a) : Theme::ink(a));

        const auto bar = [&](const char* label, float value, float y) {
            const float left = L.controls.x + 72.f;
            const float width = L.controls.w - 118.f;
            m_text.draw(dl, L.controls.x + 14.f, y - 2.f, label,
                        sty(Theme::inkDim(0.78f * a), 0.58f, true));
            dl.addRectFilled({left, y, width, 8.f}, Color::rgba(1.f, 1.f, 1.f, 0.07f * a));
            dl.addRectFilled({left, y, width * std::clamp(value, 0.f, 1.f), 8.f},
                             Color::rgba(0.46f, 0.76f, 0.98f, a));
            char percent[8];
            std::snprintf(percent, sizeof(percent), "%d%%",
                          static_cast<int>(std::clamp(value, 0.f, 1.f) * 100.f));
            m_text.draw(dl, L.controls.x + L.controls.w - 38.f, y - 2.f, percent,
                        sty(Theme::ink(0.84f * a), 0.58f, true));
        };
        bar("PIOGGIA", wx.rain, summaryY + 68.f);
        bar("BAGNATO", wx.wetness, summaryY + 86.f);
        bar("NUVOLE", wx.cloud, summaryY + 104.f);

        if (!wx.note.empty() && (mode == WeatherSource::Today || mode == WeatherSource::RealDate)) {
            dl.addTextWrapped(L.controls.x + 14.f, L.controls.y + 148.f, wx.note,
                              Color::rgba(0.95f, 0.58f, 0.42f, a), 0.62f,
                              L.controls.w - 28.f);
        }
        m_text.draw(dl, L.controls.x + 14.f, L.controls.y + L.controls.h - 22.f,
                    "QUESTI VALORI SONO CONSEGNATI ALLA SESSIONE",
                    sty(Theme::inkDim(0.62f * a), 0.56f));
    }

    void buildStepPage(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        if (a <= 0.01f) return;
        const StepPage p = stepPage(w, h);
        const int step = stepIndex(m_menu->state());
        static const char* kStepLabels[StepPage::kSteps] = {"CARS", "TRACKS", "WEATHER",
                                                           "SESSION"};
        static const char* kStepTitles[StepPage::kSteps] = {"SELECT CAR", "SELECT TRACK",
                                                            "WEATHER", "SESSION"};
        const auto& items = m_menu->items();
        const std::vector<int> rows = stepRows();
        const int sel = m_menu->selectedIndex();
        const int selSlot = stepSlot(rows, sel);

        // The room is behind this page: shaded, not hidden, so the loaded car
        // stays part of the screen while the choice is read. The page still
        // wears the theme's edges: the red rule down the left and the diagonal
        // streaks out of the bottom-left corner.
        dl.addRectFilled({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)},
                         Color::rgba(0.01f, 0.01f, 0.02f, 0.42f * a));
        for (int s = 0; s < 4; ++s) {
            const float off = static_cast<float>(s) * 34.f;
            dl.addLine(-60.f + off, static_cast<float>(h), 300.f + off,
                       static_cast<float>(h) - 420.f,
                       Color::rgba(0.52f, 0.03f, 0.03f, (0.28f - 0.05f * s) * a), 16.f);
        }
        // The choice list gets its own panel on the right, and the bar at the
        // bottom of the page, so the two text columns hold their contrast over
        // the hall instead of the hall wearing a black sheet.
        // The list's panel wears the menu backdrop: the crimson ridge, a
        // little translucent, so the hall still shows underneath the rows.
        const Rect listPanel{p.list.x - 18.f, p.list.y - 26.f, p.list.w + 36.f,
                             p.list.h + 52.f};
        if (step > 3) drawMenuBackdrop(dl, listPanel, 0.40f * a, 5u);
        dl.addRectFilled({0.f, static_cast<float>(h) - 108.f, static_cast<float>(w), 108.f},
                         Color::rgba(0.01f, 0.01f, 0.02f, 0.70f * a));
        dl.addRectFilled({0.f, 0.f, 4.f, static_cast<float>(h)}, Theme::accent(0.9f * a));
        drawHeader(dl, w, h);

        for (int i = 0; i < StepPage::kSteps; ++i) {
            // In the content browser (not the single-player walk) only the page
            // you are on is relevant: TRACKS and WEATHER are not part of it.
            if (i > step && !m_menu->inSetupFlow()) continue;
            const Rect& t = p.tab[i];
            const bool current = (i == step);
            const bool done = (i < step);
            // The step tabs are text over a rule, not blocks: the step you are
            // on is the accent, the ones behind it are plain ink, the rest stay
            // quiet until the walk reaches them.
            char number[4];
            std::snprintf(number, sizeof(number), "%02d", i + 1);
            m_text.draw(dl, t.x + 10.f, t.y + 7.f, number,
                        sty(current ? Theme::accent(a) : Theme::inkDim(0.80f * a),
                            0.85f, true));
            m_text.draw(dl, t.x + 42.f, t.y + 6.f, kStepLabels[i],
                        sty(current ? Theme::accent(a)
                                    : (done ? Theme::ink(a) : Theme::inkDim(0.72f * a)),
                            1.f, current));
            if (current)
                dl.addRectFilled({t.x + 10.f, t.y + t.h - 2.f, t.w - 20.f, 2.f},
                                 Theme::accent(a));
        }

        if (step == 0) {
            buildCarPage(dl, w, h, a);
        } else if (step == 1) {
            buildTrackPage(dl, w, h, a);
        } else if (step == 2) {
            buildWeatherPage(dl, w, h, a);
        } else if (step == 3) {
            buildEventPage(dl, w, h, a);
        } else {
        // Left card: what the step has chosen so far, named large.
        drawMenuBackdrop(dl, p.card, 0.42f * a, 3u);
        dl.addRect(p.card, Theme::hairline(a), 1.f);
        dl.addRectFilled({p.card.x, p.card.y + 26.f, 3.f, 120.f}, Theme::accent(0.85f * a));
        m_text.draw(dl, p.card.x + 24.f, p.card.y + 22.f, kStepTitles[step],
                    sty(Theme::accent(a), 1.f, true));

        if (step == 1) {
            buildTrackCard(dl, p, a);
        } else if (step == 2) {
            buildWeatherCard(dl, p, a);
        } else {
            const MenuItem* selItem =
                (sel >= 0 && sel < static_cast<int>(items.size())) ? &items[sel] : nullptr;
            // A list row names itself; the filters above it leave the card to
            // the choice already loaded behind the page.
            const bool onAListRow = selItem != nullptr && sel >= m_menu->firstContentRow();
            std::string headline;
            std::string detail;
            if (step == 0) {
                headline = onAListRow ? selItem->text
                                      : (m_menu->carName().empty() ? "NO CAR SELECTED"
                                                                   : m_menu->carName());
                detail = selItem ? selItem->description : std::string();
            } else {
                headline = selItem ? selItem->text : "EVENT";
                detail = selItem ? selItem->description : std::string();
            }
            const float headScale = 2.2f;
            m_text.draw(dl, p.card.x + 24.f, p.card.y + 70.f,
                        elideToWidth(headline, headScale, p.card.w - 48.f),
                        sty(Theme::ink(a), headScale, true));
            if (!detail.empty())
                dl.addTextWrapped(p.card.x + 24.f, p.card.y + 120.f, detail,
                                  Theme::ink(0.84f * a), 1.15f, p.card.w - 48.f);
        }

        // The event page also says what the walk already picked.
        if (step == 3) {
            char car[128];
            std::snprintf(car, sizeof(car), "CAR    %s",
                          m_menu->carName().empty() ? "none" : m_menu->carName().c_str());
            char track[160];
            std::snprintf(track, sizeof(track), "TRACK  %s",
                          m_menu->trackName().empty() ? "none" : m_menu->trackName().c_str());
            m_text.draw(dl, p.card.x + 24.f, p.card.y + p.card.h - 84.f, car,
                        sty(Theme::inkDim(a), 1.f));
            m_text.draw(dl, p.card.x + 24.f, p.card.y + p.card.h - 60.f,
                        elideToWidth(track, 1.f, p.card.w - 48.f),
                        sty(Theme::inkDim(a), 1.f));
        }

        // Right list: the choices of this step, scrolled to the selection.
        const int first = stepFirst(rows, sel, p.visible);
        for (int s = first; s < static_cast<int>(rows.size()) && s < first + p.visible; ++s) {
            const int row = rows[s];
            const MenuItem& item = items[row];
            const float y = p.list.y + 6.f + static_cast<float>(s - first) * p.rowH;
            const Rect rr{p.list.x, y, p.list.w, p.rowH - 6.f};
            const bool selected = (row == sel);
            const bool hover = (row == m_menu->hoverIndex());
            // The same nameplate the list pages draw: the plate under the
            // selected row, the dark tab past the bevel, the accent on the
            // outline of the row that has the cursor.
            drawMenuRowShape(dl, rr,
                             selected ? Theme::plate(a)
                                      : (hover ? Theme::hover(a) : Color::rgba(0.f, 0.f, 0.f, 0.f)),
                             Theme::surface((selected ? 0.97f : 0.92f) * a),
                             selected ? Theme::accent(a) : Theme::hairline(a));
            const Color tint = !item.enabled
                ? Theme::inkDim(0.55f * a)
                : (selected ? Theme::onPlate(a) : Theme::ink(0.88f * a));
            const float labelScale = selected ? 1.25f : 1.15f;
            // A selected row wears the light plate with dark ink: a shadow
            // under it would eat the contrast it already has.
            const float veilA = selected ? 0.f : a;
            // A row that carries a value ("CLASS  any") splits the way the list
            // pages do: the name in ink, the value in the accent.
            const size_t split = item.enabled ? item.text.find("  ") : std::string::npos;
            const MenuRowGeometry rowGeo = menuRowGeometry(rr);
            const float nameMaxW = std::max(0.f, rowGeo.splitMidX - (rr.x + 18.f) - 10.f);
            const float valueX = rowGeo.splitMidX + 8.f;
            const float valueMaxW =
                std::max(0.f, rr.x + rr.w - 16.f - valueX - (item.icon ? 30.f : 0.f));
            if (split != std::string::npos) {
                const std::string name =
                    elideToWidth(item.text.substr(0, split), labelScale, nameMaxW);
                const std::string value =
                    elideToWidth(item.text.substr(split + 2), labelScale, valueMaxW);
                drawMenuLabelVeil(dl, {rr.x + 18.f, rr.y + 9.f,
                                       m_text.measureWidth(name, labelScale), 16.f}, veilA);
                drawMenuLabelVeil(dl, {valueX, rr.y + 9.f,
                                       m_text.measureWidth(value, labelScale), 16.f}, veilA);
                m_text.draw(dl, rr.x + 18.f, rr.y + 9.f, name,
                            sty(tint, labelScale, selected, !selected));
                m_text.draw(dl, valueX, rr.y + 9.f, value,
                            sty(Theme::accent(0.92f * a), labelScale, selected, !selected));
            } else {
                const std::string name = elideToWidth(item.text, labelScale, nameMaxW);
                drawMenuLabelVeil(dl, {rr.x + 18.f, rr.y + 9.f,
                                       m_text.measureWidth(name, labelScale), 16.f}, veilA);
                m_text.draw(dl, rr.x + 18.f, rr.y + 9.f, name,
                            sty(tint, labelScale, selected, !selected));
            }
            if (item.icon != nullptr) {
                if (const SvgIcon* icon = icons::get(item.icon)) {
                    const float size = 18.f;
                    icon->draw(dl, rr.x + rr.w - size - 16.f, rr.y + (rr.h - size) * 0.5f,
                               size, Color::rgba(0.75f, 0.75f, 0.80f, 0.5f * a), 1.f);
                }
            }
        }
        if (static_cast<int>(rows.size()) > p.visible) {
            const bool above = first > 0;
            const bool below = first + p.visible < static_cast<int>(rows.size());
            const char* more = above && below ? "MORE ABOVE AND BELOW"
                                              : (above ? "MORE ABOVE" : "MORE BELOW");
            m_text.draw(dl, p.list.x, p.list.y + p.list.h + 6.f, more,
                        sty(Theme::inkDim(a), 0.85f));
        }
        }

        // Bottom bar: BACK and NEXT, the two moves the walk has.
        const bool canNext = sel >= 0 && sel < static_cast<int>(items.size()) &&
                             items[sel].enabled && !items[sel].isSeparator;
        const bool backHot = m_stepBackHover;
        const bool nextHot = m_stepNextHover && canNext;
        // Two square blocks: the quiet one outlined, the move forward in the
        // accent, which is how the launcher paints its primary action.
        const char* backLabel = (step == 3) ? "BACK TO MAIN" : "BACK";
        dl.addRectFilled(p.back, backHot ? Theme::hover(2.4f * a) : Theme::surface(0.94f * a));
        dl.addRect(p.back, Theme::hairline(a), 1.f);
        m_text.draw(dl, p.back.x + 22.f, p.back.y + 13.f, backLabel,
                    sty(Theme::ink(a), 1.05f, backHot));
        dl.addRectFilled(p.next, Theme::accent((canNext ? (nextHot ? 1.f : 0.85f) : 0.22f) * a));
        m_text.draw(dl, p.next.x + 26.f, p.next.y + 13.f,
                    step == 3 ? "CONFIGURE" : "NEXT",
                    sty(Color::rgba(1.f, 1.f, 1.f, canNext ? a : 0.4f * a), 1.05f, true));
        const char* hints = "UP/DOWN  browse     ENTER  choose     ESC  back";
        m_text.draw(dl, p.back.x + p.back.w + 28.f, p.back.y + 14.f, hints,
                    sty(Color::rgba(0.55f, 0.55f, 0.60f, a), 0.9f));
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

    /** The start-up splash: the wordmark, the accent rule under it and a
     *  progress hairline that fills while the first menu frame settles. Times
     *  itself off the wall clock, so the loop needs no tick for it, and it is
     *  over in well under a second - a splash that delays the menu is a bug,
     *  not a feature. Returns true while it is still on screen. */
    bool buildSplash(DrawList& dl, int w, int h) {
        const auto now = std::chrono::steady_clock::now();
        if (!m_splashStarted) {
            m_splashStarted = true;
            m_splashStart = now;
        }
        constexpr float kHold = 0.60f;
        constexpr float kFade = 0.25f;
        const float t = std::chrono::duration<float>(now - m_splashStart).count();
        if (t >= kHold + kFade) {
            m_splashDone = true;
            return false;
        }
        const float alpha = t < kHold ? 1.f : 1.f - (t - kHold) / kFade;
        drawWelcomeMark(dl, static_cast<float>(w), static_cast<float>(h), alpha,
                        std::clamp(t / kHold, 0.f, 1.f), std::string());
        return true;
    }

    /** The welcome mark itself, shared by the start-up splash and the loading
     *  cover: the wordmark on the menu's own backdrop, the crimson accent rule
     *  under it and a hairline that reads as progress. `progress` >= 0 fills
     *  the hairline to that fraction (the splash); a negative value draws an
     *  indeterminate sweep (the loading cover). `subtitle` is optional. */
    void drawWelcomeMark(DrawList& dl, float W, float H, float alpha, float progress,
                         const std::string& subtitle) {
        if (alpha <= 0.01f) return;
        // The backdrop the menus wear, full screen, under a veil: at splash size
        // the ridge is nearly hiding, so one more layer keeps the wordmark the
        // only thing on the screen.
        drawMenuBackdrop(dl, {0.f, 0.f, W, H}, alpha, 3u);
        dl.addRectFilled({0.f, 0.f, W, H}, Color::rgba(0.01f, 0.01f, 0.02f, 0.52f * alpha));

        // The same diagonal streaks the list pages carry, so the splash and the
        // menu behind it read as one product.
        for (int st = 0; st < 3; ++st) {
            const float off = static_cast<float>(st) * 34.f;
            dl.addLine(-60.f + off, H, 260.f + off, H - 360.f,
                       Color::rgba(0.52f, 0.03f, 0.03f, (0.26f - 0.05f * st) * alpha), 14.f);
        }

        // The name, spelled in Cyrillic as real text: the atlas carries the
        // 0400..04FF block whenever the loaded face has it (Segoe UI does), so
        // no hand-drawn letters are needed here any more.
        static const char* kName =
            "\xD0\xBA\xD0\xB8\xD0\xBD\xD0\xB5\xD1\x82\xD0\xB8\xD0\xBA\xD0\xB0";
        const float scale = std::clamp(H / 240.f, 1.6f, 4.8f);
        const float nameW = m_text.measureWidth(kName, scale);
        const float nameX = (W - nameW) * 0.5f;
        const float nameY = H * 0.44f;
        const float nameH = m_text.atlas().baseLineHeight() * scale;
        drawMenuLabelVeil(dl, {nameX, nameY, nameW, nameH * 0.75f}, alpha);
        TextStyle nameStyle = sty(Color::rgba(1.f, 1.f, 1.f, alpha), scale, true);
        // Bold: the wordmark is the brand, so it carries a thin outline even
        // though the rest of the UI dropped outlines for crispness.
        nameStyle.outline = true;
        nameStyle.outlineColor = Color::rgba(1.f, 1.f, 1.f, alpha);
        nameStyle.outlinePx = 1.f;
        m_text.draw(dl, nameX, nameY, kName, nameStyle);

        // The crimson rule and the hairline that fills: the rule is the accent
        // every screen already uses, the fill is the only progress the screen
        // claims.
        const float ruleW = std::min(nameW, W * 0.6f);
        const float ruleX = (W - ruleW) * 0.5f;
        const float ruleY = nameY + nameH + 14.f;
        dl.addRectFilled({ruleX, ruleY, ruleW, 3.f}, Theme::accent(0.9f * alpha));
        dl.addRectFilled({ruleX, ruleY + 2.f, ruleW, 1.f},
                         Color::rgba(1.f, 1.f, 1.f, 0.10f * alpha));
        if (progress >= 0.f) {
            const float fill = std::clamp(progress, 0.f, 1.f);
            dl.addRectFilled({ruleX, ruleY + 2.f, ruleW * fill, 1.f},
                             Color::rgba(1.f, 1.f, 1.f, 0.55f * alpha));
        } else {
            // Indeterminate: a bright segment sweeps the rule while the load
            // has no progress to report. It freezes during the blocking load,
            // which is exactly the moment the cover is doing its job.
            const float t = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - m_loadingStart).count();
            const float phase = std::fmod(t * 0.6f, 1.f);
            const float segW = std::max(24.f, ruleW * 0.22f);
            const float segX = ruleX + (ruleW - segW) * phase;
            dl.addRectFilled({segX, ruleY + 2.f, segW, 1.f},
                             Color::rgba(1.f, 1.f, 1.f, 0.55f * alpha));
        }

        if (!subtitle.empty()) {
            const float subScale = std::clamp(H / 720.f, 0.9f, 1.3f);
            const float subW = m_text.measureWidth(subtitle, subScale);
            const float subX = (W - subW) * 0.5f;
            const float subY = ruleY + 16.f;
            drawMenuLabelVeil(dl, {subX, subY, subW, 12.f}, alpha);
            m_text.draw(dl, subX, subY, subtitle,
                        sty(Color::rgba(0.80f, 0.84f, 0.90f, alpha), subScale));
        }
    }

    /** The loading cover: the welcome mark with an indeterminate sweep, held on
     *  screen from beginLoading() to endLoading(). Drawn last, over every other
     *  layer. */
    void buildLoadingScreen(DrawList& dl, int w, int h) {
        if (!m_loading) return;
        const float t = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - m_loadingStart).count();
        // A fast load still gets a readable cover: the end request is honoured
        // only after a short minimum, so the mark does not flash for one frame.
        constexpr float kMinHold = 0.45f;
        if (m_loadingEndRequested && t >= kMinHold) {
            m_loading = false;
            m_loadingEndRequested = false;
            return;
        }
        const float alpha = std::clamp(t / 0.12f, 0.f, 1.f);
        const std::string label = m_loadingLabel.empty()
            ? (m_locale != nullptr ? m_locale->tr("loading.default", "LOADING")
                                   : std::string("LOADING"))
            : m_loadingLabel;
        drawWelcomeMark(dl, static_cast<float>(w), static_cast<float>(h), alpha, -1.f, label);
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
            // Square cards: the launcher's tiles have no rounded corners — the
            // artwork and the labels do the work.
            const float radius = 0.f;
            // A tile is a window on the same backdrop, at its own phase, so no
            // two tiles wear the same slice of the ridge.
            drawMenuBackdrop(dl, r, (selected ? 0.56f : (hover ? 0.48f : 0.40f)) * a,
                             31u + static_cast<unsigned>(i) * 7u);
            // A tile is glass at 0.40, and the showroom's lit ceiling runs
            // straight under the labels: the same soft shadow the rows and the
            // bar wear keeps the caption off it.
            const float labelX = r.x + 20.f;
            const float labelY = r.y + r.h - 38.f;
            const float labelW = m_text.measureWidth(item.text, 1.15f);
            drawMenuLabelVeil(dl, {labelX, labelY, labelW, 16.f}, a);
            if (const SvgIcon* icon = icons::get(item.icon)) {
                const float size = std::min(r.w, r.h) * 0.46f;
                icon->draw(dl, r.x + r.w - size - 22.f,
                           r.y + (r.h - size) * 0.5f - 8.f, size,
                           Color::rgba(1.f, 1.f, 1.f,
                                       (selected ? 0.15f : (hover ? 0.11f : 0.075f)) * a),
                           1.f);
            }
            m_text.draw(dl, labelX, labelY, item.text,
                        sty(selected ? Color::rgba(1.f, 1.f, 1.f, a)
                                     : Color::rgba(0.82f, 0.82f, 0.86f, a),
                            1.15f, selected));
            if (selected) {
                dl.addRectFilled({r.x + 20.f, r.y + r.h - 16.f, 44.f, 3.f}, Theme::accent(a));
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
        const char* escLabel = (m_menu->state() == MenuState::EventSelect) ? "BACK" : "EXIT";
        const float afterEsc = keyHint(dl, L.margin, L.bottomBar, "ESC", escLabel, a);
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
        dl.addRectFilled(panel, Theme::surface(0.96f * a));
        dl.addRect(panel, Theme::hairline(a), 1.f);
        dl.addRectFilled({panel.x, panel.y, 3.f, 34.f}, Theme::accent(a));
        m_text.draw(dl, panel.x + 16.f, panel.y + 14.f, "NOTIFICATIONS",
                    sty(Theme::accent(a), 0.95f, true));
        if (m_notices.empty()) {
            m_text.draw(dl, panel.x + 16.f, panel.y + 40.f, "Nothing to report",
                        sty(Theme::inkDim(a), 1.f));
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
            m_text.draw(dl, panel.x + 30.f, y, notice.text, sty(Theme::ink(0.9f * a), 1.f));
            y += 30.f;
        }
    }

    /** The session card's geometry: hung under the clipboard that opens it,
     *  one row per detail the menu reports. */
    SessionPlannerLayout plannerLayout(int w, int h) const {
        const MainMenuLayout bar = mainLayout(w, h);
        return layoutSessionPlanner(w, h, bar.quick[MainMenuLayout::kPlannerSlot],
                                    static_cast<int>(m_menu->sessionPlanRows().size()));
    }

    /** The session planner: what the running session was built from, each row
     *  a click away from its next value. Only while a session exists — the
     *  clipboard in the bar stays dim until then. */
    void buildSessionPlanner(DrawList& dl, int w, int h) {
        if (!m_menu->sessionPlannerOpen() || !m_menu->isVisible()) return;
        const float a = m_menu->fadeAlpha() > 0.01f ? m_menu->fadeAlpha() : 1.f;
        const SessionPlannerLayout S = plannerLayout(w, h);
        const std::vector<GameMenuOverlay::SessionPlanRow> plan = m_menu->sessionPlanRows();
        dl.addRectFilled(S.card, Theme::surface(0.96f * a));
        dl.addRect(S.card, Theme::hairline(a), 1.f);
        dl.addRectFilled({S.card.x, S.card.y, 3.f, 34.f}, Theme::accent(a));
        m_text.draw(dl, S.card.x + 16.f, S.card.y + 12.f, "SESSION PLANNER",
                    sty(Theme::accent(a), 0.95f, true));
        dl.addLine(S.card.x + 16.f, S.card.y + 38.f, S.card.x + S.card.w - 16.f,
                   S.card.y + 38.f, Theme::hairline(a), 1.f);
        for (int i = 0; i < S.rows && i < static_cast<int>(plan.size()); ++i) {
            const Rect& r = S.row[i];
            if (i == m_plannerHover) dl.addRectFilled(r, Theme::hover(a));
            m_text.draw(dl, r.x + 8.f, r.y + r.h * 0.30f, plan[i].label,
                        sty(Theme::ink(0.9f * a), 0.92f));
            // The name and the value keep the two colours of the list pages:
            // the row reads as "what" and "what it is set to".
            const float valueW = m_text.measureWidth(plan[i].value, 1.02f);
            m_text.draw(dl, r.x + r.w - valueW - 8.f, r.y + r.h * 0.27f, plan[i].value,
                        sty(Theme::accent(a), 1.02f, true));
        }
        const char* fallback = "click a row to change it, ESC closes";
        const std::string hint = (m_plannerHover >= 0 &&
                                  m_plannerHover < static_cast<int>(plan.size()))
            ? plan[m_plannerHover].hint : fallback;
        m_text.draw(dl, S.card.x + 16.f, S.card.y + S.card.h - 24.f, hint,
                    sty(Theme::inkDim(a), 0.92f));
    }

    /** The bar every screen wears: navigation and the section's name on the
     *  left, the driver's plate and the quick actions on the right, the clock
     *  closing it. Painted from the layout that hit-tests it, so a button the
     *  player sees is a button they can press. */
    /** A vertical gradient between two RGBA colours, painted as a few bands so
     *  it costs one draw per band instead of a new primitive. */
    static void fillVerticalGradient(DrawList& dl, const Rect& r, const float top[4],
                                     const float bottom[4], float alpha, int bands = 32) {
        const int n = std::clamp(bands, 1, 64);
        const float bandH = r.h / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
            const auto mix = [t](const float a[4], const float b[4], int c) {
                return a[c] + (b[c] - a[c]) * t;
            };
            dl.addRectFilled({r.x, r.y + bandH * static_cast<float>(i), r.w, bandH + 0.5f},
                             Color::rgba(mix(top, bottom, 0), mix(top, bottom, 1),
                                         mix(top, bottom, 2),
                                         alpha * mix(top, bottom, 3)));
        }
    }

    void drawHeader(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        const MainMenuLayout L = mainLayout(w, h);
        // The bar is the backdrop in miniature: a thin slice of the same sky,
        // almost opaque, so the icons keep their contrast over the hall. A theme
        // that names an image for the bar draws it here; a theme that names
        // only colours paints the gradient. Neither: the procedural slice.
        if (themeHeaderImage >= 0)
            dl.addImage(L.topBar, themeHeaderImage,
                        Color::rgba(1.f, 1.f, 1.f, a * themeHeaderImageAlpha));
        else if (themeHeader.used)
            fillVerticalGradient(dl, L.topBar, themeHeader.top, themeHeader.bottom, a);
        else
            drawMenuBackdrop(dl, L.topBar, 0.62f * a, 19u);
        dl.addLine(0.f, L.topBar.h, static_cast<float>(w), L.topBar.h,
                   Theme::hairline(a), 1.f);

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
        // The section's name wears a red tick on its left, the same marker the
        // selected row carries down in the list.
        drawMenuLabelVeil(dl, {L.home.x, L.home.y, m_text.measureWidth(label, 1.f), 10.f}, a);
        dl.addRectFilled({L.home.x - 12.f, L.home.y, 3.f, 14.f}, Theme::accent(a));
        m_text.draw(dl, L.home.x, L.home.y, label, sty(Theme::ink(a), 1.f, true));

        // The driver's plate: the name and an avatar carrying the initial,
        // which is all the profile art there is.
        const std::string driver = m_menu->profile().name;
        if (!driver.empty()) {
            char initial = driver[0];
            if (initial >= 'a' && initial <= 'z') initial = static_cast<char>(initial - 32);
            const float nameScale = 1.05f;
            const float nameW = m_text.measureWidth(driver, nameScale);
            const float nameX = L.avatar.x - nameW - 16.f;
            drawMenuLabelVeil(dl, {nameX, L.avatar.y + L.avatar.h * 0.34f, nameW, 11.f}, a);
            m_text.draw(dl, nameX, L.avatar.y + L.avatar.h * 0.34f, driver,
                        sty(Color::rgba(0.96f, 0.96f, 0.97f, a), nameScale));
            // The driver's profile carries a country code ("IT"); the flag
            // before the name is that country, when it has one to draw.
            drawCountryFlag(dl, {nameX - 36.f, L.avatar.y + L.avatar.h * 0.5f - 9.f, 24.f, 18.f},
                            m_menu->profile().nationality, a);
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
            "settings", "speaker", "vr", "pad", "planner", "bell"};
        for (int i = 0; i < MainMenuLayout::kQuickCount; ++i) {
            const Rect& r = L.quick[i];
            const bool hot = (i == m_quickHover);
            if (hot) dl.addRectFilled(r, Color::rgba(1.f, 1.f, 1.f, 0.14f * a));
            // The bell reads lit while its list is open, the clipboard while
            // its card is down; without a session the clipboard has nothing to
            // open, so it stays dim and a press passes it by.
            const bool active = ((i == MainMenuLayout::kClockSlot) && m_notices.open()) ||
                                ((i == MainMenuLayout::kPlannerSlot) && m_menu->sessionPlannerOpen()) ||
                                ((i == MainMenuLayout::kSettingsSlot) &&
                                 m_menu->state() == MenuState::Settings);
            const bool idle = (i == MainMenuLayout::kPlannerSlot) && !m_menu->sessionActive();
            const Color tint = idle ? Color::rgba(0.46f, 0.46f, 0.50f, a)
                                    : ((hot || active) ? Color::rgba(1.f, 1.f, 1.f, a)
                                                       : Color::rgba(0.74f, 0.74f, 0.78f, a));
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
        drawMenuLabelVeil(dl, {clock.x - tw - 10.f, clock.y + clock.h * 0.22f, tw, 10.f}, a);
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
        // Over the session the bar is a header, not a trail: the moves would
        // leave a race the player only paused, so all four read dim.
        if (m_menu->state() == MenuState::Pause) return false;
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

    /** The bar's icons, left to right: the settings gear, sound, the VR screen,
     *  the controller, the session planner, then the bell, which opens the list
     *  of what just happened. */
    void activateQuick(int index) {
        // The gear is not one panel: it opens the settings page itself, the
        // screen behind the audio shortcut, so it is worth a press on any page.
        if (index == MainMenuLayout::kSettingsSlot) {
            m_menu->openScreen(MenuState::Settings);
            return;
        }
        if (index == MainMenuLayout::kSoundSlot) {
            if (m_menu->onOpenSettingsPanelRequested)
                m_menu->onOpenSettingsPanelRequested("audio");
            return;
        }
        if (index == MainMenuLayout::kVrSlot) {
            if (m_menu->onToggleFullscreenRequested) m_menu->onToggleFullscreenRequested();
            return;
        }
        if (index == MainMenuLayout::kPadSlot) {
            m_menu->openScreen(MenuState::Controls);
            return;
        }
        if (index == MainMenuLayout::kPlannerSlot) {
            if (m_menu->sessionActive()) m_menu->toggleSessionPlanner();
            return;
        }
        m_notices.toggle(); // the bell: open or close the notice list
    }

    void updateMenuHover(float mx, float my, int w, int h) {
        m_quickHover = -1;
        m_navHover = -1;
        m_plannerHover = -1;
        m_stepBackHover = false;
        m_stepNextHover = false;
        // The session card hangs over everything the bar opens: its rows are
        // what the cursor is on while it is down.
        if (m_menu->sessionPlannerOpen()) {
            m_plannerHover = sessionPlannerRowAt(plannerLayout(w, h), mx, my);
            if (m_plannerHover >= 0) {
                m_menu->setHoverIndex(-1);
                return;
            }
        }
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
        // screen that wears it — the bell used to answer only on the tiles,
        // and the pause bar now wears the header too, so a painted glyph is a
        // pressable one there as well.
        {
            const int quick = quickIconAt(mx, my, w, h);
            if (quick >= 0) {
                m_quickHover = quick;
                m_menu->setHoverIndex(-1); // the bar is not a tile
                return;
            }
        }
        if (m_menu->state() == MenuState::Main || m_menu->state() == MenuState::EventSelect) {
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

        if (isStepPage(m_menu->state())) {
            const StepPage sp = stepPage(w, h);
            m_stepBackHover = sp.back.contains(mx, my);
            m_stepNextHover = sp.next.contains(mx, my);
            if (m_stepBackHover || m_stepNextHover) {
                m_menu->setHoverIndex(-1);
                return;
            }
            if (stepIndex(m_menu->state()) == 0) {
                updateCarPageHover(mx, my);
                m_menu->setHoverIndex(-1);
                return;
            }
            if (stepIndex(m_menu->state()) == 1) {
                updateTrackPageHover(mx, my);
                m_menu->setHoverIndex(-1);
                return;
            }
            if (stepIndex(m_menu->state()) == 2) {
                updateWeatherPageHover(mx, my);
                m_menu->setHoverIndex(-1);
                return;
            }
            if (stepIndex(m_menu->state()) == 3) {
                updateEventPageHover(mx, my);
                m_menu->setHoverIndex(-1);
                return;
            }
            const int row = stepRowAt(sp, mx, my);
            m_menu->setHoverIndex(row);
            if (row >= 0) m_menu->setSelectedIndex(row);
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

    /** A label shortened with an ellipsis when it does not fit `maxW`, so a
     *  long driver, car or track name cannot run out of its card. */
    std::string elideToWidth(const std::string& text, float scale, float maxW) const {
        if (maxW <= 0.f || m_text.measureWidth(text, scale) <= maxW) return text;
        std::string cut = text;
        while (!cut.empty() && m_text.measureWidth(cut + "...", scale) > maxW) cut.pop_back();
        return cut.empty() ? std::string() : cut + "...";
    }

    void buildCinematicMenu(DrawList& dl, int w, int h) {
        const float a = m_menu->fadeAlpha();
        if (a <= 0.01f) return;

        // Start-up splash (tracked as home.splash): the wordmark over the same
        // crimson ridge the menus wear, once, on the first frame of the menu.
        // It owns the screen while it lasts and then never comes back.
        if (!m_splashDone && m_menu->state() == MenuState::Main && buildSplash(dl, w, h)) return;

        // The page ground the theme names, drawn once for every page below it:
        // the step walk, the pause screen and the tile row all sit on it.
        if (themePageImage >= 0)
            dl.addImage({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)}, themePageImage,
                        Color::rgba(1.f, 1.f, 1.f, a * themePageImageAlpha));

        // The car / track / event walk wears the session-screen layout.
        if (isStepPage(m_menu->state())) {
            buildStepPage(dl, w, h);
            return;
        }

        // The in-car overlay is a horizontal bar, not the vertical list the
        // pre-session screens use: it sits over a view the player still wants
        // to see, and it is navigated left/right.
        if (m_menu->state() == MenuState::Pause) {
            buildPauseBar(dl, w, h);
            // ESC in the car brings the bar the rest of the game wears: the
            // driver's plate, the quick actions and the clock, over the pause
            // bar. The settings gear and the audio switch sit here too, so a
            // change mid-session costs a click instead of a screen change.
            drawHeader(dl, w, h);
            return;
        }
        // The arrival screen is the tile row, not the list: it is the first
        // thing the player sees and it has to read as one.
        if (m_menu->state() == MenuState::Main || m_menu->state() == MenuState::EventSelect) {
            buildMainTiles(dl, w, h);
            return;
        }

        const float t = m_menu->transitionProgress();
        const float slide = (1.f - t) * 40.f;

        // RaceRoom's page is a near-black flat, and its accent lives at the
        // edges: a thin red rule down the left and the diagonal streaks that
        // fan out of the bottom-left corner on every one of its screens.
        // Only a wash over the scene: the showroom is the picture, and a black
        // sheet over it turned the car into a silhouette and the hall into flat
        // grey. The column carrying the labels gets its own panel below, so the
        // text keeps its contrast without costing the image its light.
        dl.addRectFilled({0, 0, (float)w, (float)h}, Color::rgba(0.01f, 0.01f, 0.02f, 0.30f * a));
        for (int s = 0; s < 4; ++s) {
            const float off = static_cast<float>(s) * 34.f;
            dl.addLine(-60.f + off, static_cast<float>(h), 300.f + off,
                       static_cast<float>(h) - 420.f,
                       Color::rgba(0.52f, 0.03f, 0.03f, (0.30f - 0.05f * s) * a), 16.f);
        }
        // The bottom band keeps the footer and the context line readable where
        // the brightest part of the floor sits under them.
        dl.addRectFilled({0.f, static_cast<float>(h) - 110.f, static_cast<float>(w), 110.f},
                         Color::rgba(0.01f, 0.01f, 0.02f, 0.72f * a));
        dl.addRectFilled({0, 0, 4.f, (float)h}, Theme::accent(0.9f * a));

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
        // The label column: a hard-edged panel, the launcher's own way of
        // seating text over a picture. It ends where the list ends, so the car
        // in the hall stays uncovered.
        {
            const float panelTop = L.top - 44.f;
            const float panelBottom = static_cast<float>(h) - 40.f;
            const float panelW = L.left + L.listW + 70.f;
            drawMenuBackdrop(dl, {0.f, panelTop, panelW, panelBottom - panelTop},
                             0.40f * a, 11u);
            dl.addRectFilled({panelW, panelTop, 1.f, panelBottom - panelTop},
                             Theme::accent(0.45f * a));
        }
        L.left += slide;
        const auto& items = m_menu->items();
        const int sel = m_menu->selectedIndex();
        // The bar and the context line above it own the bottom of the screen: a
        // page longer than that window scrolls inside it instead of running
        // through them. Without this the last rows were painted off screen —
        // SINGLE PLAYER lost BACK entirely and the context line sat on top of
        // RESULTS.
        const float bottomLimit = static_cast<float>(h) - 96.f;
        const auto rowAdvance = [&L](const MenuItem& row) {
            return row.isSeparator ? L.rowH * 0.45f : L.rowH;
        };
        float contentH = 0.f;
        for (const auto& row : items) contentH += rowAdvance(row);
        // The caption band under the list is part of the window, not something
        // painted over it: rows stop above it, so "MORE BELOW" never lands on
        // the last visible label.
        const float rowLimit = bottomLimit - 26.f;
        const float windowH = rowLimit - L.top;
        float skip = 0.f;
        if (contentH > windowH && !items.empty()) {
            const int last = static_cast<int>(items.size()) - 1;
            const int selection = std::clamp(sel, 0, last);
            float above = 0.f;
            for (int i = 0; i < selection; ++i) above += rowAdvance(items[i]);
            // Anchor the selection's bottom to the window's bottom, then never
            // scroll past its own top: whichever way the row was reached, it
            // ends up visible.
            skip = std::clamp(above + rowAdvance(items[selection]) - windowH,
                              0.f, contentH - windowH);
            skip = std::min(skip, above);
        }
        float iy = L.top - skip;
        std::string desc;

        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const auto& it = items[i];
            const float advance = rowAdvance(it);
            if (iy + advance <= L.top) { iy += advance; continue; } // scrolled above
            // A row is only drawn when all of it fits: stopping at the row's
            // top let the last one hang over the caption band, which is how
            // MASTER VOLUME ended up under "v MORE BELOW".
            if (iy + advance > rowLimit + 0.5f) break;              // scrolled below
            if (it.isSeparator) {
                // RaceRoom rules its lists with a hairline across the column,
                // not with a short floating dash.
                dl.addRectFilled({L.left - 14.f, iy + 2.f, L.listW + 28.f, 1.f},
                                 Theme::hairline(a));
                iy += advance;
                continue;
            }

            const bool selected = (i == sel);
            const bool hover = (i == m_menu->hoverIndex());

            // Every row wears the list's nameplate - the "(" on its left, the
            // rule on top, the "/" bevel and the closing "|" - with the dark
            // tab past the bevel. The selected row is the plate on top of
            // that, outlined in the accent; the one under the cursor gets the
            // hover wash.
            const Rect rowRect{L.left - 14.f, iy - 2.f, L.listW + 28.f, L.rowH - 2.f};
            // Option item: slanted (oblique) gradient bar, with the option/value
            // zone as a distinct rounded square of a different size on the right.
            auto drawOptionBar = [&](const Rect& r, bool sel) {
                // CSS .hud-bar / .hud-content shape (clip-path polygon) + gold border.
                auto shapePts = [&](const Rect& rr) {
                    const float pi = 3.14159265f;
                    const float leftH = rr.h * 0.65f;
                    const float leftR = leftH * 0.5f;
                    const float rightR = 10.f;
                    std::vector<float> p;
                    const int seg = 8;
                    for (int s = 0; s <= seg; ++s) {
                        float t = -pi * 0.5f - pi * (float)s / (float)seg;
                        p.push_back(rr.x + leftR + leftR * std::cos(t));
                        p.push_back(rr.y + leftH * 0.5f + leftR * std::sin(t));
                    }
                    p.push_back(rr.x + leftR);   p.push_back(rr.y);
                    p.push_back(rr.x + rr.w - rightR); p.push_back(rr.y);
                    for (int s = 0; s <= seg; ++s) {
                        float t = -pi * 0.5f + (pi * 0.5f) * (float)s / (float)seg;
                        p.push_back(rr.x + rr.w - rightR + rightR * std::cos(t));
                        p.push_back(rr.y + rightR + rightR * std::sin(t));
                    }
                    p.push_back(rr.x + rr.w);     p.push_back(rr.y + rightR);
                    p.push_back(rr.x + rr.w);     p.push_back(rr.y + rr.h - rightR);
                    for (int s = 0; s <= seg; ++s) {
                        float t = 0.f + (pi * 0.5f) * (float)s / (float)seg;
                        p.push_back(rr.x + rr.w - rightR + rightR * std::cos(t));
                        p.push_back(rr.y + rr.h - rightR + rightR * std::sin(t));
                    }
                    p.push_back(rr.x + rr.w * 0.75f); p.push_back(rr.y + rr.h);
                    p.push_back(rr.x + rr.w * 0.67f); p.push_back(rr.y + rr.h * 0.65f);
                    p.push_back(rr.x + leftR);        p.push_back(rr.y + rr.h * 0.65f);
                    return p;
                };
                auto pts = shapePts(r);
                // shadow
                std::vector<float> shpts;
                for (size_t i = 0; i < pts.size(); i += 2) { shpts.push_back(pts[i]+2.f); shpts.push_back(pts[i+1]+3.f); }
                dl.addPolygonFill(shpts, Color::rgba(0.f, 0.f, 0.f, sel ? 0.45f : 0.25f));
                // gradient content (left->right): #f0f0f0 0%, #e0e0e0 45%, #404040 60%, #1a1a1a 100%
                dl.addPolygonFill(pts, Color::rgba(0.94f, 0.94f, 0.94f, a));
                const int bands = 12;
                const float bw = r.w / (float)bands;
                for (int b = 0; b < bands; ++b) {
                    float mid = ((float)b + 0.5f) / (float)bands;
                    Color bc;
                    if (mid < 0.45f) bc = Color::rgba(0.94f, 0.94f, 0.94f, a);
                    else if (mid < 0.6f) {
                        float u = (mid - 0.45f) / 0.15f;
                        bc = Color::rgba(0.88f - 0.55f*u, 0.88f - 0.55f*u, 0.88f - 0.55f*u, a);
                    } else {
                        float u = (mid - 0.6f) / 0.4f;
                        bc = Color::rgba(0.25f - 0.15f*u, 0.25f - 0.15f*u, 0.25f - 0.15f*u, a);
                    }
                    float bx = r.x + (float)b * bw;
                    // clip band height to shape: top full, bottom follows step
                    float topY = r.y;
                    float botY = r.y + r.h;
                    if (bx + bw * 0.5f > r.x + r.w * 0.67f) {
                        // right of step: full height
                    } else {
                        // left of step: only top 65%
                        botY = r.y + r.h * 0.65f;
                    }
                    dl.addRectFilled({bx, topY, bw + 1.f, botY - topY}, bc);
                }
                // gold border (polyline)
                Color border = sel ? Color::rgba(0.83f, 0.69f, 0.22f, a) : Theme::hairline(a);
                dl.addPolyline(pts, border, 3.f, true);
                // hover wash
                if (hover && !sel) {
                    dl.addPolygonFill(pts, Color::rgba(1.f, 1.f, 1.f, 0.055f * a));
                }
            };
            drawOptionBar(rowRect, selected);

            // On the plate the ink flips, which is the whole point of it: a
            // dark name and a dark red value on light, light name and pure red
            // value on the dark page.
            Color titleCol = selected ? Theme::onPlate(a) : Theme::ink(a);
            // The row's icon, when it names one: an SVG parsed once and
            // stroked through the same tessellator as the rest of the UI, so
            // it stays sharp at any size and tints with the row. The label
            // moves right to leave it room.
            float labelX = L.left + 8.f;
            if (const SvgIcon* rowIcon = icons::get(it.icon)) {
                const float iconSize = 20.f;
                const Color iconCol = selected ? Theme::onPlate(0.70f * a)
                                               : Theme::inkDim(a);
                // Centred on the row, like the label's ink: the two share a
                // middle line however tall the glyphs beside it are.
                rowIcon->draw(dl, labelX, iy + (L.rowH - iconSize) * 0.5f,
                              iconSize, iconCol, selected ? 1.15f : 1.f);
                labelX += iconSize + 12.f;
            }
            // Centred by its ink, not by its line box: the line carries the
            // leading and each glyph its own offset, so the box centre sat
            // above the letters and the row read as out of step with its icon.
            const float labelScale = selected ? 1.35f : 1.2f;
            // The driver pages show the chosen country as a flag on its row.
            const bool nationalityRow =
                (m_menu->state() == MenuState::Profile ||
                 m_menu->state() == MenuState::FirstRun) &&
                it.text.rfind("NATIONALITY", 0) == 0;
            // The row's two plates: the name lives on the light one, ending at
            // the "/" , and the value on the dark one past it. Both runs are
            // shortened to their own plate instead of painted past it, and the
            // flag takes its room from the value's, since it sits at the right
            // end of the row.
            const MenuRowGeometry rowGeo = menuRowGeometry(rowRect);
            const float rowRight = rowRect.x + rowRect.w;
            const float valueX = rowGeo.splitMidX + 8.f;
            const float labelMaxW = std::max(0.f, rowGeo.splitMidX - labelX - 10.f);
            const float valueMaxW = std::max(
                0.f, rowRight - 16.f - valueX - (nationalityRow ? 44.f : 0.f));
            // The theme reads a row as two things: the option's name and the
            // value it holds. Drawn as two runs on one baseline, the value can
            // wear the accent while the name stays calm, which is what makes a
            // page of thirty rows scannable. Rows without a value (BACK) keep a
            // single run; on a section header the [-] / [+] marker takes the
            // accent instead, because that marker is the thing you press.
            std::string head = it.text;
            std::string tail;
            std::string gap = "  ";
            bool twoRuns = false;
            bool header = false;
            if (head.rfind("[-] ", 0) == 0 || head.rfind("[+] ", 0) == 0) {
                header = true;
                twoRuns = true;
                tail = head.substr(4);
                head = head.substr(0, 3);
                gap = " ";
            } else if (const size_t split = head.find("  "); split != std::string::npos) {
                twoRuns = true;
                tail = head.substr(split + 2);
                head = head.substr(0, split);
            }
            const Color headCol = header ? (selected ? Theme::onPlateAccent(a) : Theme::accent(a))
                                         : titleCol;
            // The value sits on the dark plate now, so it wears the accent at
            // full strength instead of the plate's darkened red.
            const Color tailCol = header ? titleCol : Theme::accent(0.92f * a);
            float gapW = 0.f;
            if (twoRuns && header) {
                // A section header is one thing - the marker you press and the
                // name of the section - so it stays inside the name's plate.
                gapW = m_text.measureWidth(gap, labelScale);
                float tailW = m_text.measureWidth(tail, labelScale);
                if (gapW + tailW > labelMaxW - 8.f) {
                    tail = elideToWidth(tail, labelScale,
                                        std::max(0.f, labelMaxW - gapW - 8.f));
                    tailW = m_text.measureWidth(tail, labelScale);
                }
                const float headMaxW = labelMaxW - gapW - tailW;
                if (headMaxW > 8.f)
                    head = elideToWidth(head, labelScale, headMaxW);
                else
                    twoRuns = false;
            } else if (twoRuns) {
                head = elideToWidth(head, labelScale, labelMaxW);
                tail = elideToWidth(tail, labelScale, valueMaxW);
                if (tail.empty()) twoRuns = false;
            }
            const std::string label = twoRuns ? head : elideToWidth(it.text, labelScale, labelMaxW);
            float inkTop = 0.f, inkBottom = 0.f;
            m_text.atlas().inkExtents(label, inkTop, inkBottom);
            const float inkH = (inkBottom - inkTop) * labelScale;
            const float baselineY =
                iy + std::max(0.f, (L.rowH - inkH) * 0.5f) - inkTop * labelScale;
            // The same shadow the tiles wear, one run at a time: the row
            // plates are thin here, and the row labels are the last thing that
            // should fight the hall behind them.
            const float rowVeilA = selected ? 0.f : a;
            if (twoRuns) {
                // A header keeps its marker and name together on the name's
                // plate; a value starts on the dark plate, at the split.
                const float tailX = header
                    ? labelX + m_text.measureWidth(head, labelScale) + gapW
                    : valueX;
                drawMenuLabelVeil(dl, {labelX, baselineY,
                                       m_text.measureWidth(head, labelScale), inkH}, rowVeilA);
                drawMenuLabelVeil(dl, {tailX, baselineY,
                                       m_text.measureWidth(tail, labelScale), inkH}, rowVeilA);
                // Option entries (labels) in normal text, option values in bold.
                const bool headBold = header ? selected : false;
                const bool tailBold = header ? false : true;
                m_text.draw(dl, labelX, baselineY, head,
                            sty(headCol, labelScale, headBold, !selected));
                m_text.draw(dl, tailX, baselineY, tail,
                            sty(tailCol, labelScale, tailBold, !selected));
            } else {
                drawMenuLabelVeil(dl, {labelX, baselineY,
                                       m_text.measureWidth(label, labelScale), inkH}, rowVeilA);
                // Single-run rows (e.g. BACK) stay in normal text.
                m_text.draw(dl, labelX, baselineY, label,
                            sty(headCol, labelScale, false, !selected));
            }

            if (nationalityRow)
                drawCountryFlag(dl, {L.left - 12.f + L.listW + 24.f - 44.f,
                                     iy + (L.rowH - 22.f) * 0.5f, 30.f, 22.f},
                                m_menu->profile().nationality, a);

            if (selected)
                desc = it.description;
            iy += advance;
        }

        // A page taller than the window says so, on the side that has rows
        // hidden, and says nothing when it fits. A track and thumb were tried
        // first and read as a stray border: a page that overflows by two rows
        // got a thumb the height of the list.
        if (contentH > windowH) {
            const auto moreRows = [&](float y, bool above) {
                m_text.draw(dl, L.left + 8.f, y, above ? "^" : "v",
                            sty(Theme::accent(a), 0.85f, true));
                m_text.draw(dl, L.left + 24.f, y, above ? "MORE ABOVE" : "MORE BELOW",
                            sty(Theme::inkDim(a), 0.85f));
            };
            if (skip > 1.f) moreRows(L.top - 22.f, true);
            if (skip < contentH - windowH - 1.f) moreRows(rowLimit + 4.f, false);
        }

        if (!desc.empty()) {
            const float px = static_cast<float>(w) * 0.52f;
            const float py = L.top;
            const float pw = static_cast<float>(w) - px - 64.f;
            if (pw > 120.f) {
                // A surface, not a card: square, hairline border and the red
                // edge on the left, like every panel in the launcher.
                dl.addRectFilled({px, py, pw, 160.f}, Theme::surface(0.94f * a));
                dl.addRect({px, py, pw, 160.f}, Theme::hairline(a), 1.f);
                dl.addRectFilled({px, py, 3.f, 160.f}, Theme::accent(0.9f * a));
                m_text.draw(dl, px + 20.f, py + 24.f, "INFO",
                            sty(Theme::accent(a), 1.f, true));
                // Wrapped inside the panel: a long description used to run
                // past its right edge.
                dl.addTextWrapped(px + 20.f, py + 56.f, desc,
                                  Theme::ink(0.86f * a), 1.15f, pw - 40.f);
            }
        }

        // The first-run wizard has nothing behind it, so its footer promises no
        // way back.
        const char* footer = (m_menu->state() == MenuState::FirstRun)
                                 ? "ENTER  select"
                                 : "ENTER  select    ESC  back";
        m_text.draw(dl, static_cast<float>(w) - 280.f, static_cast<float>(h) - 48.f, footer,
                    sty(Theme::inkDim(0.85f * a), 0.95f));
    }

    /** A panel's rect: 24 px from the left and 200 px up from the bottom by
     *  default, moved by the drag offsets, and kept inside the window so a panel
     *  dragged past an edge comes back with the next resize instead of being
     *  lost. */
    static Rect overlayRect(int w, int h, float pw, float baseX, float dx, float dy,
                            float ph = 170.f) {
        Rect r;
        r.w = pw;
        r.h = ph;
        r.x = std::clamp(baseX + dx, 0.f, std::max(0.f, static_cast<float>(w) - pw));
        // 24 px up from the bottom, whatever the panel's own height is.
        r.y = std::clamp(static_cast<float>(h) - ph - 24.f + dy, 0.f,
                         std::max(0.f, static_cast<float>(h) - r.h));
        return r;
    }

    /// The H-DASH: revs as a bar, speed in km/h, the gear, the fuel, and the
    /// warning lights a car actually carries - water, oil, fuel, battery - with
    /// the numbers behind them, plus the driving warnings (brake fade, ABS and
    /// TC working) that the wheel strip already shows. Every reading comes from
    /// the sample the loop filled, so this panel and the physics cannot disagree.
    void buildHDash(DrawList& dl, int w, int h) {
        const auto& s = m_raceHud.sample();
        const Rect r = hdashRect(w, h);
        const float panelW = r.w;
        const float x = r.x;
        const float y = r.y;

        const float panelH = r.h;
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0, 0, 0, 0.62f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(60, 100, 160), 1.f);
        m_text.draw(dl, x + 12, y + 8, "H-DASH", TextRenderer::hudLabel());
        // A grab mark on the title row: the panel is the driver's to move.
        for (int i = 0; i < 3; ++i)
            dl.addRectFilled({x + panelW - 30.f + i * 7.f, y + 13.f, 3.f, 3.f},
                             Color::rgba(0.75f, 0.80f, 0.88f, 0.65f));

        // Revs, amber near the limiter and red past it: the bar is the one thing
        // on this panel a driver reads without taking the eyes off the road.
        // Share of the car's own limiter, the same rule as the race HUD and the
        // dash (ui/RevScale.h): the sample carries VehicleSimulator::maxRpm().
        const float revs = revFraction(s.rpm, s.maxRpm);
        const Color revCol = revs > 0.97f ? Color::rgb(235, 60, 60)
                                         : revs > 0.85f ? Color::rgb(235, 175, 60)
                                                        : Color::rgb(80, 210, 100);
        dl.addProgressBar({x + 12, y + 30, panelW - 24, 14}, revs, revCol,
                          Color::rgba(0.15f, 0.15f, 0.15f, 1.f));

        char line[80];
        std::snprintf(line, sizeof(line), "%d km/h",
                      static_cast<int>(s.speedMs * 3.6f + 0.5f));
        m_text.draw(dl, x + 12, y + 52, line, TextRenderer::hudValue());
        // Gear and revs share a line: three digits of speed already fill the one
        // above, so stacking them is what keeps the panel readable.
        if (s.gear > 0) std::snprintf(line, sizeof(line), "GEAR %d", s.gear);
        else std::snprintf(line, sizeof(line), "GEAR %s", s.gear < 0 ? "R" : "N");
        m_text.draw(dl, x + 12, y + 76, line, TextRenderer::hudValue());
        std::snprintf(line, sizeof(line), "%d rpm", static_cast<int>(s.rpm + 0.5f));
        m_text.draw(dl, x + 148, y + 82, line, TextRenderer::hudLabel());

        // Fuel with both readings: the percentage is the one the wheel strip warns
        // on, the litres are what the tank and a pit plan are counted in. The
        // share is floored, so 9.7 L of 150 reads 9% and the light below is on for
        // the same reason the number says so.
        std::snprintf(line, sizeof(line), "FUEL %3.0f%%  %.1f L",
                      static_cast<double>(std::floor(s.fuelPct)),
                      static_cast<double>(s.fuelL));
        m_text.draw(dl, x + 12, y + 98, line, TextRenderer::hudLabel());

        // The classic lights: water, oil, fuel, battery. Lit while the condition
        // holds, dim otherwise, so the row says "all clear" without a legend.
        // Every condition is the loop's own answer (physics/DrivingWarnings.h), so
        // this panel and a car's dash cannot call the same lamp differently.
        struct Light {
            const char* name;
            bool on;
            Color lit;
        };
        const Light lights[4] = {
            {"WATER", s.warnings.waterHot, Color::rgb(235, 60, 60)},
            {"OIL", s.warnings.oilBad, Color::rgb(235, 175, 60)},
            {"FUEL", s.warnings.lowFuel, Color::rgb(235, 175, 60)},
            {"BATT", s.warnings.batteryLow, Color::rgb(235, 60, 60)},
        };
        float lx = x + 12.f;
        for (const auto& l : lights) {
            m_text.draw(dl, lx, y + 122, l.name,
                        sty(l.on ? l.lit : Color::rgba(0.55f, 0.58f, 0.62f, 0.9f), 1.0f, l.on));
            lx += 72.f;
        }

        // The driving warnings the car's systems raise, on their own row: brake
        // fade - which carries its number, so the share is read next to the word
        // instead of as a bare percentage at the end of the row - and the two
        // aids while they are really cutting, as the physics decided that step.
        // Same numbers the wheel strip gets. The fade is the hot-side share, so a
        // cold car shows 0 here and the lamp stays out.
        char fadeLabel[16];
        std::snprintf(fadeLabel, sizeof(fadeLabel), "FADE %.0f%%",
                      static_cast<double>(s.warnings.brakeFade * 100.f));
        const Light driving[3] = {
            {fadeLabel, s.warnings.brakeFade > 0.f, Color::rgb(235, 60, 60)},
            {"ABS", s.warnings.absActive, Color::rgb(235, 175, 60)},
            {"TC", s.warnings.tcActive, Color::rgb(235, 175, 60)},
        };
        lx = x + 12.f;
        for (const auto& l : driving) {
            m_text.draw(dl, lx, y + 142, l.name,
                        sty(l.on ? l.lit : Color::rgba(0.55f, 0.58f, 0.62f, 0.9f), 1.0f, l.on));
            // Wider than the old fixed step because the first label now carries
            // "FADE 100%" (nine glyphs at 8 px): 82 px keeps it clear of ABS.
            lx += 82.f;
        }

        // The numbers behind the lights: they are why a light is on, and how far
        // the next one is.
        std::snprintf(line, sizeof(line), "W %.0f C   O %.1f bar   B %.1f V",
                      static_cast<double>(s.coolantC), static_cast<double>(s.oilBar),
                      static_cast<double>(s.batteryV));
        m_text.draw(dl, x + 12, y + 164, line, TextRenderer::hudLabel());

        // The driver aids, where the driver can read them: the level the car is
        // running, or "--" when its electronics declare the system absent (a
        // number there would promise an aid nobody fitted). The console's TC and
        // ABS verbs change these while driving.
        char tcText[8];
        char absText[8];
        if (s.tcPresent == 0) std::snprintf(tcText, sizeof(tcText), "--");
        else std::snprintf(tcText, sizeof(tcText), "%d", s.tcLevel);
        if (s.absPresent == 0) std::snprintf(absText, sizeof(absText), "--");
        else std::snprintf(absText, sizeof(absText), "%d", s.absLevel);
        // The brake bias is the third cockpit knob: the front share the car is
        // braking with, which the console's BB verb moves like the other two.
        char biasText[8];
        if (s.brakeBiasPct < 0) std::snprintf(biasText, sizeof(biasText), "--");
        else std::snprintf(biasText, sizeof(biasText), "%d%%", s.brakeBiasPct);
        std::snprintf(line, sizeof(line), "AIDS  TC %s  ABS %s  BB %s", tcText, absText,
                      biasText);
        m_text.draw(dl, x + 12, y + 186, line, TextRenderer::hudLabel());
    }

    /// The pedal box and the shifter, next to the DASH and the same height: the
    /// three pedals with their positions and numbers, and the gear the car is
    /// in with what the shifter has queued that the gearbox has not taken yet.
    /// A sequential shifter can ask twice before the box has changed once, and
    /// that request is the thing the driver cannot see anywhere else.
    void buildInputs(DrawList& dl, int w, int h) {
        const auto& s = m_raceHud.sample();
        const Rect r = inputsRect(w, h);
        const float panelW = r.w;
        const float x = r.x;
        const float y = r.y;

        dl.addRectFilled({x, y, panelW, 170}, Color::rgba(0, 0, 0, 0.62f));
        dl.addRect({x, y, panelW, 170}, Color::rgb(60, 100, 160), 1.f);
        m_text.draw(dl, x + 12, y + 8, "INPUTS", TextRenderer::hudLabel());
        for (int i = 0; i < 3; ++i)
            dl.addRectFilled({x + panelW - 30.f + i * 7.f, y + 13.f, 3.f, 3.f},
                             Color::rgba(0.75f, 0.80f, 0.88f, 0.65f));

        // The numbers sit inside the panel even at 100%, so the bar is what
        // gives way, not the reading.
        const auto pedal = [&](float row, float value, const char* label, const Color& fill) {
            const float v = std::clamp(value, 0.f, 1.f);
            m_text.draw(dl, x + 12, y + row, label, TextRenderer::hudLabel());
            dl.addProgressBar({x + 52, y + row + 3, 92, 12}, v, fill,
                              Color::rgba(0.15f, 0.15f, 0.15f, 1.f));
            char pct[16];
            std::snprintf(pct, sizeof(pct), "%3.0f%%", static_cast<double>(v * 100.f));
            m_text.draw(dl, x + 152, y + row, pct, TextRenderer::hudLabel());
        };
        pedal(40.f, s.throttle, "THR", Color::rgb(80, 210, 100));
        pedal(70.f, s.brake, "BRK", Color::rgb(230, 70, 70));
        pedal(100.f, s.clutch, "CLT", Color::rgb(230, 180, 80));

        char gear[48];
        const char* shown = s.gear > 0 ? nullptr : (s.gear < 0 ? "R" : "N");
        if (shown != nullptr)
            std::snprintf(gear, sizeof(gear), "GEAR %s   %s", shown, s.autoShift ? "AUTO" : "MAN");
        else if (s.pendingShifts != 0)
            std::snprintf(gear, sizeof(gear), "GEAR %d   %s %+d", s.gear,
                          s.pendingShifts > 0 ? "UP" : "DOWN", s.pendingShifts);
        else
            std::snprintf(gear, sizeof(gear), "GEAR %d   %s", s.gear, s.autoShift ? "AUTO" : "MAN");
        m_text.draw(dl, x + 12, y + 138, gear, TextRenderer::hudValue());
    }

    /** Where the car's own display sits: bottom right, clear of the H-DASH and
     *  INPUTS panels and of the telemetry page at the top. */
    Rect carDashRect(int w, int h) const {
        Rect r;
        r.w = 360.f;
        r.h = 150.f;
        r.x = std::max(0.f, static_cast<float>(w) - r.w - 24.f);
        r.y = std::max(0.f, static_cast<float>(h) - r.h - 24.f);
        return r;
    }

    /// The car's own digital dash (CSP-3): whatever the loaded car declares in
    /// its data/data.json, painted where the driver can read it. The layout, the
    /// colours and the elements are the car's; the numbers are the frame the HUD
    /// was handed. A car that declares no display draws nothing at all.
    void buildCarDash(DrawList& dl, int w, int h) {
        if (!m_carDash.present()) return;
        const Rect panel = carDashRect(w, h);
        dl.addRectFilled(panel, Color::rgba(0, 0, 0, 0.62f));
        dl.addRect(panel, Color::rgb(60, 100, 160), 1.f);
        m_text.draw(dl, panel.x + 12, panel.y + 8, "CAR DASH", TextRenderer::hudLabel());
        // The display gets the panel minus its title row, and the primitives the
        // car declares are placed in there.
        const Rect area{panel.x + 10.f, panel.y + 30.f, panel.w - 20.f, panel.h - 40.f};
        m_carDash.draw(dl, m_text, area, carDashClockS());
    }

    /** Seconds since the UI started: the clock the car display blinks on. */
    double carDashClockS() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             m_carDashClock)
            .count();
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

    /** The drag behind the panels: grab on press, follow on move, let go on
     *  release. Returns true when the event belonged to a panel, so the press is
     *  not also read as a menu click or a look-around. */
    bool dragOverlays(const MouseEvent& e) {
        if (!m_dash->isVisible() || m_menu->isVisible() || m_console->isVisible()) return false;
        if (m_dragPanel < 0) {
            if (e.type != MouseEventType::Down || e.button != MouseButton::Left) return false;
            const Rect hd = hdashRect(m_viewW, m_viewH);
            const Rect in = inputsRect(m_viewW, m_viewH);
            if (hd.contains(e.x, e.y)) m_dragPanel = 0;
            else if (in.contains(e.x, e.y)) m_dragPanel = 1;
            else return false;
            const Rect& grabbed = m_dragPanel == 0 ? hd : in;
            m_dragGrabX = e.x - grabbed.x;
            m_dragGrabY = e.y - grabbed.y;
            return true;
        }
        if (e.type == MouseEventType::Move) {
            const Rect cur = m_dragPanel == 0 ? hdashRect(m_viewW, m_viewH)
                                              : inputsRect(m_viewW, m_viewH);
            // The offset is relative to where the panel is now, so grabbing it
            // against an edge and dragging back follows the pointer at once.
            const float dx = e.x - m_dragGrabX - cur.x;
            const float dy = e.y - m_dragGrabY - cur.y;
            if (m_dragPanel == 0) {
                m_hdashDX += dx;
                m_hdashDY += dy;
            } else {
                m_inputsDX += dx;
                m_inputsDY += dy;
            }
            return true;
        }
        if (e.type == MouseEventType::Up && e.button == MouseButton::Left) {
            m_dragPanel = -1;
            return true;
        }
        return true; // a press or a wheel while holding the panel stays with it
    }

    DigitalDashPanel m_carDash;
    std::chrono::steady_clock::time_point m_carDashClock{};
    UiRenderer m_renderer;
    TextRenderer m_text;
    UiInput m_input;
    RaceTelemetryHud m_raceHud;
    KinetikaAppsOverlay m_kinetikaApps;
    KeyRebindOverlay m_keyRebind;
    PadRebindOverlay m_padRebind;
    ControllerSettingsOverlay m_controllerSettings;
    int m_viewW = 1280, m_viewH = 720;
    /** Where the driver put the in-race panels, as offsets from their default
     *  corner (0,0 = where they are drawn on a fresh start). Session-only: the
     *  position is not written to the settings files yet. */
    float m_hdashDX = 0.f, m_hdashDY = 0.f;
    float m_inputsDX = 0.f, m_inputsDY = 0.f;
    int m_dragPanel = -1; // -1 none, 0 H-DASH, 1 INPUTS
    float m_dragGrabX = 0.f, m_dragGrabY = 0.f;
    bool m_inCarMenuAllowed = true;   // pre-session: always
    // Flag artwork, one reduced grid per country code (see flagImage).
    mutable std::map<std::string, TrackLogoPreview> m_flagImages;
    NotificationCenter m_notices;     // what the bell reports
    int m_quickHover = -1;            // header quick action under the cursor
    int m_plannerHover = -1;          // session card row under the cursor
    int m_navHover = -1;              // bar navigation button under the cursor
    bool m_stepBackHover = false;     // BACK on the walk's bottom bar
    bool m_stepNextHover = false;     // NEXT on the walk's bottom bar
    // CARS page: the selected car's declared sheet, cached per content folder,
    // and the paint swatch the page is previewing.
    CarSpecSheet m_carSpec;
    std::string m_carSpecPath;
    int m_carPaintPick = 0;
    int m_carSeriesHover = -1;
    int m_carBrandHover = -1;
    int m_carModelHover = -1;
    int m_carPaintHover = -1;
    int m_carFilterHover = -1;
    int m_trackFilterHover = -1;
    int m_trackCategoryHover = -1;
    int m_trackCardHover = -1;
    int m_trackLayoutHover = -1;
    int m_weatherModeHover = -1;
    int m_weatherTimeHover = -1;
    int m_weatherDateHover = -1;
    int m_weatherAskHover = -1;
    int m_weatherFieldHover = -1;
    int m_eventModeHover = -1;
    // The trail the bar's arrows walk: the states the player went through, in
    // order, and where in it they are now.
    std::vector<MenuState> m_navHistory{MenuState::Main};
    size_t m_navPos = 0;
    MenuState m_navLast = MenuState::Main;

    std::unique_ptr<GameMenuOverlay> m_menu;
    // home.splash: the start-up screen's clock. Wall-clock on purpose - the
    // splash is a few hundred milliseconds of the first menu frame, and
    // threading a tick through the loop to measure that would cost more than it
    // buys.
    std::chrono::steady_clock::time_point m_splashStart{};
    bool m_splashStarted = false;
    bool m_splashDone = false;
    // The welcome mark as a loading cover: begun before a blocking track or
    // session load, ended once the first frame of the new content runs.
    bool m_loading = false;
    bool m_loadingEndRequested = false;
    std::string m_loadingLabel;
    std::chrono::steady_clock::time_point m_loadingStart{};
    std::unique_ptr<DashboardOverlay> m_dash;
    std::unique_ptr<TelemetryOverlay> m_telem;
    std::unique_ptr<DeviceSettingsOverlay> m_devices;
    std::unique_ptr<ProfileSettingsOverlay> m_profiles;
    std::unique_ptr<MultiplayerOverlay> m_mp;
    std::unique_ptr<ChatOverlay> m_chat;
    SpectatorScoreboard m_scoreboard;
    AudioSettingsOverlay m_audioSettings;
    UiSettingsOverlay m_uiSettings;
    GameplaySettingsOverlay m_gameplaySettings;
    bool m_inputsVisible = false;
    bool m_carDashVisible = false;
    std::unique_ptr<ConsoleOverlay> m_console;
    SetupGarage* m_setupScreen = nullptr;    // owned by the loop
    PhysicsGarage* m_physicsScreen = nullptr; // owned by the loop
    const ks::engine::config::Locale* m_locale = nullptr;
};

} // namespace ui
} // namespace sim
} // namespace ks
