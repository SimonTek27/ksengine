#pragma once

#include <string>
#include <functional>
#include <vector>
#include <cstdint>

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

/** LAN server row for browser UI (Sprint 5 / P1.9). */
struct BrowserServerEntry {
    std::string name;
    std::string track;
    std::string host;
    uint16_t port = 40000;
    int players = 0;
    int maxPlayers = 24;
    uint8_t sessionType = 0;
    float ageSec = 0.f;
};

struct MenuItem {
    std::string text;
    std::string description;
    std::function<void()> action;
    bool isSeparator = false;
    bool enabled = true;
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

    void setTrackName(const std::string& name) { m_trackName = name; }
    void setCarName(const std::string& name) { m_carName = name; }
    void setServerList(const std::vector<BrowserServerEntry>& list);
    void setWeatherSummary(const std::string& preset, float hours);
    void setPitStrategy(float fuelL, bool tyres, bool body, bool susp, bool aero, bool engine);
    const std::vector<BrowserServerEntry>& serverList() const { return m_servers; }
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

    std::function<void()> onExitRequested;
    std::function<void()> onStartDrivingRequested;
    /** Menu entry label: PRACTICE / QUICK RACE / TIME ATTACK */
    std::function<void(const std::string& modeLabel)> onStartSessionRequested;
    std::function<void()> onLoadTrackRequested;
    std::function<void()> onLoadCarRequested;
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
    std::function<void(const std::string&)> onOpenContentBrowserRequested;
    std::function<void(const std::string&)> onOpenSettingsPanelRequested;
    // Roadmap 3.1 - network actions from the MULTI PLAYER section.
    std::function<void()> onHostServerRequested;
    std::function<void()> onOpenServerBrowserRequested;
    std::function<void()> onDisconnectRequested;
    std::function<void()> onRefreshServerListRequested;
    std::function<void(const BrowserServerEntry&)> onJoinServerRequested;
    std::function<void(const std::string& preset)> onWeatherPresetRequested;
    std::function<void(float hours)> onTimeOfDayRequested;
    std::function<void(float deltaHours)> onTimeOfDayAdjustRequested;
    /** fuelL, tyres, body, susp, aero, engine */
    std::function<void(float, bool, bool, bool, bool, bool)> onPitStrategyConfirmRequested;
    std::function<void()> onOpenPitStrategyRequested;

private:
    void buildMainMenu();
    void buildSingleplayerMenu();
    void buildMultiplayerMenu();
    void buildServerBrowserMenu();
    void buildWeatherMenu();
    void buildPitStrategyMenu();
    void buildProfileMenu();
    void buildGarageMenu();
    void buildReplayMenu();
    void buildContentManagerMenu();
    void buildSettingsMenu();
    void buildControlsMenu();
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
    std::vector<BrowserServerEntry> m_servers;
    std::string m_weatherPreset = "Dry";
    float m_timeOfDayHours = 12.f;
    // Pit strategy (P1.8)
    float m_pitFuelTargetL = 60.f;
    bool m_pitWantTyres = true;
    bool m_pitWantBody = true;
    bool m_pitWantSuspension = true;
    bool m_pitWantAero = false;
    bool m_pitWantEngine = false;
};

} // namespace ks::sim
