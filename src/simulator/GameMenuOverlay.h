#pragma once

#include "CarSkins.h"
#include "ContentLibrary.h"
#include "ResultsExport.h"
#include "SessionController.h"
#include "TrackMapPreview.h"
#include "WeatherOutlook.h"
#include "ui/UiSounds.h"
#include <functional>
#include <map>
#include <string>
#include <utility>
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
    ContentPackages,
    ShowroomSelect,
    Skins,
    Suits,
    Helmets,
    Settings,
    Options,
    Controls,
    CarSelect,
    CarData,
    TrackSelect,
    TrackWeather,
    TeamSelect,
    Results,
    Pause,
    DevModeConfirm,
    Credits,
    ModeConfig,
    QuitConfirm,
    FirstRun
};

struct DriverProfile {
    std::string name = "Player";
    // ASCII only: the atlas covers 32..126, so an unset field used to draw as
    // "???" on the profile page.
    std::string nationality = "NOT SET";
    int raceNumber = 1;
    std::string bio;
    // The gear the player chose on CONTENT -> SUITS / HELMETS: the folder id in
    // content/drivers/suits and content/drivers/helmets. Written into
    // user/<player>/driver.json with the rest of the profile.
    std::string suit;
    std::string helmet;
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
    /** The sentence the page prints under the name ("best 92.480", "DSQ",
     *  "lap 3"): display only, never parsed. */
    std::string detail;
    /** The same classification as data, for the export: a token the file can
     *  hold (dsq / finished / running), the best lap in seconds (0 = none set)
     *  and the laps the session counts as finished (0 = still on the first). */
    std::string status;
    float bestLapTime = 0.0f;
    int completedLaps = 0;
};

class GameMenuOverlay {
public:
    GameMenuOverlay();

    void render(int width, int height);
    bool handleKeyPress(int key);
    void handleMouseMove(const SimPoint& pos, int widgetWidth, int widgetHeight);
    void handleClick(const SimPoint& pos, int widgetWidth, int widgetHeight);

