#include <cstdio>
#include "GameMenuOverlay.h"
#include <algorithm>
#include <cmath>

namespace ks::sim {

GameMenuOverlay::GameMenuOverlay()
{
    buildMainMenu();
}

std::string GameMenuOverlay::sectionTitle() const
{
    switch (m_currentState) {
    case MenuState::Main: return "MAIN MENU";
    case MenuState::Singleplayer: return "SINGLE PLAYER";
    case MenuState::Multiplayer: return "MULTI PLAYER";
    case MenuState::Profile: return "DRIVER";
    case MenuState::Garage: return "GARAGE";
    case MenuState::Replay: return "REPLAY";
    case MenuState::ContentManager: return "CONTENT";
    case MenuState::Settings: return "SETTINGS";
    case MenuState::Controls: return "CONTROLS";
    case MenuState::ServerBrowser: return "SERVER BROWSER";
    case MenuState::Weather: return "WEATHER";
    case MenuState::PitStrategy: return "PIT STRATEGY";
    case MenuState::DevModeConfirm: return "EDITOR";
    case MenuState::QuitConfirm: return "QUIT";
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

void GameMenuOverlay::buildMainMenu()
{
    m_items.clear();
    m_items.push_back({ "SINGLE PLAYER", "Practice, race and time attack",
        [this]() { switchMenu(MenuState::Singleplayer); } });
    m_items.push_back({ "MULTI PLAYER", "Online and LAN sessions",
        [this]() { switchMenu(MenuState::Multiplayer); } });
    m_items.push_back({ "GARAGE", "Setup, tyres and vehicle options",
        [this]() { switchMenu(MenuState::Garage); } });
    m_items.push_back({ "REPLAY", "Playback and recording",
        [this]() { switchMenu(MenuState::Replay); } });
    m_items.push_back({ "CONTENT", "Cars, tracks and packages",
        [this]() { switchMenu(MenuState::ContentManager); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "DRIVER", "Profile and career stats",
        [this]() { switchMenu(MenuState::Profile); } });
    m_items.push_back({ "SETTINGS", "Display, audio and input",
        [this]() { switchMenu(MenuState::Settings); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "EDITOR", "Leave simulator and open the editor",
        [this]() { switchMenu(MenuState::DevModeConfirm); } });
    m_items.push_back({ "QUIT", "Exit ksim",
        [this]() { switchMenu(MenuState::QuitConfirm); } });
}

void GameMenuOverlay::buildSingleplayerMenu()
{
    m_items.clear();
    auto startMode = [this](const char* label) {
        if (onStartSessionRequested) onStartSessionRequested(label);
        else if (onStartDrivingRequested) onStartDrivingRequested();
        setVisible(false);
    };
    m_items.push_back({ "PRACTICE", "Open session on the selected circuit",
        [startMode]() { startMode("PRACTICE"); } });
    m_items.push_back({ "QUICK RACE", "Grid start against AI",
        [startMode]() { startMode("QUICK RACE"); } });
    m_items.push_back({ "TIME ATTACK", "Clean laps against the clock",
        [startMode]() { startMode("TIME ATTACK"); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return",
        [this]() { goBack(); } });
}


void GameMenuOverlay::setServerList(const std::vector<BrowserServerEntry>& list)
{
    m_servers = list;
    if (m_currentState == MenuState::ServerBrowser)
        m_menuDirty = true;
}

void GameMenuOverlay::setWeatherSummary(const std::string& preset, float hours)
{
    m_weatherPreset = preset.empty() ? "Dry" : preset;
    m_timeOfDayHours = hours;
    if (m_currentState == MenuState::Weather)
        m_menuDirty = true;
}

void GameMenuOverlay::buildServerBrowserMenu()
{
    m_items.clear();
    m_items.push_back({ "REFRESH", "Query LAN hosts (UDP discovery)",
        [this]() {
            if (onRefreshServerListRequested) onRefreshServerListRequested();
            m_menuDirty = true;
        } });
    m_items.push_back({ "", "", nullptr, true });

    if (m_servers.empty()) {
        m_items.push_back({ "(no servers found)", "Refresh or host a session on the LAN",
            nullptr, false, false });
    } else {
        for (size_t i = 0; i < m_servers.size(); ++i) {
            const auto& s = m_servers[i];
            char line[160];
            std::snprintf(line, sizeof(line), "%s  [%d/%d]",
                          s.name.c_str(), s.players, s.maxPlayers);
            char desc[192];
            std::snprintf(desc, sizeof(desc), "%s  %s:%u",
                          s.track.c_str(), s.host.c_str(), (unsigned)s.port);
            m_items.push_back({ line, desc, [this, i]() {
                if (i < m_servers.size() && onJoinServerRequested)
                    onJoinServerRequested(m_servers[i]);
            } });
        }
    }
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "MANUAL JOIN", "Enter host address (callback)",
        [this]() {
            BrowserServerEntry manual;
            manual.name = "Manual";
            manual.host = "127.0.0.1";
            manual.port = 40000;
            if (onJoinServerRequested) onJoinServerRequested(manual);
            if (onTextInputRequested) onTextInputRequested("SERVER_HOST", "127.0.0.1");
        } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildWeatherMenu()
{
    m_items.clear();
    char tod[64];
    std::snprintf(tod, sizeof(tod), "TIME OF DAY  %.1fh", m_timeOfDayHours);
    m_items.push_back({ tod, "Current session clock (hours 0–24)", nullptr, false, false });
    m_items.push_back({ "TIME -1h", "Move clock back one hour",
        [this]() {
            if (onTimeOfDayAdjustRequested) onTimeOfDayAdjustRequested(-1.f);
            else if (onTimeOfDayRequested) {
                float h = m_timeOfDayHours - 1.f;
                if (h < 0.f) h += 24.f;
                onTimeOfDayRequested(h);
            }
            m_timeOfDayHours -= 1.f;
            if (m_timeOfDayHours < 0.f) m_timeOfDayHours += 24.f;
            m_menuDirty = true;
        } });
    m_items.push_back({ "TIME +1h", "Move clock forward one hour",
        [this]() {
            if (onTimeOfDayAdjustRequested) onTimeOfDayAdjustRequested(1.f);
            else if (onTimeOfDayRequested) {
                float h = m_timeOfDayHours + 1.f;
                if (h >= 24.f) h -= 24.f;
                onTimeOfDayRequested(h);
            }
            m_timeOfDayHours += 1.f;
            if (m_timeOfDayHours >= 24.f) m_timeOfDayHours -= 24.f;
            m_menuDirty = true;
        } });
    m_items.push_back({ "NOON", "Set 12:00",
        [this]() {
            m_timeOfDayHours = 12.f;
            if (onTimeOfDayRequested) onTimeOfDayRequested(12.f);
            m_menuDirty = true;
        } });
    m_items.push_back({ "SUNSET", "Set 18:30",
        [this]() {
            m_timeOfDayHours = 18.5f;
            if (onTimeOfDayRequested) onTimeOfDayRequested(18.5f);
            m_menuDirty = true;
        } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "PRESET: DRY", "Clear, high grip",
        [this]() {
            m_weatherPreset = "Dry";
            if (onWeatherPresetRequested) onWeatherPresetRequested("dry");
            m_menuDirty = true;
        } });
    m_items.push_back({ "PRESET: DAMP", "Light moisture",
        [this]() {
            m_weatherPreset = "Damp";
            if (onWeatherPresetRequested) onWeatherPresetRequested("damp");
            m_menuDirty = true;
        } });
    m_items.push_back({ "PRESET: WET", "Rain and low grip",
        [this]() {
            m_weatherPreset = "Wet";
            if (onWeatherPresetRequested) onWeatherPresetRequested("wet");
            m_menuDirty = true;
        } });
    char cur[80];
    std::snprintf(cur, sizeof(cur), "CURRENT: %s @ %.1fh",
                  m_weatherPreset.c_str(), m_timeOfDayHours);
    m_items.push_back({ cur, "Active conditions", nullptr, false, false });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildMultiplayerMenu()
{
    m_items.clear();
    m_items.push_back({ "HOST SESSION", "Run a server on this machine (LAN/online)",
        [this]() { if (onHostServerRequested) onHostServerRequested(); } });
    m_items.push_back({ "JOIN SESSION", "Open the server browser (LAN discovery)",
        [this]() {
            if (onOpenServerBrowserRequested) onOpenServerBrowserRequested();
            if (onRefreshServerListRequested) onRefreshServerListRequested();
            switchMenu(MenuState::ServerBrowser);
        } });
    m_items.push_back({ "DISCONNECT", "Leave the current session or stop the server",
        [this]() { if (onDisconnectRequested) onDisconnectRequested(); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildProfileMenu()
{
    m_items.clear();
    m_items.push_back({ "NAME", m_profile.name,
        [this]() { if (onTextInputRequested) onTextInputRequested("NAME", m_profile.name); } });
    m_items.push_back({ "NATIONALITY", m_profile.nationality,
        [this]() { if (onNationalityInputRequested) onNationalityInputRequested(); } });
    m_items.push_back({ "NUMBER", std::to_string(m_profile.raceNumber),
        [this]() { m_profile.raceNumber = (m_profile.raceNumber % 99) + 1; m_menuDirty = true; } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}


void GameMenuOverlay::setPitStrategy(float fuelL, bool tyres, bool body, bool susp, bool aero, bool engine)
{
    m_pitFuelTargetL = fuelL;
    m_pitWantTyres = tyres;
    m_pitWantBody = body;
    m_pitWantSuspension = susp;
    m_pitWantAero = aero;
    m_pitWantEngine = engine;
    if (m_currentState == MenuState::PitStrategy)
        m_menuDirty = true;
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
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildGarageMenu()
{
    m_items.clear();
    m_items.push_back({ "SETUP", "Aero, gears, brakes and dampers",
        [this]() { if (onOpenSetupGarageRequested) onOpenSetupGarageRequested(); } });
    m_items.push_back({ "PIT STRATEGY", "Fuel, tyres and repair plan for next stop",
        [this]() {
            if (onOpenPitStrategyRequested) onOpenPitStrategyRequested();
            switchMenu(MenuState::PitStrategy);
        } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildReplayMenu()
{
    m_items.clear();
    m_items.push_back({ "LOAD", "Open a saved session",
        [this]() { if (onLoadReplayRequested) onLoadReplayRequested(); } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildContentManagerMenu()
{
    m_items.clear();
    m_items.push_back({ "TRACKS", "Browse circuits",
        [this]() { if (onOpenContentBrowserRequested) onOpenContentBrowserRequested("tracks"); } });
    m_items.push_back({ "CARS", "Browse vehicles",
        [this]() { if (onOpenContentBrowserRequested) onOpenContentBrowserRequested("cars"); } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildSettingsMenu()
{
    m_items.clear();
    m_items.push_back({ "GRAPHICS", "Resolution and quality", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("graphics");
    } });
    m_items.push_back({ "AUDIO", "Volumes and devices",
        [this]() { if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("audio"); } });
    m_items.push_back({ "CONTROLS", "Bindings and devices",
        [this]() { switchMenu(MenuState::Controls); } });
    m_items.push_back({ "BACK", "Return", [this]() { goBack(); } });
}

void GameMenuOverlay::buildControlsMenu()
{
    m_items.clear();
    m_items.push_back({ "KEYBOARD", "Driving bindings", [this]() {} });
    m_items.push_back({ "WHEEL", "Device and FFB", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("devices");
    } });
    m_items.push_back({ "BACK", "Return", [this]() { switchMenu(MenuState::Settings); } });
}

void GameMenuOverlay::buildDevModeConfirm()
{
    m_items.clear();
    m_items.push_back({ "CONFIRM", "Open editor",
        [this]() { if (onDevModeRequested) onDevModeRequested(); } });
    m_items.push_back({ "CANCEL", "Stay in ksim", [this]() { goBack(); } });
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
    case MenuState::Multiplayer: buildMultiplayerMenu(); break;
    case MenuState::Profile: buildProfileMenu(); break;
    case MenuState::Garage: buildGarageMenu(); break;
    case MenuState::Replay: buildReplayMenu(); break;
    case MenuState::ContentManager: buildContentManagerMenu(); break;
    case MenuState::Settings: buildSettingsMenu(); break;
    case MenuState::Controls: buildControlsMenu(); break;
    case MenuState::DevModeConfirm: buildDevModeConfirm(); break;
    case MenuState::QuitConfirm: buildQuitConfirm(); break;
    }
    // skip leading separators
    while (m_selectedIndex < static_cast<int>(m_items.size()) && m_items[m_selectedIndex].isSeparator)
        ++m_selectedIndex;
}

void GameMenuOverlay::goBack()
{
    if (m_currentState == MenuState::Main) {
        setVisible(false);
        return;
    }
    if (m_currentState == MenuState::Controls) {
        switchMenu(MenuState::Settings);
        return;
    }
    switchMenu(MenuState::Main);
}

void GameMenuOverlay::activateSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) return;
    const auto& item = m_items[m_selectedIndex];
    if (item.enabled && item.action && !item.isSeparator)
        item.action();
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
