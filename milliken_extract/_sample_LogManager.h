#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>
#include <QDir>
#include <QVector>
#include <QMap>
#include <QMessageLogContext>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QQueue>

enum class LogLevel {
    Trace = -1,
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4
};

Q_DECLARE_METATYPE(LogLevel)

struct LogEntry {
    QDateTime timestamp;
    LogLevel level;
    QString category;
    QString message;
    QString threadName;
    quint64 threadId;
    QString file;
    int line = 0;
    QString function;
};

class LogManager : public QObject
{
    Q_OBJECT

public:
    static LogManager& instance();

    // File logging
    void setLogFile(const QString& path);
    void setMinLevel(LogLevel level);
    void setMaxFileSize(qint64 maxBytes);
    void setMaxBackupFiles(int maxFiles);

    // Core logging
    void log(LogLevel level, const QString& category, const QString& message,
             const QString& file = QString(), int line = 0, const QString& function = QString());

    void trace(const QString& category, const QString& message,
               const QString& file = QString(), int line = 0, const QString& function = QString());
    void debug(const QString& category, const QString& message,
               const QString& file = QString(), int line = 0, const QString& function = QString());
    void info(const QString& category, const QString& message,
              const QString& file = QString(), int line = 0, const QString& function = QString());
    void warning(const QString& category, const QString& message,
                 const QString& file = QString(), int line = 0, const QString& function = QString());
    void error(const QString& category, const QString& message,
               const QString& file = QString(), int line = 0, const QString& function = QString());
    void critical(const QString& category, const QString& message,
                  const QString& file = QString(), int line = 0, const QString& function = QString());

    void flush();

    // Qt integration
    void installQtMessageHandler();
    void setQtMessageHandlerEnabled(bool enabled);
    bool isQtMessageHandlerEnabled() const { return m_qtHandlerInstalled; }

    // Entry retrieval
    QVector<LogEntry> getEntries(LogLevel minLevel = LogLevel::Debug) const;
    void clearEntries();
    void setMaxEntries(int max) { m_maxEntries = max; }

    // Per-module log levels
    void setModuleLogLevel(const QString& module, LogLevel level);
    LogLevel getModuleLogLevel(const QString& module) const;
    bool isModuleEnabled(const QString& module, LogLevel level) const;

    QString getLogFilePath() const { return m_logFile.fileName(); }

    // Remote logging aggregation
    void addRemoteEndpoint(const QString& id, const QString& host, quint16 port,
                           bool useTcp = true, bool useJson = true);
    void removeRemoteEndpoint(const QString& id);
    void setRemoteLogLevel(const QString& id, LogLevel level);
    void setRemoteEnabled(const QString& id, bool enabled);
    QMap<QString, bool> getRemoteEndpoints() const;

    // Batch send for performance
    void setBatchSize(int size) { m_batchSize = size; }
    void setFlushInterval(int ms) { m_flushInterval = ms; }

signals:
    void logMessage(LogLevel level, const QString& category, const QString& message, const QDateTime& timestamp);
    void messageLogged(const LogEntry& entry);
    void entriesCleared();
    void remoteSendFailed(const QString& endpointId, const QString& error);

private:
    explicit LogManager(QObject* parent = nullptr);
    ~LogManager();
    Q_DISABLE_COPY(LogManager)

    void writeToFile(const QString& message);
    QString levelToString(LogLevel level) const;
    void rotateLogFile();
    void appendEntry(const LogEntry& entry);

    void sendToRemote(const LogEntry& entry);
    void flushRemoteBuffer();
    void connectToEndpoint(const QString& id);

    struct RemoteEndpoint {
        QString id;
        QString host;
        quint16 port;
        bool useTcp = true;
        bool useJson = true;
        LogLevel minLevel = LogLevel::Info;
        bool enabled = true;
        QTcpSocket* tcpSocket = nullptr;
        QUdpSocket* udpSocket = nullptr;
        bool connected = false;
    };

    QFile m_logFile;
    QTextStream m_stream;
    mutable QMutex m_mutex;
    LogLevel m_minLevel = LogLevel::Info;
    qint64 m_maxFileSize = 10 * 1024 * 1024;
    int m_maxBackupFiles = 5;
    bool m_initialized = false;
    bool m_qtHandlerInstalled = false;

    QVector<LogEntry> m_entries;
    int m_maxEntries = 5000;

    QMap<QString, LogLevel> m_moduleLevels;

    // Remote logging
    QMap<QString, RemoteEndpoint> m_remoteEndpoints;
    QQueue<LogEntry> m_remoteBuffer;
    int m_batchSize = 100;
    int m_flushInterval = 1000;
    QTimer* m_flushTimer = nullptr;
    mutable QMutex m_remoteMutex;
};

#ifndef LOG_DEBUG
// 5-argument macros (explicit file/line/func)
#define LOG_TRACE_D(category, message, file, line, func) \
    LogManager::instance().trace(category, message, file, line, func)
#define LOG_DEBUG_D(category, message, file, line, func) \
    LogManager::instance().debug(category, message, file, line, func)
#define LOG_INFO_D(category, message, file, line, func) \
    LogManager::instance().info(category, message, file, line, func)
#define LOG_WARNING_D(category, message, file, line, func) \
    LogManager::instance().warning(category, message, file, line, func)
#define LOG_ERROR_D(category, message, file, line, func) \
    LogManager::instance().error(category, message, file, line, func)
#define LOG_CRITICAL_D(category, message, file, line, func) \
    LogManager::instance().critical(category, message, file, line, func)

// Convenience macros with automatic source location
#define LOG_TRACE(category, message) LOG_TRACE_D(category, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_DEBUG(category, message) LOG_DEBUG_D(category, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(category, message) LOG_INFO_D(category, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING(category, message) LOG_WARNING_D(category, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(category, message) LOG_ERROR_D(category, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_CRITICAL(category, message) LOG_CRITICAL_D(category, message, __FILE__, __LINE__, __FUNCTION__)
#endif
