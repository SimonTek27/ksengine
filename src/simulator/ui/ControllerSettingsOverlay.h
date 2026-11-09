#pragma once
/**
 * Controller settings panel (rFactor Controller.ini shape), Qt-free.
 *
 * Per axis — steering, throttle, brake — a deadzone and a sensitivity, plus
 * steering inversion. Edits land in the ControllerSettings document
 * immediately, so the caller can apply them to the live mapping and persist
 * them in the player's settings file; the panel owns no I/O.
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
    // 0-5: deadzone/sensitivity per axis; 6: steering inversion.
    static constexpr int kRows = 7;

    /** What the panel shows for a key the document does not carry yet: the
     *  live mapping, so the panel and the car can never disagree. */
    struct Fallbacks {
        double steerDeadZone = 0.12;
        double steerSensitivity = 1.0;
        double throttleDeadZone = 0.12;
        double throttleSensitivity = 1.0;
        double brakeDeadZone = 0.12;
        double brakeSensitivity = 1.0;
        bool invertSteer = false;
    };

    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }
    void toggle() { setVisible(!m_visible); }

    void setSettings(const ks::engine::config::ControllerSettings& s,
                     const Fallbacks& fallbacks) {
        m_settings = s;
        m_fallback[0] = fallbacks.steerDeadZone;
        m_fallback[1] = fallbacks.steerSensitivity;
        m_fallback[2] = fallbacks.throttleDeadZone;
        m_fallback[3] = fallbacks.throttleSensitivity;
        m_fallback[4] = fallbacks.brakeDeadZone;
        m_fallback[5] = fallbacks.brakeSensitivity;
        m_fallbackInvert = fallbacks.invertSteer;
        m_selected = 0;
    }
    const ks::engine::config::ControllerSettings& settings() const { return m_settings; }

    static const char* labelFor(int row) {
        switch (row) {
        case 0: return "Steer deadzone";
        case 1: return "Steer sensitivity";
        case 2: return "Throttle deadzone";
        case 3: return "Throttle sensitivity";
        case 4: return "Brake deadzone";
        case 5: return "Brake sensitivity";
        default: return "Invert steer";
        }
    }
    static const char* sectionFor(int row) {
        return row < 2 ? "STEER" : (row < 4 ? "THROTTLE" : "BRAKE");
    }
    static const char* keyFor(int row) {
        return (row % 2 == 0) ? "DEADZONE" : "SENSITIVITY";
    }
    static double minValue(int row) { return (row % 2 == 0) ? 0.0 : 0.50; }
    static double maxValue(int row) { return (row % 2 == 0) ? 0.30 : 2.00; }
    static double stepValue(int row) { return (row % 2 == 0) ? 0.01 : 0.05; }

    static double snap(double v, double low, double high) {
        if (!(v >= low)) return low;
        if (v > high) return high;
        return std::round(v * 100.0) / 100.0;
    }

    /** Current value of a row: the document's, or the fallback when absent. */
    double value(int row) const {
        if (row < 0 || row >= kRows) return 0.0;
        if (row == 6) return invertSteer() ? 1.0 : 0.0;
        const ks::engine::json::Value* section = m_settings.root.find(sectionFor(row));
        if (section && section->isObject()) {
            const double v = section->numberAt(keyFor(row), -1.0);
            if (v >= minValue(row)) return snap(v, minValue(row), maxValue(row));
        }
        return m_fallback[row];
    }

    bool invertSteer() const {
        const ks::engine::json::Value* steer = m_settings.root.find("STEER");
        if (steer && steer->isObject()) {
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

    void setValue(int row, double v) {
        if (row < 0 || row >= 6) return;
        writeValue(sectionFor(row), keyFor(row),
                   ks::engine::json::Value::number(snap(v, minValue(row), maxValue(row))));
        notify();
    }

    void setInvert(bool v) {
        writeValue("STEER", "INVERT", ks::engine::json::Value::boolean(v));
        notify();
    }

    /** One step on the selected row: a numeric row moves by its own step and
     *  is clamped, invert flips. The setters report through onChanged. */
    void adjustSelected(int direction) {
        if (direction == 0) return;
        if (m_selected == 6) { setInvert(!invertSteer()); return; }
        const double delta = direction > 0 ? stepValue(m_selected) : -stepValue(m_selected);
        setValue(m_selected, value(m_selected) + delta);
    }

    void build(DrawList& dl, int screenW, int screenH, UiInput* input = nullptr) {
        if (!m_visible) return;
        const float panelW = 480.f;
        const float rowH = 40.f;
        const float panelH = 60.f + kRows * rowH + 64.f;
        const float x = (screenW - panelW) * 0.5f;
        const float y = (screenH - panelH) * 0.5f;

        dl.addRectFilled({0, 0, (float)screenW, (float)screenH}, Color::rgba(0, 0, 0, 0.45f));
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0.08f, 0.09f, 0.12f, 0.95f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(80, 160, 255), 2.f);
        dl.addText(x + 16, y + 16, "Controls - Controller", Color::rgb(220, 230, 255), 1.4f);

        char text[32];
        float rowY = y + 54;
        for (int i = 0; i < kRows; ++i) {
            Rect rr{x + 12, rowY, panelW - 24, 36};
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

            dl.addText(rr.x + 10, rr.y + 9, labelFor(i), Color::rgb(230, 230, 230), 1.05f);

            if (i == 6) std::snprintf(text, sizeof(text), "%s", invertSteer() ? "ON" : "OFF");
            else std::snprintf(text, sizeof(text), "%.2f", value(i));
            dl.addText(rr.x + 310, rr.y + 9, text, Color::rgb(160, 200, 255), 1.05f);
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

    void writeValue(const std::string& section, const std::string& key,
                    const ks::engine::json::Value& value) {
        namespace json = ks::engine::json;
        json::Value root = m_settings.root;
        json::Value group = json::Value::object();
        if (const json::Value* existing = root.find(section);
            existing && existing->isObject()) {
            group = *existing;
        }
        group.set(key, value);
        root.set(section, std::move(group));
        m_settings.root = std::move(root);
    }

    bool m_visible = false;
    int m_selected = 0;
    double m_fallback[6] = {0.12, 1.0, 0.12, 1.0, 0.12, 1.0};
    bool m_fallbackInvert = false;
    ks::engine::config::ControllerSettings m_settings;
};

} // namespace ui
} // namespace sim
} // namespace ks
