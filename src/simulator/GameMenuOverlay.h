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
    Replay,
    ContentManager,
    Settings,
    Controls,
    CarSelect,
    TrackSelect,
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
    void showResults(std::vector<ResultsRow> rows);

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
    std::function<void()> onLoadReplayRequested;
    std::function<void()> onRecordReplayRequested;
    // Roadmap 1.3: content select screens scan content/cars and
    // content/tracks (ContentLibrary.h) and report the chosen folder here.
    std::function<void(const std::string&)> onCarChosen;
    std::function<void(const std::string&)> onTrackChosen;
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
    void buildReplayMenu();
    void buildContentManagerMenu();
    void buildSettingsMenu();
    void buildControlsMenu();
    void buildCarSelectMenu();
    void buildTrackSelectMenu();
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

    DriverProfile m_profile;
    std::vector<MenuItem> m_items;
    std::vector<ContentEntry> m_selectEntries;
    std::vector<ResultsRow> m_resultRows;
};

} // namespace ks::sim