    /** The clicks this menu asked for since the last drain: hover when the
     *  selection moved, select when a row was confirmed, back when a screen
     *  closed. The menu knows when they happen but cannot play them - the
     *  audio layer lives in the app - so they leave through here. */
    int drainSounds(ui::UiSound* out, int maxSounds);

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
    /** The folder the loaded car came from: the CARS page's DATA row reads the
     *  car's own numbers from it (CarSpecSheet.h). */
    void setCarDir(const std::string& dir) { m_carDir = dir; }
    void setTeamName(const std::string& name) {
        m_teamName = name;
        m_menuDirty = true;
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
    /** SINGLE PLAYER rows: the modes the config lists, in its order. An empty
     *  list keeps the built-in eight, which is what a config without
     *  singleplayer.modes gets. */
    void setSingleplayerModes(const std::vector<GameSessionMode>& modes) {
        m_singleplayerModes = modes;
        m_menuDirty = true;
        if (m_currentState == MenuState::Singleplayer) buildSingleplayerMenu();
    }
    std::vector<GameSessionMode> effectiveSingleplayerModes() const;
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

    // Entry 0 is the display mode the app read from the system (see
    // seedSystemResolution()); entries 1..2 are the sizing steps below it. A
    // build that never seeds keeps the historical defaults, which is what the
    // headless menu tests see — the list is therefore a runtime table, not a
    // compile-time constant, but its shape (three entries) is unchanged.
    static constexpr int kResolutionCount = 3;
    struct ResolutionList {
        int widths[kResolutionCount];
        int heights[kResolutionCount];
    };
    static ResolutionList& resolutionList() {
        static ResolutionList list{{1920, 1600, 1280}, {1080, 900, 720}};
        return list;
    }
    /** Replace entry 0 with the monitor mode, so the menu never offers a size
     *  the system does not have as its "current" resolution. */
    static void seedSystemResolution(int width, int height) {
        if (width <= 0 || height <= 0) return;
        ResolutionList& list = resolutionList();
        list.widths[0] = width;
        list.heights[0] = height;
    }
    static int resolutionWidth(int index) {
        const int i = index < 0 ? 0
                                : (index >= kResolutionCount ? kResolutionCount - 1 : index);
        return resolutionList().widths[i];
    }
    static int resolutionHeight(int index) {
        const int i = index < 0 ? 0
                                : (index >= kResolutionCount ? kResolutionCount - 1 : index);
        return resolutionList().heights[i];
    }
    static int resolutionIndexFor(int width, int height);

    struct MenuOptions {
        int resolutionIndex = 0;
        // Client size the renderer really has, in pixels. 0 means "not known"
        // — a headless menu or a test — and the RESOLUTION row then falls back
        // to the size of the selected entry.
        int windowWidth = 0;
        int windowHeight = 0;
        bool fullscreen = false;
        bool vsync = true;
        bool motionBlur = false;
        int exposureIndex = kExposureCount - 1;
        int tonemap = 2;
        bool ssao = false;
        bool ssr = false;
        bool mirror = false;
        float masterVolume = 1.f;
        // GAMEPLAY rows: indices into kGameplayValues, not the values, so the
        // ladder the player cycles is one table shared with the label and the
        // apply path.
        int fuelIndex = 2; // 1x
        int tireIndex = 2; // 1x
        // Strict track limits: the session's penalty ladder, not the LIMITS
        // on/off switch (that one stays the race director's).
        bool strictRules = false;
        // VIDEO rows the launcher's own settings page carries (frame limit,
        // anisotropy, particles, head physics, shadows, image gains). Indices
        // into the ladders below, so label and applied value cannot drift.
        int frameLimitIndex = 0;
        int anisoIndex = 3;      // 8x
        int shadowSizeIndex = 1; // 2048
        int cascadeIndex = 2;    // 3 cascades
        int mirrorResIndex = 1;  // 512x128
        int saturationIndex = 2; // 1.00
        int contrastIndex = 2;   // 1.00
        bool ssgi = false;
        bool particles = true;
        bool headPhysics = true;
    };

    // --- VIDEO ladders (drawn, applied and written from one table) ---------
    static constexpr int kFrameLimitCount = 8;
    static constexpr int kFrameLimitValues[kFrameLimitCount] = {0, 30, 60, 75, 100, 120, 144, 240};
    static constexpr int kAnisoCount = 5;
    static constexpr int kAnisoValues[kAnisoCount] = {1, 2, 4, 8, 16};
    static constexpr int kShadowSizeCount = 3;
    static constexpr int kShadowSizeValues[kShadowSizeCount] = {1024, 2048, 4096};
    static constexpr int kCascadeCount = 4;
    static constexpr int kCascadeValues[kCascadeCount] = {1, 2, 3, 4};
    static constexpr int kMirrorResCount = 3;
    static constexpr const char* kMirrorResValues[kMirrorResCount] = {"256x64", "512x128", "1024x256"};
    static constexpr int kSaturationCount = 5;
    static constexpr float kSaturationValues[kSaturationCount] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f};
    static constexpr int kContrastCount = 6;
    static constexpr float kContrastValues[kContrastCount] = {0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f};
    static int nearestIndex(const int* values, int count, int value);
    static int nearestIndex(const float* values, int count, float value);

    // The VIDEO page's sections. Each header folds its own rows away; the
    // order here is the order they appear in. Public because the app stores
    // which ones are folded (ui.collapsedSections).
    enum VideoSectionIndex {
        kSectionVideo = 0,
        kSectionQuality,
        kSectionImage,
        kSectionSound,
        kSectionGameplay,
        kSectionCount
    };
    /** "video", "quality", ... — the name the settings file stores. */
    static const char* sectionKey(int index);
    /** Index of a stored name, -1 when it is not a section. */
    static int sectionIndexFor(const std::string& key);
    bool sectionCollapsed(int index) const {
        return index >= 0 && index < kSectionCount && m_collapsedSections[index];
    }
    void setSectionCollapsed(int index, bool collapsed) {
        if (index >= 0 && index < kSectionCount) m_collapsedSections[index] = collapsed;
    }

    static float exposureValue(int index) {
        const int i = index < 0 ? 0 : (index >= kExposureCount ? kExposureCount - 1 : index);
        return kExposureValues[i];
    }
    static const char* tonemapName(int index) {
        const int i = index < 0 ? 0 : (index >= kTonemapCount ? kTonemapCount - 1 : index);
        return kTonemapNames[i];
    }
    static int exposureIndexFor(float exposure);

