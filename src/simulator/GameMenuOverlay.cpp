#include "GameMenuOverlay.h"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ks::sim {

namespace {
// The eight modes the config ships, used when a file lists none. The labels and
// hints stay here: they are copy, not configuration, and a mode the parser
// cannot name never reaches the menu.
struct SingleplayerModeRow {
    GameSessionMode mode;
    const char* label;
    const char* hint;
};
constexpr SingleplayerModeRow kSingleplayerModes[] = {
    { GameSessionMode::Practice, "PRACTICE", "Open session on the selected circuit" },
    { GameSessionMode::Hotlap, "HOTLAP", "Flying laps on your own" },
    { GameSessionMode::TimeAttack, "TIME ATTACK", "Clean laps against the clock" },
    { GameSessionMode::Weekend, "WEEKEND", "Practice, qualifying and race" },
    { GameSessionMode::Championship, "CHAMPIONSHIP", "A season across circuits" },
    { GameSessionMode::Race, "QUICK RACE", "Grid start against AI" },
    { GameSessionMode::Drift, "DRIFT", "Score runs on the drift pad" },
    { GameSessionMode::DragRace, "DRAG RACE", "Standing start, quarter mile" },
};

const char* singleplayerLabel(GameSessionMode mode) {
    for (const SingleplayerModeRow& row : kSingleplayerModes)
        if (row.mode == mode) return row.label;
    return "SESSION";
}

const char* singleplayerHint(GameSessionMode mode) {
    for (const SingleplayerModeRow& row : kSingleplayerModes)
        if (row.mode == mode) return row.hint;
    return "Start this session";
}
} // namespace

std::vector<GameSessionMode> GameMenuOverlay::effectiveSingleplayerModes() const {
    if (!m_singleplayerModes.empty()) return m_singleplayerModes;
    std::vector<GameSessionMode> modes;
    for (const SingleplayerModeRow& row : kSingleplayerModes) modes.push_back(row.mode);
    return modes;
}

GameMenuOverlay::GameMenuOverlay()
{
    buildMainMenu();
}

std::string GameMenuOverlay::sectionTitle() const
{
    switch (m_currentState) {
    case MenuState::Main: return "MAIN MENU";
    case MenuState::Singleplayer: return "SINGLE PLAYER";
    case MenuState::ModeConfig: return "MODE SETUP";
    case MenuState::Multiplayer: return "MULTI PLAYER";
    case MenuState::Profile: return "DRIVER";
    case MenuState::Garage: return "GARAGE";
    case MenuState::PitStrategy: return "PIT STRATEGY";
    case MenuState::Replay: return "REPLAY";
    case MenuState::ContentManager: return "CONTENT";
    case MenuState::Settings: return "SETTINGS";
    case MenuState::Options: return "VIDEO";
    case MenuState::Controls: return "CONTROLS";
    case MenuState::CarSelect: return "SELECT CAR";
    case MenuState::TrackSelect: return "SELECT TRACK";
    case MenuState::TeamSelect: return "SELECT TEAM";
    case MenuState::Results: return "RESULTS";
    case MenuState::Pause: return "PAUSED";
    case MenuState::DevModeConfirm: return "EDITOR";
    case MenuState::Credits: return "CREDITS";
    case MenuState::QuitConfirm: return "EXIT";
    }
    return "MENU";
}

void GameMenuOverlay::setVisible(bool visible)
{
    if (m_visible == visible) return;
    m_visible = visible;
    m_menuDirty = true;
    if (visible) {
        m_fadeAlpha = 0.0f;
        m_transitionProgress = 0.0f;
    }
}

void GameMenuOverlay::toggleVisible()
{
    setVisible(!m_visible);
}

void GameMenuOverlay::setProfileField(const std::string& fieldName, const std::string& value)
{
    if (fieldName == "NAME") m_profile.name = value;
    else if (fieldName == "BIO") m_profile.bio = value;
    else if (fieldName == "NATIONALITY") m_profile.nationality = value;
    m_menuDirty = true;
}

void GameMenuOverlay::setNationalityFromList(int index)
{
    static const char* nationalities[] = {
        "IT", "DE", "GB", "FR", "ES", "US", "JP", "BR",
        "AU", "CA", "NL", "FI", "SE", "BE", "CH", "AT"
    };
    if (index >= 0 && index < 16) {
        m_profile.nationality = nationalities[index];
        m_menuDirty = true;
    }
}

void GameMenuOverlay::startDriving(GameSessionMode mode, const SessionStartParams& params)
{
    if (onStartDrivingRequested) onStartDrivingRequested(mode, params);
    setVisible(false);
}

// MODE SETUP: the limits of the session before it starts. It lives one level
// under SINGLE PLAYER, so the mode comes from the row that opened it and the
// values begin at that mode's own defaults.
void GameMenuOverlay::openModeConfig(GameSessionMode mode)
{
    m_configMode = mode;
    m_configParams = defaultsForMode(mode);
    switchMenu(MenuState::ModeConfig);
}

