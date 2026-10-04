#include "LogManager.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDebug>
#include <iostream>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>

static QtMessageHandler s_prevQtHandler = nullptr;

static void ksMessageHandler(QtMsgType type,
                               const QMessageLogContext& ctx,
                               const QString& msg)
{
    LogManager& lm = LogManager::instance();
    if (lm.isQtMessageHandlerEnabled())
        lm.log(LogLevel::Debug, "Qt", msg, ctx.file ? ctx.file : "", ctx.line, ctx.function ? ctx.function : "");

    if (s_prevQtHandler)
        s_prevQtHandler(type, ctx, msg);
}

LogManager& LogManager::instance()
{
    static LogManager instance;
    return instance;
}

LogManager::LogManager(QObject* parent)
    : QObject(parent)
    , m_stream(&m_logFile)
{
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);

    QString logPath = logDir + "/kseditor.log";
    setLogFile(logPath);

    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &LogManager::flushRemoteBuffer);
    m_flushTimer->start(m_flushInterval);

    m_initialized = true;
}

LogManager::~LogManager()
{
    flush();
    if (m_flushTimer) m_flushTimer->stop();
    
    QMutexLocker locker(&m_remoteMutex);
    for (auto& endpoint : m_remoteEndpoints) {
        if (endpoint.tcpSocket) {
            endpoint.tcpSocket->disconnectFromHost();
            endpoint.tcpSocket->deleteLater();
        }
        if (endpoint.udpSocket) {
            endpoint.udpSocket->close();
            endpoint.udpSocket->deleteLater();
        }
    }
}

void LogManager::setLogFile(const QString& path)
{
    QMutexLocker locker(&m_mutex);

    if (m_logFile.isOpen()) {
        m_stream.flush();
        m_logFile.close();
    }

    m_logFile.setFileName(path);

    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open log file:" << path;
        return;
    }

    m_stream.setDevice(&m_logFile);
}

void LogManager::setMinLevel(LogLevel level)
{
    m_minLevel = level;
}

void LogManager::setMaxFileSize(qint64 maxBytes)
{
    m_maxFileSize = maxBytes;
}

void LogManager::setMaxBackupFiles(int maxFiles)
{
    m_maxBackupFiles = maxFiles;
}

void LogManager::log(LogLevel level, const QString& category, const QString& message,
                     const QString& file, int line, const QString& function)
{
    if (level < m_minLevel || !isModuleEnabled(category, level)) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString levelStr = levelToString(level);
    QString formatted = QString("[%1] [%2] [%3] %4")
                            .arg(timestamp)
                            .arg(levelStr)
                            .arg(category)
                            .arg(message);

    writeToFile(formatted);

    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.threadName = QThread::currentThread()->objectName();
    entry.threadId = reinterpret_cast<quint64>(QThread::currentThreadId());
    entry.file = file;
    entry.line = line;
    entry.function = function;
    appendEntry(entry);

    emit logMessage(level, category, message, QDateTime::currentDateTime());
    emit messageLogged(entry);

    // Buffer for remote sending
    {
        QMutexLocker remoteLock(&m_remoteMutex);
        m_remoteBuffer.enqueue(entry);
        if (m_remoteBuffer.size() >= m_batchSize) {
            flushRemoteBuffer();
        }
    }
}

void LogManager::trace(const QString& category, const QString& message,
                       const QString& file, int line, const QString& function)
{
    log(LogLevel::Trace, category, message, file, line, function);
}

void LogManager::debug(const QString& category, const QString& message,
                       const QString& file, int line, const QString& function)
{
    log(LogLevel::Debug, category, message, file, line, function);
}

void LogManager::info(const QString& category, const QString& message,
                      const QString& file, int line, const QString& function)
{
    log(LogLevel::Info, category, message, file, line, function);
}

void LogManager::warning(const QString& category, const QString& message,
                         const QString& file, int line, const QString& function)
{
    log(LogLevel::Warning, category, message, file, line, function);
}

void LogManager::error(const QString& category, const QString& message,
                       const QString& file, int line, const QString& function)
{
    log(LogLevel::Error, category, message, file, line, function);
}

