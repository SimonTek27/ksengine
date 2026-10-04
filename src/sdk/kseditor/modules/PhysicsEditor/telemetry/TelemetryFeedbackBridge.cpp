#include "TelemetryFeedbackBridge.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/physics/BrakeThermalModel.h"

namespace ks {

TelemetryFeedbackBridge* TelemetryFeedbackBridge::s_instance = nullptr;

TelemetryFeedbackBridge* TelemetryFeedbackBridge::instance() {
    if (!s_instance) {
        s_instance = new TelemetryFeedbackBridge();
    }
    return s_instance;
}

TelemetryFeedbackBridge::TelemetryFeedbackBridge(QObject* parent)
    : QObject(parent)
{
    m_simulator = phys_Simulator::instance();
}

// ============================================================================
// Public API
// ============================================================================

void TelemetryFeedbackBridge::startFeedback() {
    if (m_active) return;

    m_active = true;
    m_simSamples.clear();
    m_currentSimSamples.clear();
    m_feedbackLapCount = 0;
    m_sampleTimeAccum = 0;

    connect(m_simulator, &phys_Simulator::stateUpdated,
            this, &TelemetryFeedbackBridge::onSimStateUpdated,
            Qt::UniqueConnection);

    connect(m_simulator->lapTimer(), &phys_LapTimer::lapCompleted,
            this, [this](double lapTime, double) {
        onLapCompleted(lapTime);
    });

    if (!m_simulator->isRunning()) {
        m_simulator->startSimulation();
    }

    TelemetryQmlBridge::instance()->startSession();

    emit activeChanged();
    emit feedbackStarted();
    emit statusMessage("Telemetry feedback loop started");
}

void TelemetryFeedbackBridge::stopFeedback() {
    if (!m_active) return;

    m_active = false;

    disconnect(m_simulator, &phys_Simulator::stateUpdated,
               this, &TelemetryFeedbackBridge::onSimStateUpdated);
    disconnect(m_simulator->lapTimer(), &phys_LapTimer::lapCompleted,
               this, nullptr);

    if (m_simulator->isRunning()) {
        m_simulator->stopSimulation();
    }

    TelemetryQmlBridge::instance()->stopSession();

    emit activeChanged();
    emit feedbackStopped();
    emit statusMessage("Telemetry feedback loop stopped");
}

void TelemetryFeedbackBridge::reset() {
    stopFeedback();
    m_simSamples.clear();
    m_currentSimSamples.clear();
    m_referenceLaps.clear();
    m_referenceLoaded = false;
    m_lapCount = 0;
    m_currentLapTime = 0;
    m_simProgress = 0;
    m_correlationPercent = 0;
    m_speedRMSE = 0;
    m_lateralGRMSE = 0;
    m_longitudinalGRMSE = 0;
    m_lapTimeSim = 0;
    m_lapTimeRef = 0;
    m_lapTimeDelta = 0;
    m_feedbackLapCount = 0;
    TelemetryQmlBridge::instance()->clearSession();
    m_simulator->reset();
    emit activeChanged();
    emit referenceChanged();
    emit metricsUpdated();
    emit lapCountChanged();
    emit statusMessage("Telemetry feedback reset");
}

bool TelemetryFeedbackBridge::loadReferenceTelemetry(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit statusMessage("Cannot load reference telemetry: " + path);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        emit statusMessage("Invalid reference telemetry format");
        return false;
    }

    QJsonObject root = doc.object();
    m_referenceLaps.clear();

    QJsonArray laps = root["laps"].toArray();
    for (const auto& lapVal : laps) {
        QJsonObject lapObj = lapVal.toObject();
        ReferenceLap ref;
        ref.lapNumber = lapObj["lapNumber"].toInt();
        ref.lapTime = lapObj["lapTime"].toDouble();

        QJsonArray samples = lapObj["samples"].toArray();
        for (const auto& sv : samples) {
            QJsonObject so = sv.toObject();
            TelemetrySample s;
            s.time = so["time"].toDouble();
            s.speed = so["speed"].toDouble();
            s.rpm = so["rpm"].toDouble();
            s.throttle = so["throttle"].toDouble();
            s.brake = so["brake"].toDouble();
            s.steering = so["steering"].toDouble();
            s.latG = so["latG"].toDouble();
            s.lonG = so["lonG"].toDouble();
            ref.samples.append(s);
        }
        m_referenceLaps.append(ref);
    }

    m_referenceLoaded = !m_referenceLaps.isEmpty();

    if (m_referenceLoaded) {
        m_lapTimeRef = m_referenceLaps.first().lapTime;
        emit statusMessage(QString("Loaded %1 reference laps from %2")
                          .arg(m_referenceLaps.size())
                          .arg(QFileInfo(path).fileName()));
    }

