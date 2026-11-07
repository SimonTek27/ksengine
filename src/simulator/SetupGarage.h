#pragma once
/**
 * Garage / car setup screen — cinematic racing layout for ksim.
 * Qt-free; draws via ui::DrawList from NativeUiHub.
 */
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

namespace ks {
namespace sim {

namespace ui {
class DrawList;
class TextRenderer;
class UiInput;
}

enum class GarageTab : int {
    Overview = 0,
    Tyres,
    Suspension,
    Aero,
    Brakes,
    Gears,
    Count
};

struct SetupData {
    float tirePressureFL = 2.20f;
    float tirePressureFR = 2.20f;
    float tirePressureRL = 2.00f;
    float tirePressureRR = 2.00f;
    float brakeBias = 0.56f;       // 0..1 front fraction
    float rideHeightFront = 30.f;  // mm
    float rideHeightRear = 35.f;
    float springRateFront = 150.f; // N/mm proxy
    float springRateRear = 180.f;
    float frontWingAngle = 10.f;   // deg
    float rearWingAngle = 12.f;
    float diffPreload = 30.f;      // Nm proxy
    float fuel = 50.f;             // L
    float ballast = 0.f;           // kg
    int tcLevel = 0;
    int absLevel = 0;
};

struct GarageRow {
    std::string label;
    std::string unit;
    std::string hint;
    float* value = nullptr;
    float minV = 0.f;
    float maxV = 1.f;
    float step = 0.05f;
    bool isInt = false;
    int* intValue = nullptr;
    int intMin = 0;
    int intMax = 10;
};

class SetupGarage {
public:
    SetupGarage();
    ~SetupGarage() = default;

    void update(float dt);
    void render(int width, int height); // timers only; draw via build()

    /** Emit cinematic garage into DrawList. */
    void build(ui::DrawList& dl, ui::TextRenderer& text, int width, int height,
               ui::UiInput* input = nullptr);

    bool handleKeyPress(int key);
    bool handleKeyRelease(int key);

    void setVisible(bool v);
    bool isVisible() const { return m_visible; }
    void toggleVisible() { setVisible(!m_visible); }
    float fadeAlpha() const { return m_fadeIn; }

    /** What the car says it has: a level is only offered for a system the car
     *  declares present. A car that says nothing (the default here) keeps both
     *  rows, so content that never mentioned its aids behaves as before. */
    void setAidsAvailable(bool tc, bool abs)
    {
        if (m_tcAvailable == tc && m_absAvailable == abs) return;
        m_tcAvailable = tc;
        m_absAvailable = abs;
        rebuildRows();
    }
    bool aidsAvailable(bool tc) const { return tc ? m_tcAvailable : m_absAvailable; }

    void setCarName(const std::string& n) { m_carName = n; }
    void setTrackName(const std::string& n) { m_trackName = n; }

    const SetupData& currentSetup() const { return m_setup; }
    SetupData& setup() { return m_setup; }
    void setSetup(const SetupData& s) { m_setup = s; rebuildRows(); }

    GarageTab tab() const { return m_tab; }
    int selectedRow() const { return m_selectedRow; }

    std::function<void()> onClosed;
    std::function<void(const SetupData&)> onSetupChanged;

private:
    void rebuildRows();
    void adjustSelected(float dir);
    void nextTab(int delta);
    static const char* tabName(GarageTab t);

    bool m_visible = false;
    float m_fadeIn = 0.f;
    float m_slide = 0.f;

    GarageTab m_tab = GarageTab::Overview;
    int m_selectedRow = 0;
    SetupData m_setup;
    std::vector<GarageRow> m_rows;

    std::string m_carName;
    std::string m_trackName;
    // Rows the car's own electronics leave out (PRESENT = 0). Default: offered.
    bool m_tcAvailable = true;
    bool m_absAvailable = true;
};

} // namespace sim
} // namespace ks
