#pragma once
/** Native profile picker (no QWidget). */
#include "NativeUiTypes.h"
#include "UiInput.h"
#include <string>
#include <vector>
#include <functional>

namespace ks {
namespace sim {
namespace ui {

class ProfileSettingsOverlay {
public:
    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }
    void toggle() { m_visible = !m_visible; }

    void setProfiles(std::vector<std::string> names) { m_names = std::move(names); m_selected = 0; }
    void setSelected(int i) {
        if (i >= 0 && i < static_cast<int>(m_names.size())) m_selected = i;
    }
    int selected() const { return m_selected; }
    const std::string& selectedName() const {
        static const std::string empty;
        if (m_selected < 0 || m_selected >= static_cast<int>(m_names.size())) return empty;
        return m_names[static_cast<size_t>(m_selected)];
    }

    std::function<void(const std::string& name)> onLoad;
    std::function<void()> onNew;
    std::function<void(const std::string& name)> onDelete;

    void build(DrawList& dl, int screenW, int screenH, UiInput* input = nullptr) {
        if (!m_visible) return;
        const float panelW = 400.f;
        const float panelH = 360.f;
        const float x = (screenW - panelW) * 0.5f;
        const float y = (screenH - panelH) * 0.5f;

        dl.addRectFilled({0, 0, (float)screenW, (float)screenH}, Color::rgba(0, 0, 0, 0.45f));
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0.08f, 0.09f, 0.12f, 0.95f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(80, 160, 255), 2.f);
        dl.addText(x + 16, y + 16, "Input profiles", Color::rgb(220, 230, 255), 1.4f);

        float rowY = y + 52;
        for (int i = 0; i < static_cast<int>(m_names.size()); ++i) {
            Rect rr{x + 12, rowY, panelW - 24, 26};
            bool hover = input && input->isHovering(rr);
            if (input && input->state().leftReleased() && rr.contains(input->state().x, input->state().y)) {
                m_selected = i;
            }
            Color bg = (i == m_selected) ? Color::rgba(0.15f, 0.25f, 0.4f, 1.f)
                                         : (hover ? Color::rgba(0.14f, 0.16f, 0.22f, 1.f)
                                                  : Color::rgba(0.12f, 0.13f, 0.16f, 1.f));
            dl.addRectFilled(rr, bg);
            dl.addText(rr.x + 10, rr.y + 6, m_names[static_cast<size_t>(i)],
                       Color::rgb(230, 230, 230), 1.0f);
            rowY += 30;
        }
        if (m_names.empty()) {
            dl.addText(x + 16, rowY, "(nessun profilo salvato)", Color::rgb(150, 160, 170), 1.0f);
            rowY += 30;
        }

        // Buttons
        float btnY = y + panelH - 46;
        const char* labels[] = {"Load (Enter)", "New (N)", "Delete (Del)", "Close (Esc)"};
        for (int b = 0; b < 4; ++b) {
            Rect br{x + 12 + b * 96.f, btnY, 90, 28};
            bool hover = input && input->isHovering(br);
            dl.addRectFilled(br, hover ? Color::rgba(0.18f, 0.22f, 0.3f, 1.f)
                                       : Color::rgba(0.12f, 0.14f, 0.18f, 1.f));
            dl.addText(br.x + 6, br.y + 7, labels[b], Color::rgb(200, 220, 255), 0.85f);
            if (input && input->state().leftReleased() && br.contains(input->state().x, input->state().y)) {
                if (b == 0 && onLoad && m_selected >= 0 && m_selected < static_cast<int>(m_names.size()))
                    onLoad(m_names[static_cast<size_t>(m_selected)]);
                else if (b == 1 && onNew) onNew();
                else if (b == 2 && onDelete && m_selected >= 0 && m_selected < static_cast<int>(m_names.size()))
                    onDelete(m_names[static_cast<size_t>(m_selected)]);
                else if (b == 3) m_visible = false;
            }
        }
        dl.addText(x + 16, y + panelH - 16, "Up/Down, Enter, N, Del, Esc",
                   Color::rgb(140, 150, 160), 0.85f);
    }

    bool handleKey(int key) {
        if (!m_visible) return false;
        if (key == 27) { m_visible = false; return true; }
        if (key == 38) { if (m_selected > 0) --m_selected; return true; }
        if (key == 40) { if (m_selected + 1 < static_cast<int>(m_names.size())) ++m_selected; return true; }
        if (key == 13) {
            if (onLoad && m_selected >= 0 && m_selected < static_cast<int>(m_names.size()))
                onLoad(m_names[static_cast<size_t>(m_selected)]);
            return true;
        }
        if (key == 'N' || key == 'n') { if (onNew) onNew(); return true; }
        if (key == 46) { // Del
            if (onDelete && m_selected >= 0 && m_selected < static_cast<int>(m_names.size()))
                onDelete(m_names[static_cast<size_t>(m_selected)]);
            return true;
        }
        return true;
    }

    bool handleMouse(const MouseEvent& e) {
        if (!m_visible) return false;
        (void)e;
        return true;
    }

private:
    bool m_visible = false;
    int m_selected = 0;
    std::vector<std::string> m_names;
};

} // namespace ui
} // namespace sim
} // namespace ks