    emit referenceChanged();
    if (m_active) computeValidationMetrics();
    return m_referenceLoaded;
}

void TelemetryFeedbackBridge::clearReference() {
    m_referenceLaps.clear();
    m_referenceLoaded = false;
    m_lapTimeRef = 0;
    emit referenceChanged();
    emit metricsUpdated();
    emit statusMessage("Reference telemetry cleared");
}

// ============================================================================
// Simulator callbacks
// ============================================================================

void TelemetryFeedbackBridge::onSimStateUpdated(const SimulationState& state) {
    if (!m_active) return;

    m_currentLapTime = state.lapTime;
    m_simProgress = state.currentLapDistance;

    // Record sample at ~60Hz
    m_sampleTimeAccum += 0.016;
    if (m_sampleTimeAccum < 0.016) return;
    m_sampleTimeAccum = 0;

    TelemetrySample sample;
    sample.time = state.lapTime;
    sample.speed = state.speed * 3.6; // m/s to km/h
    sample.rpm = state.rpm;
    sample.throttle = m_simulator->getState().velocity.length() > 0.1 ? 1.0 : m_simulator->getState().rpm / m_simulator->maxRpm();
    sample.brake = m_simulator->getState().brake;
    sample.steering = m_simulator->getState().steering;
    sample.latG = m_simulator->getState().acceleration.x / 9.81;
    sample.lonG = m_simulator->getState().acceleration.z / 9.81;

    // Re-read actual values from simulator
    sample.throttle = 0;
    sample.brake = 0;
    {
        // Estimate throttle/brake from acceleration
        double accel = sample.lonG;
        if (accel > 0.05) {
            sample.throttle = std::min(1.0, accel * 2.0);
        } else if (accel < -0.05) {
            sample.brake = std::min(1.0, -accel * 3.0);
        }
    }

    m_currentSimSamples.append(sample);
    m_simSamples.append(sample);

    TelemetryQmlBridge::instance()->recordSample(
        sample.speed, sample.rpm, sample.throttle, sample.brake,
        sample.steering, m_simulator->getState().rpm > 1000 ? 1 : 0,
        sample.latG, sample.lonG);

    emit lapTimeUpdated(m_currentLapTime);
    emit sampleRecorded(sample.speed, sample.rpm, sample.throttle,
                        sample.brake, sample.steering, sample.latG, sample.lonG);
}

void TelemetryFeedbackBridge::onLapCompleted(double simLapTime) {
    m_feedbackLapCount++;
    m_lapCount = m_feedbackLapCount;
    m_lapTimeSim = simLapTime;

    if (m_referenceLoaded && m_feedbackLapCount <= m_referenceLaps.size()) {
        const auto& ref = m_referenceLaps[m_feedbackLapCount - 1];
        m_lapTimeRef = ref.lapTime;
        m_lapTimeDelta = simLapTime - ref.lapTime;
        computeValidationMetrics();
    }

    TelemetryQmlBridge::instance()->markLapStart();

    emit lapCompared(m_feedbackLapCount, simLapTime, m_lapTimeRef, m_lapTimeDelta);
    emit lapCountChanged();
    emit statusMessage(QString("Lap %1: %2s (ref: %3s, delta: %4s)")
                      .arg(m_feedbackLapCount)
                      .arg(simLapTime, 0, 'f', 3)
                      .arg(m_lapTimeRef, 0, 'f', 3)
                      .arg(m_lapTimeDelta, 0, 'f', 3));

    m_currentSimSamples.clear();
}

// ============================================================================
// Validation
// ============================================================================

