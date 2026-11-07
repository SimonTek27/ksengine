#include "SetupGarage.h"
#include "ui/NativeUiTypes.h"
#include "ui/TextRenderer.h"
#include "ui/UiInput.h"
#include <cstdio>

namespace ks {
namespace sim {

using ui::Color;
using ui::DrawList;
using ui::Rect;
using ui::TextStyle;
using ui::TextAlign;
using ui::TextRenderer;
using ui::UiInput;

SetupGarage::SetupGarage()
{
    rebuildRows();
}

void SetupGarage::setVisible(bool v)
{
    m_visible = v;
    if (v) {
        m_selectedRow = 0;
        rebuildRows();
    } else if (onClosed) {
        onClosed();
    }
}

const char* SetupGarage::tabName(GarageTab t)
{
    switch (t) {
    case GarageTab::Overview: return "OVERVIEW";
    case GarageTab::Tyres: return "TYRES";
    case GarageTab::Suspension: return "SUSPENSION";
    case GarageTab::Aero: return "AERO";
    case GarageTab::Brakes: return "BRAKES";
    case GarageTab::Gears: return "DIFF";
    default: return "";
    }
}

void SetupGarage::rebuildRows()
{
    m_rows.clear();
    auto addF = [&](const char* label, const char* unit, const char* hint,
                    float* v, float mn, float mx, float st) {
        GarageRow r;
        r.label = label; r.unit = unit; r.hint = hint;
        r.value = v; r.minV = mn; r.maxV = mx; r.step = st;
        m_rows.push_back(std::move(r));
    };
    auto addI = [&](const char* label, const char* hint, int* v, int mn, int mx) {
        GarageRow r;
        r.label = label; r.unit = ""; r.hint = hint;
        r.isInt = true; r.intValue = v; r.intMin = mn; r.intMax = mx;
        m_rows.push_back(std::move(r));
    };

    switch (m_tab) {
    case GarageTab::Overview:
        addF("FUEL", "L", "Session fuel load", &m_setup.fuel, 5.f, 120.f, 1.f);
        addF("BALLAST", "kg", "Additional mass", &m_setup.ballast, 0.f, 80.f, 1.f);
        // A car whose electronics say PRESENT = 0 has no such system, so there
        // is no level to offer: the row is simply not there, which is what AC
        // and Content Manager do. The stored level stays as it is, inert.
        if (m_tcAvailable)
            addI("TC", "Traction control level", &m_setup.tcLevel, 0, 10);
        if (m_absAvailable)
            addI("ABS", "Anti-lock level", &m_setup.absLevel, 0, 10);
        break;
    case GarageTab::Tyres:
        addF("PRESSURE FL", "bar", "Front left cold pressure", &m_setup.tirePressureFL, 1.4f, 3.2f, 0.05f);
        addF("PRESSURE FR", "bar", "Front right cold pressure", &m_setup.tirePressureFR, 1.4f, 3.2f, 0.05f);
        addF("PRESSURE RL", "bar", "Rear left cold pressure", &m_setup.tirePressureRL, 1.4f, 3.2f, 0.05f);
        addF("PRESSURE RR", "bar", "Rear right cold pressure", &m_setup.tirePressureRR, 1.4f, 3.2f, 0.05f);
        break;
    case GarageTab::Suspension:
        addF("RIDE HEIGHT F", "mm", "Front static ride height", &m_setup.rideHeightFront, 15.f, 80.f, 0.5f);
        addF("RIDE HEIGHT R", "mm", "Rear static ride height", &m_setup.rideHeightRear, 15.f, 80.f, 0.5f);
        addF("SPRING F", "N/mm", "Front spring rate", &m_setup.springRateFront, 60.f, 300.f, 2.f);
        addF("SPRING R", "N/mm", "Rear spring rate", &m_setup.springRateRear, 60.f, 300.f, 2.f);
        break;
    case GarageTab::Aero:
        addF("FRONT WING", "deg", "Front wing angle", &m_setup.frontWingAngle, 0.f, 30.f, 0.5f);
        addF("REAR WING", "deg", "Rear wing angle", &m_setup.rearWingAngle, 0.f, 35.f, 0.5f);
        break;
    case GarageTab::Brakes:
        addF("BRAKE BIAS", "%", "Front brake bias", &m_setup.brakeBias, 0.35f, 0.75f, 0.005f);
        break;
    case GarageTab::Gears:
        addF("DIFF PRELOAD", "Nm", "Differential preload", &m_setup.diffPreload, 0.f, 120.f, 1.f);
        break;
    default:
        break;
    }
    if (m_selectedRow >= static_cast<int>(m_rows.size()))
        m_selectedRow = std::max(0, static_cast<int>(m_rows.size()) - 1);
}

void SetupGarage::nextTab(int delta)
{
    int t = static_cast<int>(m_tab) + delta;
    const int n = static_cast<int>(GarageTab::Count);
    t = (t % n + n) % n;
    m_tab = static_cast<GarageTab>(t);
    m_selectedRow = 0;
    rebuildRows();
}

void SetupGarage::adjustSelected(float dir)
{
    if (m_rows.empty() || m_selectedRow < 0 ||
        m_selectedRow >= static_cast<int>(m_rows.size()))
        return;
    auto& r = m_rows[static_cast<size_t>(m_selectedRow)];
    if (r.isInt && r.intValue) {
        *r.intValue = std::clamp(*r.intValue + static_cast<int>(dir > 0 ? 1 : -1),
                                 r.intMin, r.intMax);
    } else if (r.value) {
        *r.value = std::clamp(*r.value + dir * r.step, r.minV, r.maxV);
    }
    if (onSetupChanged)
        onSetupChanged(m_setup);
}

void SetupGarage::update(float dt)
{
    const float target = m_visible ? 1.f : 0.f;
    const float speed = 6.f;
    if (m_fadeIn < target)
        m_fadeIn = std::min(m_fadeIn + dt * speed, target);
    else if (m_fadeIn > target)
        m_fadeIn = std::max(m_fadeIn - dt * speed, target);

    const float slideTarget = m_visible ? 0.f : -48.f;
    m_slide += (slideTarget - m_slide) * std::min(1.f, dt * 10.f);
}

void SetupGarage::render(int, int) {}

bool SetupGarage::handleKeyPress(int key)
{
    if (!m_visible && m_fadeIn < 0.5f) return false;

    const int n = static_cast<int>(m_rows.size());
    switch (key) {
    case 0x26: // UP
        if (n > 0)
            m_selectedRow = (m_selectedRow - 1 + n) % n;
        return true;
    case 0x28: // DOWN
        if (n > 0)
            m_selectedRow = (m_selectedRow + 1) % n;
        return true;
    case 0x25: // LEFT
        adjustSelected(-1.f);
        return true;
    case 0x27: // RIGHT
        adjustSelected(+1.f);
        return true;
    case 0x09: // TAB
    case 'E': case 'e':
        nextTab(+1);
        return true;
    case 'Q': case 'q':
        nextTab(-1);
        return true;
    case 0xBB: case 0x6B: // + VK_OEM_PLUS / num+
        adjustSelected(+1.f);
        return true;
    case 0xBD: case 0x6D: // -
        adjustSelected(-1.f);
        return true;
    case 0x1B: // ESC
        setVisible(false);
        return true;
    default:
        break;
    }
    return false;
}

bool SetupGarage::handleKeyRelease(int) { return false; }

void SetupGarage::build(DrawList& dl, TextRenderer& text, int width, int height, UiInput* input)
{
    update(1.f / 60.f);
    const float a = m_fadeIn;
    if (a <= 0.01f) return;

    auto sty = [&](const Color& c, float sc, bool bold = false) {
        TextStyle s;
        s.color = c;
        s.scale = sc;
        s.shadow = true;
        s.outline = bold;
        return s;
    };

    const float slide = m_slide;

    // Scrim + accent rail
    dl.addRectFilled({0, 0, (float)width, (float)height}, Color::rgba(0, 0, 0, 0.78f * a));
    dl.addRectFilled({0, 0, 8.f, (float)height}, Color::rgba(0.85f, 0.12f, 0.12f, 0.95f * a));

    // Header
    text.draw(dl, 72.f + slide, 36.f, "KSIM",
              sty(Color::rgba(1, 1, 1, a), 2.2f, true));
    text.draw(dl, 72.f + slide, 72.f, "GARAGE",
              sty(Color::rgba(0.75f, 0.75f, 0.78f, a), 1.15f));

    const std::string car = m_carName.empty() ? "Vehicle" : m_carName;
    text.draw(dl, (float)width - 320.f, 48.f, car,
              sty(Color::rgba(0.9f, 0.9f, 0.92f, a), 1.2f));

    // Tabs
    float tabX = 72.f + slide;
    const float tabY = 110.f;
    for (int i = 0; i < static_cast<int>(GarageTab::Count); ++i) {
        const bool on = (static_cast<int>(m_tab) == i);
        const char* name = tabName(static_cast<GarageTab>(i));
        const float tw = 110.f;
        Rect tr{tabX, tabY, tw - 8.f, 28.f};

        if (input && input->isHovering(tr) && input->state().leftReleased()) {
            m_tab = static_cast<GarageTab>(i);
            m_selectedRow = 0;
            rebuildRows();
        }

        if (on) {
            dl.addRectFilled(tr, Color::rgba(0.9f, 0.15f, 0.15f, 0.85f * a));
            text.draw(dl, tabX + 10.f, tabY + 7.f, name,
                      sty(Color::rgba(1, 1, 1, a), 1.f, true));
        } else {
            dl.addRectFilled(tr, Color::rgba(1, 1, 1, 0.06f * a));
            text.draw(dl, tabX + 10.f, tabY + 7.f, name,
                      sty(Color::rgba(0.65f, 0.65f, 0.68f, a), 1.f));
        }
        tabX += tw;
    }

    // List + detail
    const float listLeft = 72.f + slide;
    const float listTop = 160.f;
    const float listW = (float)width * 0.42f;
    const float rowH = 40.f;
    float iy = listTop;

    std::string hint;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const auto& row = m_rows[static_cast<size_t>(i)];
        const bool sel = (i == m_selectedRow);
        Rect rr{listLeft - 8.f, iy - 2.f, listW, rowH - 4.f};

        if (input && input->isHovering(rr)) {
            if (input->state().leftReleased())
                m_selectedRow = i;
        }

        if (sel) {
            dl.addRectFilled(rr, Color::rgba(1, 1, 1, 0.07f * a));
            dl.addRectFilled({listLeft - 8.f, iy - 2.f, 4.f, rowH - 4.f},
                             Color::rgba(0.9f, 0.15f, 0.15f, a));
            hint = row.hint;
        }

        text.draw(dl, listLeft + 12.f, iy + 10.f, row.label,
                  sty(sel ? Color::rgba(1, 1, 1, a) : Color::rgba(0.7f, 0.7f, 0.73f, a),
                      sel ? 1.25f : 1.1f, sel));

        // Value on right of list column
        char valBuf[48];
        float norm = 0.f;
        if (row.isInt && row.intValue) {
            std::snprintf(valBuf, sizeof(valBuf), "%d", *row.intValue);
            norm = row.intMax > row.intMin
                ? float(*row.intValue - row.intMin) / float(row.intMax - row.intMin)
                : 0.f;
        } else if (row.value) {
            if (row.label.find("BIAS") != std::string::npos)
                std::snprintf(valBuf, sizeof(valBuf), "%.1f%%", *row.value * 100.f);
            else
                std::snprintf(valBuf, sizeof(valBuf), "%.2f %s", *row.value, row.unit.c_str());
            norm = (row.maxV > row.minV)
                ? (*row.value - row.minV) / (row.maxV - row.minV)
                : 0.f;
        } else {
            valBuf[0] = 0;
        }
        text.draw(dl, listLeft + listW - 160.f, iy + 10.f, valBuf,
                  sty(Color::rgba(0.95f, 0.95f, 0.97f, a), 1.1f));

        // Mini bar under label area on detail side for selected
        if (sel) {
            const float bx = listLeft + listW + 40.f;
            const float by = listTop + 24.f;
            const float bw = (float)width - bx - 64.f;
            if (bw > 100.f) {
                dl.addRectFilled({bx, by, bw, 140.f}, Color::rgba(0.05f, 0.05f, 0.07f, 0.9f * a));
                dl.addRectFilled({bx, by, 3.f, 140.f}, Color::rgba(0.9f, 0.15f, 0.15f, 0.75f * a));
                text.draw(dl, bx + 20.f, by + 20.f, "VALUE",
                          sty(Color::rgba(0.9f, 0.25f, 0.25f, a), 1.f, true));
                text.draw(dl, bx + 20.f, by + 48.f, valBuf,
                          sty(Color::rgba(1, 1, 1, a), 1.6f, true));
                dl.addProgressBar({bx + 20.f, by + 88.f, bw - 40.f, 14.f}, norm,
                                  Color::rgba(0.9f, 0.2f, 0.2f, a),
                                  Color::rgba(0.15f, 0.15f, 0.18f, a));
                if (!hint.empty())
                    text.draw(dl, bx + 20.f, by + 112.f, hint,
                              sty(Color::rgba(0.7f, 0.7f, 0.74f, a), 1.05f));
            }
        }

        iy += rowH;
    }

    // Footer
    text.draw(dl, 72.f, (float)height - 40.f,
              "UP/DOWN  select    LEFT/RIGHT  adjust    Q/E  category    ESC  back",
              sty(Color::rgba(0.5f, 0.5f, 0.55f, a), 0.95f));
}

} // namespace sim
} // namespace ks