void LogManager::critical(const QString& category, const QString& message,
                          const QString& file, int line, const QString& function)
{
    log(LogLevel::Critical, category, message, file, line, function);
}

void LogManager::flush()
{
    QMutexLocker locker(&m_mutex);
    m_stream.flush();
}

void LogManager::installQtMessageHandler()
{
    s_prevQtHandler = qInstallMessageHandler(ksMessageHandler);
    m_qtHandlerInstalled = true;
}

void LogManager::setQtMessageHandlerEnabled(bool enabled)
{
    m_qtHandlerInstalled = enabled;
}

QVector<LogEntry> LogManager::getEntries(LogLevel minLevel) const
{
    QMutexLocker lock(&m_mutex);
    if (minLevel == LogLevel::Debug || minLevel == LogLevel::Trace)
        return m_entries;

    QVector<LogEntry> out;
    for (const auto& e : m_entries)
        if (static_cast<int>(e.level) >= static_cast<int>(minLevel))
            out << e;
    return out;
}

void LogManager::clearEntries()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    emit entriesCleared();
}

void LogManager::setModuleLogLevel(const QString& module, LogLevel level)
{
    m_moduleLevels[module] = level;
}

LogLevel LogManager::getModuleLogLevel(const QString& module) const
{
    return m_moduleLevels.value(module, LogLevel::Debug);
}

bool LogManager::isModuleEnabled(const QString& module, LogLevel level) const
{
    if (!m_moduleLevels.contains(module))
        return true;
    return static_cast<int>(level) >= static_cast<int>(m_moduleLevels.value(module));
}

void LogManager::appendEntry(const LogEntry& entry)
{
    if (m_entries.size() >= m_maxEntries)
        m_entries.removeFirst();
    m_entries.append(entry);
}

void LogManager::writeToFile(const QString& message)
{
    if (m_logFile.size() >= m_maxFileSize) {
        m_stream.flush();
        rotateLogFile();
    }

    m_stream << message << "\n";
    m_stream.flush();
}

QString LogManager::levelToString(LogLevel level) const
{
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO ";
        case LogLevel::Warning:  return "WARN ";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "FATAL";
        default:                 return "???? ";
    }
}

void LogManager::rotateLogFile()
{
    if (!m_logFile.isOpen()) return;

    m_logFile.close();

    QString basePath = m_logFile.fileName();

    QString oldest = QString("%1.%2").arg(basePath).arg(m_maxBackupFiles);
    QFile::remove(oldest);

    for (int i = m_maxBackupFiles - 1; i >= 1; --i) {
        QString oldName = QString("%1.%2").arg(basePath).arg(i);
        QString newName = QString("%1.%3").arg(basePath).arg(i + 1);
        if (!QFile::rename(oldName, newName)) {
            qWarning("LogManager: failed to rename %s to %s", qPrintable(oldName), qPrintable(newName));
        }
    }

    if (!QFile::rename(basePath, basePath + ".1")) {
        qWarning("LogManager: failed to rename %s to %s.1", qPrintable(basePath), qPrintable(basePath));
    }

    m_logFile.setFileName(basePath);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning("LogManager: failed to reopen log file after rotation: %s", qPrintable(basePath));
        return;
    }
    m_stream.setDevice(&m_logFile);
}

// Remote logging
void LogManager::addRemoteEndpoint(const QString& id, const QString& host, quint16 port,
                                   bool useTcp, bool useJson)
{
    QMutexLocker locker(&m_remoteMutex);
    
    if (m_remoteEndpoints.contains(id)) return;
    
    RemoteEndpoint ep;
    ep.id = id;
    ep.host = host;
    ep.port = port;
    ep.useTcp = useTcp;
    ep.useJson = useJson;
    ep.enabled = true;
    
    if (useTcp) {
        ep.tcpSocket = new QTcpSocket(this);
        connect(ep.tcpSocket, &QTcpSocket::connected, this, [this, id]() {
            QMutexLocker lock(&m_remoteMutex);
            if (m_remoteEndpoints.contains(id)) {
                m_remoteEndpoints[id].connected = true;
            }
        });
        connect(ep.tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                this, [this, id](QAbstractSocket::SocketError err) {
            QMutexLocker lock(&m_remoteMutex);
            if (m_remoteEndpoints.contains(id)) {
                m_remoteEndpoints[id].connected = false;
                emit remoteSendFailed(id, m_remoteEndpoints[id].tcpSocket->errorString());
            }
        });
        connect(ep.tcpSocket, &QTcpSocket::disconnected, this, [this, id]() {
            QMutexLocker lock(&m_remoteMutex);
            if (m_remoteEndpoints.contains(id)) {
                m_remoteEndpoints[id].connected = false;
            }
        });
    } else {
        ep.udpSocket = new QUdpSocket(this);
    }
    
    m_remoteEndpoints[id] = ep;
    connectToEndpoint(id);
}