    /** The ladder both GAMEPLAY rows cycle through: 1 is the model's own rate,
     *  0 leaves the tank full / the tyres alone, above 1 makes the session
     *  harsher. Shared by the label, the cycling and the apply path. */
    static constexpr int kGameplayCount = 7;
    static constexpr float kGameplayValues[kGameplayCount] = {
        0.f, 0.5f, 1.f, 2.f, 3.f, 5.f, 10.f};
    static float gameplayValue(int index) {
        const int i = index < 0 ? 0 : (index >= kGameplayCount ? kGameplayCount - 1 : index);
        return kGameplayValues[i];
    }
    /** Nearest ladder entry, so a value from a settings file lands on a row. */
    static int gameplayIndexFor(float value);

    MenuOptions& options() { return m_options; }
    const MenuOptions& options() const { return m_options; }
    void setOptions(const MenuOptions& options) { m_options = options; }

    void openOptions() {
        refreshOptions();
        switchMenu(MenuState::Options);
    }
    void openOptionsAudio();
    /** Same page, cursor on the FUEL CONSUMPTION row: the GAMEPLAY entry. */
    void openOptionsGameplay();

    /** Video options: ask for every pipeline to be rebuilt from the .spv set
     *  on disk. The request is taken by the loop rather than acted on here, so
     *  the menu never touches the device mid-draw. */
    void requestShaderReload() { m_shaderReloadRequested = true; }
    bool takeShaderReloadRequest() {
        const bool requested = m_shaderReloadRequested;
        m_shaderReloadRequested = false;
        return requested;
    }

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

    /** CARS page. The root is a parameter so a test can point it at a
     *  scratch library; the menu asks the content scanner when it is empty
     *  (run folder, KS_CONTENT_ROOT, then the install prefix). */
    void openCarSelect(const std::string& root = std::string());
    void openTrackSelect();
    /** The weather page's NEXT: keep the sky as chosen and open the event page
     *  (SINGLE PLAYER), closing the CARS -> TRACKS -> WEATHER -> EVENT walk. */
    void openSessionEvent();
    void openTeamSelect();
    /** CONTENT: rescan the libraries and open the overview with fresh counts. */
    void openContentManager();
    /** CONTENT: the packages waiting in <content>/packages, to install. */
    void openContentPackages();
    /** CONTENT: the showrooms on disk, one of which becomes the default. */
    void openShowroomSelect();
    void openSkins();
    void openSuits();
    void openHelmets();

    /** The session setup walk, in rFactor order: CARS -> TRACKS -> EVENT.
     *  Choosing a car opens the track page, choosing a track lands on the
     *  event page (the SINGLE PLAYER screen, where the modes live). */
    void beginSessionSetup();
    bool inSetupFlow() const { return m_setupFlow; }

    /** First run: the driver's own data and the controls, before any car is
     *  picked. The app opens this page while driver.json carries no
     *  completed setup, and CONTINUE reports the finished profile. */
    void openFirstRun();
    bool setupComplete() const { return m_setupComplete; }
    void setSetupComplete(bool complete) { m_setupComplete = complete; }
    std::function<void(const DriverProfile&)> onFirstRunFinished;

    /** Inline text entry: the NAME row types in place, the app's WM_CHAR
     *  reaches the menu through NativeUiHub::handleChar. */
    bool isTextEditing() const { return m_textEditing; }
    bool handleChar(int character);

    /** First row of the list a CARS page opens on (the filters sit above it),
     *  so the page can tell a choice from a filter. */
    int firstContentRow() const { return m_firstContentRow; }

    /** The content entry behind the row under the cursor on the TRACKS page:
     *  its declared country, city, length, coordinates and circuit kind. Null
     *  on the filters, on BACK, or on every other page. */
    const ContentEntry* selectedTrackEntry() const;

    /** Silhouette of the track under the cursor on the TRACKS page, decoded
     *  from its map.png on first use and cached per folder. Null when the page
     *  is not the track list or the folder has no usable map. The pointer
     *  stays valid while the menu lives. */
    const TrackMapPreview* selectedTrackMap() const;

    /** The track's logo (ui/logo.png), same caching and null rules as the map.
     *  Most content ships none: the card then shows the track's initials. */
    const TrackLogoPreview* selectedTrackLogo() const;

    /** The weather the WEATHER page has chosen, as the session will start with
     *  it. Dry until the player picks something else. */
    const WeatherOutlook& weather() const { return m_weather; }
    /** The track the walk chose, for the page that follows it (weather card). */
    const ContentEntry* chosenTrackEntry() const {
        return m_chosenTrackValid ? &m_chosenTrack : nullptr;
    }
    /** Ask the service again for the current mode and date. Offline or on a
     *  bad answer the outlook keeps its last numbers and gains a note.
     *  Returns whether the reading came from the service. */
    bool refreshWeather();

