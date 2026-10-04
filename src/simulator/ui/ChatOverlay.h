#pragma once
/**
 * In-sim chat (roadmap 3.1): a rolling message log plus a one-line
 * composer. Text arrives through WM_CHAR (SimulationLoop::handleUiChar),
 * so the full ASCII range including punctuation is available - addresses
 * and messages need ':' and '.'.
 *
 * Enter opens the composer, Enter sends (onSend), Esc closes it. While the
 * composer is open the hub swallows every other key so nothing underneath
 * (menu, server browser) reacts to what is being typed.
 */
#include "NativeUiTypes.h"
#include "UiInput.h"
#include <deque>
#include <string>
#include <functional>

namespace ks {
namespace sim {
namespace ui {

class ChatOverlay {
public:
    void setVisible(bool v) {
        m_visible = v;
        if (!v) m_typing = false;
    }
    bool isVisible() const { return m_visible; }
    bool isTyping() const { return m_typing; }
    void startTyping() { m_typing = true; }
    void stopTyping() { m_typing = false; }

    void setStatus(const std::string& s) { m_status = s; }

    void addLine(const std::string& line) {
        m_lines.push_back(line);
        while (m_lines.size() > kMaxLines) m_lines.pop_front();
    }

    const std::string& input() const { return m_input; }

    void build(DrawList& dl, int screenW, int screenH, UiInput* /*input*/) {
        if (!m_visible) return;

        const float w = 420.f;
        const float linesShown = 5.f;
        const float lineH = 16.f;
        const float headH = 20.f;
        const float pad = 10.f;
        const float h = pad * 2.f + headH + linesShown * lineH +
                        (m_typing ? lineH + 6.f : 0.f);
        const float x = 24.f;
        const float y = static_cast<float>(screenH) - h - 24.f;
        (void)screenW;

        dl.addRectFilled({x, y, w, h}, Color::rgba(0.05f, 0.06f, 0.09f, 0.88f));
        dl.addRect({x, y, w, h}, Color::rgb(70, 130, 200), 1.f);
        dl.addText(x + pad, y + pad, "CHAT", Color::rgb(150, 200, 255), 1.0f);
        if (!m_status.empty())
            dl.addText(x + pad + 46.f, y + pad, m_status, Color::rgb(140, 155, 170), 0.9f);

        // Newest at the bottom, walking backwards so the window sticks to
        // the composer instead of jumping around as lines age out.
        float ly = y + pad + headH;
        const size_t count = m_lines.size() < static_cast<size_t>(linesShown)
                                 ? m_lines.size()
                                 : static_cast<size_t>(linesShown);
        size_t first = m_lines.size() - count;
        for (size_t i = 0; i < count; ++i, ++first) {
            dl.addText(x + pad, ly, m_lines[first], Color::rgb(215, 225, 235), 0.95f);
            ly += lineH;
        }

        if (m_typing) {
            ly += 6.f;
            dl.addRectFilled({x + pad, ly - 2.f, w - pad * 2.f, lineH + 2.f},
                             Color::rgba(0.10f, 0.13f, 0.18f, 1.f));
            std::string shown = "> " + m_input;
            // Steady caret: a phase derived from the message length so it
            // does not need a frame timer inside the overlay.
            if ((m_ticks / 30) % 2 == 0) shown.push_back('_');
            dl.addText(x + pad + 4.f, ly, shown, Color::rgb(235, 240, 245), 0.95f);
        }
        ++m_ticks;
    }

    /** Non-character keys (virtual-key codes). */
    bool handleKey(int key) {
        if (m_typing) {
            if (key == 27) { m_typing = false; return true; }
            return true; // swallow: arrow keys must not move the selection
        }
        if (!m_visible) return false;
        if (key == 13) { m_typing = true; m_input.clear(); return true; }
        if (key == 27) { m_visible = false; return true; }
        return false;
    }

    /** WM_CHAR payload. Returns false when the composer is closed so the
     *  key falls through to whoever else may want it. */
    bool handleChar(int c) {
        if (!m_typing) return false;
        if (c == 13) { // Enter -> send
            if (!m_input.empty() && onSend) onSend(m_input);
            m_input.clear();
            m_typing = false;
            return true;
        }
        if (c == 8) { // Backspace
            if (!m_input.empty()) m_input.pop_back();
            return true;
        }
        if (c == 27) { m_typing = false; return true; }
        if (c >= 32 && c < 127 && m_input.size() < 160) {
            m_input.push_back(static_cast<char>(c));
            return true;
        }
        return true;
    }

    std::function<void(const std::string&)> onSend;

private:
    static constexpr size_t kMaxLines = 80;
    bool m_visible = false;
    bool m_typing = false;
    unsigned m_ticks = 0;
    std::string m_input;
    std::string m_status;
    std::deque<std::string> m_lines;
};

} // namespace ui
} // namespace sim
} // namespace ks
