#pragma once
/**
 * Controller settings panel (rFactor Controller.ini shape), Qt-free.
 *
 * Two knobs the input path really has: the steering deadzone and steering
 * inversion. Edits land in the ControllerSettings document immediately, so the
 * caller can apply them to the live mapping and persist them in the player's
 * settings file — the panel itself owns no I/O.
 */
#include "NativeUiTypes.h"
#include "UiInput.h"
#include "engine/Config/ControllerSettings.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace ks {
namespace sim {
namespace ui {

class ControllerSettingsOverlay {
public:
    static constexpr int kRows = 2;
    static constexpr double kDeadZoneMin = 0.0;
    static constexpr double kDeadZoneMax = 0.30;
    static constexpr double kDeadZoneStep = 0.01;

    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }
    void toggle() { setVisible(!m_visible); }

    /** Seed from the loaded document plus the live mapping, which is what the
     *  panel shows when a key is absent: the two never disagree by accident. */
    void setSettings(const ks::engine::config::ControllerSettings& s,
                     double deadZoneFallback, bool invertFallback) {
        m_settings = s;
        m_fallbackDeadZone = clampDeadZone(deadZoneFallback);
        m_fallbackInvert = invertFallback;
        m_selected = 0;
    }
    const ks::engine::config::ControllerSettings& settings() const { return m_settings; }

    static double clampDeadZone(double v) {
        if (!(v >= kDeadZoneMin)) return kDeadZoneMin;
        if (v > kDeadZoneMax) return kDeadZoneMax;
        return v;
    }

    static double stepDeadZone(double v, int direction) {
        const double delta = direction > 0 ? kDeadZoneStep : -kDeadZoneStep;
        return clampDeadZone(std::round((clampDeadZone(v) + delta) * 100.0) / 100.0);
    }

    double deadZone() const {
        const ks::engine::json::Value* steer = steerObject();
        if (steer) {
            const double v = steer->numberAt("DEADZONE", -1.0);
            if (v >= 0.0) return clampDeadZone(v);
        }
        return m_fallbackDeadZone;
    }

    bool invertSteer() const {
        const ks::engine::json::Value* steer = steerObject();
        if (steer) {
            if (const ks::engine::json::Value* v = steer->find("INVERT");
                v && v->isBool()) {
                return v->asBool();
            }
        }
        return m_fallbackInvert;
    }

    void select(int row) {
        if (row >= 0 && row < kRows) m_selected = row;
    }
    int selected() const { return m_selected; }

    void setDeadZone(double v) {
        setSteerValue("DEADZONE",
                      ks::engine::json::Value::number(clampDeadZone(v)));
        notify();
    }
    void setInvert(bool v) {
        setSteerValue("INVERT", ks::engine::json::Value::boolean(v));
        notify();
    }

    /** One step on the selected row: the deadzone moves by kDeadZoneStep,
     *  invert flips. The setters report the change through onChanged. */
    void adjustSelected(int direction) {
        if (direction == 0) return;
        if (m_selected == 0) setDeadZone(stepDeadZone(deadZone(), direction));
        else setInvert(!invertSteer());
    }

    void build(DrawList& dl, int screenW, int screenH, UiInput* input = nullptr) {
        if (!m_visible) return;
        const float panelW = 460.f;
        const float rowH = 44.f;
        const float panelH = 60.f + kRows * rowH + 64.f;
        const float x = (screenW - panelW) * 0.5f;
        const float y = (screenH - panelH) * 0.5f;

        dl.addRectFilled({0, 0, (float)screenW, (float)screenH}, Color::rgba(0, 0, 0, 0.45f));
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0.08f, 0.09f, 0.12f, 0.95f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(80, 160, 255), 2.f);
        dl.addText(x + 16, y + 16, "Controls - Controller", Color::rgb(220, 230, 255), 1.4f);