void TelemetryFeedbackBridge::computeValidationMetrics() {
    if (m_simSamples.isEmpty() || m_referenceLaps.isEmpty()) return;

    // Find best matching reference lap
    int bestRefIdx = 0;
    double bestMatch = 1e9;
    for (int i = 0; i < m_referenceLaps.size(); ++i) {
        double diff = std::abs(m_referenceLaps[i].lapTime - m_lapTimeSim);
        if (diff < bestMatch) {
            bestMatch = diff;
            bestRefIdx = i;
        }
    }

    const auto& ref = m_referenceLaps[bestRefIdx];
    const auto& sim = m_simSamples;

    if (sim.isEmpty() || ref.samples.isEmpty()) return;

    // Resample both traces to common time base
    int n = std::min(sim.size(), ref.samples.size());
    if (n < 2) return;

    double sumSpeedErr2 = 0;
    double sumLatGErr2 = 0;
    double sumLongGErr2 = 0;
    double maxSpeedErr = 0;
    double maxLatGErr = 0;
    double maxLongGErr = 0;

    double sumSimSpeed = 0, sumRefSpeed = 0;
    double sumSimSpeed2 = 0, sumRefSpeed2 = 0;
    double sumSpeedProd = 0;

    double sumSimLatG = 0, sumRefLatG = 0;
    double sumSimLatG2 = 0, sumRefLatG2 = 0;
    double sumLatGProd = 0;

    double sumSimLongG = 0, sumRefLongG = 0;
    double sumSimLongG2 = 0, sumRefLongG2 = 0;
    double sumLongGProd = 0;

    for (int i = 0; i < n; ++i) {
        double refIdx = (double)i * ref.samples.size() / n;

        int r0 = (int)refIdx;
        int r1 = std::min<int>(r0 + 1, static_cast<int>(ref.samples.size()) - 1);
        double frac = refIdx - r0;

        auto lerp = [&](double v0, double v1) { return v0 + frac * (v1 - v0); };

        double refSpeed = lerp(ref.samples[r0].speed, ref.samples[r1].speed);
        double refLatG = lerp(ref.samples[r0].latG, ref.samples[r1].latG);
        double refLongG = lerp(ref.samples[r0].lonG, ref.samples[r1].lonG);

        double simSpeed = sim[i].speed;
        double simLatG = sim[i].latG;
        double simLongG = sim[i].lonG;

        double spdErr = simSpeed - refSpeed;
        double latErr = simLatG - refLatG;
        double lngErr = simLongG - refLongG;

        sumSpeedErr2 += spdErr * spdErr;
        sumLatGErr2 += latErr * latErr;
        sumLongGErr2 += lngErr * lngErr;

        maxSpeedErr = std::max(maxSpeedErr, std::abs(spdErr));
        maxLatGErr = std::max(maxLatGErr, std::abs(latErr));
        maxLongGErr = std::max(maxLongGErr, std::abs(lngErr));

        sumSimSpeed += simSpeed;    sumRefSpeed += refSpeed;
        sumSimSpeed2 += simSpeed * simSpeed; sumRefSpeed2 += refSpeed * refSpeed;
        sumSpeedProd += simSpeed * refSpeed;

        sumSimLatG += simLatG;    sumRefLatG += refLatG;
        sumSimLatG2 += simLatG * simLatG; sumRefLatG2 += refLatG * refLatG;
        sumLatGProd += simLatG * refLatG;

        sumSimLongG += simLongG;    sumRefLongG += refLongG;
        sumSimLongG2 += simLongG * simLongG; sumRefLongG2 += refLongG * refLongG;
        sumLongGProd += simLongG * refLongG;
    }

    m_speedRMSE = std::sqrt(sumSpeedErr2 / n);
    m_lateralGRMSE = std::sqrt(sumLatGErr2 / n);
    m_longitudinalGRMSE = std::sqrt(sumLongGErr2 / n);

    // R^2 correlation
    auto calcR2 = [n](double sumSim, double sumRef, double sumSim2, double sumRef2, double sumProd) {
        double varRef = (sumRef2 / n) - (sumRef / n) * (sumRef / n);
        double varSim = (sumSim2 / n) - (sumSim / n) * (sumSim / n);
        double cov = (sumProd / n) - (sumSim / n) * (sumRef / n);
        if (varRef < 1e-12 || varSim < 1e-12) return 0.0;
        return (cov * cov) / (varSim * varRef);
    };

    double r2Speed = calcR2(sumSimSpeed, sumRefSpeed, sumSimSpeed2, sumRefSpeed2, sumSpeedProd);
    double r2LatG = calcR2(sumSimLatG, sumRefLatG, sumSimLatG2, sumRefLatG2, sumLatGProd);
    double r2LongG = calcR2(sumSimLongG, sumRefLongG, sumSimLongG2, sumRefLongG2, sumLongGProd);

    m_correlationPercent = (r2Speed + r2LatG + r2LongG) / 3.0 * 100.0;

    emit metricsUpdated();
}

// ============================================================================
// Data access for QML
// ============================================================================

QVariantList TelemetryFeedbackBridge::getSimSpeedTrace() const {
    QVariantList trace;
    for (const auto& s : m_simSamples) {
        QVariantMap pt;
        pt["t"] = s.time;
        pt["v"] = s.speed;
        trace.append(pt);
    }
    return trace;
}

QVariantList TelemetryFeedbackBridge::getRefSpeedTrace() const {
    QVariantList trace;
    if (m_referenceLaps.isEmpty()) return trace;
    const auto& ref = m_referenceLaps.first();
    for (const auto& s : ref.samples) {
        QVariantMap pt;
        pt["t"] = s.time;
        pt["v"] = s.speed;
        trace.append(pt);
    }
    return trace;
}

