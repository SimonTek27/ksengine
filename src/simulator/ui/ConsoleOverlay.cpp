#include "ConsoleOverlay.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ks {
namespace sim {
namespace ui {

namespace {

const char* environmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != 0 ? value : nullptr;
}

std::string upperCopy(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

ConsoleOverlay::ConsoleOverlay() {
    registerCommand("HELP", [this](const std::vector<std::string>& args) { return cmdHelp(args); });
    registerCommand("CLEAR", [this](const std::vector<std::string>& args) { return cmdClear(args); });
    registerCommand("SCREENSHOT",
                    [this](const std::vector<std::string>& args) { return cmdScreenshot(args); });
    registerCommand("PHOTOMODE",
                    [this](const std::vector<std::string>& args) { return cmdPhotomode(args); });
    registerCommand("PPFILTER",
                    [this](const std::vector<std::string>& args) { return cmdPpfilter(args); });
    registerCommand("LIST", [this](const std::vector<std::string>& args) { return cmdList(args); });
    registerCommand("VERSION",
                    [this](const std::vector<std::string>& args) { return cmdVersion(args); });
    registerCommand("DEBUG", [this](const std::vector<std::string>& args) { return cmdDebug(args); });
    registerCommand("COCKPITEDITOR",
                    [this](const std::vector<std::string>& args) { return cmdCockpitEditor(args); });
    registerCommand("THEME", [this](const std::vector<std::string>& args) { return cmdTheme(args); });
    addLine("ksim console - HELP lists the commands, INSERT closes it.");
}

void ConsoleOverlay::setVisible(bool v) {
    m_visible = v;
    if (!v) {
        m_typing = false;
        m_input.clear();
        m_historyPos = 0;
    }
}

void ConsoleOverlay::registerCommand(std::string verb, CommandHandler handler) {
    m_commands[upperCopy(std::move(verb))] = std::move(handler);
}

void ConsoleOverlay::addLine(const std::string& line) {
    m_lines.push_back(line);
    while (m_lines.size() > kMaxLines) m_lines.pop_front();
}

void ConsoleOverlay::execute(const std::string& line) {
    if (line.empty()) return;

    m_cmdHistory.push_back(line);
    while (m_cmdHistory.size() > kMaxCmdHistory) m_cmdHistory.pop_front();
    m_historyPos = 0;

    std::istringstream stream(line);
    std::string verb;
    stream >> verb;
    std::vector<std::string> args;
    std::string argument;
    while (stream >> argument) args.push_back(argument);

    addLine("> " + line);
    const auto handler = m_commands.find(upperCopy(verb));
    if (handler == m_commands.end()) {
        addLine("  unknown command '" + verb + "' (type HELP)");
        return;
    }
    handler->second(args);
}

void ConsoleOverlay::build(DrawList& dl, int screenW, int screenH, UiInput* /*input*/) {
    if (!m_visible) return;

    const float panelX = 24.f;
    const float panelW = static_cast<float>(screenW) - 48.f;
    const float panelY = static_cast<float>(screenH) * 0.34f;
    const float panelH = static_cast<float>(screenH) * 0.62f;

    dl.addRectFilled({panelX, panelY, panelW, panelH}, Color::rgba(0.03f, 0.04f, 0.06f, 0.92f));
    dl.addRect({panelX, panelY, panelW, panelH}, Color::rgb(0, 200, 255), 1.f);

    const float pad = 12.f;
    dl.addText(panelX + pad, panelY + pad, "CONSOLE", Color::rgb(120, 220, 255), 1.1f);
    dl.addText(panelX + pad + 92.f, panelY + pad + 2.f,
               "ENTER type   UP/DOWN history   ESC or INSERT close",
               Color::rgb(130, 145, 160), 0.9f);

    const float headH = 30.f;
    const float inputH = 30.f;
    drawHistory(dl, panelX + pad, panelY + pad + headH, panelW - pad * 2.f,
                panelH - headH - inputH - pad * 2.f);
    drawInputLine(dl, panelX + pad, panelY + panelH - inputH - pad - 4.f, panelW - pad * 2.f);
    ++m_ticks;
}

void ConsoleOverlay::drawHistory(DrawList& dl, float x, float y, float w, float h) {
    const float lineH = 15.f;
    const std::size_t visible = h > lineH ? static_cast<std::size_t>(h / lineH) : 0;
    const std::size_t count = m_lines.size() < visible ? m_lines.size() : visible;
    const std::size_t first = m_lines.size() - count;

    float lineY = y;
    for (std::size_t i = first; i < m_lines.size(); ++i, lineY += lineH) {
        const std::string& line = m_lines[i];
        // The history keeps its own width: a long line is cut, not wrapped,
        // so the panel height stays predictable.
        std::string shown = line;
        if (shown.size() > 4 && m_text.measureWidth(shown, 0.92f) > w - 12.f) {
            while (shown.size() > 4 && m_text.measureWidth(shown + "...", 0.92f) > w - 12.f)
                shown.pop_back();
            shown += "...";
        }
        const Color tint = (!line.empty() && line[0] == '>') ? Color::rgb(150, 210, 255)
                                                            : Color::rgb(215, 225, 235);
        dl.addText(x, lineY, shown, tint, 0.92f);
    }
}

void ConsoleOverlay::drawInputLine(DrawList& dl, float x, float y, float w) {
    dl.addRectFilled({x, y, w, 22.f}, Color::rgba(0.06f, 0.09f, 0.14f, 1.f));
    dl.addRect({x, y, w, 22.f}, Color::rgb(60, 150, 200), 1.f);

    std::string shown = std::string(m_typing ? "> " : "1> ") + m_input;
    if (m_typing && ((m_ticks / 30) % 2 == 0)) shown.push_back('_');
    dl.addText(x + 6.f, y + 5.f, shown, Color::rgb(240, 245, 250), 0.95f);
}

bool ConsoleOverlay::handleKey(int key) {
    if (!m_visible) return false;
    switch (key) {
        case 0x1B: // ESC
            if (m_typing) {
                m_typing = false;
                m_input.clear();
            } else {
                setVisible(false);
            }
            return true;
        case 0x0D: // ENTER
            m_typing = true;
            return true;
        case 0x26: { // UP: older command
            if (m_cmdHistory.empty()) return true;
            if (m_historyPos < m_cmdHistory.size()) ++m_historyPos;
            m_input = m_cmdHistory[m_cmdHistory.size() - m_historyPos];
            m_typing = true;
            return true;
        }
        case 0x28: // DOWN: newer command, then an empty prompt
            if (m_historyPos == 0) return true;
            --m_historyPos;
            m_input = m_historyPos == 0 ? std::string()
                                        : m_cmdHistory[m_cmdHistory.size() - m_historyPos];
            m_typing = true;
            return true;
        default:
            // While the console is up every key belongs to it: nothing
            // underneath (menu, camera, car) reacts to what is being typed.
            return true;
    }
}

bool ConsoleOverlay::handleChar(int c) {
    if (!m_visible) return false;
    if (c == 13) { // ENTER runs the line
        const std::string line = m_input;
        m_input.clear();
        m_typing = false;
        m_historyPos = 0;
        if (!line.empty()) execute(line);
        return true;
    }
    if (c == 8) { // BACKSPACE
        if (!m_input.empty()) m_input.pop_back();
        return true;
    }
    if (c == 27) { // ESC
        if (m_typing) {
            m_typing = false;
            m_input.clear();
        } else {
            setVisible(false);
        }
        return true;
    }
    if (c >= 32 && c < 127) {
        if (m_input.size() < 200) m_input.push_back(static_cast<char>(c));
        m_typing = true;
    }
    return true;
}

// --- commands ---------------------------------------------------------------

bool ConsoleOverlay::cmdHelp(const std::vector<std::string>& /*args*/) {
    addLine("  HELP        - this list");
    addLine("  CLEAR       - empty the log");
    addLine("  LIST        - presets shipped in system/ppfilters");
    addLine("  PPFILTER    - current post-processing preset");
    addLine("  SCREENSHOT  - how to capture a frame");
    addLine("  PHOTOMODE   - photo mode key");
    addLine("  COCKPITEDITOR - toggle the cockpit editor flag");
    addLine("  THEME       - interface theme in use");
    addLine("  DEBUG       - list or set the KS_DEBUG_* flags");
    addLine("  VERSION     - build info");
    return true;
}

bool ConsoleOverlay::cmdClear(const std::vector<std::string>& /*args*/) {
    m_lines.clear();
    addLine("log cleared");
    return true;
}

bool ConsoleOverlay::cmdScreenshot(const std::vector<std::string>& /*args*/) {
    addLine("start with KS_SCREENSHOT=<path>; the frame is written after the first render");
    return true;
}

bool ConsoleOverlay::cmdPhotomode(const std::vector<std::string>& /*args*/) {
    addLine("photo mode: KS_PHOTOMODE=<key>, or F12 while driving");
    return true;
}

bool ConsoleOverlay::cmdPpfilter(const std::vector<std::string>& /*args*/) {
    if (const char* preset = environmentValue("KS_PPFILTER"))
        addLine(std::string("current preset: ") + preset);
    else
        addLine("no KS_PPFILTER set: the filter is chosen by the session");
    return true;
}

bool ConsoleOverlay::cmdList(const std::vector<std::string>& /*args*/) {
    addLine("presets in system/ppfilters: normal, bw, inverted, sepia, tv80, photomode");
    addLine("pick one with KS_PPFILTER=<id> at start-up");
    return true;
}

bool ConsoleOverlay::cmdVersion(const std::vector<std::string>& /*args*/) {
    addLine("ksim - developer console");
    addLine(std::string("build ") + __DATE__ + " " + __TIME__);
    return true;
}

bool ConsoleOverlay::cmdDebug(const std::vector<std::string>& args) {
    static const char* kFlags[] = {"TYRES",     "ENGINE",      "TURBO",    "HYBRID",
                                   "ELECTRIC",  "ELECTRONICS", "SUSPENSIONS", "TRANSMISSION",
                                   "DRS",       "ASPHALT",     "ESCAPE",   "GRASS",
                                   "GRAVEL",    "PITLANE",     "GRID",     "SECTOR1",
                                   "SECTOR2",   "SECTOR3",     "CARS",     "TRACKS",
                                   "CHARACTERS", "SHOWROOM"};
    if (args.empty()) {
        addLine("debug flags (set with: DEBUG <name>, or KS_DEBUG_<NAME>=1):");
        for (const char* flag : kFlags) {
            const std::string env = std::string("KS_DEBUG_") + flag;
            if (environmentValue(env.c_str()) != nullptr) addLine(std::string("  ") + flag + ": on");
        }
        addLine("nothing listed means every flag is off");
        return true;
    }

    const std::string flag = upperCopy(args[0]);
    bool known = false;
    for (const char* candidate : kFlags) {
        if (flag == candidate) {
            known = true;
            break;
        }
    }
    if (!known) {
        addLine("unknown debug target '" + args[0] + "'");
        return true;
    }
    const std::string env = "KS_DEBUG_" + flag;
#ifdef _WIN32
    _putenv_s(env.c_str(), "1");
#else
    setenv(env.c_str(), "1", 1);
#endif
    addLine(env + "=1 (read by the next start)");
    return true;
}

bool ConsoleOverlay::cmdCockpitEditor(const std::vector<std::string>& /*args*/) {
    const bool on = environmentValue("KS_COCKPIT_EDITOR") == nullptr;
#ifdef _WIN32
    _putenv_s("KS_COCKPIT_EDITOR", on ? "1" : "0");
#else
    setenv("KS_COCKPIT_EDITOR", on ? "1" : "0", 1);
#endif
    addLine(on ? "cockpit editor: on" : "cockpit editor: off");
    return true;
}

bool ConsoleOverlay::cmdTheme(const std::vector<std::string>& /*args*/) {
    if (const char* theme = environmentValue("KS_UI_THEME"))
        addLine(std::string("interface theme: ") + theme);
    else
        addLine("interface theme: default (KS_UI_THEME picks another)");
    return true;
}

} // namespace ui
} // namespace sim
} // namespace ks