    // ---- SESSION PLANNER --------------------------------------------------
    // The card the header opens once a session exists: the car, the track,
    // the sky and the session's own numbers, one row each, every row a click
    // away from its next value. The app marks the session created; the card
    // is the menu's, so the details it shows are the ones a start would use.
    enum SessionPlanField {
        PlanCar = 0,
        PlanTrack,
        PlanWeather,
        PlanMode,
        PlanLaps,
        PlanAiCars,
        PlanClock,
        PlanStandingStart,
        PlanPitStart,
    };
    struct SessionPlanRow {
        std::string label;  // "CAR", "WEATHER", ...
        std::string value;  // the detail as it stands
        int field = -1;     // SessionPlanField a click cycles
        std::string hint;   // what a click does, drawn under the rows
    };
    /** The session the app just started outside the pages (the env/CLI paths),
     *  so the planner card reports the session that is really running instead
     *  of the defaults the menu still holds. */
    void adoptSessionConfig(GameSessionMode mode, const SessionStartParams& params) {
        m_configMode = mode;
        m_configParams = params;
    }
    void setSessionActive(bool active);
    bool sessionActive() const { return m_sessionActive; }
    bool sessionPlannerOpen() const { return m_plannerOpen; }
    void toggleSessionPlanner();
    void closeSessionPlanner() { m_plannerOpen = false; }
    std::vector<SessionPlanRow> sessionPlanRows() const;
    /** Moves one detail to its next value. False when the field is unknown or
     *  the thing it cycles (a content list) is empty. */
    bool cycleSessionPlanField(int field);
    void showResults(std::vector<ResultsRow> rows);

    void setPitStrategy(float fuelL, bool tyres, bool body, bool susp, bool aero,
                        bool engine);

    void openPauseMenu();
    void openScreen(MenuState state) { switchMenu(state); }
    void activateSelection() { activateSelected(); }

    /** MODE SETUP: configure laps/AI/time/standing start before session start. */
    void openModeConfig(GameSessionMode mode);
    /** The weather page, the one step between the track and the event. */
    void openTrackWeather();
    /** The page's sky, on another day when asked (days from today, REAL DATE
     *  uses it): the app's capture hook, and any caller that opens the page
     *  already knowing what it should show. */
    void chooseWeatherMode(WeatherSource source, int dayOffset = 0);
    /** The weather page of a track from the library, without walking the
     *  pages: `trackHint` matches a track's label or folder name (case does
     *  not), the first track stands in when nothing matches. False when the
     *  library is empty, which leaves the menu where it was. */
    bool openWeatherPreview(const std::string& trackHint, WeatherSource source,
                            int dayOffset = 0);

