#pragma once

#include "ContentLibrary.h"
#include "SessionController.h"
#include <functional>
#include <string>
#include <vector>

namespace ks { namespace engine { namespace config { class Locale; } } }

namespace ks::sim {

/** May the pause overlay open while the player is in the car? Standing still
 *  or singleplayer only: freezing the world mid-race in multiplayer would
 *  leave the rest of the field racing while one driver reads a menu. */
inline bool inCarMenuAllowed(bool multiplayer, float speedKmh) {
    return !multiplayer || speedKmh < 5.f;
}

struct SimPoint { int x = 0; int y = 0; };
struct SimRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(const SimPoint& p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

enum class MenuState {
    Main,
    Singleplayer,
    Multiplayer,
    Profile,
    Garage,
    PitStrategy,
    Replay,
    ContentManager,
    Settings,
    Options,
    Controls,
    CarSelect,
    TrackSelect,
    TeamSelect,
    Results,
    Pause,
    DevModeConfirm,
    Credits,
    QuitConfirm
};

struct DriverProfile {
    std::string name = "Player";
    std::string nationality = "—";
    int raceNumber = 1;
    std::string bio;
    int helmetDesign = 0;
    int controlsPreset = 0;
    int lastReplayIndex = -1;
    int wins = 0;
    int poles = 0;
    int podiums = 0;
    int totalRaces = 0;
    float bestLapTime = 0.0f;
};

struct MenuItem {
    std::string text;
    std::string description;
    std::function<void()> action;
    bool isSeparator = false;
    bool enabled = true;
    /** Icon id from ui/IconSet.h, or nullptr for a row without one. An id,
     *  not a picture: the overlay never learns how an icon is drawn, and the
     *  hub resolves it against the SVG set when it paints the row. */
    const char* icon = nullptr;
};

/** One line of the RESULTS screen (roadmap 1.3). */
struct ResultsRow {
    std::string position;
    std::string driver;
    std::string detail;
};

class GameMenuOverlay {
public:
    GameMenuOverlay();

    void render(int width, int height); // advances fade/transition timers
    bool handleKeyPress(int key);
    void handleMouseMove(const SimPoint& pos, int widgetWidth, int widgetHeight);
    void handleClick(const SimPoint& pos, int widgetWidth, int widgetHeight);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);
    void toggleVisible();

    void setTrackName(const std::string& name) {
        m_trackName = name;
        m_menuDirty = true;
        // The session rows' gating (below) depends on these names: refresh
        // the screen when a load lands while it is showing.
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
    void setCarName(const std::string& name) {
        m_carName = name;
        m_menuDirty = true;
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
    // Roadmap 2.8: the chosen team sits next to car/track; unlike those two
    // it never gates the session rows (no team = legacy identity).
    void setTeamName(const std::string& name) {
        m_teamName = name;
        m_menuDirty = true;
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
    void setProfileField(const std::string& fieldName, const std::string& value);
    void setNationalityFromList(int index);

    bool isInputBlocked() const { return m_visible; }

    DriverProfile& profile() { return m_profile; }
    const DriverProfile& profile() const { return m_profile; }

    // --- settings: the values the OPTIONS screen shows ------------------
    // The page is a readout as much as a switch: every row prints what the
    // renderer or the mixer actually holds, so a knob moved on the command
    // line (KS_EXPOSURE, the ppfilter preset, KS_SSAO) still shows the truth
    // here. Nothing in this struct is a placeholder.
    static constexpr int kExposureCount = 4;
    static constexpr int kTonemapCount = 5;
    static constexpr float kExposureValues[kExposureCount] = {0.45f, 0.55f, 0.62f, 0.72f};
    static constexpr const char* kTonemapNames[kTonemapCount] = {
        "NONE", "REINHARD", "ACES", "UNCHARTED2", "FILMIC"};

    struct MenuOptions {
        int exposureIndex = kExposureCount - 1;
        int tonemap = 2; // ACES, the renderer's own default
        bool ssao = false;
        bool ssr = false;
        float masterVolume = 1.f;
    };

    static float exposureValue(int index) {
        const int i = index < 0 ? 0 : (index >= kExposureCount ? kExposureCount - 1 : index);
        return kExposureValues[i];
    }
    static const char* tonemapName(int index) {
        const int i = index < 0 ? 0 : (index >= kTonemapCount ? kTonemapCount - 1 : index);
        return kTonemapNames[i];
    }
    /** Closest exposure step to a value the engine reports. */
    static int exposureIndexFor(float exposure);

    MenuOptions& options() { return m_options; }
    const MenuOptions& options() const { return m_options; }
    void setOptions(const MenuOptions& options) { m_options = options; }

    /** Open the settings values; the audio entry of SETTINGS lands on the
     *  first volume row instead of the first exposure row. */
    void openOptions() {
        refreshOptions();
        switchMenu(MenuState::Options);
    }
    void openOptionsAudio();

    // --- for NativeUi cinematic draw ---
    const std::vector<MenuItem>& items() const { return m_items; }
    int selectedIndex() const { return m_selectedIndex; }
    int hoverIndex() const { return m_hoverIndex; }
    void setSelectedIndex(int i) { m_selectedIndex = i; }
    void setHoverIndex(int i) { m_hoverIndex = i; }
    MenuState state() const { return m_currentState; }
    float fadeAlpha() const { return m_fadeAlpha; }
    float transitionProgress() const { return m_transitionProgress; }
    const std::string& trackName() const { return m_trackName; }
    const std::string& carName() const { return m_carName; }
    std::string sectionTitle() const;

    // --- Roadmap 1.3: menu-minimum flow (select car -> track -> practice
    //     -> results) ------------------------------------------------------
    void openCarSelect();
    void openTrackSelect();
    void openTeamSelect(); // roadmap 2.8: team.ini roster picker
    void showResults(std::vector<ResultsRow> rows);

    // Roadmap 2.5 (Sprint 8 / P1.8): GARAGE -> PIT STRATEGY screen — fuel
    // target and repair jobs for the next box stop. setPitStrategy() seeds
    // the screen from the loop-owned state; CONFIRM PLAN reports the edited
    // tuple through onPitStrategyConfirmRequested.
    void setPitStrategy(float fuelL, bool tyres, bool body, bool susp, bool aero,
                        bool engine);

    /** In-car pause overlay (ESC while driving). A screen of its own because
     *  its rows act on a running session: resume, restart, garage, settings,
     *  quit — never the pre-session "pick a car and a track" flow. */
    void openPauseMenu();

    /** Open a screen directly: on the startup tile row a tile is not a row of
     *  the current screen, so the hub reports the section the player picked. */
    void openScreen(MenuState state) { switchMenu(state); }

    /** Activate the selected row — the tile row's ENTER. */
    void activateSelection() { activateSelected(); }

    std::function<void()> onExitRequested;
    // Fase 1 exit: the SINGLE PLAYER session rows report which session the
    // player picked (Practice / TimeAttack / Race) — SimulationLoop applies
    // the mode (laps, AI grid, countdown) through startSession().
    std::function<void(GameSessionMode)> onStartDrivingRequested;
    std::function<void()> onResetRequested;
    /** Pause overlay: re-enter the current session mode from the pits. */
    std::function<void()> onRestartSessionRequested;
    std::function<void()> onToggleFullscreenRequested;
    std::function<void(const DriverProfile&)> onProfileChanged;
    std::function<void()> onDevModeRequested;
    std::function<void(const std::string&, const std::string&)> onTextInputRequested;
    std::function<void()> onNationalityInputRequested;
    std::function<void()> onOpenGarageRequested;
    std::function<void()> onOpenSetupGarageRequested;
    // Roadmap 2.5 (Sprint 8 / P1.8): pit strategy screen actions.
    std::function<void(float, bool, bool, bool, bool, bool)> onPitStrategyConfirmRequested;
    // Roadmap 2.7: REQUEST SERVICE is deliberately separate from CONFIRM
    // PLAN — the plan only says what the next stop should do, this fires the
    // "do it now, I am stopped in the box" intent (SimulationLoop gates it).
    std::function<void()> onPitServiceRequested;
    std::function<void()> onOpenPitStrategyRequested;
    // Roadmap 2.7: garage exit / pit entry (GarageExit.h driven by the loop).
    std::function<void()> onLeaveGarageRequested;
    std::function<void()> onReturnToBoxRequested;
    std::function<void()> onLoadReplayRequested;
    std::function<void()> onRecordReplayRequested;
    // Roadmap 1.3: content select screens scan content/cars and
    // content/tracks (ContentLibrary.h) and report the chosen folder here.
    std::function<void(const std::string&)> onCarChosen;
    std::function<void(const std::string&)> onTrackChosen;
    // Roadmap 2.8: the TEAM row reports the chosen team folder (holding
    // team.ini); SimulationLoop::loadTeam applies it at session start.
    std::function<void(const std::string&)> onTeamChosen;
    // Roadmap 2.9: GARAGE upgrade rows — labels come from the loop's package
    // list, cycling a row reports its index back (select next level + apply).
    std::function<std::vector<std::string>()> upgradeRowLabels;
    std::function<void(int)> onUpgradeRowCycled;
    std::function<void()> onShowResultsRequested;
    std::function<void(const std::string&)> onOpenSettingsPanelRequested;
    /** Fires with the whole set after a row is cycled: the app applies it to
     *  the renderer and the mixer in one place. */
    std::function<void(const MenuOptions&)> onOptionsChanged;
    /** Read the current values back before the page is built: the app
     *  hooks this, so a knob moved by a session or a preset shows up. */
    std::function<void(MenuOptions&)> onOptionsRefresh;
    // Roadmap 3.1 - network actions from the MULTI PLAYER section.
    std::function<void()> onHostServerRequested;
    std::function<void()> onOpenServerBrowserRequested;
    std::function<void()> onDisconnectRequested;

private:
    // Fires onStartDrivingRequested and closes the menu (used by the gated
    // session rows of the SINGLE PLAYER screen).
    void startDriving(GameSessionMode mode);
    void buildMainMenu();
    void buildSingleplayerMenu();
    void buildMultiplayerMenu();
    void buildProfileMenu();
    void buildGarageMenu();
    void buildPitStrategyMenu();
    void buildPauseMenu();
    // Rebuilds the pit strategy rows in place (fuel step / toggles edit the
    // stored values) keeping the cursor — the row layout is stable.
    void refreshPitStrategy();
    /** Rebuilds the OPTIONS rows after a value moved, keeping the cursor. */
    void applyOptions();
    void refreshOptions() { if (onOptionsRefresh) onOptionsRefresh(m_options); }
    void buildReplayMenu();
    void buildContentManagerMenu();
    void buildSettingsMenu();
    void buildOptionsMenu();
    void buildControlsMenu();
    void buildCarSelectMenu();
    void buildTrackSelectMenu();
    void buildTeamSelectMenu();
    void buildResultsMenu();
    void buildCreditsMenu();
    void buildDevModeConfirm();
    void buildQuitConfirm();
    void switchMenu(MenuState state);
    void goBack();
    void activateSelected();

    bool m_visible = true;
    bool m_menuDirty = true;
    bool m_videoSubmode = false;
    bool m_hdrEnabled = false;
    int m_resIdx = 0;
    std::vector<std::string> m_resolutions;
    bool m_vsyncEnabled = true;
    int m_qualityIdx = 0;
    int kNumQualities = 3;
    int m_aaIdx = 0;
    int kNumAAModes = 3;
    MenuState m_currentState = MenuState::Main;
    MenuState m_previousState = MenuState::Main;
    int m_selectedIndex = 0;
    int m_hoverIndex = -1;
    float m_animationTime = 0.0f;
    float m_fadeAlpha = 1.0f;
    float m_transitionProgress = 1.0f;
    std::string m_trackName;
    std::string m_carName;
    std::string m_teamName; // roadmap 2.8

    DriverProfile m_profile;
    MenuOptions m_options;
    std::vector<MenuItem> m_items;
    std::vector<ContentEntry> m_selectEntries;
    std::vector<ResultsRow> m_resultRows;

    // Pit strategy (Roadmap 2.5 / Sprint 8): what the next box stop does.
    float m_pitFuelTargetL = 60.f;
    bool m_pitWantTyres = true;
    bool m_pitWantBody = true;
    bool m_pitWantSuspension = true;
    bool m_pitWantAero = false;
    bool m_pitWantEngine = false;
};

} // namespace ks::sim
