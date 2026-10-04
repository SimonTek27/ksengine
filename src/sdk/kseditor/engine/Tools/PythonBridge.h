#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QMap>
#include <QVector>
#include <QProcess>

namespace ks {

class PythonBridge : public QObject {
    Q_OBJECT

public:
    static PythonBridge* instance();

    Q_INVOKABLE bool isAvailable();
    Q_INVOKABLE QString getVersion();
    Q_INVOKABLE QString getPythonPath();

    Q_INVOKABLE QVariant evaluate(const QString& script);
    Q_INVOKABLE bool executeFile(const QString& path);
    Q_INVOKABLE bool reloadModules();

    Q_INVOKABLE void setVariable(const QString& name, const QVariant& value);
    Q_INVOKABLE QVariant getVariable(const QString& name);
    Q_INVOKABLE void clearVariables();

    Q_INVOKABLE QStringList getAutoComplete(const QString& prefix);
    Q_INVOKABLE QStringList getModuleList();
    Q_INVOKABLE QStringList getFunctionList(const QString& module);

    Q_INVOKABLE void addSearchPath(const QString& path);
    Q_INVOKABLE void clearSearchPaths();
    Q_INVOKABLE QStringList getSearchPaths();

    Q_INVOKABLE QString getDocumentation(const QString& function);
    Q_INVOKABLE QString getSignature(const QString& function);

    Q_INVOKABLE void setMeshData(const QVariant& meshData);
    Q_INVOKABLE QVariant getMeshData();
    Q_INVOKABLE void setSceneNodes(const QVariant& nodes);
    Q_INVOKABLE QVariant getSceneNodes();

    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariant getSetting(const QString& key);

    Q_INVOKABLE QString runMacro(const QString& macroName, const QVariantList& args);
    Q_INVOKABLE QStringList getMacroList();

    signals:
        void outputGenerated(const QString& output);
        void errorOccurred(const QString& error);
        void variableChanged(const QString& name, const QVariant& value);
        void moduleLoaded(const QString& module);

private:
    PythonBridge(QObject* parent = nullptr);
    ~PythonBridge();
    Q_DISABLE_COPY(PythonBridge)

    static PythonBridge* s_instance;

    QMap<QString, QVariant> m_variables;
    QVector<QString> m_searchPaths;
    QVariant m_meshData;
    QVariant m_sceneNodes;
    bool m_pythonAvailable;
    QString m_pythonPath;
    QProcess* m_process = nullptr;
    QByteArray m_processOutput;
};

class ScriptMacro : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ getName)
    Q_PROPERTY(QString description READ getDescription)
    Q_PROPERTY(QString category READ getCategory)

public:
    ScriptMacro(const QString& name, const QString& script, const QString& category = "Default")
        : m_name(name), m_script(script), m_category(category) {}

    QString getName() const { return m_name; }
    QString getDescription() const { return m_description; }
    QString getCategory() const { return m_category; }
    QString getScript() const { return m_script; }
    QStringList getArgNames() const { return m_argNames; }
    void setArgNames(const QStringList& names) { m_argNames = names; }

    Q_INVOKABLE QVariant run(const QVariantList& args);
    Q_INVOKABLE void setDescription(const QString& desc) { m_description = desc; }

private:
    QString m_name;
    QString m_script;
    QString m_description;
    QString m_category;
    QStringList m_argNames;
};

class PythonMacroManager : public QObject {
    Q_OBJECT

public:
    static PythonMacroManager* instance();

    Q_INVOKABLE void registerMacro(ScriptMacro* macro);
    Q_INVOKABLE void unregisterMacro(const QString& name);
    Q_INVOKABLE ScriptMacro* getMacro(const QString& name);
    Q_INVOKABLE QStringList getMacroNames();
    Q_INVOKABLE QStringList getCategories();

    Q_INVOKABLE void importMacros(const QString& path);
    Q_INVOKABLE void exportMacros(const QString& path);

    Q_INVOKABLE void runMacro(const QString& name, const QVariantList& args);

signals:
    void macroRegistered(const QString& name);
    void macroUnregistered(const QString& name);

private:
    PythonMacroManager(QObject* parent = nullptr);
    ~PythonMacroManager();
    Q_DISABLE_COPY(PythonMacroManager)

    void registerDefaultMacros();

    static PythonMacroManager* s_instance;

    QMap<QString, ScriptMacro*> m_macros;
    QMap<QString, QStringList> m_categories;
};

}