void GameMenuOverlay::buildModeConfigMenu()
{
    m_items.clear();
    const SessionStartParams& p = m_configParams;
    char text[96];
    char now[40];

    auto clockText = [](int seconds, char* out, size_t n) {
        if (seconds <= 0) std::snprintf(out, n, "no time limit");
        else std::snprintf(out, n, "%d:%02d", seconds / 60, seconds % 60);
    };

    std::snprintf(now, sizeof(now), "now %d lap(s)", p.totalLaps);
    m_items.push_back({ "LAPS  -1", p.totalLaps > 0 ? now : "now: open session",
        [this]() {
            m_configParams.totalLaps = std::max(0, m_configParams.totalLaps - 1);
            m_menuDirty = true;
        }, false, true, "flag" });
    m_items.push_back({ "LAPS  +1", "0 keeps the session open until you stop",
        [this]() {
            m_configParams.totalLaps = std::min(500, m_configParams.totalLaps + 1);
            m_menuDirty = true;
        }, false, true, "flag" });

    std::snprintf(now, sizeof(now), "now %d car(s)", p.aiCars);
    m_items.push_back({ "AI CARS  -1", now,
        [this]() {
            m_configParams.aiCars = std::max(0, m_configParams.aiCars - 1);
            m_menuDirty = true;
        }, false, true, "users" });
    m_items.push_back({ "AI CARS  +1", "Up to 31 on the grid",
        [this]() {
            m_configParams.aiCars = std::min(31, m_configParams.aiCars + 1);
            m_menuDirty = true;
        }, false, true, "users" });

    clockText(p.sessionTimeSeconds, now, sizeof(now));
    m_items.push_back({ "CLOCK  -5 min", now,
        [this]() {
            m_configParams.sessionTimeSeconds =
                std::max(0, m_configParams.sessionTimeSeconds - 300);
            m_menuDirty = true;
        } });
    m_items.push_back({ "CLOCK  +5 min", "0 runs the session on laps only",
        [this]() {
            m_configParams.sessionTimeSeconds =
                std::min(3 * 3600, m_configParams.sessionTimeSeconds + 300);
            m_menuDirty = true;
        } });

    std::snprintf(text, sizeof(text), "STANDING START  [%s]",
                  p.standingStart ? "ON" : "OFF");
    m_items.push_back({ text, "OFF starts rolling instead of counting down",
        [this]() {
            m_configParams.standingStart = !m_configParams.standingStart;
            m_menuDirty = true;
        } });

    std::snprintf(text, sizeof(text), "START IN GARAGE  [%s]",
                  p.startInGarage ? "ON" : "OFF");
    m_items.push_back({ text, "Begin in the pit box instead of on the grid",
        [this]() {
            m_configParams.startInGarage = !m_configParams.startInGarage;
            m_menuDirty = true;
        } });

    m_items.push_back({ "", "", nullptr, true });

    const char* modeName = sessionModeName(m_configMode);
    if (!modeName) modeName = "SESSION";
    clockText(p.sessionTimeSeconds, now, sizeof(now));
    if (p.totalLaps > 0)
        std::snprintf(text, sizeof(text), "%s - %d lap(s), %d AI, %s",
                      modeName, p.totalLaps, p.aiCars, now);
    else
        std::snprintf(text, sizeof(text), "%s - open, %d AI, %s",
                      modeName, p.aiCars, now);
    m_items.push_back({ "START SESSION", text,
        [this]() { startDriving(m_configMode, m_configParams); }, false, true, "flag" });
    m_items.push_back({ "BACK", "Back to the mode list",
        [this]() { goBack(); } });
}

void GameMenuOverlay::buildMainMenu()
{
    m_items.clear();
    // The row order is the tile order of the startup screen (ui/MainMenu.h):
    // the session first, then the driver, the content and the settings, then
    // the three rows that do not open a part of the game. Every row names an
    // icon, because a tile is drawn as its section's glyph behind the label.
    //
    // GARAGE and REPLAY used to live here. The garage is one level down, in
    // the in-car pause overlay, where a running session is what a setup is
    // for; this screen only opens it through the pause screen now.
    m_items.push_back({ "SINGLE PLAYER", "Practice, hotlap, time attack and more",
        [this]() { switchMenu(MenuState::Singleplayer); }, false, true, "user" });
    m_items.push_back({ "MULTIPLAYER", "Online and LAN events, ranked or not",
        [this]() { switchMenu(MenuState::Multiplayer); }, false, true, "users" });
    // The quick actions - clock, sound, VR screen, controller - belong to the
    // header bar, not to this row: ui/MainMenu.h lays them out and the hub
    // runs them, so they are no longer menu rows.
    m_items.push_back({ "DRIVER", "Profile and career stats",
        [this]() { switchMenu(MenuState::Profile); }, false, true, "helmet" });
    m_items.push_back({ "CONTENT", "Cars, tracks and packages",
        [this]() { switchMenu(MenuState::ContentManager); }, false, true, "download" });
    m_items.push_back({ "SETTINGS", "Display, audio and input",
        [this]() { switchMenu(MenuState::Settings); }, false, true, "settings" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "EDITOR", "Leave simulator and open the editor",
        [this]() { switchMenu(MenuState::DevModeConfirm); }, false, true, "setup" });
    m_items.push_back({ "CREDITS", "What ksim is built from",
        [this]() { switchMenu(MenuState::Credits); }, false, true, "info" });
    m_items.push_back({ "EXIT", "Exit ksim",
        [this]() { switchMenu(MenuState::QuitConfirm); }, false, true, "exit" });
}

