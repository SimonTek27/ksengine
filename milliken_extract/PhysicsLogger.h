#pragma once

/**
 * @file PhysicsLogger.h
 * @brief Thread-safe physics logger — Qt-free (stdio + callbacks)
 */

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

namespace ks {
namespace physics {

namespace LogCategory {
    constexpr const char* GENERAL = "General";
    constexpr const char* ENGINE = "Engine";
    constexpr const char* TIRE = "Tire";
    constexpr const char* AERO = "Aero";
    constexpr const char* CHASSIS = "Chassis";
    constexpr const char* WEATHER = "Weather";
    constexpr const char* DAMAGE = "Damage";
    constexpr const char* FUEL = "Fuel";
    constexpr const char* PERFORMANCE = "Performance";
}

class PhysicsLogger {
public:
    enum class Level { Trace, Debug, Info, Warning, Error, Critical };

    static PhysicsLogger& instance();

    void setLogFile(const std::string& path);
    void setLevel(Level level) { m_level = level; }
    Level level() const { return m_level; }
    void setConsoleEnabled(bool e) { m_console = e; }

    void log(Level level, const std::string& category, const std::string& message);
    void logf(Level level, const char* category, const char* fmt, ...);

    std::function<void(Level, const std::string& category, const std::string& msg)> onLog;

private:
    PhysicsLogger() = default;
    static const char* levelName(Level l);

    std::mutex m_mutex;
    Level m_level = Level::Info;
    bool m_console = true;
    std::ofstream m_file;
};

// Compatibility macros (no Qt)
#define PHYSICS_LOG(level, cat, msg) \
    ::ks::physics::PhysicsLogger::instance().log(::ks::physics::PhysicsLogger::Level::level, cat, msg)
#define PHYSICS_INFO(cat, msg) PHYSICS_LOG(Info, cat, msg)
#define PHYSICS_WARN(cat, msg) PHYSICS_LOG(Warning, cat, msg)
#define PHYSICS_ERROR(cat, msg) PHYSICS_LOG(Error, cat, msg)

} // namespace physics
} // namespace ks
