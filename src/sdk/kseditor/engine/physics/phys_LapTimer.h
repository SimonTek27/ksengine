#pragma once

#include <QObject>
#include <QVector>
#include "PhysicsCoreTypes.h"

namespace ks {

using LapTimeEstimate = ::ks::physics::LapTimeEstimate;

class phys_LapTimer : public QObject {
    Q_OBJECT
public:
    explicit phys_LapTimer(QObject* parent = nullptr);

    void startLap();
    void stopLap();
    void reset();
    void update(double dt, double speed, double distance);

    double currentLapTime() const { return m_currentLapTime; }
    double bestLapTime() const { return m_bestLapTime; }
    double lastLapTime() const { return m_lastLapTime; }
    int lapCount() const { return m_lapCount; }

    void setSectorDistances(double sector1, double sector2, double sector3);
    void recordLateralG(double gForce);
    void recordGearChange();
    double sector1Time() const { return m_sector1Time; }
    double sector2Time() const { return m_sector2Time; }
    double sector3Time() const { return m_sector3Time; }
    int currentSector() const { return m_currentSector; }

    LapTimeEstimate estimateLapTime(const QVector<double>& historicalLapTimes,
                                     double trackLength,
                                     double avgCornerSpeed) const;

signals:
    void lapCompleted(double lapTime, double bestLapTime);
    void sectorCompleted(int sector, double sectorTime);
    void lapTimeUpdated(double currentTime);

private:
    double m_currentLapTime = 0.0;
    double m_bestLapTime = 1e9;
    double m_lastLapTime = 0.0;
    int m_lapCount = 0;
    double m_totalDistance = 0.0;
    double m_sector1Distance = 0.0;
    double m_sector2Distance = 0.0;
    double m_sector3Distance = 0.0;
    double m_sector1Time = 0.0;
    double m_sector2Time = 0.0;
    double m_sector3Time = 0.0;
    int m_currentSector = 1;
    double m_lastSectorDistance = 0.0;

    double m_topSpeedRecorded = 0.0;
    double m_maxLateralGRecorded = 0.0;
    int m_gearChangesRecorded = 0;
};

} // namespace ks