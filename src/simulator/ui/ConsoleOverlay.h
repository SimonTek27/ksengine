#pragma once
/**
 * Developer console (roadmap 4.x): a rolling log plus a one-line command
 * prompt, opened and closed with INSERT.
 *
 * Text arrives through WM_CHAR, so the printable ASCII range is available
 * including punctuation; ENTER runs the line, ESC closes the console, the
 * arrow keys walk the command history. Commands are registered by verb (upper
 * case) and each handler appends its own output with addLine().
 *
 * Nothing here reaches into the renderer or the simulation: an overlay owns
 * what it draws and what it prints, like the chat does.
 */
#include "NativeUiTypes.h"
#include "TextRenderer.h"
#include "UiInput.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ks {
namespace sim {
namespace ui {

class ConsoleOverlay {
public:
    ConsoleOverlay();

    void setVisible(bool v);
    bool isVisible() const { return m_visible; }
    bool isTyping() const { return m_typing; }
    void startTyping() { m_typing = true; }
    void stopTyping() { m_typing = false; }
    void toggle() { setVisible(!m_visible); }

    /// Command execution: return true when the command was handled.
    using CommandHandler = std::function<bool(const std::vector<std::string>& args)>;
    void registerCommand(std::string verb, CommandHandler handler);
    void execute(const std::string& line);

    void addLine(const std::string& line);
    const std::string& input() const { return m_input; }

    void build(DrawList& dl, int screenW, int screenH, UiInput* input);

    /// Non-character keys (virtual-key codes). True when handled.
    bool handleKey(int key);
    /// WM_CHAR payload. False when the console is closed, so the key falls
    /// through to whoever else may want it.
    bool handleChar(int c);

private:
    static constexpr std::size_t kMaxLines = 80;
    static constexpr std::size_t kMaxCmdHistory = 32;

    bool cmdHelp(const std::vector<std::string>& args);
    bool cmdClear(const std::vector<std::string>& args);
    bool cmdScreenshot(const std::vector<std::string>& args);
    bool cmdPhotomode(const std::vector<std::string>& args);
    bool cmdPpfilter(const std::vector<std::string>& args);
    bool cmdList(const std::vector<std::string>& args);
    bool cmdVersion(const std::vector<std::string>& args);
    bool cmdDebug(const std::vector<std::string>& args);
    bool cmdCockpitEditor(const std::vector<std::string>& args);
    bool cmdTheme(const std::vector<std::string>& args);

    void drawHistory(DrawList& dl, float x, float y, float w, float h);
    void drawInputLine(DrawList& dl, float x, float y, float w);

    std::deque<std::string> m_lines;
    std::deque<std::string> m_cmdHistory;
    std::size_t m_historyPos = 0; // command history: 0 = newest
    std::unordered_map<std::string, CommandHandler> m_commands;
    std::string m_input;
    bool m_visible = false;
    bool m_typing = false;
    unsigned m_ticks = 0; // caret phase, no frame timer needed
    TextRenderer m_text;
};

} // namespace ui
} // namespace sim
} // namespace ks
