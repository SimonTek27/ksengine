#pragma once
/**
 * Race telemetry HUD — cinematic ksim overlay (Qt-free).
 * Modes: Compact | Race | Engineer.
 */
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

enum class RaceHudMode : int {
    Off = 0,
    Compact,
    Race,
    Engineer
};

struct RaceHudSample {
    float speedMs = 0.f;
    float rpm = 0.f;
    float maxRpm = 8500.f;
    int gear = 1;           // 1 = 1st
    float throttle = 0.f;   // 0..1
    float brake = 0.f;
    float steer = 0.f;      // -1..1
    float latG = 0.f;
    float lonG = 0.f;
    float fuelL = 0.f;
    float tyreTemp[4] = {80, 80, 80, 80}; // FL FR RL RR
    float tyreWear[4] = {0, 0, 0, 0};     // 0..1 worn
    int position = 1;
    int totalCars = 1;
    int lap = 1;
    int totalLaps = 0;
    int currentTimeMs = 0;
    int lastTimeMs = 0;
    int bestTimeMs = 0;
    int sector = 0;
    int sectorTimeMs[3] = {0, 0, 0};
    bool inPit = false;
    bool pitLimiter = false;
};

class RaceTelemetryHud {
public:
    void setMode(RaceHudMode m) { m_mode = m; }
    RaceHudMode mode() const { return m_mode; }
    bool isVisible() const { return m_mode != RaceHudMode::Off; }

    /** Cycle Off → Compact → Race → Engineer → Off */
    void cycleMode() {
        int m = static_cast<int>(m_mode) + 1;
        if (m > static_cast<int>(RaceHudMode::Engineer))
            m = 0;
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
            TextStyle s;
            s.color = c;
            s.scale = sc;
            s.shadow = true;
            s.outline = bold;
            return s;
        };

        if (m_mode == RaceHudMode::Compact)
            buildCompact(dl, text, w, h, sty);
        else if (m_mode == RaceHudMode::Race)
            buildRace(dl, text, w, h, sty);
        else
            buildEngineer(dl, text, w, h, sty);
    }

