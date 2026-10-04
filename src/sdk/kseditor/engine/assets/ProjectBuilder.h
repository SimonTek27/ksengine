#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>

/**
 * @brief Handles async project build: validates, copies assets to AC content dir.
 */
class ProjectBuilder : public QObject
{
    Q_OBJECT

public:
    explicit ProjectBuilder(QObject* parent = nullptr);
    ~ProjectBuilder() override;

    void build(const QString& projectPath, const QString& simPath);
    void cancel();
    bool isRunning() const { return m_running.load(); }

signals:
    void progressUpdated(int percent);
    void buildComplete(bool success, const QString& message);
    void logMessage(const QString& message);

private:
    void doBuild(const QString& projectPath, const QString& simPath);
    bool validateProject(const QString& projectPath, QString& error);
    bool copyAssets(const QString& projectPath, const QString& simPath,
                    int basePercent, int endPercent);

    QThread*          m_thread  = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};
};
