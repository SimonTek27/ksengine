#pragma once

#include <QTimer>
#include <QDateTime>
#include <atomic>
#include <thread>

// Watchdog that detects when the GUI thread stops responding and offers the
// user a native dialog with the choices: Relaunch / Export logs /
// Keep waiting / Terminate program.
//
// A QTimer running on the GUI thread keeps refreshing a heartbeat timestamp.
// A dedicated watchdog thread polls that timestamp; when it goes stale the
// main thread is considered hung and a native dialog is shown. The dialog is
// created on the watchdog thread (with its own message loop) so it stays
// responsive even while the main thread is fully blocked.
class HangMonitor
{
public:
    static HangMonitor& instance();

    void start();
    void stop();

    // True when the GUI thread heartbeat is currently fresh (app recovered).
    bool hasRecovered() const;

private:
    HangMonitor() = default;
    ~HangMonitor();
    HangMonitor(const HangMonitor&) = delete;
    HangMonitor& operator=(const HangMonitor&) = delete;

    void watchdogLoop();
    void showHangDialog();

    QTimer* m_heartbeatTimer = nullptr;
    std::atomic<qint64> m_lastHeartbeatMs {0};
    std::atomic<bool> m_running {false};
    std::thread m_watchdog;

    static constexpr qint64 kHeartbeatIntervalMs = 500;
    static constexpr qint64 kHangThresholdMs = 5000;
    static constexpr qint64 kWaitCooldownMs = 15000;
};
