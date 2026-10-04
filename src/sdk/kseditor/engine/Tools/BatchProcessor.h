#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QQueue>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QThread>

namespace ks {

enum class BatchOperation {
    CompressTextures,
    OptimizeMesh,
    GenerateLODs,
    ConvertFormat,
    ApplyMaterial,
    RenameFiles
};

struct BatchTaskItem {
    QString inputPath;
    QString outputPath;
    BatchOperation operation;
    QVariantMap options;
    bool completed = false;
    bool success = false;
    QString errorMessage;
};

class BatchProcessor : public QObject {
    Q_OBJECT

public:
    static BatchProcessor* instance();

    void addTask(const BatchTaskItem& task);
    void addFiles(const QStringList& files, BatchOperation operation, const QVariantMap& options = {});
    void startProcessing();
    void stopProcessing();
    void clearQueue();
    
    int pendingCount() const { return m_pendingQueue.size(); }
    int completedCount() const { return m_completedCount; }
    int failedCount() const { return m_failedCount; }

signals:
    void taskStarted(const QString& file);
    void taskCompleted(const QString& file, bool success);
    void progressChanged(int current, int total);
    void batchComplete(int success, int failed);

private slots:
    void processNextTask();

private:
    explicit BatchProcessor(QObject* parent = nullptr);
    static BatchProcessor* s_instance;

    QQueue<BatchTaskItem> m_pendingQueue;
    int m_completedCount = 0;
    int m_failedCount    = 0;
    bool m_isRunning     = false;

    bool processTask(const BatchTaskItem& task);
    bool compressTextures(const BatchTaskItem& task);
    bool optimizeMesh(const BatchTaskItem& task);
    bool generateLODs(const BatchTaskItem& task);
    bool convertFormat(const BatchTaskItem& task);
    bool renameFiles(const BatchTaskItem& task);
    bool applyMaterial(const BatchTaskItem& task);
};

}