    std::function<void()> onExitRequested;
    std::function<void(GameSessionMode, const SessionStartParams&)> onStartDrivingRequested;
    /** The card changed the car or the track of a session that is up: the app
     *  starts the same session again on the new content, with the sky and the
     *  numbers the card shows. */
    std::function<void(const SessionStartParams&)> onSessionRestartRequested;
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
    // GAP_MATRIX P0.5: the REPLAY page lists what the loop found on disk as
    // (file name, path) and hands the path back; the menu never parses a file
    // and never learns where recordings live beyond what it is given.
    std::function<std::vector<std::pair<std::string, std::string>>()> replayChoices;
    std::function<void(const std::string&)> onReplayFileChosen;
    /** CONTENT -> INSTALL: the packages waiting to be installed, name + path. */
    std::function<std::vector<std::pair<std::string, std::string>>()> contentPackages;
    /** Installs one package into the content tree and returns the line the page
     *  shows ("cars: x_y (12 files)" or the reason it was refused). */
    std::function<std::string(const std::string&)> onContentInstallRequested;
    /** The showroom the game opens with (folder id, empty when the first one on
     *  disk stands in). The CONTENT page marks it and sets it. */
    std::function<std::string()> showroomDefault;
    std::function<void(const std::string&, const std::string&)> onShowroomChosen;
    /** A section header was folded or unfolded: the app stores the layout. */
    std::function<void()> onSectionsChanged;
    std::function<bool()> replayRecordingState;
    std::function<void(const std::string&)> onCarChosen;
    // Bodywork paint picked on the CARS page (rgb 0..1).
    std::function<void(float, float, float)> onPaintChosen;
    std::function<void(const std::string&)> onTrackChosen;
    std::function<void(const std::string&)> onTeamChosen;
    std::function<std::vector<std::string>()> upgradeRowLabels;
    std::function<void(int)> onUpgradeRowCycled;
    // The loaded .kn5's own record (model.json's kn5 block): the file the car is
    // built from and the skin it wears. The loop owns both the values and the
    // document, so the page only shows what it reports and asks for the next
    // one — row 0 is the model, row 1 the skin.
    std::function<std::vector<std::string>()> carImportRowLabels;
    std::function<void(int)> onCarImportRowCycled;
    // CONTENT -> SKINS: the texture sets of the car that is loaded, which one it
    // wears (model.json's kn5.skin) and the choice being handed back. The loop
    // owns the car folder and the document; the page only lists and asks.
    std::function<std::vector<CarSkin>()> carSkinChoices;
    std::function<std::string()> activeCarSkin;
    std::function<void(const std::string&)> onCarSkinChosen;
    // Phase 3.3 — layout picker on the SINGLE PLAYER page. The loop owns the
    // catalog: the menu asks for (id, name) pairs and which id is bound, and
    // hands back the id it cycled to, so it never learns how a layout is
    // stored. Fewer than two layouts = no row.
    std::function<std::vector<std::pair<std::string, std::string>>()> layoutChoices;
    std::function<std::string()> activeLayoutId;
    std::function<void(const std::string&)> onLayoutChosen;
    std::function<void()> onShowResultsRequested;
    /** The RESULTS page's EXPORT row: the rows the page is showing, handed over
     *  because the page owns them and the app owns the folder. The string it
     *  returns is the answer the page prints under its rows. */
    std::function<std::string(const std::vector<ResultExportRow>&)> onExportResults;
    std::function<void(const std::string&)> onOpenSettingsPanelRequested;
    std::function<void(const MenuOptions&)> onOptionsChanged;
    std::function<void(MenuOptions&)> onOptionsRefresh;
    std::function<void(int width, int height)> onResolutionRequested;
    std::function<void()> onHostServerRequested;
    std::function<void()> onOpenServerBrowserRequested;
    std::function<void()> onDisconnectRequested;

private:
    void startDriving(GameSessionMode mode, const SessionStartParams& params);
    void buildMainMenu();
    void buildSingleplayerMenu();
    std::vector<GameSessionMode> m_singleplayerModes; // empty = built-in eight
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
    bool m_collapsedSections[kSectionCount] = {false, false, false, false, false};

    void buildContentManagerMenu();
    void buildContentPackagesMenu();
    void buildShowroomSelectMenu();
    void buildSkinsMenu();
    // One page for both driver-gear libraries: same rows, same rule (the
    // player's document names the one being worn).
    void buildDriverGearMenu(bool helmets);
    /** Library sizes for the CONTENT page: rescanned when the page opens, so an
     *  installed package shows up without a restart. */
    void refreshContentCounts();
    struct ContentLibraryCounts {
        std::string carsRoot;
        std::string tracksRoot;
        std::string teamsRoot;
        std::string showroomsRoot;
        size_t cars = 0;
        size_t tracks = 0;
        size_t teams = 0;
        size_t showrooms = 0;
    };
    ContentLibraryCounts m_contentCounts;
    bool m_contentCountsValid = false;
    std::string m_contentInstallStatus;
    // The manifest row's answer (written / not written and where), shown under
    // it until the page is opened again.
    std::string m_manifestStatus;
    // The RESULTS page's answer to the last export, shown under its rows until
    // the page is opened again.
    std::string m_resultsStatus;
    void writeContentManifestNow();
    // Last showroom chosen on the page, so the mark moves before the app's
    // settings are read back.
    std::string m_showroomDefaultId;
    void buildSettingsMenu();
    void buildOptionsMenu();
    void buildControlsMenu();
    void buildCarSelectMenu();
    void applyCarFilter();
    void applyTrackFilter();
    void buildTrackSelectMenu();
    void buildTrackWeatherMenu();
    void setWeatherMode(WeatherSource source);
    void buildFirstRunMenu();
    void beginTextEdit(const std::string& field, const std::string& value);
    void commitTextEdit();
    void cancelTextEdit();
    void buildTeamSelectMenu();
    void buildResultsMenu();
    void buildCreditsMenu();
    void buildCarDataMenu();
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