private:
    static constexpr int kHist = 160;

    void pushHist(std::deque<float>& q, float v) {
        q.push_back(v);
        while (static_cast<int>(q.size()) > kHist)
            q.pop_front();
    }

    static void fmtTime(int ms, char* buf, size_t n) {
        if (ms <= 0) {
            std::snprintf(buf, n, "--:--.---");
            return;
        }
        int m = ms / 60000;
        int s = (ms / 1000) % 60;
        int frac = ms % 1000;
        std::snprintf(buf, n, "%d:%02d.%03d", m, s, frac);
    }

    int deltaMs() const {
        if (m_s.bestTimeMs <= 0 || m_s.currentTimeMs <= 0) return 0;
        // live delta vs best at same progress is approximate: use last vs best
        if (m_s.lastTimeMs > 0 && m_s.bestTimeMs > 0)
            return m_s.lastTimeMs - m_s.bestTimeMs;
        return 0;
    }

    template <typename StyFn>
    void buildCompact(DrawList& dl, TextRenderer& text, int w, int h, StyFn sty) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "P%d", m_s.position);
        text.draw(dl, 24.f, 20.f, buf, sty(Color::rgba(1, 1, 1, 0.95f), 1.4f, true));

        fmtTime(m_s.currentTimeMs, buf, sizeof(buf));
        text.draw(dl, (float)w * 0.5f - 60.f, 20.f, buf,
                  sty(Color::rgba(0.95f, 0.95f, 0.97f, 0.95f), 1.3f, true));

        const int d = deltaMs();
        std::snprintf(buf, sizeof(buf), "%s%d.%03d",
                      d > 0 ? "+" : (d < 0 ? "-" : " "),
                      std::abs(d) / 1000, std::abs(d) % 1000);
        Color dc = d <= 0 ? Color::rgba(0.3f, 0.9f, 0.4f, 0.95f)
                          : Color::rgba(0.95f, 0.25f, 0.25f, 0.95f);
        text.draw(dl, (float)w - 140.f, 20.f, buf, sty(dc, 1.25f, true));

        // Speed + gear bottom center
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(m_s.speedMs * 3.6f + 0.5f));
        text.draw(dl, (float)w * 0.5f - 40.f, (float)h - 72.f, buf,
                  sty(Color::rgba(1, 1, 1, 0.95f), 2.2f, true));
        std::snprintf(buf, sizeof(buf), "%d", m_s.gear);
        text.draw(dl, (float)w * 0.5f - 10.f, (float)h - 40.f, buf,
                  sty(Color::rgba(0.9f, 0.2f, 0.2f, 0.95f), 1.6f, true));
    }

    template <typename StyFn>
    void buildRace(DrawList& dl, TextRenderer& text, int w, int h, StyFn sty) {
        char buf[64];

        // Top bar
        dl.addRectFilled({0, 0, (float)w, 56.f}, Color::rgba(0, 0, 0, 0.45f));
        std::snprintf(buf, sizeof(buf), "P%d/%d", m_s.position, std::max(1, m_s.totalCars));
        text.draw(dl, 24.f, 18.f, buf, sty(Color::rgba(1, 1, 1, 0.95f), 1.35f, true));

        if (m_s.totalLaps > 0)
            std::snprintf(buf, sizeof(buf), "LAP %d/%d", m_s.lap, m_s.totalLaps);
        else
            std::snprintf(buf, sizeof(buf), "LAP %d", m_s.lap);
        text.draw(dl, (float)w * 0.5f - 50.f, 18.f, buf,
                  sty(Color::rgba(0.9f, 0.9f, 0.92f, 0.95f), 1.25f));

        const int d = deltaMs();
        std::snprintf(buf, sizeof(buf), "%s%d.%03d",
                      d > 0 ? "+" : (d < 0 ? "-" : " "),
                      std::abs(d) / 1000, std::abs(d) % 1000);
        Color dc = d <= 0 ? Color::rgba(0.3f, 0.9f, 0.4f, 0.95f)
                          : Color::rgba(0.95f, 0.3f, 0.3f, 0.95f);
        text.draw(dl, (float)w - 150.f, 18.f, buf, sty(dc, 1.3f, true));

        if (m_s.pitLimiter || m_s.inPit) {
            dl.addRectFilled({(float)w * 0.5f - 70.f, 56.f, 140.f, 22.f},
                             Color::rgba(0.9f, 0.15f, 0.15f, 0.85f));
            text.draw(dl, (float)w * 0.5f - 50.f, 60.f,
                      m_s.inPit ? "IN PIT" : "LIMITER",
                      sty(Color::rgba(1, 1, 1, 1), 1.05f, true));
        }

        // Center cluster
        const float cx = (float)w * 0.5f;
        const float cy = (float)h - 150.f;
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(m_s.speedMs * 3.6f + 0.5f));
        text.draw(dl, cx - 48.f, cy, buf, sty(Color::rgba(1, 1, 1, 0.98f), 2.4f, true));
        text.draw(dl, cx + 50.f, cy + 18.f, "km/h",
                  sty(Color::rgba(0.65f, 0.65f, 0.7f, 0.9f), 1.f));

        std::snprintf(buf, sizeof(buf), "%d", m_s.gear);
        text.draw(dl, cx - 12.f, cy + 48.f, buf,
                  sty(Color::rgba(0.95f, 0.2f, 0.2f, 0.98f), 1.8f, true));

        // RPM bar
        const float rpmN = std::clamp(m_s.rpm / std::max(1.f, m_s.maxRpm), 0.f, 1.f);
        Color rpmC = rpmN > 0.92f ? Color::rgba(0.95f, 0.15f, 0.15f, 0.95f)
                     : rpmN > 0.8f  ? Color::rgba(0.95f, 0.75f, 0.2f, 0.95f)
                                    : Color::rgba(0.85f, 0.85f, 0.9f, 0.9f);
        dl.addProgressBar({cx - 120.f, cy + 88.f, 240.f, 10.f}, rpmN, rpmC,
                          Color::rgba(0.1f, 0.1f, 0.12f, 0.8f));

        // Inputs left
        const float ix = 24.f;
        const float iy = (float)h - 130.f;
        dl.addRectFilled({ix, iy, 160.f, 100.f}, Color::rgba(0, 0, 0, 0.4f));
        text.draw(dl, ix + 10.f, iy + 8.f, "THR", sty(Color::rgba(0.7f, 0.7f, 0.75f, 0.9f), 0.95f));
        dl.addProgressBar({ix + 10.f, iy + 28.f, 140.f, 10.f}, m_s.throttle,
                          Color::rgba(0.25f, 0.85f, 0.35f, 0.95f), Color::rgba(0.12f, 0.12f, 0.14f, 0.9f));
        text.draw(dl, ix + 10.f, iy + 44.f, "BRK", sty(Color::rgba(0.7f, 0.7f, 0.75f, 0.9f), 0.95f));
        dl.addProgressBar({ix + 10.f, iy + 64.f, 140.f, 10.f}, m_s.brake,
                          Color::rgba(0.9f, 0.2f, 0.2f, 0.95f), Color::rgba(0.12f, 0.12f, 0.14f, 0.9f));

        // Tyres right
        const float tx = (float)w - 150.f;
        dl.addRectFilled({tx, iy, 130.f, 100.f}, Color::rgba(0, 0, 0, 0.4f));
        text.draw(dl, tx + 10.f, iy + 8.f, "TYRES C",
                  sty(Color::rgba(0.7f, 0.7f, 0.75f, 0.9f), 0.95f));
        std::snprintf(buf, sizeof(buf), "%3.0f  %3.0f", m_s.tyreTemp[0], m_s.tyreTemp[1]);
        text.draw(dl, tx + 16.f, iy + 32.f, buf, sty(Color::rgba(1, 1, 1, 0.95f), 1.15f));
        std::snprintf(buf, sizeof(buf), "%3.0f  %3.0f", m_s.tyreTemp[2], m_s.tyreTemp[3]);
        text.draw(dl, tx + 16.f, iy + 54.f, buf, sty(Color::rgba(1, 1, 1, 0.95f), 1.15f));

        // Bottom timing strip
        dl.addRectFilled({0, (float)h - 28.f, (float)w, 28.f}, Color::rgba(0, 0, 0, 0.5f));
        std::snprintf(buf, sizeof(buf), "FUEL %.1f L", m_s.fuelL);
        text.draw(dl, 20.f, (float)h - 20.f, buf, sty(Color::rgba(0.85f, 0.85f, 0.88f, 0.9f), 1.f));

        fmtTime(m_s.bestTimeMs, buf, sizeof(buf));
        char best[64];
        std::snprintf(best, sizeof(best), "BEST %s", buf);
        text.draw(dl, (float)w - 200.f, (float)h - 20.f, best,
                  sty(Color::rgba(0.85f, 0.85f, 0.88f, 0.9f), 1.f));

        fmtTime(m_s.currentTimeMs, buf, sizeof(buf));
        text.draw(dl, (float)w * 0.5f - 50.f, (float)h - 20.f, buf,
                  sty(Color::rgba(1, 1, 1, 0.95f), 1.05f));
    }

    template <typename StyFn>
    void buildEngineer(DrawList& dl, TextRenderer& text, int w, int h, StyFn sty) {
        buildRace(dl, text, w, h, sty);

        // Sparkline strip
        const float sy = 70.f;
        const float sh = 56.f;
        dl.addRectFilled({20.f, sy, (float)w - 40.f, sh}, Color::rgba(0, 0, 0, 0.4f));
        drawSpark(dl, 28.f, sy + 4.f, (float)w - 56.f, sh - 8.f, m_histSpeed, 0.f, 320.f,
                  Color::rgba(0.4f, 0.75f, 1.f, 0.9f));

        char buf[48];
        std::snprintf(buf, sizeof(buf), "G lat %+.2f  lon %+.2f", m_s.latG, m_s.lonG);
        text.draw(dl, 28.f, sy + sh + 8.f, buf,
                  sty(Color::rgba(0.8f, 0.8f, 0.85f, 0.9f), 1.05f));

        // Wear line
        std::snprintf(buf, sizeof(buf), "WEAR  %.0f%% %.0f%% / %.0f%% %.0f%%",
                      m_s.tyreWear[0] * 100.f, m_s.tyreWear[1] * 100.f,
                      m_s.tyreWear[2] * 100.f, m_s.tyreWear[3] * 100.f);
        text.draw(dl, (float)w - 320.f, sy + sh + 8.f, buf,
                  sty(Color::rgba(0.8f, 0.8f, 0.85f, 0.9f), 1.f));
    }

    static void drawSpark(DrawList& dl, float x, float y, float w, float h,
                          const std::deque<float>& data, float vmin, float vmax,
                          const Color& col) {
        if (data.size() < 2) return;
        const float span = std::max(1e-3f, vmax - vmin);
        float px = x;
        float py = y + h * (1.f - (std::clamp(data[0], vmin, vmax) - vmin) / span);
        const float dx = w / float(data.size() - 1);
        for (size_t i = 1; i < data.size(); ++i) {
            float nx = x + dx * float(i);
            float ny = y + h * (1.f - (std::clamp(data[i], vmin, vmax) - vmin) / span);
            dl.addLine(px, py, nx, ny, col, 1.5f);
            px = nx;
            py = ny;
        }
    }

    RaceHudMode m_mode = RaceHudMode::Race;
    RaceHudSample m_s;
    std::deque<float> m_histSpeed, m_histThr, m_histBrk, m_histRpm;
};

} // namespace ui
} // namespace sim
} // namespace ks
