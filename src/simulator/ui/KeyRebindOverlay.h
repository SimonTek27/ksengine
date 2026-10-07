#pragma once
/**
 * Keyboard rebind panel (roadmap 1.4 / GAP P2.7). Qt-free, follows the
 * DeviceSettingsOverlay row-list pattern.
 *
 * ENTER (or a row click) enters capture mode; the next non-modifier key
 * becomes the new binding. The captured key is *swapped* with whatever
 * already owned it, so two actions can never end up on the same key. The
 * arrow keys are reserved as the fixed throttle/brake/steer alternates and
 * cannot be bound.
 */
#include "NativeUiTypes.h"
#include "UiInput.h"
#include "../InputManager.h"
#include <functional>
#include <string>

namespace ks {
namespace sim {
namespace ui {

class KeyRebindOverlay {
public:
    static constexpr int kActions = 7;

    void setVisible(bool v) { m_visible = v; if (!v) m_capturing = false; }
    bool isVisible() const { return m_visible; }
    void toggle() { setVisible(!m_visible); }

    void setMapping(const KeyboardMapping& m) { m_map = m; }
    const KeyboardMapping& mapping() const { return m_map; }

    static const char* actionLabel(int i) {
        switch (i) {
        case 0: return "Throttle";
        case 1: return "Brake";
        case 2: return "Steer left";
        case 3: return "Steer right";
        case 4: return "Shift up";
        case 5: return "Shift down";
        default: return "Handbrake";
        }
    }

    void build(DrawList& dl, int screenW, int screenH, UiInput* input = nullptr) {
        if (!m_visible) return;
        const float panelW = 460.f;
        const float rowH = 44.f;
        const float panelH = 60.f + kActions * rowH + 64.f;
        const float x = (screenW - panelW) * 0.5f;
        const float y = (screenH - panelH) * 0.5f;

        dl.addRectFilled({0, 0, (float)screenW, (float)screenH}, Color::rgba(0, 0, 0, 0.45f));
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0.08f, 0.09f, 0.12f, 0.95f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(80, 160, 255), 2.f);
        dl.addText(x + 16, y + 16, "Controls - Keyboard", Color::rgb(220, 230, 255), 1.4f);

        float rowY = y + 56;
        for (int i = 0; i < kActions; ++i) {
            Rect rr{x + 12, rowY, panelW - 24, 40};
            bool hover = input && input->isHovering(rr);
            if (input && input->state().leftReleased() &&
                rr.contains(input->state().x, input->state().y)) {
                m_selected = i;
                if (!m_capturing) m_capturing = true;
            }

            Color bg = (i == m_selected)
                ? Color::rgba(0.15f, 0.25f, 0.4f, 1.f)
                : (hover ? Color::rgba(0.14f, 0.16f, 0.22f, 1.f)
                         : Color::rgba(0.12f, 0.13f, 0.16f, 1.f));
            dl.addRectFilled(rr, bg);
            if (hover) dl.addRect(rr, Color::rgb(100, 180, 255), 1.f);

            dl.addText(rr.x + 10, rr.y + 11, actionLabel(i), Color::rgb(230, 230, 230), 1.1f);

            const bool capturing = m_capturing && i == m_selected;
            const std::string value = capturing ? "PRESS KEY..." : bindingText(i);
            const Color valueColor = capturing
                ? Color::rgb(255, 210, 90)
                : Color::rgb(160, 200, 255);
            dl.addText(rr.x + 250, rr.y + 11, value, valueColor, 1.1f);
            rowY += rowH;
        }

        // Close button
        Rect closeBtn{x + panelW - 100, y + panelH - 40, 80, 28};
        bool closeHover = input && input->isHovering(closeBtn);
        dl.addRectFilled(closeBtn, closeHover ? Color::rgba(0.3f, 0.15f, 0.15f, 1.f)
                                              : Color::rgba(0.2f, 0.12f, 0.12f, 1.f));
        dl.addText(closeBtn.x + 18, closeBtn.y + 6, "Close", Color::rgb(230, 200, 200), 1.f);
        if (input && input->state().leftReleased() &&
            closeBtn.contains(input->state().x, input->state().y))
            setVisible(false);

        dl.addText(x + 16, y + panelH - 28, "Enter: rebind (next key)   Esc: close",
                   Color::rgb(140, 150, 160), 0.9f);
    }

    bool handleKey(int key) {
        if (!m_visible) return false;

        if (m_capturing) {
            if (key == 27) { m_capturing = false; return true; } // cancel capture
            if (key == 13) return true;                          // ignore Enter auto-repeat
            if (isModifier(key)) return true;                    // keep waiting
            // Arrows are reserved as the fixed alternates.
            if (key >= 0x25 && key <= 0x28) return true;
            applyKey(key);
            m_capturing = false;
            if (onMappingChanged) onMappingChanged(m_map);
            return true;
        }

        if (key == 27) { setVisible(false); return true; }
        if (key == 38) { if (m_selected > 0) --m_selected; return true; }
        if (key == 40) { if (m_selected + 1 < kActions) ++m_selected; return true; }
        if (key == 13) { m_capturing = true; return true; }
        return true; // modal: swallow everything else while open
    }

    /** Returns true if event was consumed (modal). */
    bool handleMouse(const MouseEvent& e) {
        if (!m_visible) return false;
        (void)e;
        return true;
    }

    std::function<void(const KeyboardMapping&)> onMappingChanged;

private:
    static bool isModifier(int k) {
        return k == 0x10 || k == 0x11 || k == 0x12 ||      // shift/ctrl/alt
               k == 0x5B || k == 0x5C ||                   // win keys
               (k >= 0x01 && k <= 0x07);                   // mouse buttons
    }

    int* slot(int i) {
        switch (i) {
        case 0: return &m_map.throttle;
        case 1: return &m_map.brake;
        case 2: return &m_map.steerLeft;
        case 3: return &m_map.steerRight;
        case 4: return &m_map.shiftUp;
        case 5: return &m_map.shiftDown;
        default: return &m_map.handbrake;
        }
    }

    std::string bindingText(int i) const {
        switch (i) {
        case 0: return keyName(m_map.throttle) + " / " + keyName(KeyboardMapping::AltThrottle);
        case 1: return keyName(m_map.brake) + " / " + keyName(KeyboardMapping::AltBrake);
        case 2: return keyName(m_map.steerLeft) + " / " + keyName(KeyboardMapping::AltSteerLeft);
        case 3: return keyName(m_map.steerRight) + " / " + keyName(KeyboardMapping::AltSteerRight);
        case 4: return keyName(m_map.shiftUp);
        case 5: return keyName(m_map.shiftDown);
        default: return keyName(m_map.handbrake);
        }
    }

    void applyKey(int key) {
        int* mine = slot(m_selected);
        if (*mine == key) return;
        // Swap: whoever else owns the captured key receives this action's old
        // key, keeping the mapping a bijection (no action can ever trigger
        // two inputs).
        for (int i = 0; i < kActions; ++i) {
            if (i != m_selected && *slot(i) == key) {
                *slot(i) = *mine;
                break;
            }
        }
        *mine = key;
    }

    bool m_visible = false;
    bool m_capturing = false;
    int m_selected = 0;
    KeyboardMapping m_map;
};

} // namespace ui
} // namespace sim
} // namespace ks
