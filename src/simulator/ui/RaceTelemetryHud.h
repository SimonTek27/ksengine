#pragma once
/** Race telemetry HUD — modes Compact|Race|Engineer + damage strip. Qt-free. */
#include "NativeUiTypes.h"
#include "TextRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>

namespace ks {
namespace sim {
namespace ui {

enum class RaceHudMode : int { Off = 0, Compact, Race, Engineer };

struct RaceHudSample {
    float speedMs = 0.f, rpm = 0.f, maxRpm = 8500.f;
    int gear = 1;
    float throttle = 0.f, brake = 0.f, steer = 0.f;
    float latG = 0.f, lonG = 0.f, fuelL = 0.f;
    float tyreTemp[4] = {80, 80, 80, 80};
    float tyreWear[4] = {};
    int position = 1, totalCars = 1, lap = 1, totalLaps = 0;
    int currentTimeMs = 0, lastTimeMs = 0, bestTimeMs = 0, sector = 0;
    int sectorTimeMs[3] = {};
    bool inPit = false, pitLimiter = false;
    float damageOverall = 0.f, engineHealth = 1.f, powerMult = 1.f;
    int damageWarning = 0;
    bool engineSeized = false;
    // Roadmap 2.10 — one line for the state the loop owns: pit service status
    // ("REFUEL 60 L", "REPAIR 42%") or garage exit status ("IN GARAGE",
    // "PIT LANE"). Empty = nothing to say. Filled by SimulationLoop only, so
    // there is exactly one place that decides what the HUD reports.
    char statusText[64] = {};
};

class RaceTelemetryHud {
public:
    void setMode(RaceHudMode m) { m_mode = m; }
    RaceHudMode mode() const { return m_mode; }
    bool isVisible() const { return m_mode != RaceHudMode::Off; }
    void cycleMode() {
        int m = static_cast<int>(m_mode) + 1;
        if (m > static_cast<int>(RaceHudMode::Engineer)) m = 0;
        m_mode = static_cast<RaceHudMode>(m);
    }
    void push(const RaceHudSample& s) {
        m_s = s;
        pushHist(m_histSpeed, s.speedMs * 3.6f);
        pushHist(m_histThr, s.throttle);
        pushHist(m_histBrk, s.brake);
        pushHist(m_histRpm, s.rpm / std::max(1.f, s.maxRpm));
    }
    const RaceHudSample& sample() const { return m_s; }

    void build(DrawList& dl, TextRenderer& text, int w, int h) {
        if (m_mode == RaceHudMode::Off) return;
        auto sty = [](const Color& c, float sc, bool bold = false) {
            TextStyle s; s.color = c; s.scale = sc; s.shadow = true; s.outline = bold; return s;
        };
        if (m_mode == RaceHudMode::Compact) buildCompact(dl, text, w, h, sty);
        else if (m_mode == RaceHudMode::Race) buildRace(dl, text, w, h, sty);
        else buildEngineer(dl, text, w, h, sty);
    }

private:
    static constexpr int kHist = 160;
    void pushHist(std::deque<float>& q, float v) {
        q.push_back(v);
        while ((int)q.size() > kHist) q.pop_front();
    }
    static void fmtTime(int ms, char* buf, size_t n) {
        if (ms <= 0) { std::snprintf(buf, n, "--:--.---"); return; }
        std::snprintf(buf, n, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    }
    int deltaMs() const {
        if (m_s.lastTimeMs > 0 && m_s.bestTimeMs > 0) return m_s.lastTimeMs - m_s.bestTimeMs;
        return 0;
    }

    template <typename StyFn>
    void buildCompact(DrawList& dl, TextRenderer& text, int w, int h, StyFn sty) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "P%d", m_s.position);
        text.draw(dl, 24.f, 20.f, buf, sty(Color::rgba(1,1,1,0.95f), 1.4f, true));
        fmtTime(m_s.currentTimeMs, buf, sizeof(buf));
        text.draw(dl, (float)w * 0.5f - 60.f, 20.f, buf, sty(Color::rgba(0.95f,0.95f,0.97f,0.95f), 1.3f, true));
        std::snprintf(buf, sizeof(buf), "%d", (int)(m_s.speedMs * 3.6f + 0.5f));
        text.draw(dl, (float)w * 0.5f - 40.f, (float)h - 72.f, buf, sty(Color::rgba(1,1,1,0.95f), 2.2f, true));
        if (m_s.damageWarning > 0) {
            std::snprintf(buf, sizeof(buf), "DMG %.0f%%", m_s.damageOverall * 100.f);
            text.draw(dl, 24.f, 48.f, buf, sty(Color::rgba(0.95f,0.3f,0.3f,0.95f), 1.1f, true));
        }
        drawStatus(dl, text, w, sty, 1.15f);
    }