        char value[32];
        float rowY = y + 56;
        for (int i = 0; i < kRows; ++i) {
            Rect rr{x + 12, rowY, panelW - 24, 40};
            const bool hover = input && input->isHovering(rr);
            if (input && input->state().leftReleased() &&
                rr.contains(input->state().x, input->state().y)) {
                m_selected = i;
                adjustSelected(+1);
            }

            const Color bg = (i == m_selected)
                ? Color::rgba(0.15f, 0.25f, 0.4f, 1.f)
                : (hover ? Color::rgba(0.14f, 0.16f, 0.22f, 1.f)
                         : Color::rgba(0.12f, 0.13f, 0.16f, 1.f));
            dl.addRectFilled(rr, bg);
            if (hover) dl.addRect(rr, Color::rgb(100, 180, 255), 1.f);

            const char* label = i == 0 ? "Steer deadzone" : "Invert steer";
            dl.addText(rr.x + 10, rr.y + 11, label, Color::rgb(230, 230, 230), 1.1f);

            if (i == 0) std::snprintf(value, sizeof(value), "%.2f", deadZone());
            else std::snprintf(value, sizeof(value), "%s", invertSteer() ? "ON" : "OFF");
            dl.addText(rr.x + 300, rr.y + 11, value, Color::rgb(160, 200, 255), 1.1f);
            rowY += rowH;
        }

        Rect closeBtn{x + panelW - 100, y + panelH - 40, 80, 28};
        const bool closeHover = input && input->isHovering(closeBtn);
        dl.addRectFilled(closeBtn, closeHover ? Color::rgba(0.3f, 0.15f, 0.15f, 1.f)
                                              : Color::rgba(0.2f, 0.12f, 0.12f, 1.f));
        dl.addText(closeBtn.x + 18, closeBtn.y + 6, "Close", Color::rgb(230, 200, 200), 1.f);
        if (input && input->state().leftReleased() &&
            closeBtn.contains(input->state().x, input->state().y))
            setVisible(false);

        dl.addText(x + 16, y + panelH - 28, "Up/Down: row   Left/Right: change   Esc: close",
                   Color::rgb(140, 150, 160), 0.9f);
    }

    bool handleKey(int key) {
        if (!m_visible) return false;
        if (key == 27) { setVisible(false); return true; }
        if (key == 38) { if (m_selected > 0) --m_selected; return true; }
        if (key == 40) { if (m_selected + 1 < kRows) ++m_selected; return true; }
        if (key == 37) { adjustSelected(-1); return true; }
        if (key == 39) { adjustSelected(+1); return true; }
        if (key == 13 || key == 32) { adjustSelected(+1); return true; }
        return true; // modal: swallow everything else while open
    }

    /** Returns true if the event was consumed (modal). */
    bool handleMouse(const MouseEvent& e) {
        if (!m_visible) return false;
        (void)e;
        return true;
    }

    /** Called after every edit with the document the caller should apply and
     *  persist. */
    std::function<void(const ks::engine::config::ControllerSettings&)> onChanged;

private:
    void notify() {
        if (onChanged) onChanged(m_settings);
    }

    const ks::engine::json::Value* steerObject() const {
        const ks::engine::json::Value* steer = m_settings.root.find("STEER");
        return (steer && steer->isObject()) ? steer : nullptr;
    }

    void setSteerValue(const std::string& key, const ks::engine::json::Value& value) {
        namespace json = ks::engine::json;
        json::Value root = m_settings.root;
        json::Value steer = json::Value::object();
        if (const json::Value* existing = root.find("STEER");
            existing && existing->isObject()) {
            steer = *existing;
        }
        steer.set(key, value);
        root.set("STEER", std::move(steer));
        m_settings.root = std::move(root);
    }

    bool m_visible = false;
    int m_selected = 0;
    double m_fallbackDeadZone = 0.12;
    bool m_fallbackInvert = false;
    ks::engine::config::ControllerSettings m_settings;
};

} // namespace ui
} // namespace sim
} // namespace ks
