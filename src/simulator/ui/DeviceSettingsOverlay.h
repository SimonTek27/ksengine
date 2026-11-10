#pragma once
/** Native replacement for DeviceSettingsWidget (no QWidget). */
#include "NativeUiTypes.h"
#include "UiInput.h"
#include "simulator/InputManager.h"
#include <string>
#include <vector>
#include <functional>

namespace ks {
namespace sim {
namespace ui {

struct DeviceInfoRow {
    std::string name;
    std::string detail;
    bool connected = false;
};

class DeviceSettingsOverlay {
public:
    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }
    void toggle() { m_visible = !m_visible; }

    void setDevices(std::vector<DeviceInfoRow> rows) { m_rows = std::move(rows); }
    void setSelected(int index) {
        if (index >= 0 && index < static_cast<int>(m_rows.size()))
            m_selected = index;
    }
    int selected() const { return m_selected; }

    void setInputManager(InputManager* im) { m_im = im; }

    std::function<void(int index, const std::string& name)> onDeviceSelected;
    std::function<void(int axis)> onCalibrationBegin;
    std::function<void()> onCalibrationAdvance;
    std::function<void()> onCalibrationCancel;

    void build(DrawList& dl, int screenW, int screenH, UiInput* input = nullptr) {
        if (!m_visible) return;
        const float panelW = 460.f;
        const float panelH = 420.f;
        const float x = (screenW - panelW) * 0.5f;
        const float y = (screenH - panelH) * 0.5f;

        dl.addRectFilled({0, 0, (float)screenW, (float)screenH}, Color::rgba(0, 0, 0, 0.45f));
        dl.addRectFilled({x, y, panelW, panelH}, Color::rgba(0.08f, 0.09f, 0.12f, 0.95f));
        dl.addRect({x, y, panelW, panelH}, Color::rgb(80, 160, 255), 2.f);
        dl.addText(x + 16, y + 16, m_calibrating ? "Calibration wizard" : "Device Settings",
                   Color::rgb(220, 230, 255), 1.4f);

        if (m_calibrating && m_im) {
            // --- wizard view -------------------------------------------------
            const char* stepName = "Idle";
            switch (m_im->calibrationStep()) {
                case InputManager::CalibrationStep::CaptureMin:    stepName = "1/3  muovi l'asse al MINIMO"; break;
                case InputManager::CalibrationStep::CaptureCenter: stepName = "2/3  metti l'asse al CENTRO"; break;
                case InputManager::CalibrationStep::CaptureMax:    stepName = "3/3  muovi l'asse al MASSIMO"; break;
                case InputManager::CalibrationStep::Done:         stepName = "Fatto"; break;
                default: break;
            }
            dl.addText(x + 16, y + 52, std::string("Asse: ") +
                       axisName(m_im->calibratingAxis()), Color::rgb(200, 220, 255), 1.1f);
            dl.addText(x + 16, y + 74, stepName, Color::rgb(230, 230, 230), 1.1f);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "min visto: %.3f    max visto: %.3f",
                          m_im->calibrationMinSeen(), m_im->calibrationMaxSeen());
            dl.addText(x + 16, y + 98, buf, Color::rgb(180, 190, 200), 1.0f);
            if (m_im->calibrationHasCenter()) {
                std::snprintf(buf, sizeof(buf), "centro catturato: %.3f", m_im->calibrationCenterSeen());
                dl.addText(x + 16, y + 118, buf, Color::rgb(180, 190, 200), 1.0f);
            }
            dl.addText(x + 16, y + 148, "Premi 'Avanza' ad ogni passo, 'Annulla' per uscire.",
                       Color::rgb(150, 160, 170), 0.9f);

            // Advance button
            Rect adv{x + 16, y + panelH - 80, 150, 32};
            bool advHover = input && input->isHovering(adv);
            dl.addRectFilled(adv, advHover ? Color::rgba(0.15f, 0.3f, 0.2f, 1.f)
                                           : Color::rgba(0.12f, 0.22f, 0.16f, 1.f));
            dl.addText(adv.x + 24, adv.y + 8, "Avanza", Color::rgb(200, 240, 210), 1.1f);
            if (input && input->state().leftReleased() && adv.contains(input->state().x, input->state().y)) {
                if (onCalibrationAdvance) onCalibrationAdvance();
            }
            // Cancel button
            Rect canc{x + panelW - 166, y + panelH - 80, 150, 32};
            bool cancHover = input && input->isHovering(canc);
            dl.addRectFilled(canc, cancHover ? Color::rgba(0.3f, 0.15f, 0.15f, 1.f)
                                             : Color::rgba(0.2f, 0.12f, 0.12f, 1.f));
            dl.addText(canc.x + 24, canc.y + 8, "Annulla", Color::rgb(230, 200, 200), 1.1f);
            if (input && input->state().leftReleased() && canc.contains(input->state().x, input->state().y)) {
                if (onCalibrationCancel) onCalibrationCancel();
            }
        } else {
            // --- device list view -------------------------------------------
            float rowY = y + 56;
            for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
                const auto& row = m_rows[static_cast<size_t>(i)];
                Rect rr{x + 12, rowY, panelW - 24, 40};
                bool hover = input && input->isHovering(rr);
                if (input && input->state().leftReleased() && rr.contains(input->state().x, input->state().y)) {
                    m_selected = i;
                    if (onDeviceSelected) onDeviceSelected(i, row.name);
                }
                Color bg = (i == m_selected)
                    ? Color::rgba(0.15f, 0.25f, 0.4f, 1.f)
                    : (hover ? Color::rgba(0.14f, 0.16f, 0.22f, 1.f)
                             : Color::rgba(0.12f, 0.13f, 0.16f, 1.f));
                dl.addRectFilled(rr, bg);
                if (hover) dl.addRect(rr, Color::rgb(100, 180, 255), 1.f);
                std::string label = row.name + (row.connected ? "  [ON]" : "  [off]");
                dl.addText(rr.x + 10, rr.y + 8, label, Color::rgb(230, 230, 230), 1.1f);
                if (!row.detail.empty())
                    dl.addText(rr.x + 10, rr.y + 24, row.detail, Color::rgb(160, 170, 180), 0.9f);
                rowY += 46;
            }
            // Axis calibration entry points
            const char* axes[] = {"Sterzo", "Acceleratore", "Freno", "Frizione"};
            float axY = rowY + 8;
            for (int i = 0; i < 4; ++i) {
                Rect ar{x + 12, axY, panelW - 24, 28};
                bool hover = input && input->isHovering(ar);
                dl.addRectFilled(ar, hover ? Color::rgba(0.16f, 0.2f, 0.28f, 1.f)
                                           : Color::rgba(0.12f, 0.14f, 0.18f, 1.f));
                std::string lbl = std::string("Calibra: ") + axes[i];
                dl.addText(ar.x + 10, ar.y + 6, lbl, Color::rgb(200, 220, 255), 1.0f);
                if (input && input->state().leftReleased() && ar.contains(input->state().x, input->state().y)) {
                    if (onCalibrationBegin) onCalibrationBegin(i);
                }
                axY += 32;
            }
            // Close button
            Rect closeBtn{x + panelW - 100, y + panelH - 40, 80, 28};
            bool closeHover = input && input->isHovering(closeBtn);
            dl.addRectFilled(closeBtn, closeHover ? Color::rgba(0.3f, 0.15f, 0.15f, 1.f)
                                                  : Color::rgba(0.2f, 0.12f, 0.12f, 1.f));
            dl.addText(closeBtn.x + 18, closeBtn.y + 6, "Close", Color::rgb(230, 200, 200), 1.f);
            if (input && input->state().leftReleased() && closeBtn.contains(input->state().x, input->state().y))
                m_visible = false;
            dl.addText(x + 16, y + panelH - 28, "Click row / Up-Down / Esc",
                       Color::rgb(140, 150, 160), 0.9f);
        }

