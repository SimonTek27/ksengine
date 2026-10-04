#ifndef PUBLISHVALIDATORBRIDGE_H
#define PUBLISHVALIDATORBRIDGE_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlEngine>

namespace ks {
namespace fileformat {

class PublishValidatorBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList issues READ issues NOTIFY issuesChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY issuesChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY issuesChanged)
    Q_PROPERTY(int infoCount READ infoCount NOTIFY issuesChanged)
    Q_PROPERTY(bool hasBlockingErrors READ hasBlockingErrors NOTIFY issuesChanged)
    Q_PROPERTY(QString lastModPath READ lastModPath NOTIFY validationComplete)

public:
    static PublishValidatorBridge* instance()
    {
        static PublishValidatorBridge s_instance;
        return &s_instance;
    }

    Q_INVOKABLE void validate(const QString& modPath, const QString& contentType);
    Q_INVOKABLE void cancel();

    QVariantList issues() const { return m_issues; }
    bool isRunning() const { return m_running; }
    int errorCount() const { return m_errorCount; }
    int warningCount() const { return m_warningCount; }
    int infoCount() const { return m_infoCount; }
    bool hasBlockingErrors() const { return m_hasBlocking; }
    QString lastModPath() const { return m_lastModPath; }

signals:
    void issuesChanged();
    void runningChanged();
    void validationComplete(bool success, const QString& message);
    void errorOccurred(const QString& error);

private:
    explicit PublishValidatorBridge(QObject* parent = nullptr);
    ~PublishValidatorBridge() override = default;
    Q_DISABLE_COPY(PublishValidatorBridge)

    void setRunning(bool running);
    void parseResult(const QString& jsonStr);

    QVariantList m_issues;
    bool m_running = false;
    int m_errorCount = 0;
    int m_warningCount = 0;
    int m_infoCount = 0;
    bool m_hasBlocking = false;
    QString m_lastModPath;
};

} // namespace fileformat
} // namespace ks

#endif // PUBLISHVALIDATORBRIDGE_H
