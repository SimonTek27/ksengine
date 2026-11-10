#pragma once
#include <string>
#include <cstdio>
#include <mutex>
#include <vector>

namespace ks {
namespace engine {
namespace sys {

enum class LogLevel { Debug, Info, Warning, Error };

class LogManager {
public:
    static LogManager& instance() { static LogManager s; return s; }
    void log(LogLevel level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const char* tag = "I";
        if (level == LogLevel::Debug) tag = "D";
        else if (level == LogLevel::Warning) tag = "W";
        else if (level == LogLevel::Error) tag = "E";
        std::fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
        m_lines.push_back(msg);
        if (m_lines.size() > 2000) m_lines.erase(m_lines.begin(), m_lines.begin() + 500);
    }
    void info(const std::string& m) { log(LogLevel::Info, m); }
    void info(const std::string& category, const std::string& message) {
        info(category + ": " + message);
    }
    void debug(const std::string& category, const std::string& message) {
        log(LogLevel::Debug, category + ": " + message);
    }
    void warn(const std::string& m) { log(LogLevel::Warning, m); }
    void warn(const std::string& category, const std::string& message) {
        warn(category + ": " + message);
    }
    void error(const std::string& m) { log(LogLevel::Error, m); }
private:
    std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

} // namespace sys
} // namespace engine
} // namespace ks

namespace ks {
namespace engine {
namespace sys {

// Message helpers used by the LOG_* macros below: accept std::string and
// C-strings directly, and anything string-like exposing toStdString()
// (e.g. QString in the Qt build).
inline std::string logText(const std::string& s) { return s; }
inline std::string logText(const char* s) { return s ? std::string(s) : std::string(); }
template <typename T>
inline std::string logText(const T& v) { return v.toStdString(); }

} // namespace sys
} // namespace engine
} // namespace ks

#ifndef LOG_DEBUG
#define KS_LOG_AT(level, category, message)                                       \
    ::ks::engine::sys::LogManager::instance().log(                                \
        level, ::ks::engine::sys::logText(category) + ": " +                      \
                    ::ks::engine::sys::logText(message))
#define LOG_TRACE(category, message) \
    KS_LOG_AT(::ks::engine::sys::LogLevel::Debug, category, message)
#define LOG_DEBUG(category, message) \
    KS_LOG_AT(::ks::engine::sys::LogLevel::Debug, category, message)
#define LOG_INFO(category, message) \
    KS_LOG_AT(::ks::engine::sys::LogLevel::Info, category, message)
#define LOG_WARNING(category, message) \
    KS_LOG_AT(::ks::engine::sys::LogLevel::Warning, category, message)
#define LOG_ERROR(category, message) \
    KS_LOG_AT(::ks::engine::sys::LogLevel::Error, category, message)
#define LOG_CRITICAL(category, message) \
    KS_LOG_AT(::ks::engine::sys::LogLevel::Error, category, message)
#endif
