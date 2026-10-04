#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QStringList>
#include <QSettings>

namespace ks {

class TemplateManager : public QObject {
    Q_OBJECT

public:
    static TemplateManager* instance();
    virtual ~TemplateManager() {}
    
    virtual void initialize() = 0;
    virtual bool isInitialized() const = 0;
    
    virtual QJsonObject getTemplate(const QString& id) const = 0;
    virtual QVector<QString> getTemplateIds() const = 0;
    virtual QVector<QString> getTemplateIdsByType(const QString& type) const = 0;
    virtual QJsonObject createNewTemplate(const QString& name, const QString& description, const QString& type) = 0;
    
    virtual QString getTemplatePath(const QString& id) const = 0;
    virtual QString getTemplateDirectory(const QString& id) const = 0;
    
    virtual QString getProjectPathFromTemplate(const QString& templateId, const QString& outputDir) const = 0;
    virtual bool initializeTemplateProject(const QString& templateId, const QString& projectPath) = 0;
    
    virtual void setTemplateDirectory(const QString& directory) = 0;
    virtual QString getTemplateDirectory() const = 0;
    
    virtual bool hasTemplate(const QString& id) const = 0;
    virtual void removeTemplate(const QString& id) = 0;
    
    virtual bool exportTemplate(const QString& templateId, const QString& outputPath) = 0;
    virtual bool importTemplate(const QString& inputPath) = 0;
    
    struct TemplateInfo {
        QString id;
        QString name;
        QString description;
        QString type;
        QString version;
        QString author;
        QDateTime created;
        QDateTime lastModified;
        QVector<QString> modules;
        QVector<QString> files;
        QJsonObject metadata;
        QString thumbnailPath;
        int difficulty = 0;
        int estimatedTime = 0;
    };
    
    virtual TemplateInfo getTemplateInfo(const QString& id) const = 0;
    virtual QVector<TemplateInfo> getAllTemplateInfos() const = 0;
    
    signals:
        void templateAdded(const QString& id);
        void templateRemoved(const QString& id);
        void templateUpdated(const QString& id);

private:
    static TemplateManager* s_instance;
};

} // namespace ks
