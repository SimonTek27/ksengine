#include "PhysicsProfiler.h"
#include <algorithm>
#include <QVariantList>
#include <QVariantMap>

namespace ks {

PhysicsProfiler* PhysicsProfiler::s_instance = nullptr;

PhysicsProfiler* PhysicsProfiler::instance() {
    if (!s_instance) {
        s_instance = new PhysicsProfiler();
    }
    return s_instance;
}

PhysicsProfiler::PhysicsProfiler(QObject* parent)
    : QObject(parent)
{
    for (int i = 0; i <= Total; ++i) {
        m_samples[static_cast<Subsystem>(i)] = {};
    }
}

void PhysicsProfiler::beginFrame() {
    if (!m_enabled) return;
    m_frameTimer.start();
}

void PhysicsProfiler::endFrame() {
    if (!m_enabled) return;
    double elapsed = m_frameTimer.nsecsElapsed() / 1e6;
    m_lastFrameMs = elapsed;
    m_totalFrameMs += elapsed;
    ++m_frameCount;
    if (elapsed > m_peakFrameMs) {
        m_peakFrameMs = elapsed;
    }

    for (auto it = m_samples.begin(); it != m_samples.end(); ++it) {
        if (it.key() == Total) continue;
        auto& s = it.value();
        if (s.count > 0) {
            double pct = (s.avgMs() / m_lastFrameMs) * 100.0;
            if (pct > 60.0) {
                emit bottleneckDetected(subsystemName(it.key()), pct);
            }
        }
    }

    emit profileUpdated(m_lastFrameMs, fps());
}

void PhysicsProfiler::beginSubsystem(Subsystem s) {
    if (!m_enabled || s == Total) return;
    m_subTimers[s].start();
}

void PhysicsProfiler::endSubsystem(Subsystem s) {
    if (!m_enabled || s == Total) return;
    if (!m_subTimers[s].isValid()) return;
    double elapsed = m_subTimers[s].nsecsElapsed() / 1e6;
    auto& sample = m_samples[s];
    sample.totalMs += elapsed;
    sample.minMs = std::min(sample.minMs, elapsed);
    sample.maxMs = std::max(sample.maxMs, elapsed);
    ++sample.count;
}

double PhysicsProfiler::subsystemTimeMs(Subsystem s) const {
    return m_samples.value(s).avgMs();
}

double PhysicsProfiler::subsystemPercent(Subsystem s) const {
    double frame = m_lastFrameMs;
    if (frame <= 0) return 0;
    return (m_samples.value(s).avgMs() / frame) * 100.0;
}

QString PhysicsProfiler::subsystemName(Subsystem s) const {
    static const QString names[] = {
        "Engine", "Drivetrain", "Differential", "Brakes",
        "ABS/TC", "Aero", "WeightTransfer", "PerWheelForces",
        "VehicleDynamics", "Fuel", "TireThermal", "LapTimer",
        "ERS/Hybrid", "DRS", "Damage", "Weather"
    };
    int idx = static_cast<int>(s);
    return (idx >= 0 && idx < Total) ? names[idx] : "Unknown";
}

QVector<QPair<QString, double>> PhysicsProfiler::allSubsystemTimes() const {
    QVector<QPair<QString, double>> result;
    for (int i = 0; i < Total; ++i) {
        auto s = static_cast<Subsystem>(i);
        double t = subsystemTimeMs(s);
        if (t > 0) {
            result.append({subsystemName(s), t});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const QPair<QString, double>& a, const QPair<QString, double>& b) {
                  return a.second > b.second;
              });
    return result;
}

QVariantList PhysicsProfiler::allSubsystemPercentages() const {
    QVector<QPair<QString, double>> sorted;
    for (int i = 0; i < Total; ++i) {
        auto s = static_cast<Subsystem>(i);
        double pct = subsystemPercent(s);
        if (pct > 0) {
            sorted.append({subsystemName(s), pct});
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<QString, double>& a, const QPair<QString, double>& b) {
                  return a.second > b.second;
              });
    QVariantList result;
    for (const auto& pair : sorted) {
        QVariantMap item;
        item["key"] = pair.first;
        item["value"] = pair.second;
        result.append(item);
    }
    return result;
}

QVariantList PhysicsProfiler::allSubsystemTimesList() const {
    QVector<QPair<QString, double>> sorted;
    for (int i = 0; i < Total; ++i) {
        auto s = static_cast<Subsystem>(i);
        double t = subsystemTimeMs(s);
        if (t > 0) {
            sorted.append({subsystemName(s), t});
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const QPair<QString, double>& a, const QPair<QString, double>& b) {
                  return a.second > b.second;
              });
    QVariantList result;
    for (const auto& pair : sorted) {
        QVariantMap item;
        item["key"] = pair.first;
        item["value"] = pair.second;
        result.append(item);
    }
    return result;
}

void PhysicsProfiler::reset() {
    m_frameCount = 0;
    m_totalFrameMs = 0;
    m_lastFrameMs = 0;
    m_peakFrameMs = 0;
    m_subTimers.clear();
    m_samples.clear();
    for (int i = 0; i <= Total; ++i) {
        m_samples[static_cast<Subsystem>(i)] = {};
    }
}

} // namespace ks
