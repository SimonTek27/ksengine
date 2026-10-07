#pragma once

/**
 * @file PhysicsLogger.h
 * @brief Logging utilities for physics simulation
 * @copyright KS Physics Engine
 */

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>

namespace ks {
namespace physics {

// ============================================================================
// Log Categories
// ============================================================================

namespace LogCategory {
    constexpr const char* GENERAL = "General";
    constexpr const char* ENGINE = "Engine";
    constexpr const char* TIRE = "Tire";
    constexpr const char* AERO = "Aero";
    constexpr const char* CHASSIS = "Chassis";
    constexpr const char* WEATHER = "Weather";
    constexpr const char* DAMAGE = "Damage";
    constexpr const char* FUEL = "Fuel";
    constexpr const char* STRATEGY = "Strategy";
    constexpr const char* VALIDATION = "Validation";
    constexpr const char* PERFORMANCE = "Performance";
    constexpr const char* INTEGRATION = "Integration";
}

// ============================================================================
// Physics Logger
// ============================================================================

/**
 * @brief Thread-safe logger for physics simulation
 */
class PhysicsLogger : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief Log levels
     */
    enum class Level {
        Trace,    ///< Detailed trace information
        Debug,    ///< Debug information
        Info,     ///< General information
        Warning,  ///< Warning messages
        Error,    ///< Error messages
        Critical  ///< Critical errors
    };
    Q_ENUM(Level)
    
    /**
     * @brief Get singleton instance
     */
    static PhysicsLogger& instance();
    
    /**
     * @brief Set log file path
     * @param path Path to log file
     */
    void setLogFile(const QString& path);
    
    /**
     * @brief Set minimum log level
     * @param level Minimum level to log
     */
    void setLevel(Level level);
    
    /**
     * @brief Get current log level
     */
    Level level() const { return m_level; }
    
    /**
     * @brief Log a message
     * @param level Message level
     * @param message Message content
     */
    void log(Level level, const QString& message);
    
    /**
     * @brief Log a categorized message
     * @param level Message level
     * @param category Category name
     * @param message Message content
     */
    void log(Level level, const QString& category, const QString& message);
    
    /**
     * @brief Log a formatted message
     * @param level Message level
     * @param format Format string
     * @param args Arguments
     */
    template<typename... Args>
    void log(Level level, const QString& format, const Args&... args) {
        log(level, QString(format).arg(args...));
    }
    
    /**
     * @brief Log a formatted categorized message
     */
    template<typename... Args>
    void log(Level level, const QString& category, const QString& format, const Args&... args) {
        log(level, category, QString(format).arg(args...));
    }
    
    // Convenience methods
    void trace(const QString& msg) { log(Level::Trace, msg); }
    void debug(const QString& msg) { log(Level::Debug, msg); }
    void info(const QString& msg) { log(Level::Info, msg); }
    void warning(const QString& msg) { log(Level::Warning, msg); }
    void error(const QString& msg) { log(Level::Error, msg); }
    void critical(const QString& msg) { log(Level::Critical, msg); }
    
    void trace(const QString& cat, const QString& msg) { log(Level::Trace, cat, msg); }
    void debug(const QString& cat, const QString& msg) { log(Level::Debug, cat, msg); }
    void info(const QString& cat, const QString& msg) { log(Level::Info, cat, msg); }
    void warning(const QString& cat, const QString& msg) { log(Level::Warning, cat, msg); }
    void error(const QString& cat, const QString& msg) { log(Level::Error, cat, msg); }
    void critical(const QString& cat, const QString& msg) { log(Level::Critical, cat, msg); }

    template<typename... Args>
    void trace(const QString& fmt, const Args&... args) { log(Level::Trace, fmt, args...); }
    template<typename... Args>
    void debug(const QString& fmt, const Args&... args) { log(Level::Debug, fmt, args...); }
    template<typename... Args>
    void info(const QString& fmt, const Args&... args) { log(Level::Info, fmt, args...); }
    template<typename... Args>
    void warning(const QString& fmt, const Args&... args) { log(Level::Warning, fmt, args...); }
    template<typename... Args>
    void error(const QString& fmt, const Args&... args) { log(Level::Error, fmt, args...); }
    
    /**
     * @brief Flush log buffer
     */
    void flush();
    
    /**
     * @brief Check if logging is enabled
     */
    bool isEnabled() const { return m_enabled; }
    
signals:
    /**
     * @brief Emitted when a message is logged
     * @param level Message level
     * @param message Message content
     * @param timestamp Timestamp
     */
    void logMessage(Level level, const QString& message, const QString& timestamp);
    
private:
    PhysicsLogger();
    ~PhysicsLogger();
    
    PhysicsLogger(const PhysicsLogger&) = delete;
    PhysicsLogger& operator=(const PhysicsLogger&) = delete;
    
    QString levelToString(Level level) const;
    QString levelColor(Level level) const;
    QString currentTimestamp() const;
    void writeToFile(const QString& formatted);
    
    QFile m_logFile;
    QTextStream m_stream;
    QMutex m_mutex;
    Level m_level = Level::Info;
    bool m_enabled = true;
    bool m_fileOpen = false;
};

// ============================================================================
// Convenience Macros (category + variadic message format)
// ============================================================================

#define PHYSICS_LOG(level, cat, ...) ks::physics::PhysicsLogger::instance().log(level, cat, __VA_ARGS__)
#define PHYSICS_LOG_CAT(level, cat, ...) ks::physics::PhysicsLogger::instance().log(level, cat, __VA_ARGS__)
#define PHYSICS_TRACE(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Trace, cat, __VA_ARGS__)
#define PHYSICS_DEBUG(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Debug, cat, __VA_ARGS__)
#define PHYSICS_INFO(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Info, cat, __VA_ARGS__)
#define PHYSICS_WARN(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Warning, cat, __VA_ARGS__)
#define PHYSICS_ERROR(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Error, cat, __VA_ARGS__)

} // namespace physics
} // namespace ks

// ============================================================================
// Convenience Macros
// ============================================================================

#ifndef PHYSICS_LOG
#define PHYSICS_LOG(level, cat, ...) \
    ks::physics::PhysicsLogger::instance().log(level, cat, __VA_ARGS__)
#endif

#ifndef PHYSICS_LOG_CAT
#define PHYSICS_LOG_CAT(level, category, ...) \
    ks::physics::PhysicsLogger::instance().log(level, category, __VA_ARGS__)
#endif

#ifndef PHYSICS_TRACE
#define PHYSICS_TRACE(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Trace, cat, __VA_ARGS__)
#endif
#ifndef PHYSICS_DEBUG
#define PHYSICS_DEBUG(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Debug, cat, __VA_ARGS__)
#endif
#ifndef PHYSICS_INFO
#define PHYSICS_INFO(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Info, cat, __VA_ARGS__)
#endif
#ifndef PHYSICS_WARN
#define PHYSICS_WARN(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Warning, cat, __VA_ARGS__)
#endif
#ifndef PHYSICS_ERROR
#define PHYSICS_ERROR(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Error, cat, __VA_ARGS__)
#endif
#ifndef PHYSICS_CRITICAL
#define PHYSICS_CRITICAL(cat, ...) ks::physics::PhysicsLogger::instance().log(ks::physics::PhysicsLogger::Level::Critical, cat, __VA_ARGS__)
#endif