QVariantList TelemetryFeedbackBridge::getSimThrottleTrace() const {
    QVariantList trace;
    for (const auto& s : m_simSamples) {
        QVariantMap pt;
        pt["t"] = s.time;
        pt["v"] = s.throttle;
        trace.append(pt);
    }
    return trace;
}

QVariantList TelemetryFeedbackBridge::getSimBrakeTrace() const {
    QVariantList trace;
    for (const auto& s : m_simSamples) {
        QVariantMap pt;
        pt["t"] = s.time;
        pt["v"] = s.brake;
        trace.append(pt);
    }
    return trace;
}

QVariantList TelemetryFeedbackBridge::getSimSteeringTrace() const {
    QVariantList trace;
    for (const auto& s : m_simSamples) {
        QVariantMap pt;
        pt["t"] = s.time;
        pt["v"] = s.steering;
        trace.append(pt);
    }
    return trace;
}

QVariantMap TelemetryFeedbackBridge::getValidationSummary() const {
    QVariantMap summary;
    summary["active"] = m_active;
    summary["referenceLoaded"] = m_referenceLoaded;
    summary["correlationPercent"] = m_correlationPercent;
    summary["speedRMSE"] = m_speedRMSE;
    summary["lateralGRMSE"] = m_lateralGRMSE;
    summary["longitudinalGRMSE"] = m_longitudinalGRMSE;
    summary["lapTimeSim"] = m_lapTimeSim;
    summary["lapTimeRef"] = m_lapTimeRef;
    summary["lapTimeDelta"] = m_lapTimeDelta;
    summary["lapCount"] = m_lapCount;
    summary["currentLapTime"] = m_currentLapTime;
    return summary;
}

QVariantList TelemetryFeedbackBridge::getSectorComparison() const {
    QVariantList sectors;
    if (m_referenceLaps.isEmpty()) return sectors;

    const auto& ref = m_referenceLaps.first();
    double refTotal = ref.lapTime;

    // Estimate sector times from reference
    double refSectors[3] = {0, 0, 0};
    if (ref.samples.size() > 10) {
        int split1 = ref.samples.size() / 3;
        int split2 = 2 * ref.samples.size() / 3;
        refSectors[0] = ref.samples[split1].time;
        refSectors[1] = ref.samples[split2].time - ref.samples[split1].time;
        refSectors[2] = refTotal - refSectors[0] - refSectors[1];
    }

    double simTotal = m_lapTimeSim;
    double simSectors[3] = {0, 0, 0};
    if (m_simSamples.size() > 10) {
        int split1 = m_simSamples.size() / 3;
        int split2 = 2 * m_simSamples.size() / 3;
        simSectors[0] = m_simSamples[split1].time;
        simSectors[1] = m_simSamples[split2].time - m_simSamples[split1].time;
        simSectors[2] = simTotal - simSectors[0] - simSectors[1];
    }

    for (int i = 0; i < 3; ++i) {
        QVariantMap sec;
        sec["sector"] = i + 1;
        sec["simTime"] = simSectors[i];
        sec["refTime"] = refSectors[i];
        sec["delta"] = simSectors[i] - refSectors[i];
        sectors.append(sec);
    }

    return sectors;
}

QVariantMap TelemetryFeedbackBridge::getLapAnalysis(int lapIndex) const {
    QVariantMap analysis;
    if (lapIndex < 0 || lapIndex >= m_referenceLaps.size()) return analysis;

    const auto& ref = m_referenceLaps[lapIndex];

    analysis["lapNumber"] = ref.lapNumber;
    analysis["refLapTime"] = ref.lapTime;
    analysis["simLapTime"] = m_lapTimeSim;
    analysis["lapTimeDelta"] = m_lapTimeDelta;

    // Compute average values
    double avgSpeed = 0, avgRpm = 0;
    double maxSpeed = 0, maxLatG = 0;
    for (const auto& s : m_simSamples) {
        avgSpeed += s.speed;
        avgRpm += s.rpm;
        maxSpeed = std::max(maxSpeed, s.speed);
        maxLatG = std::max(maxLatG, std::abs(s.latG));
    }
    if (!m_simSamples.empty()) {
        avgSpeed /= m_simSamples.size();
        avgRpm /= m_simSamples.size();
    }

    analysis["avgSpeed"] = avgSpeed;
    analysis["maxSpeed"] = maxSpeed;
    analysis["avgRpm"] = avgRpm;
    analysis["maxLateralG"] = maxLatG;
    analysis["correlation"] = m_correlationPercent;

    return analysis;
}

} // namespace ks