        // keep calibrating flag in sync with the InputManager state
        if (m_im) m_calibrating = m_im->calibrating();
    }

    bool handleKey(int key) {
        if (!m_visible) return false;
        if (m_calibrating) {
            if (key == 27) { // Esc
                if (onCalibrationCancel) onCalibrationCancel();
                return true;
            }
            if (key == 13 || key == 32) { // Enter / Space
                if (onCalibrationAdvance) onCalibrationAdvance();
                return true;
            }
            return true;
        }
        if (key == 27) { m_visible = false; return true; }
        if (key == 38) {
            if (m_selected > 0) --m_selected;
            return true;
        }
        if (key == 40) {
            if (m_selected + 1 < static_cast<int>(m_rows.size())) ++m_selected;
            return true;
        }
        if (key == 13) {
            if (onDeviceSelected && m_selected >= 0 && m_selected < static_cast<int>(m_rows.size()))
                onDeviceSelected(m_selected, m_rows[static_cast<size_t>(m_selected)].name);
            return true;
        }
        return false;
    }

    bool handleMouse(const MouseEvent& e) {
        if (!m_visible) return false;
        (void)e;
        return true;
    }

    InputManager* m_im = nullptr;
    bool m_calibrating = false;

private:
    const char* axisName(int i) const {
        static const char* names[] = {"Sterzo", "Acceleratore", "Freno", "Frizione"};
        if (i < 0 || i > 3) return "Asse";
        return names[i];
    }
    bool m_visible = false;
    int m_selected = 0;
    std::vector<DeviceInfoRow> m_rows;
};

} // namespace ui
} // namespace sim
} // namespace ks
