#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QString>
#include <QVariant>
#include <QMap>
#include <QVector>
#include <QPair>
#include <QMutex>

namespace ks {

struct ProfileSample {
    double totalMs = 0;
    double minMs = 1e9;
    double maxMs = 0;
    int count = 0;
    double avgMs() const { return count > 0 ? totalMs / count : 0; }
};

class PhysicsProfiler : public QObject {
    Q_OBJECT
    Q_PROPERTY(double frameTimeMs READ frameTimeMs NOTIFY profileUpdated)
    Q_PROPERTY(int fps READ fps NOTIFY profileUpdated)
    Q_PROPERTY(double peakFrameTimeMs READ peakFrameTimeMs NOTIFY profileUpdated)
    Q_PROPERTY(double avgFrameTimeMs READ avgFrameTimeMs NOTIFY profileUpdated)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY profileUpdated)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

public:
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

    static PhysicsProfiler* instance();

    void beginFrame();
    void endFrame();

    void beginSubsystem(Subsystem s);
    void endSubsystem(Subsystem s);

    double frameTimeMs() const { return m_lastFrameMs; }
    double peakFrameTimeMs() const { return m_peakFrameMs; }
    double avgFrameTimeMs() const { return m_frameCount > 0 ? m_totalFrameMs / m_frameCount : 0; }
    int frameCount() const { return m_frameCount; }
    int fps() const { return m_lastFrameMs > 0 ? static_cast<int>(1000.0 / m_lastFrameMs) : 0; }

    double subsystemTimeMs(Subsystem s) const;
    double subsystemPercent(Subsystem s) const;
    QString subsystemName(Subsystem s) const;
    QVector<QPair<QString, double>> allSubsystemTimes() const;
    Q_INVOKABLE QVariantList allSubsystemPercentages() const;
    QVariantList allSubsystemTimesList() const;

    Q_INVOKABLE void reset();

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { if (m_enabled != e) { m_enabled = e; emit enabledChanged(); } }

    double totalSimTimeMs() const { return m_totalFrameMs; }

signals:
    void profileUpdated(double frameMs, int fps);
    void bottleneckDetected(const QString& subsystem, double percent);
    void enabledChanged();

private:
    explicit PhysicsProfiler(QObject* parent = nullptr);
    static PhysicsProfiler* s_instance;

    bool m_enabled = true;
    int m_frameCount = 0;
    double m_totalFrameMs = 0;
    double m_lastFrameMs = 0;
    double m_peakFrameMs = 0;

    QElapsedTimer m_frameTimer;
    QMap<Subsystem, QElapsedTimer> m_subTimers;
    QMap<Subsystem, ProfileSample> m_samples;
    QMutex m_mutex;
};

class ScopedProfile {
public:
    ScopedProfile(PhysicsProfiler::Subsystem s) : m_subsystem(s) {
        PhysicsProfiler::instance()->beginSubsystem(m_subsystem);
    }
    ~ScopedProfile() {
        PhysicsProfiler::instance()->endSubsystem(m_subsystem);
    }
private:
    PhysicsProfiler::Subsystem m_subsystem;
};

} // namespace ks