    template <typename StyFn>
    void drawStatus(DrawList& dl, TextRenderer& text, int w, StyFn sty, float scale) {
        if (!m_s.statusText[0]) return;
        // Right-aligned in the top bar: whatever the loop has to say (pit
        // service or garage exit) lands in one predictable place.
        const float sw = text.measureWidth(m_s.statusText, scale);
        text.draw(dl, (float)w - 24.f - sw, 18.f, m_s.statusText,
                  sty(Color::rgba(1.f, 0.85f, 0.3f, 0.95f), scale));
    }

    template <typename StyFn>
    void buildRace(DrawList& dl, TextRenderer& text, int w, int h, StyFn sty) {
        char buf[64];
        dl.addRectFilled({0, 0, (float)w, 56.f}, Color::rgba(0,0,0,0.45f));
        std::snprintf(buf, sizeof(buf), "P%d/%d", m_s.position, std::max(1, m_s.totalCars));
        text.draw(dl, 24.f, 18.f, buf, sty(Color::rgba(1,1,1,0.95f), 1.35f, true));
        if (m_s.totalLaps > 0) std::snprintf(buf, sizeof(buf), "LAP %d/%d", m_s.lap, m_s.totalLaps);
        else std::snprintf(buf, sizeof(buf), "LAP %d", m_s.lap);
        text.draw(dl, (float)w * 0.5f - 50.f, 18.f, buf, sty(Color::rgba(0.9f,0.9f,0.92f,0.95f), 1.25f));
        drawStatus(dl, text, w, sty, 1.25f);
        const float cx = (float)w * 0.5f, cy = (float)h - 150.f;
        std::snprintf(buf, sizeof(buf), "%d", (int)(m_s.speedMs * 3.6f + 0.5f));
        text.draw(dl, cx - 48.f, cy, buf, sty(Color::rgba(1,1,1,0.98f), 2.4f, true));
        std::snprintf(buf, sizeof(buf), "%d", m_s.gear);
        text.draw(dl, cx - 12.f, cy + 48.f, buf, sty(Color::rgba(0.95f,0.2f,0.2f,0.98f), 1.8f, true));
        const float rpmN = std::clamp(m_s.rpm / std::max(1.f, m_s.maxRpm), 0.f, 1.f);
        dl.addProgressBar({cx - 120.f, cy + 88.f, 240.f, 10.f}, rpmN,
            rpmN > 0.92f ? Color::rgba(0.95f,0.15f,0.15f,0.95f) : Color::rgba(0.85f,0.85f,0.9f,0.9f),
            Color::rgba(0.1f,0.1f,0.12f,0.8f));
        if (m_s.engineSeized)
            text.draw(dl, cx - 40.f, cy - 24.f, "ENGINE OUT", sty(Color::rgba(1,0.2f,0.2f,1), 1.2f, true));
    }

    template <typename StyFn>
    void buildEngineer(DrawList& dl, TextRenderer& text, int w, int h, StyFn sty) {
        buildRace(dl, text, w, h, sty);
        const float sy = 70.f, sh = 56.f;
        dl.addRectFilled({20.f, sy, (float)w - 40.f, sh}, Color::rgba(0,0,0,0.4f));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "G lat %+.2f  lon %+.2f", m_s.latG, m_s.lonG);
        text.draw(dl, 28.f, sy + sh + 8.f, buf, sty(Color::rgba(0.8f,0.8f,0.85f,0.9f), 1.05f));
        Color dmgC = m_s.damageWarning >= 2 ? Color::rgba(0.95f,0.2f,0.2f,0.95f)
                    : m_s.damageWarning == 1 ? Color::rgba(0.95f,0.75f,0.2f,0.95f)
                    : Color::rgba(0.8f,0.85f,0.9f,0.9f);
        std::snprintf(buf, sizeof(buf), "DMG %.0f%%  ENG %.0f%%  PWR %.0f%%%s",
            m_s.damageOverall * 100.f, m_s.engineHealth * 100.f, m_s.powerMult * 100.f,
            m_s.engineSeized ? "  SEIZED" : "");
        text.draw(dl, 28.f, sy + sh + 28.f, buf, sty(dmgC, 1.05f, true));
        dl.addProgressBar({28.f, sy + sh + 48.f, 200.f, 8.f}, m_s.damageOverall, dmgC,
                          Color::rgba(0.12f,0.12f,0.14f,0.9f));
    }

    RaceHudMode m_mode = RaceHudMode::Race;
    RaceHudSample m_s;
    std::deque<float> m_histSpeed, m_histThr, m_histBrk, m_histRpm;
};

} // namespace ui
} // namespace sim
} // namespace ks