void GameMenuOverlay::buildSingleplayerMenu()
{
    m_items.clear();
    m_items.push_back({ "CAR", m_carName.empty() ? "Select a car" : m_carName,
        [this]() { openCarSelect(); } });
    m_items.push_back({ "TRACK", m_trackName.empty() ? "Select a track" : m_trackName,
        [this]() { openTrackSelect(); } });
    // Roadmap 2.8: optional team roster (drivers, numbers, liveries, grid).
    // It never gates the session rows — no team keeps the legacy identity.
    m_items.push_back({ "TEAM", m_teamName.empty() ? "Select a team (optional)" : m_teamName,
        [this]() { openTeamSelect(); } });
    m_items.push_back({ "", "", nullptr, true });
    // Fase 1 exit ("sessione practice da giocatore"): a session starts only
    // once BOTH sides are chosen — otherwise the player would drop into an
    // empty world with no explanation. The disabled rows say what is missing.
    const bool carOk = !m_carName.empty();
    const bool trackOk = !m_trackName.empty();
    const bool ready = carOk && trackOk;
    std::string hint;
    if (!carOk && !trackOk) hint = "Select a car and a track first";
    else if (!carOk) hint = "Select a car first";
    else hint = "Select a track first";
    // One row per entry of singleplayer.modes, in the order the config lists
    // them; a file that names none gets the built-in eight. Each row starts the
    // session with that mode's own defaults (laps, grid, clock).
    for (GameSessionMode mode : effectiveSingleplayerModes()) {
        m_items.push_back({ singleplayerLabel(mode), ready ? singleplayerHint(mode) : hint,
            [this, mode]() { openModeConfig(mode); }, false, ready });
    }
    m_items.push_back({ "RESULTS", "Session standings",
        [this]() { if (onShowResultsRequested) onShowResultsRequested(); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return",
        [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildMultiplayerMenu()
{
    m_items.clear();
    m_items.push_back({ "HOST SESSION", "Run a server on this machine (LAN/online)",
        [this]() { if (onHostServerRequested) onHostServerRequested(); },
        false, true, "users" });
    m_items.push_back({ "JOIN SESSION", "Open the server browser, F2 also brings it up",
        [this]() { if (onOpenServerBrowserRequested) onOpenServerBrowserRequested(); },
        false, true, "download" });
    m_items.push_back({ "DISCONNECT", "Leave the current session or stop the server",
        [this]() { if (onDisconnectRequested) onDisconnectRequested(); },
        false, true, "exit" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildProfileMenu()
{
    m_items.clear();
    // Every row edits one field of the profile the header plate and the
    // results read from. The bio and the race number are fields the overlay
    // already carries, so the page lists them instead of losing them.
    m_items.push_back({ "NAME", m_profile.name,
        [this]() { if (onTextInputRequested) onTextInputRequested("NAME", m_profile.name); },
        false, true, "user" });
    m_items.push_back({ "NATIONALITY", m_profile.nationality,
        [this]() { if (onNationalityInputRequested) onNationalityInputRequested(); },
        false, true, "helmet" });
    m_items.push_back({ "NUMBER", std::to_string(m_profile.raceNumber),
        [this]() { m_profile.raceNumber = (m_profile.raceNumber % 99) + 1; m_menuDirty = true; },
        false, true, "drive" });
    m_items.push_back({ "BIO", m_profile.bio.empty() ? "Add a short biography" : m_profile.bio,
        [this]() { if (onTextInputRequested) onTextInputRequested("BIO", m_profile.bio); },
        false, true, "info" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildGarageMenu()
{
    m_items.clear();
    m_items.push_back({ "SETUP", "Aero, gears, brakes and dampers",
        [this]() { if (onOpenSetupGarageRequested) onOpenSetupGarageRequested(); } });
    // Roadmap 2.9: one row per upgrade package of the loaded car — the label
    // shows the selected level, activating cycles to the next one and the
    // loop re-applies physics, nodes, livery texture and sound pack.
    if (upgradeRowLabels) {
        const std::vector<std::string> labels = upgradeRowLabels();
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            const int row = i;
            m_items.push_back({ labels[i], "Cycle to the next level",
                [this, row]() {
                    if (onUpgradeRowCycled) onUpgradeRowCycled(row);
                    m_menuDirty = true; // rebuild so the label shows the new level
                } });
        }
    }
    m_items.push_back({ "PIT STRATEGY", "Fuel, tyres and repair plan for next stop",
        [this]() {
            if (onOpenPitStrategyRequested) onOpenPitStrategyRequested();
            switchMenu(MenuState::PitStrategy);
        } });
    // Roadmap 2.7: leaving and re-entering the box are menu actions too (the
    // P key does the same smart toggle while driving). The loop decides
    // whether either applies — on track / already out they are no-ops.
    m_items.push_back({ "LEAVE GARAGE", "Roll out to the pit lane (pit limit)",
        [this]() {
            if (onLeaveGarageRequested) onLeaveGarageRequested();
            goBack();
        } });
    m_items.push_back({ "RETURN TO BOX", "Drive back in slowly to stop for service",
        [this]() {
            if (onReturnToBoxRequested) onReturnToBoxRequested();
            goBack();
        } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::setPitStrategy(float fuelL, bool tyres, bool body, bool susp,
                                     bool aero, bool engine)
{
    m_pitFuelTargetL = fuelL;
    m_pitWantTyres = tyres;
    m_pitWantBody = body;
    m_pitWantSuspension = susp;
    m_pitWantAero = aero;
    m_pitWantEngine = engine;
    if (m_currentState == MenuState::PitStrategy)
        refreshPitStrategy();
}

void GameMenuOverlay::refreshPitStrategy()
{
    if (m_currentState != MenuState::PitStrategy) return;
    const int sel = m_selectedIndex;
    buildPitStrategyMenu();
    m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
    m_menuDirty = false;
}

void GameMenuOverlay::buildPitStrategyMenu()
{
    m_items.clear();
    char fuelLine[64];
    std::snprintf(fuelLine, sizeof(fuelLine), "FUEL TARGET  %.0f L", m_pitFuelTargetL);
    m_items.push_back({ fuelLine, "Target fuel after stop", nullptr, false, false });
    m_items.push_back({ "FUEL -5 L", "Reduce target",
        [this]() {
            m_pitFuelTargetL = std::max(0.f, m_pitFuelTargetL - 5.f);
            m_menuDirty = true;
        } });
    m_items.push_back({ "FUEL +5 L", "Increase target",
        [this]() {
            m_pitFuelTargetL = std::min(120.f, m_pitFuelTargetL + 5.f);
            m_menuDirty = true;
        } });
    m_items.push_back({ "", "", nullptr, true });

    auto toggle = [this](const char* label, bool& flag, const char* desc) {
        char t[80];
        std::snprintf(t, sizeof(t), "%s  [%s]", label, flag ? "ON" : "OFF");
        m_items.push_back({ t, desc, [&flag, this]() {
            flag = !flag;
            m_menuDirty = true;
        } });
    };
    toggle("TYRES", m_pitWantTyres, "Change all four tyres");
    toggle("BODY", m_pitWantBody, "Repair bodywork");
    toggle("SUSPENSION", m_pitWantSuspension, "Repair suspension");
    toggle("AERO", m_pitWantAero, "Repair wings / aero");
    toggle("ENGINE", m_pitWantEngine, "Engine service");

    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "CONFIRM PLAN", "Apply strategy for next pit stop",
        [this]() {
            if (onPitStrategyConfirmRequested)
                onPitStrategyConfirmRequested(
                    m_pitFuelTargetL, m_pitWantTyres, m_pitWantBody,
                    m_pitWantSuspension, m_pitWantAero, m_pitWantEngine);
        } });
    // Roadmap 2.7: the plan alone queues nothing. This is the player's
    // separate "I am stopped in the box, do the confirmed work now" signal;
    // the loop accepts it only inside the garage box (updatePitRepair).
    m_items.push_back({ "REQUEST SERVICE", "Start the confirmed jobs in the box",
        [this]() {
            if (onPitServiceRequested) onPitServiceRequested();
        } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildReplayMenu()
{
    m_items.clear();
    m_items.push_back({ "LOAD", "Open a saved session",
        [this]() { if (onLoadReplayRequested) onLoadReplayRequested(); },
        false, true, "download" });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildContentManagerMenu()
{
    m_items.clear();
    // The three libraries the loader scans. CARS and TRACKS are reachable
    // from a session too; TEAMS was the one with no way in from here.
    m_items.push_back({ "CARS", "Browse vehicles in content/cars",
        [this]() { openCarSelect(); }, false, true, "drive" });
    m_items.push_back({ "TRACKS", "Browse circuits in content/tracks",
        [this]() { openTrackSelect(); }, false, true, "restart" });
    m_items.push_back({ "TEAMS", "Driver rosters in content/teams (optional)",
        [this]() { openTeamSelect(); }, false, true, "users" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildSettingsMenu()
{
    m_items.clear();
    m_items.push_back({ "VIDEO", "Resolution, image and quality",
        [this]() { openOptions(); }, false, true, "settings" });
    m_items.push_back({ "AUDIO", "Master, engine and environment volumes",
        [this]() { openOptionsAudio(); }, false, true, "speaker" });
    m_items.push_back({ "CONTROLS", "Bindings and devices",
        [this]() { switchMenu(MenuState::Controls); }, false, true, "keyboard" });
    // Real window toggle (already wired), listed here because this is where
    // players look for it.
    m_items.push_back({ "FULLSCREEN", "Switch between window and full screen",
        [this]() { if (onToggleFullscreenRequested) onToggleFullscreenRequested(); },
        false, true, "vr" });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

int GameMenuOverlay::resolutionIndexFor(int width, int height)
{
    int best = 0;
    int bestDelta = 1 << 30;
    for (int i = 0; i < kResolutionCount; ++i) {
        const int delta = std::abs(kResolutionWidths[i] - width) +
                          std::abs(kResolutionHeights[i] - height);
        if (delta < bestDelta) { bestDelta = delta; best = i; }
    }
    return best;
}

int GameMenuOverlay::exposureIndexFor(float exposure)
{
    int best = 0;
    for (int i = 1; i < kExposureCount; ++i) {
        if (std::fabs(kExposureValues[i] - exposure) <
            std::fabs(kExposureValues[best] - exposure))
            best = i;
    }
    return best;
}

void GameMenuOverlay::openOptionsAudio()
{
    refreshOptions();
    switchMenu(MenuState::Options);
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].text.rfind("MASTER", 0) == 0) {
            m_selectedIndex = i;
            break;
        }
    }
}

void GameMenuOverlay::applyOptions()
{
    m_menuDirty = true;
    if (onOptionsChanged) onOptionsChanged(m_options);
    // Every label carries its value, so the rows are rebuilt in place and the
    // cursor stays on the row the player just moved.
    if (m_currentState == MenuState::Options) {
        const int sel = m_selectedIndex;
        buildOptionsMenu();
        m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
        m_menuDirty = false;
    }
}

void GameMenuOverlay::buildOptionsMenu()
{
    // Laid out like Content Manager's Video page: resolution and frame pacing
    // first, then quality, then image, then sound. Only knobs the renderer or
    // the mixer really honours are listed: no MSAA row while the pipelines are
    // built with one sample, no shadow resolution while the map size is fixed.
    m_items.clear();
    char label[96];
    const MenuOptions& o = m_options;
    const auto toggleRow = [this](const char* name, bool MenuOptions::*field,
                                  const char* description) {
        char text[96];
        std::snprintf(text, sizeof(text), "%s  [%s]", name,
                      (m_options.*field) ? "ON" : "OFF");
        m_items.push_back({ text, description, [this, field]() {
            m_options.*field = !(m_options.*field);
            applyOptions();
        }, false, true, "settings" });
    };

    std::snprintf(label, sizeof(label), "RESOLUTION  %dx%d",
                  resolutionWidth(o.resolutionIndex),
                  resolutionHeight(o.resolutionIndex));
    m_items.push_back({ label, "Window size; fullscreen follows the monitor",
        [this]() {
            m_options.resolutionIndex =
                (m_options.resolutionIndex + 1) % kResolutionCount;
            if (onResolutionRequested)
                onResolutionRequested(resolutionWidth(m_options.resolutionIndex),
                                      resolutionHeight(m_options.resolutionIndex));
            applyOptions();
        }, false, true, "settings" });

    // FULLSCREEN goes through the window path F11 already uses, and the value
    // is re-read from the window style every time the page opens.
    std::snprintf(label, sizeof(label), "FULLSCREEN  [%s]",
                  o.fullscreen ? "ON" : "OFF");
    m_items.push_back({ label, "Window or exclusive monitor",
        [this]() {
            m_options.fullscreen = !m_options.fullscreen;
            if (onToggleFullscreenRequested) onToggleFullscreenRequested();
            applyOptions();
        }, false, true, "vr" });

    toggleRow("VSYNC", &MenuOptions::vsync,
              "FIFO when on, immediate present when off");

    m_items.push_back({ "", "", nullptr, true });
    toggleRow("SSAO", &MenuOptions::ssao,
              "Ambient occlusion, deferred pass only");
    toggleRow("SSR", &MenuOptions::ssr,
              "Screen-space reflections, deferred pass only");
    toggleRow("MOTION BLUR", &MenuOptions::motionBlur,
              "Resolve-pass blur while the camera moves");

    m_items.push_back({ "", "", nullptr, true });
    std::snprintf(label, sizeof(label), "EXPOSURE  %.2f",
                  static_cast<double>(exposureValue(o.exposureIndex)));
    m_items.push_back({ label, "Display exposure of the final image",
        [this]() {
            m_options.exposureIndex = (m_options.exposureIndex + 1) % kExposureCount;
            applyOptions();
        }, false, true, "settings" });

    std::snprintf(label, sizeof(label), "TONEMAP  %s", tonemapName(o.tonemap));
    m_items.push_back({ label, "Tone curve the display pass uses",
        [this]() {
            m_options.tonemap = (m_options.tonemap + 1) % kTonemapCount;
            applyOptions();
        }, false, true, "settings" });

    // Shaders are compiled from the .spv set at every start, but a running
    // session keeps the pipelines it built: this row is how a replaced .spv or
    // a driver update reaches the frame already on screen.
    m_items.push_back({ "RELOAD SHADERS", "Re-read the .spv set and rebuild every pipeline",
        [this]() { requestShaderReload(); }, false, true, "settings" });

    m_items.push_back({ "", "", nullptr, true });
    // Only the master gain is a setting: the engine and environment channels
    // are the session's (silent in the showroom, live in the car).
    std::snprintf(label, sizeof(label), "MASTER VOLUME  %.0f%%",
                  static_cast<double>(o.masterVolume * 100.f));
    m_items.push_back({ label, "Everything the simulator plays",
        [this]() {
            const float value = m_options.masterVolume;
            m_options.masterVolume = value > 0.899f
                ? 0.f
                : std::min(1.f, std::round((value + 0.1f) * 10.f) / 10.f);
            applyOptions();
        }, false, true, "speaker" });

    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildControlsMenu()
{
    m_items.clear();
    m_items.push_back({ "KEYBOARD", "Driving bindings", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("keyboard");
    }, false, true, "keyboard" });
    m_items.push_back({ "WHEEL", "Device and FFB", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("devices");
    }, false, true, "pad" });
    m_items.push_back({ "BACK", "Return", [this]() { switchMenu(MenuState::Settings); },
        false, true, "back" });
}

void GameMenuOverlay::openCarSelect(const std::string& root)
{
    m_carLibrary = scanCarLibrary(root);
    m_carClassFilter.clear();
    m_carYearFilter = 0;
    m_carClasses.clear();
    m_carYears.clear();
    for (const auto& e : m_carLibrary) {
        if (!e.carClass.empty() &&
            std::find(m_carClasses.begin(), m_carClasses.end(), e.carClass) == m_carClasses.end())
            m_carClasses.push_back(e.carClass);
        if (e.year > 0 &&
            std::find(m_carYears.begin(), m_carYears.end(), e.year) == m_carYears.end())
            m_carYears.push_back(e.year);
    }
    std::sort(m_carClasses.begin(), m_carClasses.end());
    std::sort(m_carYears.begin(), m_carYears.end());
    applyCarFilter();
    switchMenu(MenuState::CarSelect);
}

void GameMenuOverlay::applyCarFilter()
{
    m_selectEntries = filterCars(m_carLibrary, std::string(), m_carClassFilter,
                                 m_carYearFilter, m_carYearFilter);
}

void GameMenuOverlay::openTrackSelect()
{
    m_selectEntries = scanTrackLibrary();
    switchMenu(MenuState::TrackSelect);
}

void GameMenuOverlay::openTeamSelect()
{
    m_selectEntries = scanTeamLibrary();
    switchMenu(MenuState::TeamSelect);
}

void GameMenuOverlay::showResults(std::vector<ResultsRow> rows)
{
    m_resultRows = std::move(rows);
    switchMenu(MenuState::Results);
    setVisible(true);
}

void GameMenuOverlay::buildCarSelectMenu()
{
    m_items.clear();
    char label[96];

    // Only the metadata the library carries gets a row: a content folder with
    // no class never sees a CLASS row it could not fill.
    if (!m_carClasses.empty()) {
        std::snprintf(label, sizeof(label), "CLASS  %s",
                      m_carClassFilter.empty() ? "any" : m_carClassFilter.c_str());
        m_items.push_back({ label, "Show one class only", [this]() {
            size_t next = 0;
            if (!m_carClassFilter.empty()) {
                const auto it = std::find(m_carClasses.begin(), m_carClasses.end(),
                                          m_carClassFilter);
                if (it != m_carClasses.end())
                    next = static_cast<size_t>(it - m_carClasses.begin()) + 1;
            }
            m_carClassFilter = next >= m_carClasses.size() ? std::string() : m_carClasses[next];
            applyCarFilter();
            m_menuDirty = true;
        }, false, true, "flag" });
    }
    if (!m_carYears.empty()) {
        std::snprintf(label, sizeof(label), "YEAR  %s",
                      m_carYearFilter > 0 ? std::to_string(m_carYearFilter).c_str() : "any");
        m_items.push_back({ label, "Show one model year only", [this]() {
            size_t next = 0;
            if (m_carYearFilter > 0) {
                const auto it = std::find(m_carYears.begin(), m_carYears.end(), m_carYearFilter);
                if (it != m_carYears.end())
                    next = static_cast<size_t>(it - m_carYears.begin()) + 1;
            }
            m_carYearFilter = next >= m_carYears.size() ? 0 : m_carYears[next];
            applyCarFilter();
            m_menuDirty = true;
        }, false, true, "flag" });
    }
    if (!m_carPaints.empty()) {
        const std::size_t index = std::min(m_carPaintIndex, m_carPaints.size() - 1);
        std::snprintf(label, sizeof(label), "PAINT  %s", m_carPaints[index].name.c_str());
        m_items.push_back({ label, "Repaint the bodywork", [this]() {
            if (m_carPaints.empty()) return;
            m_carPaintIndex = (m_carPaintIndex + 1) % m_carPaints.size();
            if (onPaintChosen) {
                const CarPaint& p = m_carPaints[m_carPaintIndex];
                onPaintChosen(p.r, p.g, p.b);
            }
            m_menuDirty = true;
        }, false, true, "flag" });
    }
    if (!m_items.empty())
        m_items.push_back({ "", "", nullptr, true });

    if (m_selectEntries.empty()) {
        m_items.push_back({ m_carLibrary.empty() ? "(no cars found)" : "(no car matches)",
            m_carLibrary.empty() ? "Put car folders in content/cars"
                                 : "Set the filters back to any", nullptr });
    } else {
        for (const auto& e : m_selectEntries) {
            const std::string path = e.path;
            std::string detail = e.brand;
            if (!e.carClass.empty()) detail += (detail.empty() ? "" : " - ") + e.carClass;
            if (e.year > 0) detail += (detail.empty() ? "" : " - ") + std::to_string(e.year);
            if (detail.empty()) detail = e.path;
            const std::vector<CarPaint> paints = e.paints;
            m_items.push_back({ e.label, detail, [this, path, paints]() {
                m_carPaints = paints;
                m_carPaintIndex = 0;
                if (onCarChosen) onCarChosen(path);
                // The car's own first colour is its paint, applied as soon as
                // the body exists; without a list the texture shows as baked.
                if (!m_carPaints.empty() && onPaintChosen) {
                    const CarPaint& p = m_carPaints[0];
                    onPaintChosen(p.r, p.g, p.b);
                }
                goBack();
            } });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildTrackSelectMenu()
{
    m_items.clear();
    if (m_selectEntries.empty()) {
        m_items.push_back({ "(no tracks found)",
            "Put track folders in content/tracks", nullptr });
    } else {
        for (const auto& e : m_selectEntries) {
            const std::string path = e.path;
            m_items.push_back({ e.label, e.path, [this, path]() {
                if (onTrackChosen) onTrackChosen(path);
                goBack();
            } });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

// Roadmap 2.8: teams are folders holding a team.ini under content/teams —
// same list screen as cars/tracks, applying the roster the moment it is hit.
void GameMenuOverlay::buildTeamSelectMenu()
{
    m_items.clear();
    if (m_selectEntries.empty()) {
        m_items.push_back({ "(no teams found)",
            "Put team folders (team.ini) in content/teams", nullptr });
    } else {
        for (const auto& e : m_selectEntries) {
            const std::string path = e.path;
            m_items.push_back({ e.label, e.path, [this, path]() {
                if (onTeamChosen) onTeamChosen(path);
                goBack();
            } });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildResultsMenu()
{
    m_items.clear();
    if (m_resultRows.empty()) {
        m_items.push_back({ "(no results yet)",
            "Finish or open a session first", nullptr });
    } else {
        for (const auto& r : m_resultRows)
            m_items.push_back({ r.position + "  " + r.driver, r.detail, nullptr });
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

// The in-car overlay (ESC while driving, gated by inCarMenuAllowed). Every row
// here acts on the session that is already running, so it is built separately
// from the pre-session screens; GARAGE/SETTINGS are the same screens as on
// MAIN and BACK returns to this one, not to MAIN.
void GameMenuOverlay::buildPauseMenu()
{
    m_items.clear();
    // Every row carries an icon id (ui/IconSet.h): the in-car overlay is the
    // one screen a player sees while sitting in the box, and a list of bare
    // words is hard to read at a glance from a rig.
    m_items.push_back({ "RESUME", "Back to the car",
        [this]() { setVisible(false); }, false, true, "drive" });
    m_items.push_back({ "RESTART SESSION",
        "Restart this session from the pits",
        [this]() {
            if (onRestartSessionRequested) onRestartSessionRequested();
            setVisible(false);
        }, false, true, "restart" });
    m_items.push_back({ "GARAGE", "Setup, tyres and vehicle options",
        [this]() { switchMenu(MenuState::Garage); }, false, true, "setup" });
    m_items.push_back({ "SETTINGS", "Display, audio and input",
        [this]() { switchMenu(MenuState::Settings); }, false, true, "settings" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "QUIT", "Exit ksim",
        [this]() { switchMenu(MenuState::QuitConfirm); }, false, true, "exit" });
}

void GameMenuOverlay::openPauseMenu()
{
    switchMenu(MenuState::Pause);
    setVisible(true);
}

void GameMenuOverlay::buildDevModeConfirm()
{
    m_items.clear();
    m_items.push_back({ "CONFIRM", "Open editor",
        [this]() { if (onDevModeRequested) onDevModeRequested(); } });
    m_items.push_back({ "CANCEL", "Stay in ksim", [this]() { goBack(); } });
}

void GameMenuOverlay::buildCreditsMenu()
{
    m_items.clear();
    // Only what this screen can honestly claim: the project, the formats it
    // reads and the paths it plays sound through. A credits page invented from
    // names nobody checked would be worse than a short one.
    m_items.push_back({ "KSIM", "Simulator, engine and tooling of the ks engine project",
        nullptr, false, true, "drive" });
    m_items.push_back({ "CONTENT", "Assetto Corsa car, track and showroom formats, read by ksim",
        nullptr, false, true, "download" });
    m_items.push_back({ "AUDIO", "Sample playback through WASAPI",
        nullptr, false, true, "settings" });
    m_items.push_back({ "INTERFACE", "Vector icons drawn from SVG sources inside the simulator",
        nullptr, false, true, "user" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "exit" });
}

void GameMenuOverlay::buildQuitConfirm()
{
    m_items.clear();
    m_items.push_back({ "CONFIRM", "Exit ksim",
        [this]() { if (onExitRequested) onExitRequested(); } });
    m_items.push_back({ "CANCEL", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::switchMenu(MenuState state)
{
    m_previousState = m_currentState;
    m_currentState = state;
    m_selectedIndex = 0;
    m_hoverIndex = -1;
    m_transitionProgress = 0.0f;

    switch (state) {
    case MenuState::Main: buildMainMenu(); break;
    case MenuState::Singleplayer: buildSingleplayerMenu(); break;
    case MenuState::ModeConfig: buildModeConfigMenu(); break;
    case MenuState::Multiplayer: buildMultiplayerMenu(); break;
    case MenuState::Profile: buildProfileMenu(); break;
    case MenuState::Garage: buildGarageMenu(); break;
    case MenuState::PitStrategy: buildPitStrategyMenu(); break;
    case MenuState::Pause: buildPauseMenu(); break;
    case MenuState::Replay: buildReplayMenu(); break;
    case MenuState::ContentManager: buildContentManagerMenu(); break;
    case MenuState::Settings: buildSettingsMenu(); break;
    case MenuState::Options: buildOptionsMenu(); break;
    case MenuState::Controls: buildControlsMenu(); break;
    case MenuState::CarSelect: buildCarSelectMenu(); break;
    case MenuState::TrackSelect: buildTrackSelectMenu(); break;
    case MenuState::TeamSelect: buildTeamSelectMenu(); break;
    case MenuState::Results: buildResultsMenu(); break;
    case MenuState::DevModeConfirm: buildDevModeConfirm(); break;
    case MenuState::Credits: buildCreditsMenu(); break;
    case MenuState::QuitConfirm: buildQuitConfirm(); break;
    }
    // skip leading separators
    while (m_selectedIndex < static_cast<int>(m_items.size()) && m_items[m_selectedIndex].isSeparator)
        ++m_selectedIndex;
}

void GameMenuOverlay::goBack()
{
    if (m_currentState == MenuState::Main || m_currentState == MenuState::Pause) {
        setVisible(false); // MAIN: leave the menu; PAUSE: back in the car
        return;
    }
    if (m_currentState == MenuState::Controls || m_currentState == MenuState::Options) {
        switchMenu(MenuState::Settings);
        return;
    }
    // The pit strategy screen lives one level under GARAGE.
    if (m_currentState == MenuState::PitStrategy) {
        switchMenu(MenuState::Garage);
        return;
    }
    // Select/results screens return to whoever opened them (Singleplayer or
    // Content), falling back to Main when entered directly.
    if (m_currentState == MenuState::CarSelect ||
        m_currentState == MenuState::TrackSelect ||
        m_currentState == MenuState::TeamSelect ||
        m_currentState == MenuState::Results) {
        if (m_previousState != m_currentState &&
            m_previousState != MenuState::CarSelect &&
            m_previousState != MenuState::TrackSelect &&
            m_previousState != MenuState::TeamSelect &&
            m_previousState != MenuState::Results)
            switchMenu(m_previousState);
        else
            switchMenu(MenuState::Main);
        return;
    }
    // MODE SETUP sits one level under the mode list.
    if (m_currentState == MenuState::ModeConfig) {
        switchMenu(MenuState::Singleplayer);
        return;
    }
    // Screens opened from the pause overlay go back to it, so ESC from the
    // garage while driving never drops the player on the pre-session menu.
    if (m_previousState == MenuState::Pause) {
        switchMenu(MenuState::Pause);
        return;
    }
    switchMenu(MenuState::Main);
}

void GameMenuOverlay::activateSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) return;
    const auto& item = m_items[m_selectedIndex];
    if (item.enabled && item.action && !item.isSeparator) {
        item.action();
        // The pit strategy rows edit stored values in place (fuel step,
        // repair toggles): rebuild that screen after the action so the
        // value rows stay true, keeping the cursor put.
        if (m_menuDirty && m_currentState == MenuState::PitStrategy)
            refreshPitStrategy();
        // The GARAGE upgrade rows rebuild after cycling so the label reflects
        // the newly selected level (same cursor-keeping as PitStrategy).
        else if (m_menuDirty && m_currentState == MenuState::Garage) {
            const int sel = m_selectedIndex;
            buildGarageMenu();
            m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = false;
        }
        // MODE SETUP rows carry their value in the label or the "now ..."
        // description, so they rebuild after each step with the cursor kept.
        // CARS filter rows carry their value in the label, same rebuild rule.
        else if (m_menuDirty && m_currentState == MenuState::CarSelect) {
            const int sel = m_selectedIndex;
            buildCarSelectMenu();
            m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = false;
        }
        else if (m_menuDirty && m_currentState == MenuState::ModeConfig) {
            const int sel = m_selectedIndex;
            buildModeConfigMenu();
            m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = false;
        }
    }
}

void GameMenuOverlay::render(int /*width*/, int /*height*/)
{
    if (!m_visible && m_fadeAlpha <= 0.01f) return;
    m_animationTime += 0.016f;
    if (m_visible && m_fadeAlpha < 1.0f)
        m_fadeAlpha = std::min(1.0f, m_fadeAlpha + 0.07f);
    else if (!m_visible && m_fadeAlpha > 0.0f)
        m_fadeAlpha = std::max(0.0f, m_fadeAlpha - 0.09f);
    if (m_transitionProgress < 1.0f)
        m_transitionProgress = std::min(1.0f, m_transitionProgress + 0.08f);
}

bool GameMenuOverlay::handleKeyPress(int key)
{
    if (!m_visible) return false;

    auto nextEnabled = [&](int from, int dir) {
        int n = static_cast<int>(m_items.size());
        if (n == 0) return 0;
        int idx = from;
        for (int k = 0; k < n; ++k) {
            idx = (idx + dir + n) % n;
            if (!m_items[idx].isSeparator && m_items[idx].enabled)
                return idx;
        }
        return from;
    };

    switch (key) {
    case 0x26: // UP
        m_selectedIndex = nextEnabled(m_selectedIndex, -1);
        return true;
    case 0x28: // DOWN
        m_selectedIndex = nextEnabled(m_selectedIndex, +1);
        return true;
    // The in-car bar is horizontal, so LEFT/RIGHT are the same walk as
    // UP/DOWN; on the vertical screens they are harmless equivalents and a
    // pad mapped either way still moves the cursor.
    case 0x25: // LEFT
        m_selectedIndex = nextEnabled(m_selectedIndex, -1);
        return true;
    case 0x27: // RIGHT
        m_selectedIndex = nextEnabled(m_selectedIndex, +1);
        return true;
    case 0x0D: // ENTER
        activateSelected();
        return true;
    case 0x1B: // ESC
        goBack();
        return true;
    default:
        break;
    }
    return false;
}

void GameMenuOverlay::handleMouseMove(const SimPoint& pos, int /*w*/, int /*h*/)
{
    if (!m_visible) return;
    // hit-test is performed in NativeUiHub using layout constants
    (void)pos;
}

void GameMenuOverlay::handleClick(const SimPoint& /*pos*/, int /*w*/, int /*h*/)
{
    if (!m_visible) return;
    activateSelected();
}

} // namespace ks::sim
