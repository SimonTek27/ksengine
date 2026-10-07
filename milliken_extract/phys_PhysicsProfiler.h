#pragma once

/**
 * @file PhysicsProfiler.h
 * @brief Unified performance profiler for physics simulation
 * @copyright KS Physics Engine
 *
 * Combines string-based section profiling (flexible) with enum-based subsystem
 * profiling (type-safe, QML-friendly). Thread-safe singleton.
 */

#include <QObject>
#include <QElapsedTimer>
#include <QMap>
#include <QString>
#include <QVector>
#include <QPair>
#include <QMutex>
#include <QVariant>
#include <array>

namespace ks {
namespace physics {

// ============================================================================
// Physics Profiler
// ============================================================================

/**
 * @brief Unified performance profiler for physics simulation
 *
 * Supports two profiling modes:
 *  - String-based sections: beginSection("Physics.Update") for ad-hoc profiling
 *  - Enum-based subsystems: beginSubsystem(Engine) for structured profiling
 *
 * Both modes are thread-safe and share the same frame timing.
 */
class PhysicsProfiler : public QObject {
    Q_OBJECT

    Q_PROPERTY(double frameTimeMs READ frameTimeMs NOTIFY profileUpdated)
    Q_PROPERTY(int fps READ fps NOTIFY profileUpdated)
    Q_PROPERTY(double peakFrameTimeMs READ peakFrameTimeMs NOTIFY profileUpdated)
    Q_PROPERTY(double avgFrameTimeMs READ avgFrameTimeMs NOTIFY profileUpdated)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY profileUpdated)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

public:
    // ========================================================================
    // Enum-based subsystems (structured profiling)
    // ========================================================================

    enum Subsystem {
        Engine = 0,
        Drivetrain,
        Differential,
        Brakes,
        ABS_TC,
        Aero,
        WeightTransfer,
        PerWheelForces,
        VehicleDynamics,
        Fuel,
        TireThermal,
        LapTimer,
        ERS_Hybrid,
        DRS_Logic,
        DamageModel,
        WeatherPhysics,
        Total
    };
    Q_ENUM(Subsystem)

    // ========================================================================
    // Section statistics (string-based profiling)
    // ========================================================================

    struct SectionStats {
        qint64 totalTime = 0;          ///< Total time in nanoseconds
        qint64 maxTime = 0;            ///< Maximum time in nanoseconds
        qint64 minTime = LLONG_MAX;    ///< Minimum time in nanoseconds
        int count = 0;                 ///< Number of samples

        double avgTime() const {
            return count > 0 ? totalTime / static_cast<double>(count) : 0.0;
        }
        double avgMs() const { return avgTime() / 1000000.0; }
        double maxMs() const { return maxTime / 1000000.0; }
    };

    struct ProfileSample {
        double totalMs = 0;
        double minMs = 1e9;
        double maxMs = 0;
        int count = 0;
        double avgMs() const { return count > 0 ? totalMs / count : 0; }
    };

    struct FrameStats {
        qint64 frameTime = 0;
        int sectionsCount = 0;
        double ms() const { return frameTime / 1000000.0; }
    };

    // ========================================================================
    // Singleton
    // ========================================================================

    static PhysicsProfiler& instanceRef();
    static PhysicsProfiler* instance();

    // ========================================================================
    // Frame management
    // ========================================================================

    void beginFrame();
    void endFrame();

    // ========================================================================
    // String-based section profiling
    // ========================================================================

    void beginSection(const QString& name);
    void endSection(const QString& name);
    QMap<QString, SectionStats> getSectionStats() const;

    // ========================================================================
    // Enum-based subsystem profiling
    // ========================================================================

    void beginSubsystem(Subsystem s);
    void endSubsystem(Subsystem s);
    double subsystemTimeMs(Subsystem s) const;
    double subsystemPercent(Subsystem s) const;
    QString subsystemName(Subsystem s) const;
    QVector<QPair<QString, double>> allSubsystemTimes() const;
    Q_INVOKABLE QVariantList allSubsystemPercentages() const;
    QVariantList allSubsystemTimesList() const;

    // ========================================================================
    // Query
    // ========================================================================