    bool m_shaderReloadRequested = false;
    int m_qualityIdx = 0;
    int kNumQualities = 3;
    int m_aaIdx = 0;
    int kNumAAModes = 3;
    MenuState m_currentState = MenuState::Main;
    MenuState m_previousState = MenuState::Main;
    int m_selectedIndex = 0;
    int m_hoverIndex = -1;
    /** A burst of keys is drained once per rendered block, so this holds a few
     *  and drops the rest instead of growing without a player. */
    static constexpr int kMaxPendingSounds = 8;
    ui::UiSound m_pendingSounds[kMaxPendingSounds]{};
    int m_pendingSoundCount = 0;

    void emitSound(ui::UiSound sound) {
        if (m_pendingSoundCount >= kMaxPendingSounds) return;
        m_pendingSounds[m_pendingSoundCount++] = sound;
    }
    float m_animationTime = 0.0f;
    float m_fadeAlpha = 1.0f;
    float m_transitionProgress = 1.0f;
    std::string m_trackName;
    std::string m_carName;
    std::string m_carDir;
    std::string m_teamName;

    DriverProfile m_profile;
    MenuOptions m_options;
    std::vector<MenuItem> m_items;
    std::vector<ContentEntry> m_selectEntries;
    // Folder the current select page lists from, named by its empty-list hint
    // (it is not always content/<kind>: see ContentLibrary.h contentRootCandidates).
    std::vector<ContentEntry> m_trackLibrary;
    std::string m_selectRoot;
    std::vector<std::string> m_trackCountries;
    std::vector<std::string> m_trackCircuitTypes;
    std::string m_trackCountryFilter;
    std::string m_trackCircuitTypeFilter;
    // map.png silhouettes and logos, one per track visited (see
    // selectedTrackMap / selectedTrackLogo).
    mutable std::map<std::string, TrackMapPreview> m_trackMaps;
    mutable std::map<std::string, TrackLogoPreview> m_trackLogos;
    // CARS page: the whole library, the filter values it actually carries and
    // the current selection. Filters exist only for metadata a car declares.
    std::vector<ContentEntry> m_carLibrary;
    // Paints of the car chosen last: the page keeps them so reopening CARS
    // can cycle them on the body behind the menu.
    std::vector<CarPaint> m_carPaints;
    std::size_t m_carPaintIndex = 0;
    std::vector<std::string> m_carClasses;
    std::vector<int> m_carYears;
    std::vector<std::string> m_carBrands;
    std::string m_carClassFilter;
    int m_carYearFilter = 0;
    std::string m_carBrandFilter;
    std::vector<ResultsRow> m_resultRows;

    float m_pitFuelTargetL = 60.f;
    bool m_pitWantTyres = true;
    bool m_pitWantBody = true;
    bool m_pitWantSuspension = true;
    bool m_pitWantAero = false;
    bool m_pitWantEngine = false;

    GameSessionMode m_configMode = GameSessionMode::Race;
    SessionStartParams m_configParams{};

    // CARS -> TRACKS -> EVENT: the chain the pre-session pages walk. Set when
    // a session setup starts and cleared when the player is back on MAIN.
    bool m_setupFlow = false;
    bool m_setupComplete = false;

    // WEATHER page: the track the walk chose (its coordinates are the lookup
    // point), the mode, the date a real lookup asks about, and the outlook the
    // session starts with.
    ContentEntry m_chosenTrack;
    bool m_chosenTrackValid = false;
    /** SESSION PLANNER tail of a car or track change: a session that is up
     *  starts again on the new content. True when there is nothing to do. */
    bool restartAfterPlanChange(bool sessionWasActive);

    bool m_sessionActive = false;  // a session was started and has not been stopped
    bool m_plannerOpen = false;    // the header's session card is on screen
    WeatherOutlook m_weather = dryOutlook();
    WeatherSource m_weatherMode = WeatherSource::Dry;
    std::string m_weatherDate = todayDateText();
    bool m_weatherFetched = false;
    // Row a freshly opened page puts the cursor on (first car of the list,
    // first mode of the event page), consumed by the builder itself.
    bool m_focusContent = false;
    int m_firstContentRow = 0;
    int m_firstModeRow = 0;

    bool m_textEditing = false;
    std::string m_textField;
    std::string m_textBuffer;
};

} // namespace ks::sim
