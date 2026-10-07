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
    case MenuState::CarSelect: return "SELECT CAR";
    case MenuState::TrackSelect: return "SELECT TRACK";
    case MenuState::Results: return "RESULTS";
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
    m_items.push_back({ "CAR", m_carName.empty() ? "Select a car" : m_carName,
        [this]() { openCarSelect(); } });
    m_items.push_back({ "TRACK", m_trackName.empty() ? "Select a track" : m_trackName,
        [this]() { openTrackSelect(); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "PRACTICE", "Open session on the selected circuit",
        [this]() { if (onStartDrivingRequested) onStartDrivingRequested(); setVisible(false); } });
    m_items.push_back({ "QUICK RACE", "Grid start against AI",
        [this]() { if (onStartDrivingRequested) onStartDrivingRequested(); setVisible(false); } });
    m_items.push_back({ "TIME ATTACK", "Clean laps against the clock",
        [this]() { if (onStartDrivingRequested) onStartDrivingRequested(); setVisible(false); } });
    m_items.push_back({ "RESULTS", "Session standings",
        [this]() { if (onShowResultsRequested) onShowResultsRequested(); } });
    m_items.push_back({ "", "", nullptr, true });
    m_items.push_back({ "BACK", "Return",
        [this]() { goBack(); } });
}

void GameMenuOverlay::buildMultiplayerMenu()
{
    m_items.clear();
    m_items.push_back({ "HOST SESSION", "Run a server on this machine (LAN/online)",
        [this]() { if (onHostServerRequested) onHostServerRequested(); } });
    m_items.push_back({ "JOIN SESSION", "Open the server browser, F2 also brings it up",
        [this]() { if (onOpenServerBrowserRequested) onOpenServerBrowserRequested(); } });
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

void GameMenuOverlay::buildGarageMenu()
{
    m_items.clear();
    m_items.push_back({ "SETUP", "Aero, gears, brakes and dampers",
        [this]() { if (onOpenSetupGarageRequested) onOpenSetupGarageRequested(); } });
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
        [this]() { openTrackSelect(); } });
    m_items.push_back({ "CARS", "Browse vehicles",
        [this]() { openCarSelect(); } });
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
    m_items.push_back({ "KEYBOARD", "Driving bindings", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("keyboard");
    } });
    m_items.push_back({ "WHEEL", "Device and FFB", [this]() {
        if (onOpenSettingsPanelRequested) onOpenSettingsPanelRequested("devices");
    } });
    m_items.push_back({ "BACK", "Return", [this]() { switchMenu(MenuState::Settings); } });
}

void GameMenuOverlay::openCarSelect()
{
    m_selectEntries = scanCarLibrary();
    switchMenu(MenuState::CarSelect);
}

void GameMenuOverlay::openTrackSelect()
{
    m_selectEntries = scanTrackLibrary();
    switchMenu(MenuState::TrackSelect);
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
    if (m_selectEntries.empty()) {
        m_items.push_back({ "(no cars found)",
            "Put car folders in content/cars", nullptr });
    } else {
        for (const auto& e : m_selectEntries) {
            const std::string path = e.path;
            m_items.push_back({ e.label, e.path, [this, path]() {
                if (onCarChosen) onCarChosen(path);
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
    case MenuState::CarSelect: buildCarSelectMenu(); break;
    case MenuState::TrackSelect: buildTrackSelectMenu(); break;
    case MenuState::Results: buildResultsMenu(); break;
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
    // Select/results screens return to whoever opened them (Singleplayer or
    // Content), falling back to Main when entered directly.
    if (m_currentState == MenuState::CarSelect ||
        m_currentState == MenuState::TrackSelect ||
        m_currentState == MenuState::Results) {
        if (m_previousState != m_currentState &&
            m_previousState != MenuState::CarSelect &&
            m_previousState != MenuState::TrackSelect &&
            m_previousState != MenuState::Results)
            switchMenu(m_previousState);
        else
            switchMenu(MenuState::Main);
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