    double frameTimeMs() const { return m_lastFrameMs; }
    double peakFrameTimeMs() const { return m_peakFrameMs; }
    double avgFrameTimeMs() const {
        return m_frameCount > 0 ? m_totalFrameMs / m_frameCount : 0.0;
    }
    int frameCount() const { return m_frameCount; }
    int fps() const { return m_lastFrameMs > 0 ? static_cast<int>(1000.0 / m_lastFrameMs) : 0; }
    double totalSimTimeMs() const { return m_totalFrameMs; }

    QVector<FrameStats> getFrameHistory() const { return m_frameHistory; }

    // ========================================================================
    // Configuration
    // ========================================================================

    Q_INVOKABLE void reset();
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e);
    void setHistorySize(int size) { m_maxHistorySize = size; }

signals:
    void profileUpdated(double frameMs, int fps);
    void bottleneckDetected(const QString& subsystem, double percent);
    void enabledChanged();
    void frameCompleted(qint64 frameTime);
    void sectionCompleted(const QString& name, qint64 time);
    void statsReset();

private:
    explicit PhysicsProfiler(QObject* parent = nullptr);
    ~PhysicsProfiler() override = default;
    PhysicsProfiler(const PhysicsProfiler&) = delete;
    PhysicsProfiler& operator=(const PhysicsProfiler&) = delete;

    void updateSectionStats(const QString& name, qint64 time);
    void updateSubsystemStats(Subsystem s, qint64 time);
    void trimHistory();

    bool m_enabled = true;
    int m_frameCount = 0;
    double m_totalFrameMs = 0;
    double m_lastFrameMs = 0;
    double m_peakFrameMs = 0;
    int m_maxHistorySize = 1000;

    QElapsedTimer m_frameTimer;
    mutable QMutex m_mutex;

    // String-based section tracking
    QMap<QString, SectionStats> m_sectionStats;
    QMap<QString, QElapsedTimer> m_sectionTimers;

    // Enum-based subsystem tracking
    std::array<QElapsedTimer, Total> m_subTimers;
    std::array<bool, Total> m_subTimerActive{};
    std::array<ProfileSample, Total> m_subSamples;

    // Frame history
    QVector<FrameStats> m_frameHistory;
};

// ============================================================================
// RAII Profiler Wrappers
// ============================================================================

/**
 * @brief RAII wrapper for string-based section profiling
 *
 * Usage:
 *   {
 *       ProfilerSection section("Physics.Update");
 *       // Code to profile
 *   }
 */
class ProfilerSection {
public:
    explicit ProfilerSection(const QString& name) : m_name(name) {
        PhysicsProfiler::instance()->beginSection(name);
    }
    ~ProfilerSection() {
        PhysicsProfiler::instance()->endSection(m_name);
    }
    ProfilerSection(const ProfilerSection&) = delete;
    ProfilerSection& operator=(const ProfilerSection&) = delete;

private:
    QString m_name;
};

/**
 * @brief RAII wrapper for enum-based subsystem profiling
 *
 * Usage:
 *   {
 *       ScopedProfile profile(PhysicsProfiler::Engine);
 *       // Code to profile
 *   }
 */
class ScopedProfile {
public:
    explicit ScopedProfile(PhysicsProfiler::Subsystem s) : m_subsystem(s) {
        PhysicsProfiler::instance()->beginSubsystem(m_subsystem);
    }
    ~ScopedProfile() {
        PhysicsProfiler::instance()->endSubsystem(m_subsystem);
    }
    ScopedProfile(const ScopedProfile&) = delete;
    ScopedProfile& operator=(const ScopedProfile&) = delete;

private:
    PhysicsProfiler::Subsystem m_subsystem;
};

// ============================================================================
// Convenience Macros
// ============================================================================

#define PROFILE_FRAME() ks::physics::PhysicsProfiler::instance().beginFrame()
#define PROFILE_END_FRAME() ks::physics::PhysicsProfiler::instance().endFrame()
#define PROFILE_SECTION(name) ks::physics::ProfilerSection _profiler_section_##__LINE__(name)
#define PROFILE_BEGIN(name) ks::physics::PhysicsProfiler::instance().beginSection(name)
#define PROFILE_END(name) ks::physics::PhysicsProfiler::instance().endSection(name)
#define PROFILE_SUBSYSTEM(s) ks::physics::ScopedProfile _profiler_sub_##__LINE__(s)

} // namespace physics
} // namespace ks
