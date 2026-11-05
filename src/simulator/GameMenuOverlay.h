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
    ModeConfig,
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
    const char* icon = nullptr;
};

struct ResultsRow {
    std::string position;
    std::string driver;
    std::string detail;
};

class GameMenuOverlay {
public:
    GameMenuOverlay();

    void render(int width, int height);
    bool handleKeyPress(int key);
    void handleMouseMove(const SimPoint& pos, int widgetWidth, int widgetHeight);
    void handleClick(const SimPoint& pos, int widgetWidth, int widgetHeight);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);
    void toggleVisible();

    void setTrackName(const std::string& name) {
        m_trackName = name;
        m_menuDirty = true;
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
    void setCarName(const std::string& name) {
        m_carName = name;
        m_menuDirty = true;
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
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

    static constexpr int kExposureCount = 5;
    static constexpr int kTonemapCount = 5;
    static constexpr float kExposureValues[kExposureCount] = {0.45f, 0.55f, 0.62f, 0.72f, 1.00f};
    static constexpr const char* kTonemapNames[kTonemapCount] = {
        "NONE", "REINHARD", "ACES", "UNCHARTED2", "FILMIC"};

    static constexpr int kResolutionCount = 3;
    static constexpr int kResolutionWidths[kResolutionCount] = {1920, 1600, 1280};
    static constexpr int kResolutionHeights[kResolutionCount] = {1080, 900, 720};
    static int resolutionWidth(int index) {
        const int i = index < 0 ? 0
                                : (index >= kResolutionCount ? kResolutionCount - 1 : index);
        return kResolutionWidths[i];
    }
    static int resolutionHeight(int index) {
        const int i = index < 0 ? 0
                                : (index >= kResolutionCount ? kResolutionCount - 1 : index);
        return kResolutionHeights[i];
    }
    static int resolutionIndexFor(int width, int height);

    struct MenuOptions {
        int resolutionIndex = 0;
        bool fullscreen = false;
        bool vsync = true;
        bool motionBlur = false;
        int exposureIndex = kExposureCount - 1;
        int tonemap = 2;
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
    static int exposureIndexFor(float exposure);

    MenuOptions& options() { return m_options; }
    const MenuOptions& options() const { return m_options; }
    void setOptions(const MenuOptions& options) { m_options = options; }

    void openOptions() {
        refreshOptions();
        switchMenu(MenuState::Options);
    }
    void openOptionsAudio();

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

    void openCarSelect();
    void openTrackSelect();
    void openTeamSelect();
    void showResults(std::vector<ResultsRow> rows);

    void setPitStrategy(float fuelL, bool tyres, bool body, bool susp, bool aero,
                        bool engine);

    void openPauseMenu();
    void openScreen(MenuState state) { switchMenu(state); }
    void activateSelection() { activateSelected(); }

    /** MODE SETUP: configure laps/AI/time/standing start before session start. */
    void openModeConfig(GameSessionMode mode);

    std::function<void()> onExitRequested;
    std::function<void(GameSessionMode)> onStartDrivingRequested;
    std::function<void()> onResetRequested;
    std::function<void()> onRestartSessionRequested;
    std::function<void()> onToggleFullscreenRequested;
    std::function<void(const DriverProfile&)> onProfileChanged;
    std::function<void()> onDevModeRequested;
    std::function<void(const std::string&, const std::string&)> onTextInputRequested;
    std::function<void()> onNationalityInputRequested;
    std::function<void()> onOpenGarageRequested;
    std::function<void()> onOpenSetupGarageRequested;
    std::function<void(float, bool, bool, bool, bool, bool)> onPitStrategyConfirmRequested;
    std::function<void()> onPitServiceRequested;
    std::function<void()> onOpenPitStrategyRequested;
    std::function<void()> onLeaveGarageRequested;
    std::function<void()> onReturnToBoxRequested;
    std::function<void()> onLoadReplayRequested;
    std::function<void()> onRecordReplayRequested;
    std::function<void(const std::string&)> onCarChosen;
    std::function<void(const std::string&)> onTrackChosen;
    std::function<void(const std::string&)> onTeamChosen;
    std::function<std::vector<std::string>()> upgradeRowLabels;
    std::function<void(int)> onUpgradeRowCycled;
    std::function<void()> onShowResultsRequested;
    std::function<void(const std::string&)> onOpenSettingsPanelRequested;
    std::function<void(const MenuOptions&)> onOptionsChanged;
    std::function<void(MenuOptions&)> onOptionsRefresh;
    std::function<void(int width, int height)> onResolutionRequested;
    std::function<void()> onHostServerRequested;
    std::function<void()> onOpenServerBrowserRequested;
    std::function<void()> onDisconnectRequested;

private:
    void startDriving(GameSessionMode mode);
    void buildMainMenu();
    void buildSingleplayerMenu();
    void buildModeConfigMenu();
    void buildMultiplayerMenu();
    void buildProfileMenu();
    void buildGarageMenu();
    void buildPitStrategyMenu();
    void buildPauseMenu();
    void refreshPitStrategy();
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
    std::string m_teamName;

    DriverProfile m_profile;
    MenuOptions m_options;
    std::vector<MenuItem> m_items;
    std::vector<ContentEntry> m_selectEntries;
    std::vector<ResultsRow> m_resultRows;

    float m_pitFuelTargetL = 60.f;
    bool m_pitWantTyres = true;
    bool m_pitWantBody = true;
    bool m_pitWantSuspension = true;
    bool m_pitWantAero = false;
    bool m_pitWantEngine = false;

    GameSessionMode m_configMode = GameSessionMode::Race;
    SessionStartParams m_configParams{};
};

} // namespace ks::sim