void LogManager::removeRemoteEndpoint(const QString& id)
{
    QMutexLocker locker(&m_remoteMutex);
    
    if (!m_remoteEndpoints.contains(id)) return;
    
    auto& ep = m_remoteEndpoints[id];
    if (ep.tcpSocket) {
        ep.tcpSocket->disconnectFromHost();
        ep.tcpSocket->deleteLater();
    }
    if (ep.udpSocket) {
        ep.udpSocket->close();
        ep.udpSocket->deleteLater();
    }
    
    m_remoteEndpoints.remove(id);
}

void LogManager::setRemoteLogLevel(const QString& id, LogLevel level)
{
    QMutexLocker locker(&m_remoteMutex);
    if (m_remoteEndpoints.contains(id)) {
        m_remoteEndpoints[id].minLevel = level;
    }
}

void LogManager::setRemoteEnabled(const QString& id, bool enabled)
{
    QMutexLocker locker(&m_remoteMutex);
    if (m_remoteEndpoints.contains(id)) {
        m_remoteEndpoints[id].enabled = enabled;
    }
}

QMap<QString, bool> LogManager::getRemoteEndpoints() const
{
    QMutexLocker locker(&m_remoteMutex);
    QMap<QString, bool> result;
    for (auto it = m_remoteEndpoints.constBegin(); it != m_remoteEndpoints.constEnd(); ++it) {
        result[it.key()] = it->connected && it->enabled;
    }
    return result;
}

void LogManager::connectToEndpoint(const QString& id)
{
    if (!m_remoteEndpoints.contains(id)) return;
    
    auto& ep = m_remoteEndpoints[id];
    if (!ep.enabled) return;
    
    if (ep.useTcp && ep.tcpSocket) {
        if (ep.tcpSocket->state() == QAbstractSocket::UnconnectedState) {
            ep.tcpSocket->connectToHost(ep.host, ep.port);
        }
    }
}

void LogManager::flushRemoteBuffer()
{
    QMutexLocker locker(&m_remoteMutex);
    
    if (m_remoteBuffer.isEmpty()) return;
    
    QVector<LogEntry> toSend;
    while (!m_remoteBuffer.isEmpty() && toSend.size() < m_batchSize) {
        toSend.append(m_remoteBuffer.dequeue());
    }
    
    for (const auto& entry : toSend) {
        sendToRemote(entry);
    }
}

void LogManager::sendToRemote(const LogEntry& entry)
{
    for (auto it = m_remoteEndpoints.begin(); it != m_remoteEndpoints.end(); ++it) {
        auto& ep = it.value();
        if (!ep.enabled || static_cast<int>(entry.level) < static_cast<int>(ep.minLevel)) continue;
        
        if (ep.useTcp && ep.tcpSocket && ep.tcpSocket->state() == QAbstractSocket::ConnectedState) {
            QJsonObject obj;
            obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
            obj["level"] = static_cast<int>(entry.level);
            obj["category"] = entry.category;
            obj["message"] = entry.message;
            obj["thread"] = entry.threadName;
            obj["file"] = entry.file;
            obj["line"] = entry.line;
            obj["function"] = entry.function;
            
            QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
            ep.tcpSocket->write(data);
        } else if (!ep.useTcp && ep.udpSocket) {
            QJsonObject obj;
            obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
            obj["level"] = static_cast<int>(entry.level);
            obj["category"] = entry.category;
            obj["message"] = entry.message;
            
            QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            ep.udpSocket->writeDatagram(data, QHostAddress(ep.host), ep.port);
        }
    }
}
