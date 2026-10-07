#include "PhysicsLogger.h"

#include <ctime>

namespace ks {
namespace physics {

PhysicsLogger& PhysicsLogger::instance() {
    static PhysicsLogger s;
    return s;
}

const char* PhysicsLogger::levelName(Level l) {
    switch (l) {
    case Level::Trace: return "TRACE";
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
    case Level::Critical: return "CRIT";
    }
    return "?";
}

void PhysicsLogger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) m_file.close();
    if (!path.empty()) m_file.open(path, std::ios::app);
}

void PhysicsLogger::log(Level level, const std::string& category, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(m_level)) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    using clock = std::chrono::system_clock;
    auto t = clock::to_time_t(clock::now());
    char tbuf[32];
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&t));

    char line[1024];
    std::snprintf(line, sizeof(line), "[%s][%s][%s] %s\n",
                  tbuf, levelName(level), category.c_str(), message.c_str());

    if (m_console) {
        std::fputs(line, level >= Level::Error ? stderr : stdout);
    }
    if (m_file.is_open()) {
        m_file << line;
        m_file.flush();
    }
    if (onLog) onLog(level, category, message);
}

void PhysicsLogger::logf(Level level, const char* category, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(level, category ? category : LogCategory::GENERAL, buf);
}

} // namespace physics
} // namespace ks
