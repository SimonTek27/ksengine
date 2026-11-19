#include "GameMenuOverlay.h"
#include "CarSpecSheet.h" // CARS -> DATA: the car's declared numbers
#include "ContentCredits.h" // CREDITS: the authors the content declares
#include "DriverGear.h" // CONTENT -> SUITS / HELMETS: readDriverGear
#include "WeatherService.h"
#include "ShowroomConfig.h" // CONTENT -> SHOWROOMS: scanShowrooms, loadShowroom

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
    case MenuState::Singleplayer: return "EVENT";
    case MenuState::ModeConfig: return "MODE SETUP";
    case MenuState::Multiplayer: return "MULTI PLAYER";
    case MenuState::Profile: return "DRIVER";
    case MenuState::Garage: return "GARAGE";
    case MenuState::PitStrategy: return "PIT STRATEGY";
    case MenuState::Replay: return "REPLAY";
    case MenuState::ContentManager: return "CONTENT";
    case MenuState::ContentPackages: return "INSTALL";
    case MenuState::ShowroomSelect: return "SHOWROOMS";
    case MenuState::Skins: return "SKINS";
    case MenuState::Suits: return "SUITS";
    case MenuState::Helmets: return "HELMETS";
    case MenuState::Settings: return "SETTINGS";
    case MenuState::Options: return "VIDEO";
    case MenuState::Controls: return "CONTROLS";
    case MenuState::CarSelect: return "CARS";
    case MenuState::TrackSelect: return "TRACKS";
    case MenuState::TrackWeather: return "WEATHER";
    case MenuState::TeamSelect: return "SELECT TEAM";
    case MenuState::Results: return "RESULTS";
    case MenuState::Pause: return "PAUSED";
    case MenuState::DevModeConfirm: return "EDITOR";
    case MenuState::Credits: return "CREDITS";
    case MenuState::CarData: return "CAR DATA";
    case MenuState::QuitConfirm: return "EXIT";
    case MenuState::FirstRun: return "DRIVER SETUP";
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
    else if (fieldName == "SUIT") m_profile.suit = value;
    else if (fieldName == "HELMET") m_profile.helmet = value;
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

    // The field's strength and its willingness to fight. Percent on the row,
    // multiplier in the params, because "100% is the line" reads better than
    // "1.00x" while the AI cars want the factor.
    {
        const int pct = static_cast<int>(p.aiDifficulty * 100.0f + 0.5f);
        std::snprintf(now, sizeof(now), "now %d%%", pct);
        m_items.push_back({ "AI LEVEL  -5", now,
            [this]() {
                const int now_ = static_cast<int>(m_configParams.aiDifficulty * 100.0f + 0.5f);
                m_configParams.aiDifficulty = std::max(80, now_ - 5) / 100.0f;
                m_menuDirty = true;
            }, false, true, "users" });
        m_items.push_back({ "AI LEVEL  +5", "Pace against the racing line: 100% is the line",
            [this]() {
                const int now_ = static_cast<int>(m_configParams.aiDifficulty * 100.0f + 0.5f);
                m_configParams.aiDifficulty = std::min(120, now_ + 5) / 100.0f;
                m_menuDirty = true;
            }, false, true, "users" });
    }
    {
        const int pct = static_cast<int>(p.aiAggression * 100.0f + 0.5f);
        std::snprintf(now, sizeof(now), "now %d%%", pct);
        m_items.push_back({ "AI AGGRESSION  -10", now,
            [this]() {
                const int now_ = static_cast<int>(m_configParams.aiAggression * 100.0f + 0.5f);
                m_configParams.aiAggression = std::max(0, now_ - 10) / 100.0f;
                m_menuDirty = true;
            }, false, true, "users" });
        m_items.push_back({ "AI AGGRESSION  +10", "How hard they fight for a place, 0-100%",
            [this]() {
                const int now_ = static_cast<int>(m_configParams.aiAggression * 100.0f + 0.5f);
                m_configParams.aiAggression = std::min(100, now_ + 10) / 100.0f;
                m_menuDirty = true;
            }, false, true, "users" });
    }

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
    const int aiPct = static_cast<int>(p.aiDifficulty * 100.0f + 0.5f);
    if (p.totalLaps > 0)
        std::snprintf(text, sizeof(text), "%s - %d lap(s), %d AI at %d%%, %s",
                      modeName, p.totalLaps, p.aiCars, aiPct, now);
    else
        std::snprintf(text, sizeof(text), "%s - open, %d AI at %d%%, %s",
                      modeName, p.aiCars, aiPct, now);
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
    // The session walk starts at the car, like rFactor: CARS, then TRACKS,
    // then the event itself on the SINGLE PLAYER page.
    m_items.push_back({ "SINGLE PLAYER", "Pick a car, a track, then the event type",
        [this]() { beginSessionSetup(); }, false, true, "user" });
    m_items.push_back({ "MULTIPLAYER", "Online and LAN events, ranked or not",
        [this]() { switchMenu(MenuState::Multiplayer); }, false, true, "users" });
    // The quick actions - settings gear, sound, VR screen, controller, the
    // session planner and the clock - belong to the header bar, not to this
    // row: ui/MainMenu.h lays them out and the hub runs them, so they are no
    // longer menu rows.
    m_items.push_back({ "DRIVER", "Profile and career stats",
        [this]() { switchMenu(MenuState::Profile); }, false, true, "helmet" });
    m_items.push_back({ "CONTENT", "Cars, tracks and packages",
        [this]() { openContentManager(); }, false, true, "download" });
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
    // Phase 3.3 — layout of the chosen track. The loop hands out the layouts
    // it discovered; a single layout has nothing to pick, so no row.
    if (layoutChoices && activeLayoutId) {
        const auto choices = layoutChoices();
        if (choices.size() > 1) {
            const std::string active = activeLayoutId();
            std::string label = active;
            for (const auto& c : choices)
                if (c.first == active) label = c.second;
            m_items.push_back({ "LAYOUT  " + label,
                std::to_string(choices.size()) + " layout(s) on this track",
                [this, choices, active]() {
                    size_t next = 0;
                    for (size_t i = 0; i < choices.size(); ++i) {
                        if (choices[i].first == active) {
                            next = (i + 1) % choices.size();
                            break;
                        }
                    }
                    if (onLayoutChosen) onLayoutChosen(choices[next].first);
                    m_menuDirty = true; // the label follows the new binding
                } });
        }
    }
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
    m_firstModeRow = static_cast<int>(m_items.size());
    for (GameSessionMode mode : effectiveSingleplayerModes()) {
        m_items.push_back({ singleplayerLabel(mode), ready ? singleplayerHint(mode) : hint,
            [this, mode]() { openModeConfig(mode); }, false, ready });
    }
    m_items.push_back({ "RESULTS", "Session standings",
        [this]() { if (onShowResultsRequested) onShowResultsRequested(); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return",
        [this]() { goBack(); }, false, true, "back" });
    // Arriving from the track page, the cursor belongs on the event rows: the
    // car and the track are already chosen, so the next ENTER picks the event.
    if (m_focusContent) {
        m_focusContent = false;
        if (m_firstModeRow < static_cast<int>(m_items.size()))
            m_selectedIndex = m_firstModeRow;
    }
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
    // The value rides on the label and the panel explains the row, the way the
    // video page reads: these used to put the value in the panel, so NUMBER
    // showed a bare "1" and the row itself showed nothing at all.
    m_items.push_back({ "NAME  " + m_profile.name, "Press to type the driver name",
        [this]() { if (onTextInputRequested) onTextInputRequested("NAME", m_profile.name); },
        false, true, "user" });
    m_items.push_back({ "NATIONALITY  " + m_profile.nationality, "Press to pick a country from the list",
        [this]() { if (onNationalityInputRequested) onNationalityInputRequested(); },
        false, true, "helmet" });
    m_items.push_back({ "NUMBER  " + std::to_string(m_profile.raceNumber), "Press to cycle between 1 and 99",
        [this]() { m_profile.raceNumber = (m_profile.raceNumber % 99) + 1; m_menuDirty = true; },
        false, true, "drive" });
    m_items.push_back({ "BIO", m_profile.bio.empty() ? "Add a short biography" : m_profile.bio,
        [this]() { if (onTextInputRequested) onTextInputRequested("BIO", m_profile.bio); },
        false, true, "info" });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

// FIRST RUN: the driver's own data and the controls, once, before the car is
// picked. It sits in front of the CARS -> TRACKS -> EVENT walk, so a fresh
// install never reaches a session with an unnamed driver or unbound pedals.
void GameMenuOverlay::openFirstRun()
{
    m_setupComplete = false;
    switchMenu(MenuState::FirstRun);
    setVisible(true);
}

void GameMenuOverlay::buildFirstRunMenu()
{
    m_items.clear();
    static const char* kNationalities[] = {
        "IT", "DE", "GB", "FR", "ES", "US", "JP", "BR",
        "AU", "CA", "NL", "FI", "SE", "BE", "CH", "AT"
    };
    std::string nationality = "NOT SET";
    for (const char* code : kNationalities)
        if (m_profile.nationality == code) nationality = code;

    const std::string name =
        m_textEditing && m_textField == "NAME" ? m_textBuffer + "_" : m_profile.name;
    m_items.push_back({ "NAME  " + name, "Press to type the driver name",
        [this]() { beginTextEdit("NAME", m_profile.name); }, false, true, "user" });
    m_items.push_back({ std::string("NATIONALITY  ") + nationality,
        "Press to cycle the country",
        [this]() {
            static const char* codes[] = {
                "IT", "DE", "GB", "FR", "ES", "US", "JP", "BR",
                "AU", "CA", "NL", "FI", "SE", "BE", "CH", "AT"
            };
            int current = -1;
            for (int i = 0; i < 16; ++i)
                if (m_profile.nationality == codes[i]) current = i;
            setNationalityFromList((current + 1) % 16);
            m_menuDirty = true;
        }, false, true, "helmet" });
    m_items.push_back({ "RACE NUMBER  " + std::to_string(m_profile.raceNumber),
        "Press to cycle between 1 and 99",
        [this]() {
            m_profile.raceNumber = (m_profile.raceNumber % 99) + 1;
            m_menuDirty = true;
        }, false, true, "drive" });
    m_items.push_back({ "", "", nullptr, true });
    // The bindings panel is the same one SETTINGS opens; the wizard only
    // reports the intent, so the menu never learns what a key is.
    m_items.push_back({ "CONTROLS", "Assign throttle, brake, steering and gears",
        [this]() {
            if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("keyboard");
        }, false, true, "keyboard" });
    // Wheel/pad users bind nothing by hand: what they need is the device, its
    // calibration and the steering options.
    m_items.push_back({ "WHEEL / PAD", "Device, calibration and steering options",
        [this]() {
            if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("devices");
        }, false, true, "pad" });
    m_items.push_back({ "", "", nullptr, true });
    const bool named = !m_profile.name.empty();
    m_items.push_back({ "CONTINUE TO CARS",
        named ? "Save this driver and pick a car" : "Enter a driver name first",
        [this]() {
            if (m_textEditing) commitTextEdit();
            m_setupComplete = true;
            if (onFirstRunFinished) onFirstRunFinished(m_profile);
            beginSessionSetup();
        }, false, named, "flag" });
}

void GameMenuOverlay::beginSessionSetup()
{
    m_setupFlow = true;
    openCarSelect(); // sets m_focusContent: cursor on the first car
}

void GameMenuOverlay::beginTextEdit(const std::string& field, const std::string& value)
{
    m_textEditing = true;
    m_textField = field;
    m_textBuffer = value;
    m_menuDirty = true;
}

void GameMenuOverlay::commitTextEdit()
{
    if (!m_textEditing) return;
    if (m_textField == "NAME") m_profile.name = m_textBuffer;
    else if (m_textField == "BIO") m_profile.bio = m_textBuffer;
    m_textEditing = false;
    m_textField.clear();
    m_textBuffer.clear();
    m_menuDirty = true;
}

void GameMenuOverlay::cancelTextEdit()
{
    m_textEditing = false;
    m_textField.clear();
    m_textBuffer.clear();
    m_menuDirty = true;
}

// WM_CHAR payloads: printable ASCII only, which is what the baked atlas and
// the JSON files both carry; nothing here reaches a text buffer past 32 chars.
bool GameMenuOverlay::handleChar(int character)
{
    if (!m_textEditing) return false;
    if (character < 32 || character > 126) return true;
    if (m_textBuffer.size() >= 32) return true;
    m_textBuffer.push_back(static_cast<char>(character));
    m_menuDirty = true;
    return true;
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
    // GAP_MATRIX P0.5: this page used to hold a LOAD row with nothing behind
    // it. It now lists the recordings the loop found under replay/ and hands
    // the chosen path back, plus the record toggle the loop owns.
    int listed = 0;
    if (replayChoices) {
        for (const auto& entry : replayChoices()) {
            const std::string label = entry.first;
            const std::string path = entry.second;
            m_items.push_back({ label, "Play this recording",
                [this, path]() {
                    if (onReplayFileChosen) onReplayFileChosen(path);
                    goBack();
                }, false, true, "download" });
            ++listed;
        }
    }
    if (listed == 0)
        m_items.push_back({ "NO REPLAYS", "Record a session first", nullptr, true });
    const bool recording = replayRecordingState && replayRecordingState();
    m_items.push_back({ recording ? "STOP RECORDING" : "RECORD",
        recording ? "Stop and save to replay/" : "Record this session to replay/",
        [this]() {
            if (onRecordReplayRequested) onRecordReplayRequested();
            m_menuDirty = true; // the label flips with the recorder state
        }, false, true, "drive" });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::refreshContentCounts()
{
    // Same roots the select screens list from, so a row's count is exactly the
    // list the row opens.
    m_contentCounts.carsRoot = carLibraryRoot();
    m_contentCounts.tracksRoot = trackLibraryRoot();
    m_contentCounts.teamsRoot = teamLibraryRoot();
    m_contentCounts.showroomsRoot = showroomLibraryRoot();
    m_contentCounts.cars = scanCarLibrary(m_contentCounts.carsRoot).size();
    m_contentCounts.tracks = scanTrackLibrary(m_contentCounts.tracksRoot).size();
    m_contentCounts.teams = scanTeamLibrary(m_contentCounts.teamsRoot).size();
    m_contentCounts.showrooms = scanShowrooms(m_contentCounts.showroomsRoot).size();
    m_contentCountsValid = true;
}

void GameMenuOverlay::openContentManager()
{
    refreshContentCounts();
    switchMenu(MenuState::ContentManager);
}

void GameMenuOverlay::openContentPackages()
{
    switchMenu(MenuState::ContentPackages);
}

void GameMenuOverlay::writeContentManifestNow()
{
    if (!m_contentCountsValid) refreshContentCounts();
    const std::string path = contentManifestPath(m_contentCounts.carsRoot);
    const ContentManifest now = buildContentManifest(
        m_contentCounts.carsRoot, m_contentCounts.tracksRoot, m_contentCounts.teamsRoot);
    std::string error;
    if (!writeContentManifest(path, now, &error)) {
        m_manifestStatus = "Manifest not written: " + error;
        std::printf("[content] %s\n", m_manifestStatus.c_str());
    } else {
        const auto counted = [](size_t count, const char* one, const char* many) {
            return std::to_string(count) + " " + (count == 1 ? one : many);
        };
        m_manifestStatus = "Manifest written: " + counted(now.cars.size(), "car", "cars") +
                           ", " + counted(now.tracks.size(), "track", "tracks") + ", " +
                           counted(now.teams.size(), "team", "teams") + " -> " + path;
        std::printf("[content] %s\n", m_manifestStatus.c_str());
    }
    m_menuDirty = true;
}

void GameMenuOverlay::buildContentManagerMenu()
{
    m_items.clear();
    if (!m_contentCountsValid) refreshContentCounts();
    char label[96];
    // Like the launcher's own content page: every library with the number of
    // items it holds, and the folder a missing one goes into. The count is the
    // list the row opens, so the page never claims content the browser cannot
    // show.
    const auto libraryRow = [this](const char* name, size_t count, const std::string& root,
                                   const char* one, const char* many) {
        char text[96];
        std::snprintf(text, sizeof(text), "%s  %zu", name, count);
        std::string detail = std::to_string(count) + " " + (count == 1 ? one : many) + " in " + root;
        return MenuItem{text, detail, nullptr, false, true, nullptr};
    };
    MenuItem cars =
        libraryRow("CARS", m_contentCounts.cars, m_contentCounts.carsRoot, "car folder", "car folders");
    cars.action = [this]() { openCarSelect(); };
    cars.icon = "drive";
    m_items.push_back(cars);

    MenuItem tracks =
        libraryRow("TRACKS", m_contentCounts.tracks, m_contentCounts.tracksRoot, "circuit", "circuits");
    tracks.action = [this]() { openTrackSelect(); };
    tracks.icon = "restart";
    m_items.push_back(tracks);

    MenuItem teams =
        libraryRow("TEAMS", m_contentCounts.teams, m_contentCounts.teamsRoot, "roster", "rosters");
    teams.action = [this]() { openTeamSelect(); };
    teams.icon = "users";
    m_items.push_back(teams);

    // SHOWROOMS: the room the game opens in. The row names the current choice,
    // so the default is visible without opening the list.
    std::snprintf(label, sizeof(label), "SHOWROOMS  %zu", m_contentCounts.showrooms);
    const std::string currentRoom =
        showroomDefault ? showroomDefault() : m_showroomDefaultId;
    const std::string roomNote = currentRoom.empty() ? "first installed" : currentRoom;
    m_items.push_back({ label,
        "Start-up room: " + roomNote + " (" + std::to_string(m_contentCounts.showrooms) +
            " in " + m_contentCounts.showroomsRoot + ")",
        [this]() { openShowroomSelect(); }, false, true, "settings" });

    // SKINS: the texture sets of the car that is loaded, and the one it wears.
    // The count is the list the row opens, and the detail names the choice so
    // the skin is visible without opening the page.
    {
        const std::vector<CarSkin> skins =
            carSkinChoices ? carSkinChoices() : std::vector<CarSkin>{};
        const std::string worn = activeCarSkin ? activeCarSkin() : std::string();
        std::snprintf(label, sizeof(label), "SKINS  %zu", skins.size());
        const std::string detail =
            skins.empty()
                ? std::string("Load a car from CARS, then choose its skin here")
                : "Skins of the loaded car, wearing " +
                      (worn.empty() ? std::string("the first installed") : worn);
        m_items.push_back(
            { label, detail, [this]() { openSkins(); }, false, true, "settings" });
    }

    // SUITS / HELMETS: the driver gear the content tree ships and the player's
    // choice, which is kept in the profile's own document (driver.json).
    {
        std::vector<DriverGear> suits;
        std::vector<DriverGear> helmets;
        readDriverGear(driverSuitLibraryRoot(), suits);
        readDriverGear(driverHelmetLibraryRoot(), helmets);
        std::snprintf(label, sizeof(label), "SUITS  %zu", suits.size());
        // "Chosen", not "worn": the gear is listed and recorded here, and the
        // room has no driver model to wear it yet.
        m_items.push_back({ label,
            m_profile.suit.empty() ? std::string("Driver suits installed; none chosen")
                                   : "Chosen suit: " + m_profile.suit + " (driver.json)",
            [this]() { openSuits(); }, false, true, "user" });
        std::snprintf(label, sizeof(label), "HELMETS  %zu", helmets.size());
        m_items.push_back({ label,
            m_profile.helmet.empty() ? std::string("Helmets installed; none chosen")
                                     : "Chosen helmet: " + m_profile.helmet + " (driver.json)",
            [this]() { openHelmets(); }, false, true, "helmet" });
    }

    m_items.push_back({ "", "", nullptr, true });

    // INSTALL: the packages dropped in <content>/packages, unpacked into the
    // content tree. This is the launcher's add-content path; the row carries
    // how many are waiting so an empty inbox is visible without opening it.
    std::vector<std::pair<std::string, std::string>> packages;
    if (contentPackages) packages = contentPackages();
    std::snprintf(label, sizeof(label), "INSTALL FROM ARCHIVE  %zu waiting",
                  packages.size());
    m_items.push_back({ label,
        "Install a ZIP from the content/packages folder",
        [this]() { openContentPackages(); }, false, true, "download" });
    // RESCAN: a package installed outside the simulator (or a folder copied in
    // by hand) shows up here without a restart, like the launcher's refresh.
    m_items.push_back({ "RESCAN", "Count the libraries again", [this]() {
        m_contentCountsValid = false;
        refreshContentCounts();
        m_menuDirty = true;
    }, false, true, "restart" });

    // MANIFEST: what is installed, written as one document with the versions
    // the content declares. The row reads the file back, so a manifest left
    // over from an older copy of the content cannot pass for the truth: it
    // says how far behind it is, and choosing the row writes it again.
    {
        const std::string manifestPath = contentManifestPath(m_contentCounts.carsRoot);
        const ContentManifest now = buildContentManifest(
            m_contentCounts.carsRoot, m_contentCounts.tracksRoot, m_contentCounts.teamsRoot);
        ContentManifest onDisk;
        std::string why;
        std::string detail;
        if (!readContentManifest(manifestPath, onDisk, &why)) {
            detail = "No manifest yet; write " + manifestPath + " for " +
                     std::to_string(now.cars.size()) + " cars, " +
                     std::to_string(now.tracks.size()) + " tracks, " +
                     std::to_string(now.teams.size()) + " teams";
        } else {
            const ManifestDrift drift = diffContentManifest(onDisk, now);
            if (drift.total() == 0) {
                detail = manifestPath + " is current: " + std::to_string(now.cars.size()) +
                         " cars, " + std::to_string(now.tracks.size()) + " tracks, " +
                         std::to_string(now.teams.size()) + " teams, versions included";
            } else {
                detail = manifestPath + " is behind: " + std::to_string(drift.added) +
                         " new, " + std::to_string(drift.removed) + " gone, " +
                         std::to_string(drift.changed) + " version change(s)";
            }
        }
        std::snprintf(label, sizeof(label), "MANIFEST  %zu items",
                      now.cars.size() + now.tracks.size() + now.teams.size());
        m_items.push_back({ label, detail, [this]() { writeContentManifestNow(); }, false,
                            true, "download" });
    }

    if (!m_contentInstallStatus.empty()) {
        m_items.push_back({ "", "", nullptr, true });
        m_items.push_back({ m_contentInstallStatus, "Last install", nullptr, false, false });
    }
    if (!m_manifestStatus.empty()) {
        m_items.push_back({ "", "", nullptr, true });
        m_items.push_back({ m_manifestStatus, "Last manifest", nullptr, false, false });
    }

    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::openShowroomSelect()
{
    switchMenu(MenuState::ShowroomSelect);
}

void GameMenuOverlay::openSkins()
{
    switchMenu(MenuState::Skins);
}

void GameMenuOverlay::openSuits()
{
    switchMenu(MenuState::Suits);
}

void GameMenuOverlay::openHelmets()
{
    switchMenu(MenuState::Helmets);
}

// CONTENT -> SUITS / HELMETS: one page shape for both driver-gear libraries,
// the same one the skins use. The choice is the player's, not the car's, so it
// goes on the profile (which driver.json carries) instead of a model document;
// the row of the chosen one carries (current). The room has no driver model of
// its own yet, so the page's job today is to show what is installed and to
// record the choice, and the rows' detail is the gear's own folder.
void GameMenuOverlay::buildDriverGearMenu(bool helmets)
{
    m_items.clear();
    const std::string root = helmets ? driverHelmetLibraryRoot() : driverSuitLibraryRoot();
    std::vector<DriverGear> gear;
    readDriverGear(root, gear);
    const std::string worn = helmets ? m_profile.helmet : m_profile.suit;
    bool marked = false;
    for (const DriverGear& item : gear) {
        const bool current = !worn.empty() && worn == item.id;
        if (current) marked = true;
        std::string label = item.id;
        if (current) label += "  (current)";
        const std::string id = item.id;
        m_items.push_back({ label, item.path, [this, id, helmets]() {
            if (helmets) m_profile.helmet = id;
            else m_profile.suit = id;
            if (onProfileChanged) onProfileChanged(m_profile);
            // The mark moves at once, like every other list with a current one.
            const int selected = m_selectedIndex;
            buildDriverGearMenu(helmets);
            m_selectedIndex = std::min(selected, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = true;
        }, false, true, helmets ? "helmet" : "user" });
    }
    if (gear.empty())
        m_items.push_back({ helmets ? "(no helmets)" : "(no suits)",
            "Put the folders in " + root + ": an empty library has nothing to choose",
            nullptr });
    // A choice the library no longer holds stays a preference and is said out
    // loud, so a folder deleted from the content tree is visible here.
    if (!worn.empty() && !marked && !gear.empty())
        m_items.push_back({ "MISSING: " + worn,
            "driver.json names gear that is not installed; it stays a preference only",
            nullptr, false, false });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

// CONTENT -> SKINS: the texture sets the loaded car ships. The row that is
// marked is the one model.json records (kn5.skin), so choosing here and the
// car on screen always agree; picking one hands the id to the loop, which
// writes the document and rebuilds the car with it.
void GameMenuOverlay::buildSkinsMenu()
{
    m_items.clear();
    const std::vector<CarSkin> skins =
        carSkinChoices ? carSkinChoices() : std::vector<CarSkin>{};
    const std::string worn = activeCarSkin ? activeCarSkin() : std::string();
    bool marked = false;
    for (const CarSkin& skin : skins) {
        const bool current = !worn.empty() && worn == skin.id;
        if (current) marked = true;
        // The skin's own name leads and the folder id travels with it, because
        // the id is what the loader and the document use.
        std::string label = skin.name;
        if (skin.name != skin.id) label += "  [" + skin.id + "]";
        if (current) label += "  (current)";
        const std::string detail =
            skin.description.empty() ? "skins/" + skin.id : skin.description;
        const std::string id = skin.id;
        m_items.push_back({ label, detail, [this, id]() {
            if (onCarSkinChosen) onCarSkinChosen(id);
            // The mark moves at once; the loop has already rebuilt the car.
            const int selected = m_selectedIndex;
            buildSkinsMenu();
            m_selectedIndex = std::min(selected, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = true;
        }, false, true, "settings" });
    }
    if (skins.empty())
        m_items.push_back({ "(no skins)",
            "Load a car from CARS first: this page lists the skins of the car in the room",
            nullptr });
    // A recorded skin the folder no longer ships is worth saying out loud: the
    // loader opens the first folder instead, and nothing else would show it.
    if (!worn.empty() && !marked && !skins.empty())
        m_items.push_back({ "MISSING: " + worn,
            "model.json names a skin that is not installed; the first one opens instead",
            nullptr, false, false });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildShowroomSelectMenu()
{
    m_items.clear();
    const std::string root = m_contentCounts.showroomsRoot.empty() ? showroomLibraryRoot()
                                                                  : m_contentCounts.showroomsRoot;
    const std::vector<ShowroomEntry> rooms = scanShowrooms(root);
    const std::string current = showroomDefault ? showroomDefault() : m_showroomDefaultId;
    bool marked = false;
    for (const auto& room : rooms) {
        // The room's own name and one-line description, when its document has
        // them; the folder id always travels with the row, because that is
        // what the settings file stores.
        std::string name = room.id;
        std::string detail = "Starts the game in " + room.path;
        const ShowroomConfig config = loadShowroom(room.path);
        if (config.valid) {
            if (!config.name.empty()) name = config.name;
            if (!config.description.empty()) detail = config.description;
        }
        const bool isDefault = !current.empty() && current == room.id;
        if (isDefault) marked = true;
        // The folder id travels with the row (the settings store it), and the
        // document's own name leads when it has one.
        std::string label = name;
        if (name != room.id) label += "  [" + room.id + "]";
        if (isDefault) label += "  (default)";
        const std::string id = room.id;
        const std::string path = room.path;
        m_items.push_back({ label, detail, [this, id, path]() {
            m_showroomDefaultId = id;
            if (onShowroomChosen) onShowroomChosen(id, path);
            // The mark moves at once; the app's settings are read back the
            // next time the page opens.
            const int selected = m_selectedIndex;
            buildShowroomSelectMenu();
            m_selectedIndex = std::min(selected, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = true;
        }, false, true, "settings" });
    }
    if (rooms.empty())
        m_items.push_back({ "(no showrooms found)",
            "Put showroom folders (with showroom.json) in " + root, nullptr });
    // A stored default that is no longer on disk is worth saying out loud: the
    // loader opens the first room instead, and nothing else would show it.
    if (!current.empty() && !marked && !rooms.empty()) {
        m_items.push_back({ "", "", nullptr, true });
        m_items.push_back({ "MISSING: " + current,
            "showroom.default names a folder that is not installed; the first room opens instead",
            nullptr, false, false });
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "back" });
}

void GameMenuOverlay::buildContentPackagesMenu()
{
    m_items.clear();
    const std::vector<std::pair<std::string, std::string>> packages =
        contentPackages ? contentPackages() : std::vector<std::pair<std::string, std::string>>{};
    int listed = 0;
    for (const auto& entry : packages) {
        const std::string name = entry.first;
        const std::string path = entry.second;
        m_items.push_back({ name, "Install this package into the content tree",
            [this, path]() {
                m_contentInstallStatus = onContentInstallRequested
                    ? onContentInstallRequested(path)
                    : std::string("No install path");
                // Counts and the waiting list both move with an install.
                m_contentCountsValid = false;
                refreshContentCounts();
                switchMenu(MenuState::ContentManager);
            }, false, true, "download" });
        ++listed;
    }
    if (listed == 0) {
        // The folder is named, not created: an empty page is still a page that
        // says where content goes.
        m_items.push_back({ "NO PACKAGES",
            "Drop content ZIPs in the packages folder and reopen this page", nullptr });
    }
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
    // The session's own tweaks (fuel burn, tyre wear): GameplayTweaks holds
    // them, so this page is how they are chosen without editing the JSON.
    m_items.push_back({ "GAMEPLAY", "Fuel consumption and tyre wear multipliers",
        [this]() { openOptionsGameplay(); }, false, true, "settings" });
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
        // Through the accessors, not the old constant arrays: entry 0 is
        // whatever the app seeded from the system's display mode.
        const int delta = std::abs(resolutionWidth(i) - width) +
                          std::abs(resolutionHeight(i) - height);
        if (delta < bestDelta) { bestDelta = delta; best = i; }
    }
    return best;
}

const char* GameMenuOverlay::sectionKey(int index)
{
    switch (index) {
    case kSectionVideo: return "video";
    case kSectionQuality: return "quality";
    case kSectionImage: return "image";
    case kSectionSound: return "sound";
    case kSectionGameplay: return "gameplay";
    default: return "";
    }
}

int GameMenuOverlay::sectionIndexFor(const std::string& key)
{
    for (int i = 0; i < kSectionCount; ++i)
        if (key == sectionKey(i)) return i;
    return -1;
}

int GameMenuOverlay::nearestIndex(const int* values, int count, int value)
{
    int best = 0;
    for (int i = 1; i < count; ++i)
        if (std::abs(values[i] - value) < std::abs(values[best] - value)) best = i;
    return best;
}

int GameMenuOverlay::nearestIndex(const float* values, int count, float value)
{
    int best = 0;
    for (int i = 1; i < count; ++i)
        if (std::fabs(values[i] - value) < std::fabs(values[best] - value)) best = i;
    return best;
}

int GameMenuOverlay::gameplayIndexFor(float value)
{
    int best = 0;
    for (int i = 1; i < kGameplayCount; ++i) {
        if (std::fabs(kGameplayValues[i] - value) <
            std::fabs(kGameplayValues[best] - value))
            best = i;
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
    // AUDIO opens the same page with the cursor on MASTER VOLUME: the section
    // has to be unfolded, or the row it is looking for is not there.
    m_collapsedSections[kSectionSound] = false;
    switchMenu(MenuState::Options);
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].text.rfind("MASTER", 0) == 0) {
            m_selectedIndex = i;
            break;
        }
    }
}

void GameMenuOverlay::openOptionsGameplay()
{
    refreshOptions();
    m_collapsedSections[kSectionGameplay] = false;
    switchMenu(MenuState::Options);
    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        if (m_items[i].text.rfind("FUEL CONSUMPTION", 0) == 0) {
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
    // Laid out like the launcher's own video page: the same rows, grouped in
    // the same sections, and every section folds away behind its header so the
    // page stays navigable at thirty rows. Only knobs the renderer or the mixer
    // really honours are listed: no MSAA row while the pipelines are built with
    // one sample, no world-detail row the scene does not have.
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
    // A section header is a row of its own: [-] open, [+] folded, and pressing
    // it keeps the cursor where it was, exactly like a value row does.
    const auto section = [this](int index, const char* title, const char* description) {
        const bool open = !m_collapsedSections[index];
        char text[96];
        std::snprintf(text, sizeof(text), "%s %s", open ? "[-]" : "[+]", title);
        m_items.push_back({ text, description, [this, index]() {
            m_collapsedSections[index] = !m_collapsedSections[index];
            if (onSectionsChanged) onSectionsChanged();
            const int selected = m_selectedIndex;
            buildOptionsMenu();
            m_selectedIndex = std::min(selected, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = true;
        }, false, true, "settings" });
        return open;
    };

    // --- VIDEO: what the window and the frame pacing do -------------------
    if (section(kSectionVideo, "VIDEO", "Resolution, frame pacing and filtering")) {
        // The window's own client size, when the app knows it: on a 2560x1440
        // panel the work area (what the window really is) is 2560x1392, so
        // printing the monitor mode here would name a size the window does not
        // have.
        const int shownWidth =
            o.windowWidth > 0 ? o.windowWidth : resolutionWidth(o.resolutionIndex);
        const int shownHeight =
            o.windowHeight > 0 ? o.windowHeight : resolutionHeight(o.resolutionIndex);
        std::snprintf(label, sizeof(label), "RESOLUTION  %dx%d", shownWidth, shownHeight);
        m_items.push_back({ label, "Window size; fullscreen follows the monitor",
            [this]() {
                m_options.resolutionIndex =
                    (m_options.resolutionIndex + 1) % kResolutionCount;
                const int wantW = resolutionWidth(m_options.resolutionIndex);
                const int wantH = resolutionHeight(m_options.resolutionIndex);
                if (onResolutionRequested) onResolutionRequested(wantW, wantH);
                // Until the app reads the new extent back, the row shows what
                // was just asked for; the next refresh replaces it with the
                // real one.
                m_options.windowWidth = wantW;
                m_options.windowHeight = wantH;
                applyOptions();
            }, false, true, "settings" });

        // FULLSCREEN goes through the window path F11 already uses, and the
        // value is re-read from the window style every time the page opens.
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

        // Frame pacing and filtering belong to the same group: they decide when
        // and how the frame reaches the display, not how it was lit.
        if (o.frameLimitIndex < 0 || o.frameLimitIndex >= kFrameLimitCount)
            m_options.frameLimitIndex = 0;
        {
            const int fps = kFrameLimitValues[m_options.frameLimitIndex];
            if (fps > 0)
                std::snprintf(label, sizeof(label), "FRAME LIMIT  %d fps", fps);
            else
                std::snprintf(label, sizeof(label), "FRAME LIMIT  off");
            m_items.push_back({ label, "Cap the frame rate (video.frameLimit)",
                [this]() {
                    m_options.frameLimitIndex =
                        (m_options.frameLimitIndex + 1) % kFrameLimitCount;
                    applyOptions();
                }, false, true, "settings" });
        }
        std::snprintf(label, sizeof(label), "ANISOTROPIC  %dx",
                      kAnisoValues[std::min(o.anisoIndex, kAnisoCount - 1)]);
        m_items.push_back({ label, "Anisotropic filtering of the road textures",
            [this]() {
                m_options.anisoIndex = (m_options.anisoIndex + 1) % kAnisoCount;
                applyOptions();
            }, false, true, "settings" });
    }

    // --- QUALITY: lighting, reflections, motion and shadows --------------
    if (section(kSectionQuality, "QUALITY",
                "Lighting, mirrors, particles and shadows")) {
        toggleRow("SSAO", &MenuOptions::ssao,
                  "Ambient occlusion, deferred pass only");
        toggleRow("SSR", &MenuOptions::ssr,
                  "Screen-space reflections, deferred pass only");
        toggleRow("SSGI", &MenuOptions::ssgi,
                  "Global illumination, deferred pass only (video.ssgi)");
        toggleRow("MIRROR", &MenuOptions::mirror,
                  "Real-time rear view tile (video.mirrors)");
        toggleRow("MOTION BLUR", &MenuOptions::motionBlur,
                  "Resolve-pass blur while the camera moves");
        toggleRow("PARTICLES", &MenuOptions::particles,
                  "Dust, rain, spray and skid smoke (video.particles)");
        toggleRow("HEAD PHYSICS", &MenuOptions::headPhysics,
                  "The camera moves with the car (video.headPhysics)");

        // These three build GPU state once, before the first frame: the row
        // stores the value and says it takes effect at the next start, instead
        // of tearing the shadow stack or the mirror tile down while the frame
        // is in flight.
        std::snprintf(label, sizeof(label), "SHADOW RESOLUTION  %d",
                      kShadowSizeValues[std::min(o.shadowSizeIndex, kShadowSizeCount - 1)]);
        m_items.push_back({ label, "Shadow map size, applied at next start",
            [this]() {
                m_options.shadowSizeIndex =
                    (m_options.shadowSizeIndex + 1) % kShadowSizeCount;
                applyOptions();
            }, false, true, "settings" });
        std::snprintf(label, sizeof(label), "SHADOW CASCADES  %d",
                      kCascadeValues[std::min(o.cascadeIndex, kCascadeCount - 1)]);
        m_items.push_back({ label, "Cascade count, applied at next start",
            [this]() {
                m_options.cascadeIndex = (m_options.cascadeIndex + 1) % kCascadeCount;
                applyOptions();
            }, false, true, "settings" });
        std::snprintf(label, sizeof(label), "MIRROR RESOLUTION  %s",
                      kMirrorResValues[std::min(o.mirrorResIndex, kMirrorResCount - 1)]);
        m_items.push_back({ label, "Rear-view tile size, applied at next start",
            [this]() {
                m_options.mirrorResIndex = (m_options.mirrorResIndex + 1) % kMirrorResCount;
                applyOptions();
            }, false, true, "settings" });

        // Shaders are compiled from the .spv set at every start, but a running
        // session keeps the pipelines it built: this row is how a replaced .spv
        // or a driver update reaches the frame already on screen.
        m_items.push_back({ "RELOAD SHADERS", "Re-read the .spv set and rebuild every pipeline",
            [this]() { requestShaderReload(); }, false, true, "settings" });
    }

    // --- IMAGE: the display pass and its grading -------------------------
    if (section(kSectionImage, "IMAGE", "Exposure, colour and tone curve")) {
        std::snprintf(label, sizeof(label), "EXPOSURE  %.2f",
                      static_cast<double>(exposureValue(o.exposureIndex)));
        m_items.push_back({ label, "Display exposure of the final image",
            [this]() {
                m_options.exposureIndex = (m_options.exposureIndex + 1) % kExposureCount;
                applyOptions();
            }, false, true, "settings" });

        std::snprintf(label, sizeof(label), "SATURATION  %.2f",
                      static_cast<double>(kSaturationValues[std::min(o.saturationIndex,
                                                                   kSaturationCount - 1)]));
        m_items.push_back({ label, "Colour saturation of the final image",
            [this]() {
                m_options.saturationIndex = (m_options.saturationIndex + 1) % kSaturationCount;
                applyOptions();
            }, false, true, "settings" });
        std::snprintf(label, sizeof(label), "CONTRAST  %.2f",
                      static_cast<double>(kContrastValues[std::min(o.contrastIndex,
                                                                  kContrastCount - 1)]));
        m_items.push_back({ label, "Contrast around mid-grey",
            [this]() {
                m_options.contrastIndex = (m_options.contrastIndex + 1) % kContrastCount;
                applyOptions();
            }, false, true, "settings" });

        std::snprintf(label, sizeof(label), "TONEMAP  %s", tonemapName(o.tonemap));
        m_items.push_back({ label, "Tone curve the display pass uses",
            [this]() {
                m_options.tonemap = (m_options.tonemap + 1) % kTonemapCount;
                applyOptions();
            }, false, true, "settings" });
    }

    // --- SOUND ------------------------------------------------------------
    if (section(kSectionSound, "SOUND", "Master volume")) {
        // Only the master gain is a setting: the engine and environment
        // channels are the session's (silent in the showroom, live in the car).
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
    }

    // --- GAMEPLAY ---------------------------------------------------------
    if (section(kSectionGameplay, "GAMEPLAY", "Fuel, tyre wear and racing rules")) {
        // The two tweaks the session really applies. The ladder is a table
        // (kGameplayValues), so the label, the cursor and the value pushed to
        // the car cannot disagree.
        std::snprintf(label, sizeof(label), "FUEL CONSUMPTION  x%.1f",
                      static_cast<double>(gameplayValue(o.fuelIndex)));
        m_items.push_back({ label, "Tank drain multiplier (1.0 is the car's own rate)",
            [this]() {
                m_options.fuelIndex = (m_options.fuelIndex + 1) % kGameplayCount;
                applyOptions();
            }, false, true, "settings" });
        std::snprintf(label, sizeof(label), "TYRE WEAR  x%.1f",
                      static_cast<double>(gameplayValue(o.tireIndex)));
        m_items.push_back({ label, "Wear multiplier (1.0 is the model's own rate)",
            [this]() {
                m_options.tireIndex = (m_options.tireIndex + 1) % kGameplayCount;
                applyOptions();
            }, false, true, "settings" });
        // How hard track limits bite, not whether they exist: that is the
        // LIMITS verb (FeatureHub), which keeps its own on/off state.
        std::snprintf(label, sizeof(label), "RACING RULES  %s",
                      o.strictRules ? "STRICT" : "LENIENT");
        m_items.push_back({ label,
            "STRICT: every cut costs time and the pit penalties come sooner",
            [this]() {
                m_options.strictRules = !m_options.strictRules;
                applyOptions();
            }, false, true, "settings" });
    }

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
    m_items.push_back({ "PAD BUTTONS", "Remap wheel/pad buttons and H-shifter", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("pad");
    }, false, true, "pad" });
    m_items.push_back({ "CONTROLLER", "Steering deadzone and inversion", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("controller");
    }, false, true, "settings" });
    m_items.push_back({ "BACK", "Return", [this]() { switchMenu(MenuState::Settings); },
        false, true, "back" });
}

void GameMenuOverlay::openCarSelect(const std::string& root)
{
    // No explicit root: the scanner resolves one (run folder, then whatever
    // KS_CONTENT_ROOT or the install prefix this build was configured for
    // carries), so the page lists real content from a build-folder launch.
    m_selectRoot = root.empty() ? carLibraryRoot() : root;
    m_carLibrary = scanCarLibrary(m_selectRoot);
    m_carClassFilter.clear();
    m_carYearFilter = 0;
    m_carBrandFilter.clear();
    m_carClasses.clear();
    m_carYears.clear();
    m_carBrands.clear();
    for (const auto& e : m_carLibrary) {
        if (!e.brand.empty() &&
            std::find(m_carBrands.begin(), m_carBrands.end(), e.brand) == m_carBrands.end())
            m_carBrands.push_back(e.brand);
        if (!e.carClass.empty() &&
            std::find(m_carClasses.begin(), m_carClasses.end(), e.carClass) == m_carClasses.end())
            m_carClasses.push_back(e.carClass);
        if (e.year > 0 &&
            std::find(m_carYears.begin(), m_carYears.end(), e.year) == m_carYears.end())
            m_carYears.push_back(e.year);
    }
    std::sort(m_carBrands.begin(), m_carBrands.end());
    std::sort(m_carClasses.begin(), m_carClasses.end());
    std::sort(m_carYears.begin(), m_carYears.end());
    applyCarFilter();
    // Walking the setup, the page opens on the car itself; from the content
    // browser it opens on its filters, like any other list page.
    if (m_setupFlow) m_focusContent = true;
    switchMenu(MenuState::CarSelect);
}

void GameMenuOverlay::applyCarFilter()
{
    m_selectEntries = filterCars(m_carLibrary, m_carBrandFilter, m_carClassFilter,
                                 m_carYearFilter, m_carYearFilter);
}

void GameMenuOverlay::openTrackSelect()
{
    m_selectRoot = trackLibraryRoot();
    m_selectEntries = scanTrackLibrary(m_selectRoot);
    switchMenu(MenuState::TrackSelect);
}

void GameMenuOverlay::openSessionEvent()
{
    // The weather rows edit the sky in place; unlike a car or a track there is
    // no "chosen" row for NEXT to activate, so the step advances here. Outside
    // the setup walk the page keeps its own parent and NEXT is a no-op.
    if (!m_setupFlow) return;
    switchMenu(MenuState::Singleplayer);
}

void GameMenuOverlay::openTeamSelect()
{
    m_selectRoot = teamLibraryRoot();
    m_selectEntries = scanTeamLibrary(m_selectRoot);
    switchMenu(MenuState::TeamSelect);
}

void GameMenuOverlay::showResults(std::vector<ResultsRow> rows)
{
    m_resultRows = std::move(rows);
    // A new classification is not the one the last export reported.
    m_resultsStatus.clear();
    switchMenu(MenuState::Results);
    setVisible(true);
}

void GameMenuOverlay::buildCarSelectMenu()
{
    m_items.clear();
    char label[96];

    // Only the metadata the library carries gets a row: a content folder with
    // no class never sees a CLASS row it could not fill.
    if (!m_carBrands.empty()) {
        std::snprintf(label, sizeof(label), "BRAND  %s",
                       m_carBrandFilter.empty() ? "any" : m_carBrandFilter.c_str());
        m_items.push_back({ label, "Show one brand only", [this]() {
            size_t next = 0;
            if (!m_carBrandFilter.empty()) {
                const auto it = std::find(m_carBrands.begin(), m_carBrands.end(),
                                          m_carBrandFilter);
                if (it != m_carBrands.end())
                    next = static_cast<size_t>(it - m_carBrands.begin()) + 1;
            }
            m_carBrandFilter = next >= m_carBrands.size() ? std::string() : m_carBrands[next];
            applyCarFilter();
            m_menuDirty = true;
        }, false, true, "flag" });
    }
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
    // The .kn5's own record, as rows above the list: the file the car is built
    // from and the skin it wears. Cycling one writes model.json and rebuilds
    // the car, so what the row says is what is on the turntable — the same
    // shape as the GARAGE upgrade rows.
    if (carImportRowLabels && onCarImportRowCycled) {
        const std::vector<std::string> labels = carImportRowLabels();
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            const int row = i;
            m_items.push_back({ labels[i], "Cycle to the next one, saved in model.json",
                [this, row]() {
                    if (onCarImportRowCycled) onCarImportRowCycled(row);
                    m_menuDirty = true; // rebuild so the label shows the new choice
                }, false, true, "flag" });
        }
    }
    // DATA: what the loaded car declares about itself, read from the folder it
    // came from (CarSpecSheet.h). Only offered when a car is loaded, since a
    // sheet with no car behind it would be a page of dashes.
    if (!m_carDir.empty()) {
        std::snprintf(label, sizeof(label), "DATA  %s",
                      m_carName.empty() ? "car" : m_carName.c_str());
        m_items.push_back({ label, "Weight, power, drivetrain and tyres it declares",
            [this]() { switchMenu(MenuState::CarData); }, false, true, "settings" });
    }
    if (!m_items.empty())
        m_items.push_back({ "", "", nullptr, true });

    m_firstContentRow = static_cast<int>(m_items.size());
    if (m_selectEntries.empty()) {
        const std::string where = "Put car folders in " + m_selectRoot;
        m_items.push_back({ m_carLibrary.empty() ? "(no cars found)" : "(no car matches)",
            m_carLibrary.empty() ? where.c_str() : "Set the filters back to any", nullptr });
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
                // Step 2 of the walk: the track page follows the car. The
                // content browser keeps the old jump back to its own page.
                if (m_setupFlow) openTrackSelect();
                else goBack();
            } });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
    // The walk opens on the car list itself, not on the filter rows.
    if (m_focusContent) {
        m_focusContent = false;
        if (m_firstContentRow < static_cast<int>(m_items.size()))
            m_selectedIndex = m_firstContentRow;
    }
}

void GameMenuOverlay::buildTrackSelectMenu()
{
    m_items.clear();
    m_firstContentRow = 0;
    if (m_selectEntries.empty()) {
        const std::string where = "Put track folders in " + m_selectRoot;
        m_items.push_back({ "(no tracks found)", where, nullptr });
    } else {
        for (const auto& e : m_selectEntries) {
            const std::string path = e.path;
            // What the folder's own metadata declares (ui_track.json, or the
            // data.json CSP builds carry); a folder without any keeps an empty
            // detail instead of a raw path on screen.
            std::string detail = e.country;
            if (!e.city.empty())
                detail += (detail.empty() ? "" : " - ") + e.city;
            if (e.lengthM > 0.0) {
                char length[32];
                std::snprintf(length, sizeof(length), "%.3f km", e.lengthM / 1000.0);
                detail += (detail.empty() ? "" : " - ") + std::string(length);
            }
            if (e.year > 0)
                detail += (detail.empty() ? "" : " - ") + std::to_string(e.year);
            const ContentEntry chosen = e;
            m_items.push_back({ e.label, detail, [this, path, chosen]() {
                if (onTrackChosen) onTrackChosen(path);
                // Step 3 of the walk: the weather of the session, asked right
                // after the track because it is the track's coordinates that
                // the real lookups need. The event page follows it.
                if (m_setupFlow) {
                    m_chosenTrack = chosen;
                    m_chosenTrackValid = true;
                    openTrackWeather();
                } else {
                    goBack();
                }
            } });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
    if (m_focusContent) {
        m_focusContent = false;
        if (m_firstContentRow < static_cast<int>(m_items.size()))
            m_selectedIndex = m_firstContentRow;
    }
}

namespace {
// How the weather page names its four sources.
const char* weatherModeLabel(WeatherSource source) {
    switch (source) {
    case WeatherSource::Dry: return "DRY";
    case WeatherSource::Custom: return "CUSTOM";
    case WeatherSource::Today: return "TODAY";
    case WeatherSource::RealDate: return "REAL DATE";
    }
    return "DRY";
}
} // namespace

// WEATHER: the session's sky, asked between the track and the event, because
// the real lookups need the track's own coordinates. The four modes are dry, a
// custom set of values, today at the track, and the real weather of a chosen
// date; only the last two ask the service (Open-Meteo, no key). Whatever it
// answers is shown; when it cannot answer, the page says why and keeps the
// numbers it had instead of pretending.
void GameMenuOverlay::openTrackWeather()
{
    m_weatherMode = WeatherSource::Dry;
    m_weather = dryOutlook();
    m_weatherDate = todayDateText();
    m_weatherFetched = false;
    m_focusContent = true;
    switchMenu(MenuState::TrackWeather);
}

void GameMenuOverlay::chooseWeatherMode(WeatherSource source, int dayOffset)
{
    setWeatherMode(source);
    if (dayOffset != 0) {
        // setWeatherMode put the date back on today; another day is a second
        // ask, which is the price of not duplicating its rules here.
        m_weatherDate = shiftDate(todayDateText(), dayOffset);
        m_weatherFetched = false;
        refreshWeather();
    }
    // The page's rows carry the mode (MODE, DATE, the time), so a sky chosen
    // from outside its own rows has to rebuild them or the list keeps reading
    // the mode it opened on.
    if (m_currentState == MenuState::TrackWeather) {
        const int sel = m_selectedIndex;
        buildTrackWeatherMenu();
        m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
        m_menuDirty = false;
    }
}

bool GameMenuOverlay::openWeatherPreview(const std::string& trackHint, WeatherSource source,
                                         int dayOffset)
{
    const std::vector<ContentEntry> tracks = scanTrackLibrary(trackLibraryRoot());
    if (tracks.empty()) return false;
    auto lowered = [](std::string text) {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return text;
    };
    const std::string hint = lowered(trackHint);
    const ContentEntry* pick = &tracks.front();
    if (!hint.empty()) {
        for (const ContentEntry& e : tracks) {
            if (lowered(e.label) == hint || lowered(e.id) == hint) {
                pick = &e;
                break;
            }
        }
    }
    m_chosenTrack = *pick;
    m_chosenTrackValid = true;
    m_trackName = pick->label;
    openTrackWeather();
    chooseWeatherMode(source, dayOffset);
    return true;
}

void GameMenuOverlay::setWeatherMode(WeatherSource source)
{
    m_weatherMode = source;
    if (source == WeatherSource::Dry) {
        m_weather = dryOutlook();
        m_weatherFetched = false;
        m_weather.note.clear();
    } else if (source == WeatherSource::Custom) {
        // Custom starts from what the page already shows, so switching mode
        // reads as an edit of the same sky rather than a reset.
        WeatherOutlook custom = customOutlook();
        custom.kind = m_weather.kind;
        custom.ambientC = m_weather.ambientC;
        custom.trackC = m_weather.trackC;
        custom.cloud = m_weather.cloud;
        custom.rain = m_weather.rain;
        custom.wetness = m_weather.wetness;
        custom.windMs = m_weather.windMs;
        custom.fog = m_weather.fog;
        custom.hourOfDay = m_weather.hourOfDay;
        m_weather = custom;
        m_weatherFetched = false;
        m_weather.note.clear();
    } else {
        m_weatherDate = todayDateText();
        m_weather.source = source;
        m_weatherFetched = false;
        m_weather.note.clear();
        refreshWeather();
    }
    m_menuDirty = true;
}

bool GameMenuOverlay::refreshWeather()
{
    m_weather.source = m_weatherMode;
    m_weather.date = m_weatherDate;
    m_weatherFetched = false;
    if (m_weatherMode == WeatherSource::Dry || m_weatherMode == WeatherSource::Custom) {
        m_weather.note.clear();
        m_weatherFetched = true;
        return true;
    }
    // Until the service answers there is no reading: the card draws no icon
    // and says why, instead of showing the previous sky as if it were this one.
    m_weather.kind = WeatherKind::Unknown;
    if (!m_chosenTrackValid || !m_chosenTrack.hasGeo) {
        m_weather.note = "The track declares no coordinates";
        return false;
    }
    const std::string url = openMeteoUrl(m_chosenTrack.latitude, m_chosenTrack.longitude,
                                         m_weatherDate, m_weather.hourOfDay);
    std::string body;
    std::string error;
    if (!httpGetText(url, body, error)) {
        m_weather.note = error.empty() ? "No answer from the weather service" : error;
        std::printf("[weather] %s\n", m_weather.note.c_str());
        return false;
    }
    WeatherOutlook reading = m_weather;
    std::string parseError;
    if (!parseOpenMeteo(body, m_weather.hourOfDay, reading, &parseError)) {
        m_weather.note =
            parseError.empty() ? "The weather answer could not be read" : parseError;
        std::printf("[weather] %s\n", m_weather.note.c_str());
        return false;
    }
    reading.source = m_weather.source;
    reading.date = m_weatherDate;
    reading.hourOfDay = m_weather.hourOfDay;
    reading.note.clear();
    m_weather = reading;
    m_weatherFetched = true;
    std::printf("[weather] %s %s: %s, %.1f C, rain %.2f, cloud %.2f\n",
                weatherModeLabel(m_weather.source), m_weatherDate.c_str(),
                weatherKindName(m_weather.kind), m_weather.ambientC, m_weather.rain,
                m_weather.cloud);
    return true;
}

void GameMenuOverlay::buildTrackWeatherMenu()
{
    m_items.clear();
    m_firstContentRow = 0;
    char text[192];

    std::snprintf(text, sizeof(text), "MODE  %s", weatherModeLabel(m_weatherMode));
    m_items.push_back({ text,
        "Press to cycle dry, custom, today's weather and a real date",
        [this]() {
            static const WeatherSource kOrder[] = { WeatherSource::Dry, WeatherSource::Custom,
                                                    WeatherSource::Today,
                                                    WeatherSource::RealDate };
            int current = 0;
            for (int i = 0; i < 4; ++i)
                if (kOrder[i] == m_weatherMode) current = i;
            setWeatherMode(kOrder[(current + 1) % 4]);
        },
        false, true, "flag" });

    if (m_weatherMode == WeatherSource::Custom) {
        // One row per value: press raises it, wrapping at the top. The track
        // temperature follows the air and the sun, like the real mapping.
        const auto bump = [this](float& value, float step, float lo, float hi) {
            value += step;
            if (value > hi + 0.001f) value = lo;
            if (value < lo) value = hi;
            m_weather.trackC = m_weather.ambientC + (m_weather.cloud < 0.5f ? 8.f : 4.f);
            m_weather.kind = m_weather.rain > 0.05f
                                 ? (m_weather.rain > 0.5f ? WeatherKind::HeavyRain
                                                          : WeatherKind::Rain)
                                 : (m_weather.cloud > 0.8f
                                        ? WeatherKind::Overcast
                                        : (m_weather.cloud > 0.35f ? WeatherKind::PartlyCloudy
                                                                   : WeatherKind::Clear));
            if (m_weather.fog > 0.3f) m_weather.kind = WeatherKind::Fog;
            m_menuDirty = true;
        };
        std::snprintf(text, sizeof(text), "AIR TEMP  %.0f C", m_weather.ambientC);
        m_items.push_back({ text, "Press to raise, wraps at 45 C",
            [this, bump]() { bump(m_weather.ambientC, 1.f, 0.f, 45.f); }, false, true,
            "info" });
        std::snprintf(text, sizeof(text), "RAIN  %.2f", m_weather.rain);
        m_items.push_back({ text, "Press to raise, 0 is dry",
            [this, bump]() { bump(m_weather.rain, 0.1f, 0.f, 1.f); }, false, true, "info" });
        std::snprintf(text, sizeof(text), "WETNESS  %.2f", m_weather.wetness);
        m_items.push_back({ text, "Press to raise, 1 is a soaked track",
            [this, bump]() { bump(m_weather.wetness, 0.1f, 0.f, 1.f); }, false, true,
            "info" });
        std::snprintf(text, sizeof(text), "WIND  %.1f m/s", m_weather.windMs);
        m_items.push_back({ text, "Press to raise, wraps at 20 m/s",
            [this, bump]() { bump(m_weather.windMs, 1.f, 0.f, 20.f); }, false, true,
            "info" });
        std::snprintf(text, sizeof(text), "FOG  %.2f", m_weather.fog);
        m_items.push_back({ text, "Press to raise, 1 is a wall of fog",
            [this, bump]() { bump(m_weather.fog, 0.1f, 0.f, 1.f); }, false, true, "info" });
        std::snprintf(text, sizeof(text), "CLOUD  %.2f", m_weather.cloud);
        m_items.push_back({ text, "Press to raise, 1 is overcast",
            [this, bump]() { bump(m_weather.cloud, 0.1f, 0.f, 1.f); }, false, true, "info" });
    }

    if (m_weatherMode == WeatherSource::Today || m_weatherMode == WeatherSource::RealDate) {
        std::snprintf(text, sizeof(text), "DATE  %s", m_weatherDate.c_str());
        m_items.push_back({ text,
            m_weatherMode == WeatherSource::Today
                ? "Today at the track; press to step a day on"
                : "The day the real weather is asked about",
            [this]() {
                m_weatherDate = shiftDate(m_weatherDate, 1);
                refreshWeather();
                m_menuDirty = true;
            }, false, true, "flag" });
        m_items.push_back({ "DATE  -1 DAY", "Walk the date back a day", [this]() {
                m_weatherDate = shiftDate(m_weatherDate, -1);
                refreshWeather();
                m_menuDirty = true;
            }, false, true, "flag" });
        std::snprintf(text, sizeof(text), "ASK SERVICE  %s",
                      m_weatherFetched ? "OK" : (m_weather.note.empty() ? "-" : "FAILED"));
        m_items.push_back({ text,
            m_weather.note.empty() ? "Ask Open-Meteo again for this day"
                                   : m_weather.note.c_str(),
            [this]() {
                refreshWeather();
                m_menuDirty = true;
            }, false, true, "refresh" });
    }

    std::snprintf(text, sizeof(text), "TIME OF DAY  %02d:%02d",
                  static_cast<int>(m_weather.hourOfDay),
                  static_cast<int>((m_weather.hourOfDay - static_cast<int>(m_weather.hourOfDay)) * 60.f));
    m_items.push_back({ text, "The session runs at this hour; press to move it on",
        [this]() {
            m_weather.hourOfDay += 1.f;
            if (m_weather.hourOfDay >= 24.f) m_weather.hourOfDay = 0.f;
            if (m_weatherMode == WeatherSource::Today || m_weatherMode == WeatherSource::RealDate)
                refreshWeather();
            m_menuDirty = true;
        }, false, true, "clock" });

    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Back to the track list", [this]() { goBack(); } });
    if (m_focusContent) {
        m_focusContent = false;
        m_selectedIndex = 0;
    }
}

// Roadmap 2.8: teams are folders holding a team.ini under content/teams —
// same list screen as cars/tracks, applying the roster the moment it is hit.
const ContentEntry* GameMenuOverlay::selectedTrackEntry() const
{
    if (m_currentState != MenuState::TrackSelect) return nullptr;
    const int index = m_selectedIndex;
    if (index < 0 || index >= static_cast<int>(m_selectEntries.size())) return nullptr;
    return &m_selectEntries[static_cast<std::size_t>(index)];
}

const TrackLogoPreview* GameMenuOverlay::selectedTrackLogo() const
{
    const ContentEntry* entry = selectedTrackEntry();
    if (entry == nullptr) return nullptr;
    const std::string& dir = entry->path;
    const auto found = m_trackLogos.find(dir);
    if (found != m_trackLogos.end())
        return found->second.empty() ? nullptr : &found->second;
    TrackLogoPreview logo;
    loadTrackLogoPreview(dir, logo);
    const auto inserted = m_trackLogos.emplace(dir, std::move(logo));
    return inserted.first->second.empty() ? nullptr : &inserted.first->second;
}

const TrackMapPreview* GameMenuOverlay::selectedTrackMap() const
{
    if (m_currentState != MenuState::TrackSelect) return nullptr;
    const int index = m_selectedIndex;
    if (index < 0 || index >= static_cast<int>(m_selectEntries.size())) return nullptr;
    const std::string& dir = m_selectEntries[static_cast<std::size_t>(index)].path;
    const auto found = m_trackMaps.find(dir);
    if (found != m_trackMaps.end())
        return found->second.empty() ? nullptr : &found->second;
    // First visit: decode once, and remember the folders without a usable map
    // too, so a missing or broken file is not retried every frame.
    TrackMapPreview preview;
    loadTrackMapPreview(dir, preview);
    const auto inserted = m_trackMaps.emplace(dir, std::move(preview));
    return inserted.first->second.empty() ? nullptr : &inserted.first->second;
}

void GameMenuOverlay::buildTeamSelectMenu()
{
    m_items.clear();
    if (m_selectEntries.empty()) {
        const std::string where = "Put team folders (team.ini) in " + m_selectRoot;
        m_items.push_back({ "(no teams found)", where, nullptr });
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
        // The screen is the only place a classification exists, so the row
        // below writes it out. Offered only when the app named a folder; the
        // action marks the page dirty instead of rebuilding it, because
        // activateSelected() is running from inside an item it would replace.
        if (onExportResults) {
            m_items.push_back({ "EXPORT CLASSIFICATION",
                "Write these rows to a CSV file", [this]() {
                    std::vector<ResultExportRow> rows;
                    rows.reserve(m_resultRows.size());
                    // The page keeps its sentence; the file gets the numbers.
                    for (const auto& r : m_resultRows)
                        rows.push_back({ r.position, r.driver, r.status, r.bestLapTime,
                                         r.completedLaps });
                    m_resultsStatus = onExportResults(rows);
                    m_menuDirty = true;
                }, false, true, "download" });
        }
        if (!m_resultsStatus.empty()) {
            m_items.push_back({ "", "", nullptr, true });
            m_items.push_back({ m_resultsStatus, "Last export", nullptr, false, false });
        }
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
    // The people the installed content names, with what each one made. Only
    // declared authors: a folder that names nobody is not credited to anybody.
    const std::vector<ContentCredit> credits =
        collectContentCredits(carLibraryRoot(), trackLibraryRoot());
    m_items.push_back({ "", "", nullptr, true });
    if (credits.empty()) {
        m_items.push_back({ "(no content credits declared)",
            "The installed cars and tracks declare no author in their ui_car.json / ui_track.json",
            nullptr, false, true, "user" });
    } else {
        m_items.push_back({ "CONTENT AUTHORS", "Declared by the cars and tracks installed here",
            nullptr, false, true, "users" });
        for (const ContentCredit& credit : credits) {
            m_items.push_back({ credit.name, contentCreditWhat(credit), nullptr, false, true,
                                "user" });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); }, false, true, "exit" });
}

void GameMenuOverlay::buildCarDataMenu()
{
    m_items.clear();
    CarSpecSheet sheet;
    const bool loaded = !m_carDir.empty() && loadCarSpecSheet(m_carDir, sheet);
    if (!loaded || sheet.empty()) {
        m_items.push_back({ "(no data declared)",
            m_carDir.empty() ? "No car loaded: choose one on the CARS page"
                             : m_carDir + " declares no specs in its data/ folder",
            nullptr, false, false });
    } else {
        char text[128];
        if (sheet.massKg > 0.0) {
            std::snprintf(text, sizeof(text), "WEIGHT  %.0f kg", sheet.massKg);
            m_items.push_back({ text, "car.ini [BASIC] TOTALMASS", nullptr, false, true,
                                "drive" });
        }
        if (sheet.hasCurve()) {
            std::snprintf(text, sizeof(text), "POWER  %.0f hp (%.0f kW) @ %d rpm",
                          sheet.peakPowerHp, sheet.peakPowerKw, sheet.peakPowerRpm);
            m_items.push_back({ text, "power.lut: the curve's highest torque x rpm", nullptr,
                                false, true, "settings" });
            std::snprintf(text, sizeof(text), "TORQUE  %.0f Nm @ %d rpm",
                          sheet.peakTorqueNm, sheet.peakTorqueRpm);
            m_items.push_back({ text, "power.lut: the curve's highest torque", nullptr, false,
                                true, "settings" });
        }
        if (sheet.rpmLimit > 0) {
            std::snprintf(text, sizeof(text), "RPM LIMIT  %d", sheet.rpmLimit);
            m_items.push_back({ text, "engine.ini [ENGINE_DATA] LIMITER", nullptr, false, true,
                                "flag" });
        }
        if (!sheet.drivetrain.empty()) {
            m_items.push_back({ "DRIVETRAIN  " + sheet.drivetrain,
                                "drivetrain.ini [TRACTION] TYPE", nullptr, false, true,
                                "restart" });
        }
        if (!sheet.tyreFront.empty() || !sheet.tyreRear.empty()) {
            const std::string front = sheet.tyreFront.empty() ? "-" : sheet.tyreFront;
            const std::string rear = sheet.tyreRear.empty() ? "-" : sheet.tyreRear;
            m_items.push_back({ "TYRES  " + front + " / " + rear,
                                "tyres.ini [FRONT] and [REAR] NAME", nullptr, false, true,
                                "flag" });
        }
        if (sheet.fuelLitres > 0.0) {
            std::snprintf(text, sizeof(text), "FUEL  %.0f L", sheet.fuelLitres);
            m_items.push_back({ text, "car.ini [FUEL] FUEL", nullptr, false, true, "download" });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return to the car list", [this]() { goBack(); }, false, true,
                        "back" });
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
    // MAIN is where a setup walk ends: entering it drops the chain, so the
    // next SINGLE PLAYER starts again at the car.
    if (state == MenuState::Main) m_setupFlow = false;
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
    case MenuState::ContentPackages: buildContentPackagesMenu(); break;
    case MenuState::ShowroomSelect: buildShowroomSelectMenu(); break;
    case MenuState::Skins: buildSkinsMenu(); break;
    case MenuState::Suits: buildDriverGearMenu(false); break;
    case MenuState::Helmets: buildDriverGearMenu(true); break;
    case MenuState::Settings: buildSettingsMenu(); break;
    case MenuState::Options: buildOptionsMenu(); break;
    case MenuState::Controls: buildControlsMenu(); break;
    case MenuState::CarSelect: buildCarSelectMenu(); break;
    case MenuState::TrackSelect: buildTrackSelectMenu(); break;
    case MenuState::TrackWeather: buildTrackWeatherMenu(); break;
    case MenuState::TeamSelect: buildTeamSelectMenu(); break;
    case MenuState::Results: buildResultsMenu(); break;
    case MenuState::DevModeConfirm: buildDevModeConfirm(); break;
    case MenuState::Credits: buildCreditsMenu(); break;
    case MenuState::CarData: buildCarDataMenu(); break;
    case MenuState::QuitConfirm: buildQuitConfirm(); break;
    case MenuState::FirstRun: buildFirstRunMenu(); break;
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
    // The first-run wizard is mandatory: there is nowhere behind it.
    if (m_currentState == MenuState::FirstRun) return;
    // The setup walk steps back along the chain it walked: event -> tracks ->
    // cars -> MAIN. A page opened from the content browser keeps its own
    // parent instead (m_previousState), which is what the branch below does.
    if (m_setupFlow && m_currentState == MenuState::TrackSelect) {
        m_focusContent = true;
        openCarSelect();
        return;
    }
    // The walk steps back one page at a time: event -> weather -> tracks.
    if (m_setupFlow && m_currentState == MenuState::Singleplayer) {
        openTrackWeather();
        return;
    }
    if (m_setupFlow && m_currentState == MenuState::TrackWeather) {
        m_focusContent = true;
        openTrackSelect();
        return;
    }
    if (m_currentState == MenuState::Controls || m_currentState == MenuState::Options) {
        switchMenu(MenuState::Settings);
        return;
    }
    // The data sheet is the CARS page's own detail view.
    if (m_currentState == MenuState::CarData) {
        switchMenu(MenuState::CarSelect);
        return;
    }
    // The pit strategy screen lives one level under GARAGE.
    if (m_currentState == MenuState::PitStrategy) {
        switchMenu(MenuState::Garage);
        return;
    }
    // The package, showroom and skin lists live one level under CONTENT.
    if (m_currentState == MenuState::ContentPackages ||
        m_currentState == MenuState::ShowroomSelect ||
        m_currentState == MenuState::Skins || m_currentState == MenuState::Suits ||
        m_currentState == MenuState::Helmets) {
        switchMenu(MenuState::ContentManager);
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
        // WEATHER rows edit the chosen sky in place, and the rows themselves
        // change with the mode: rebuild with the cursor kept, like the two
        // pages above.
        else if (m_menuDirty && m_currentState == MenuState::TrackWeather) {
            const int sel = m_selectedIndex;
            buildTrackWeatherMenu();
            m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
            m_menuDirty = false;
        }
    }
}

void GameMenuOverlay::render(int /*width*/, int /*height*/)
{
    if (!m_visible && m_fadeAlpha <= 0.01f) return;
    // The profile rows carry their value in the label, so a field written from
    // outside the page - the text dialog, a loaded profile - has to rebuild
    // them here: every other screen rebuilds from the action that changed it,
    // and without this the page kept showing the old name beside a header that
    // already read the new one.
    if (m_menuDirty && m_currentState == MenuState::Profile) {
        const int sel = m_selectedIndex;
        buildProfileMenu();
        m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
        m_menuDirty = false;
    }
    // The CONTENT page's rows are counts and a manifest state read from disk,
    // so an action that changes either (RESCAN, MANIFEST) rebuilds it here.
    if (m_menuDirty && m_currentState == MenuState::ContentManager) {
        const int sel = m_selectedIndex;
        buildContentManagerMenu();
        m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
        m_menuDirty = false;
    }
    // The wizard's rows carry the name being typed, the country and the
    // number, so every keystroke rebuilds them in place.
    if (m_menuDirty && m_currentState == MenuState::FirstRun) {
        const int sel = m_selectedIndex;
        buildFirstRunMenu();
        m_selectedIndex = std::min(sel, static_cast<int>(m_items.size()) - 1);
        m_menuDirty = false;
    }
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

    // Inline text entry owns the keyboard: BACKSPACE edits, ENTER commits,
    // ESC drops the edit and leaves the row as it was.
    if (m_textEditing) {
        const auto rebuildRows = [this]() {
            const int sel = m_selectedIndex;
            if (m_currentState == MenuState::FirstRun) buildFirstRunMenu();
            else if (m_currentState == MenuState::Profile) buildProfileMenu();
            m_selectedIndex = std::min(
                sel, std::max(0, static_cast<int>(m_items.size()) - 1));
            m_menuDirty = false;
        };
        if (key == 0x08) {
            if (!m_textBuffer.empty()) m_textBuffer.pop_back();
            rebuildRows();
            return true;
        }
        if (key == 0x0D) {
            commitTextEdit();
            rebuildRows();
            return true;
        }
        if (key == 0x1B) {
            cancelTextEdit();
            rebuildRows();
            return true;
        }
        return true; // every other key belongs to the field, not the menu
    }

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

    // One move, one click: the theme's hover sound follows the selection, and a
    // key that landed on the same row (a separator at the end of the list, a
    // disabled row) stays silent because nothing moved on screen.
    auto move = [&](int dir) {
        const int before = m_selectedIndex;
        m_selectedIndex = nextEnabled(m_selectedIndex, dir);
        if (m_selectedIndex != before) emitSound(ui::UiSound::Hover);
        return true;
    };

    switch (key) {
    case 0x26: // UP
    // The in-car bar is horizontal, so LEFT/RIGHT are the same walk as
    // UP/DOWN; on the vertical screens they are harmless equivalents and a
    // pad mapped either way still moves the cursor.
    case 0x25: // LEFT
        return move(-1);
    case 0x28: // DOWN
    case 0x27: // RIGHT
        return move(+1);
    case 0x0D: // ENTER
        emitSound(ui::UiSound::Select);
        activateSelected();
        return true;
    case 0x1B: // ESC
        emitSound(ui::UiSound::Back);
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
    emitSound(ui::UiSound::Select);
    activateSelected();
}

int GameMenuOverlay::drainSounds(ui::UiSound* out, int maxSounds)
{
    if (out == nullptr || maxSounds <= 0) return 0;
    const int count = std::min(maxSounds, m_pendingSoundCount);
    for (int i = 0; i < count; ++i) out[i] = m_pendingSounds[i];
    for (int i = count; i < m_pendingSoundCount; ++i)
        m_pendingSounds[i - count] = m_pendingSounds[i];
    m_pendingSoundCount -= count;
    return count;
}

// ---- SESSION PLANNER ------------------------------------------------------
// The card the header hangs under its clipboard icon. It reads the values a
// start would use and moves one of them per click; the app is what learns the
// car and the track changed, through the same callbacks the pages use.

void GameMenuOverlay::setSessionActive(bool active)
{
    m_sessionActive = active;
    // A card about a session that no longer exists has nothing to show.
    if (!active) m_plannerOpen = false;
}

void GameMenuOverlay::toggleSessionPlanner()
{
    if (!m_sessionActive) return;
    m_plannerOpen = !m_plannerOpen;
}

std::vector<GameMenuOverlay::SessionPlanRow> GameMenuOverlay::sessionPlanRows() const
{
    std::vector<SessionPlanRow> rows;
    char text[64];
    rows.push_back({ "CAR", m_carName.empty() ? "NOT CHOSEN" : m_carName, PlanCar,
                     "click: the next car in the list" });
    rows.push_back({ "TRACK", m_trackName.empty() ? "NOT CHOSEN" : m_trackName, PlanTrack,
                     "click: the next track in the list" });
    std::string sky = weatherKindName(m_weather.kind);
    sky += "  ";
    sky += weatherSourceName(m_weather.source);
    if (m_weather.source == WeatherSource::RealDate && !m_weather.date.empty())
        sky += "  " + m_weather.date;
    rows.push_back({ "WEATHER", sky, PlanWeather, "click: dry, custom, today, a date" });
    rows.push_back({ "SESSION", sessionModeName(m_configMode), PlanMode,
                     "click: the next session mode" });
    if (m_configParams.totalLaps > 0)
        std::snprintf(text, sizeof(text), "%d", m_configParams.totalLaps);
    else
        std::snprintf(text, sizeof(text), "OPEN");
    rows.push_back({ "LAPS", text, PlanLaps, "0 keeps the session open" });
    std::snprintf(text, sizeof(text), "%d", m_configParams.aiCars);
    rows.push_back({ "AI CARS", text, PlanAiCars, "up to 31 on the grid" });
    if (m_configParams.sessionTimeSeconds > 0)
        std::snprintf(text, sizeof(text), "%d:%02d", m_configParams.sessionTimeSeconds / 60,
                      m_configParams.sessionTimeSeconds % 60);
    else
        std::snprintf(text, sizeof(text), "NONE");
    rows.push_back({ "CLOCK", text, PlanClock, "0 runs the session on laps only" });
    rows.push_back({ "STANDING START", m_configParams.standingStart ? "ON" : "OFF",
                     PlanStandingStart, "OFF starts rolling instead of counting down" });
    rows.push_back({ "PIT START", m_configParams.startInGarage ? "ON" : "OFF", PlanPitStart,
                     "begin in the pit box instead of on the grid" });
    return rows;
}

bool GameMenuOverlay::restartAfterPlanChange(bool sessionWasActive)
{
    // The pages change the content of the next session; the card changes the
    // content of one that is running, so that session starts again on it. The
    // card's own sky travels with the restart, as it would on a start. The
    // state is the one from before the load: loading stops the running
    // session, and that is not the same thing as the player leaving it.
    if (sessionWasActive && onSessionRestartRequested) {
        m_configParams.weather = m_weather;
        // A restart from the card puts the car back in its box on the track
        // that was just loaded, not on that track's grid. The row shows it, so
        // the next start from MODE SETUP agrees with what just happened.
        m_configParams.startInGarage = true;
        onSessionRestartRequested(m_configParams);
    }
    return true;
}

bool GameMenuOverlay::cycleSessionPlanField(int field)
{
    // Read before the load: the callbacks that load the content stop the
    // running session, and the restart still belongs to that session.
    const bool sessionWasActive = m_sessionActive;
    switch (field) {
    case PlanCar: {
        const std::vector<ContentEntry> cars = scanCarLibrary(carLibraryRoot());
        if (cars.empty()) return false;
        int at = 0;
        for (int i = 0; i < static_cast<int>(cars.size()); ++i)
            if (cars[i].label == m_carName) at = i;
        const ContentEntry& next = cars[(at + 1) % static_cast<int>(cars.size())];
        setCarName(next.label);
        // The car's own first colour is its paint, same rule as the CARS page.
        m_carPaints = next.paints;
        m_carPaintIndex = 0;
        if (onCarChosen) onCarChosen(next.path);
        if (!m_carPaints.empty() && onPaintChosen) {
            const CarPaint& p = m_carPaints[0];
            onPaintChosen(p.r, p.g, p.b);
        }
        return restartAfterPlanChange(sessionWasActive);
    }
    case PlanTrack: {
        const std::vector<ContentEntry> tracks = scanTrackLibrary(trackLibraryRoot());
        if (tracks.empty()) return false;
        int at = 0;
        for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
            if (tracks[i].label == m_trackName) at = i;
        const ContentEntry& next = tracks[(at + 1) % static_cast<int>(tracks.size())];
        setTrackName(next.label);
        // The weather card reads the coordinates of the track the session
        // runs on, so the choice travels with the new track.
        m_chosenTrack = next;
        m_chosenTrackValid = true;
        if (onTrackChosen) onTrackChosen(next.path);
        if (m_weatherMode != WeatherSource::Dry) refreshWeather();
        return restartAfterPlanChange(sessionWasActive);
    }
    case PlanWeather: {
        static const WeatherSource kOrder[] = {
            WeatherSource::Dry, WeatherSource::Custom, WeatherSource::Today,
            WeatherSource::RealDate};
        constexpr int kCount = sizeof(kOrder) / sizeof(kOrder[0]);
        int at = 0;
        for (int i = 0; i < kCount; ++i)
            if (kOrder[i] == m_weatherMode) at = i;
        setWeatherMode(kOrder[(at + 1) % kCount]);
        return true;
    }
    case PlanMode: {
        const std::vector<GameSessionMode> modes = effectiveSingleplayerModes();
        if (modes.empty()) return false;
        int at = 0;
        for (int i = 0; i < static_cast<int>(modes.size()); ++i)
            if (modes[i] == m_configMode) at = i;
        m_configMode = modes[(at + 1) % static_cast<int>(modes.size())];
        return true;
    }
    case PlanLaps:
        m_configParams.totalLaps = m_configParams.totalLaps >= 100
            ? 0 : m_configParams.totalLaps + 1;
        return true;
    case PlanAiCars:
        m_configParams.aiCars = (m_configParams.aiCars + 1) % 32;
        return true;
    case PlanClock:
        m_configParams.sessionTimeSeconds =
            (m_configParams.sessionTimeSeconds + 300) % (3 * 3600 + 300);
        return true;
    case PlanStandingStart:
        m_configParams.standingStart = !m_configParams.standingStart;
        return true;
    case PlanPitStart:
        m_configParams.startInGarage = !m_configParams.startInGarage;
        return true;
    default:
        return false;
    }
}

} // namespace ks::sim
