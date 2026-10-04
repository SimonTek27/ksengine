#pragma once
/**
 * PhysicsGolden — compare simulated trajectory vs reference CSV.
 * Parity milestone 1.1: correlation / MAE vs real telemetry.
 *
 * CSV columns (header required):
 *   time_s,speed_ms,rpm,x,y,z,throttle,brake,steer
 */
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace ks {
namespace physics {

struct GoldenSample {
    double time = 0;
    float speedMs = 0;
    float rpm = 0;
    float x = 0, y = 0, z = 0;
    float throttle = 0, brake = 0, steer = 0;
};

struct GoldenMetrics {
    int samples = 0;
    double maeSpeed = 0;
    double maeRpm = 0;
    double corrSpeed = 0; // Pearson on speed
    double maxSpeedErr = 0;
    bool ok = false;
};

class PhysicsGolden {
public:
    bool loadCsv(const std::string& path) {
        m_ref.clear();
        std::ifstream in(path);
        if (!in) {
            std::fprintf(stderr, "PhysicsGolden: cannot open %s\n", path.c_str());
            return false;
        }
        std::string line;
        bool header = true;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (header) {
                header = false;
                if (line.find("time") != std::string::npos) continue;
            }
            std::replace(line.begin(), line.end(), ';', ',');
            std::stringstream ss(line);
            std::string cell;
            GoldenSample s;
            auto next = [&](float& v) {
                if (!std::getline(ss, cell, ',')) return false;
                v = static_cast<float>(std::atof(cell.c_str()));
                return true;
            };
            float t = 0;
            if (!next(t)) continue;
            s.time = t;
            if (!next(s.speedMs)) continue;
            next(s.rpm);
            next(s.x); next(s.y); next(s.z);
            next(s.throttle); next(s.brake); next(s.steer);
            m_ref.push_back(s);
        }
        std::fprintf(stderr, "PhysicsGolden: loaded %zu samples from %s\n",
                     m_ref.size(), path.c_str());
        return !m_ref.empty();
    }

    void clearSim() { m_sim.clear(); }

    void pushSim(const GoldenSample& s) { m_sim.push_back(s); }

    void clearRef() { m_ref.clear(); }
    void pushRef(const GoldenSample& s) { m_ref.push_back(s); }
    size_t refCount() const { return m_ref.size(); }

    /** Export the reference set as a golden CSV (loadCsv-compatible format). */
    bool saveCsv(const std::string& path) const {
        if (m_ref.empty()) return false;
        std::ofstream out(path);
        if (!out) {
            std::fprintf(stderr, "PhysicsGolden: cannot write %s\n", path.c_str());
            return false;
        }
        out << "# ksengine golden lap export\n";
        out << "# time_s,speed_ms,rpm,x,y,z,throttle,brake,steer\n";
        out << "time_s,speed_ms,rpm,x,y,z,throttle,brake,steer\n";
        char buf[256];
        for (const auto& s : m_ref) {
            std::snprintf(buf, sizeof(buf),
                          "%.3f,%.3f,%.1f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f\n",
                          s.time, double(s.speedMs), double(s.rpm),
                          double(s.x), double(s.y), double(s.z),
                          double(s.throttle), double(s.brake), double(s.steer));
            out << buf;
        }
        return true;
    }

    const std::vector<GoldenSample>& reference() const { return m_ref; }
    const std::vector<GoldenSample>& simulated() const { return m_sim; }

    /** Align by nearest time and compute MAE + Pearson correlation on speed. */
    GoldenMetrics evaluate() const {
        GoldenMetrics m;
        if (m_ref.empty() || m_sim.empty()) return m;

        std::vector<double> a, b;
        a.reserve(m_ref.size());
        b.reserve(m_ref.size());
        double sumAbsSp = 0, sumAbsRpm = 0;
        double maxErr = 0;
        int n = 0;

        for (const auto& r : m_ref) {
            // nearest sim sample by time
            size_t best = 0;
            double bestDt = 1e9;
            for (size_t i = 0; i < m_sim.size(); ++i) {
                const double dt = std::abs(m_sim[i].time - r.time);
                if (dt < bestDt) { bestDt = dt; best = i; }
            }
            if (bestDt > 0.05) continue; // >50 ms drift skip
            const auto& s = m_sim[best];
            const double eSp = std::abs(double(s.speedMs) - double(r.speedMs));
            const double eRpm = std::abs(double(s.rpm) - double(r.rpm));
            sumAbsSp += eSp;
            sumAbsRpm += eRpm;
            maxErr = std::max(maxErr, eSp);
            a.push_back(r.speedMs);
            b.push_back(s.speedMs);
            ++n;
        }
        if (n == 0) return m;
        m.samples = n;
        m.maeSpeed = sumAbsSp / n;
        m.maeRpm = sumAbsRpm / n;
        m.maxSpeedErr = maxErr;
        m.corrSpeed = pearson(a, b);
        // Soft pass for synthetic data; real telemetry target corr > 0.95
        m.ok = (m.corrSpeed > 0.85 && m.maeSpeed < 5.0) || (m.samples > 10 && m.maeSpeed < 2.0);
        return m;
    }

    /** Run open-loop: feed ref throttle/brake/steer into a step callback. */
    template <typename StepFn>
    GoldenMetrics runOpenLoop(StepFn step) {
        clearSim();
        for (const auto& r : m_ref) {
            GoldenSample out = step(r);
            out.time = r.time;
            pushSim(out);
        }
        return evaluate();
    }

private:
    static double pearson(const std::vector<double>& x, const std::vector<double>& y) {
        const size_t n = x.size();
        if (n < 2 || y.size() != n) return 0;
        double sx = 0, sy = 0;
        for (size_t i = 0; i < n; ++i) { sx += x[i]; sy += y[i]; }
        const double mx = sx / n, my = sy / n;
        double num = 0, dx = 0, dy = 0;
        for (size_t i = 0; i < n; ++i) {
            const double a = x[i] - mx, b = y[i] - my;
            num += a * b;
            dx += a * a;
            dy += b * b;
        }
        const double den = std::sqrt(dx * dy);
        return den > 1e-12 ? num / den : 0.0;
    }

    std::vector<GoldenSample> m_ref;
    std::vector<GoldenSample> m_sim;
};

} // namespace physics
} // namespace ks
