#pragma once
/** Native multiplayer panel (no QWidget). */
#include "NativeUiTypes.h"
#include "UiInput.h"
#include <string>
#include <vector>
#include <functional>

namespace ks {
namespace sim {
namespace ui {

struct MpServerRow {
    std::string name;
    std::string address;
    int players = 0;
    int maxPlayers = 0;
    int pingMs = 0;
};

class MultiplayerOverlay {
public:
    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }
    void toggle() { m_visible = !m_visible; }

    void setServers(std::vector<MpServerRow> rows) { m_servers = std::move(rows); }
    void setStatus(const std::string& s) { m_status = s; }
    const std::string& status() const { return m_status; }
    void setSelected(int i) {
        if (i >= 0 && i < static_cast<int>(m_servers.size())) m_selected = i;
    }

    void build(DrawList& dl, int screenW, int screenH, UiInput* input = nullptr) {
        if (!m_visible) return;
        const float panelW = 480.f;
        const float panelH = 400.f;
        const float x = (screenW - panelW) * 0.5f;
        const float y = (screenH - panelH) * 0.5f;

        dl.addRectFilled({0, 0, (float)screenW, (float)screenH}, Color::rgba(0, 0, 0, 0.45f));
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0.07f, 0.08f, 0.11f, 0.96f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(100, 200, 140), 2.f);
        dl.addText(x + 16, y + 16, "Multiplayer", Color::rgb(200, 255, 220), 1.4f);
        dl.addText(x + 16, y + 36, m_status.empty() ? "Offline" : m_status,
                   Color::rgb(150, 175, 200), 0.9f);

        float rowY = y + 56;
        if (m_servers.empty()) {
            dl.addText(x + 16, rowY + 6, "No servers yet - host one from the menu",
                       Color::rgb(130, 145, 155), 0.95f);
            rowY += 50;
        }
        for (int i = 0; i < static_cast<int>(m_servers.size()); ++i) {
            const auto& s = m_servers[static_cast<size_t>(i)];
            Rect rr{x + 12, rowY, panelW - 24, 44};
            bool hover = input && input->isHovering(rr);
            if (input && input->state().leftReleased() && rr.contains(input->state().x, input->state().y)) {
                m_selected = i;
                if (onJoin)
                    onJoin(s.address);
            }
            Color bg = (i == m_selected)
                ? Color::rgba(0.12f, 0.28f, 0.18f, 1.f)
                : (hover ? Color::rgba(0.12f, 0.18f, 0.14f, 1.f)
                         : Color::rgba(0.1f, 0.12f, 0.14f, 1.f));
            dl.addRectFilled(rr, bg);
            dl.addText(rr.x + 10, rr.y + 6, s.name, Color::rgb(230, 240, 230), 1.1f);
            std::string sub = s.address + "  " + std::to_string(s.players) + "/" +
                              std::to_string(s.maxPlayers) + "  " + std::to_string(s.pingMs) + "ms";
            dl.addText(rr.x + 10, rr.y + 24, sub, Color::rgb(150, 170, 160), 0.9f);
            rowY += 50;
        }

        Rect closeBtn{x + panelW - 100, y + panelH - 40, 80, 28};
        bool ch = input && input->isHovering(closeBtn);
        dl.addRectFilled(closeBtn, ch ? Color::rgba(0.3f, 0.15f, 0.15f, 1.f)
                                      : Color::rgba(0.2f, 0.12f, 0.12f, 1.f));
        dl.addText(closeBtn.x + 18, closeBtn.y + 6, "Close", Color::rgb(230, 200, 200), 1.f);
        if (input && input->state().leftReleased() && closeBtn.contains(input->state().x, input->state().y))
            m_visible = false;

        dl.addText(x + 16, y + panelH - 28, "Click to join  Esc close",
                   Color::rgb(140, 150, 160), 0.9f);
    }

    bool handleKey(int key) {
        if (!m_visible) return false;
        if (key == 27) { m_visible = false; return true; }
        if (key == 38 && m_selected > 0) { --m_selected; return true; }
        if (key == 40 && m_selected + 1 < static_cast<int>(m_servers.size())) {
            ++m_selected; return true;
        }
        if (key == 13 && m_selected >= 0 && m_selected < static_cast<int>(m_servers.size())) {
            if (onJoin) onJoin(m_servers[static_cast<size_t>(m_selected)].address);
            return true;
        }
        return false;
    }

    bool handleMouse(const MouseEvent& e) {
        if (!m_visible) return false;
        (void)e;
        return true;
    }

    std::function<void(const std::string& address)> onJoin;

private:
    bool m_visible = false;
    int m_selected = 0;
    std::string m_status;
    std::vector<MpServerRow> m_servers;
};

} // namespace ui
} // namespace sim
} // namespace ks
