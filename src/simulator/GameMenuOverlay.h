#pragma once

#include "ContentLibrary.h"
#include "SessionController.h"
#include <functional>
#include <string>
#include <vector>

namespace ks::sim {

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
    Controls,
    CarSelect,
    TrackSelect,
    TeamSelect,
    Results,
    DevModeConfirm,
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

    std::function<void()> onExitRequested;
    // Fase 1 exit: the SINGLE PLAYER session rows report which session the
    // player picked (Practice / TimeAttack / Race) — SimulationLoop applies
    // the mode (laps, AI grid, countdown) through startSession().
    std::function<void(GameSessionMode)> onStartDrivingRequested;
    std::function<void()> onResetRequested;
    std::function<void()> onToggleFullscreenRequested;
    std::function<void(const DriverProfile&)> onProfileChanged;
    std::function<void()> onDevModeRequested;
    std::function<void(const std::string&, const std::string&)> onTextInputRequested;
    std::function<void()> onNationalityInputRequested;
    std::function<void()> onOpenGarageRequested;
    std::function<void()> onOpenSetupGarageRequested;
    // Roadmap 2.5 (Sprint 8 / P1.8): pit strategy screen actions.
    std::function<void(float, bool, bool, bool, bool, bool)> onPitStrategyConfirmRequested;
    std::function<void()> onOpenPitStrategyRequested;
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
    // Rebuilds the pit strategy rows in place (fuel step / toggles edit the
    // stored values) keeping the cursor — the row layout is stable.
    void refreshPitStrategy();
    void buildReplayMenu();
    void buildContentManagerMenu();
    void buildSettingsMenu();
    void buildControlsMenu();
    void buildCarSelectMenu();
    void buildTrackSelectMenu();
    void buildTeamSelectMenu();
    void buildResultsMenu();
    void buildDevModeConfirm();
    void buildQuitConfirm();
    void switchMenu(MenuState state);
    void goBack();
    void activateSelected();

    bool m_visible = true;
    bool m_menuDirty = true;
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
