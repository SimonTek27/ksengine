#pragma once

#include <QString>
#include <QMap>

class EditorConfig {
public:
    static EditorConfig& instance();

    // Simulator paths
    QString simInstallPath() const { return m_simInstallPath; }
    void setSimInstallPath(const QString& path) { m_simInstallPath = path; }

    QString simDocumentsPath() const { return m_simDocumentsPath; }
    void setSimDocumentsPath(const QString& path) { m_simDocumentsPath = path; }

    QString simExeName() const { return m_simExeName; }
    void setSimExeName(const QString& name) { m_simExeName = name; }

    QString simContentCarsPath() const { return m_simContentCarsPath; }
    void setSimContentCarsPath(const QString& path) { m_simContentCarsPath = path; }

    // Editor paths
    QString ppFiltersPath() const { return m_ppFiltersPath; }
    void setPPFiltersPath(const QString& path) { m_ppFiltersPath = path; }

    QString materialLibraryPath() const { return m_materialLibraryPath; }
    void setMaterialLibraryPath(const QString& path) { m_materialLibraryPath = path; }

    QString platesPath() const { return m_platesPath; }
    void setPlatesPath(const QString& path) { m_platesPath = path; }

    QString modCfgPath() const { return m_modCfgPath; }
    void setModCfgPath(const QString& path) { m_modCfgPath = path; }

    // Profile defaults
    QString defaultAuthorName() const { return m_defaultAuthorName; }
    void setDefaultAuthorName(const QString& name) { m_defaultAuthorName = name; }

    QString defaultAuthorEmail() const { return m_defaultAuthorEmail; }
    void setDefaultAuthorEmail(const QString& email) { m_defaultAuthorEmail = email; }

    QString defaultWebsite() const { return m_defaultWebsite; }
    void setDefaultWebsite(const QString& url) { m_defaultWebsite = url; }

    // Display strings
    QString editorTitle() const { return m_editorTitle; }
    void setEditorTitle(const QString& title) { m_editorTitle = title; }

    QString aboutDescription() const { return m_aboutDescription; }
    void setAboutDescription(const QString& desc) { m_aboutDescription = desc; }

    QString serverConfigHeader() const { return m_serverConfigHeader; }
    void setServerConfigHeader(const QString& header) { m_serverConfigHeader = header; }

    QString serverEntryListHeader() const { return m_serverEntryListHeader; }
    void setServerEntryListHeader(const QString& header) { m_serverEntryListHeader = header; }

    QString showroomConfigDesc() const { return m_showroomConfigDesc; }
    void setShowroomConfigDesc(const QString& desc) { m_showroomConfigDesc = desc; }

    // File format descriptions
    QString formatDescription(const QString& ext) const {
        return m_formatDescriptions.value(ext);
    }
    void setFormatDescription(const QString& ext, const QString& desc) {
        m_formatDescriptions[ext] = desc;
    }

    // Simulator detection
    QStringList registryKeys() const { return m_registryKeys; }
    void setRegistryKeys(const QStringList& keys) { m_registryKeys = keys; }
    void addRegistryKey(const QString& key) { m_registryKeys.append(key); }

    QStringList defaultSearchPaths() const { return m_defaultSearchPaths; }
    void setDefaultSearchPaths(const QStringList& paths) { m_defaultSearchPaths = paths; }
    void addDefaultSearchPath(const QString& path) { m_defaultSearchPaths.append(path); }

    QString steamRelativePath() const { return m_steamRelativePath; }
    void setSteamRelativePath(const QString& path) { m_steamRelativePath = path; }

    void resetToDefaults();

private:
    EditorConfig() { resetToDefaults(); }

    QString m_simInstallPath;
    QString m_simDocumentsPath;
    QString m_simExeName;
    QString m_simContentCarsPath;
    QString m_ppFiltersPath;
    QString m_materialLibraryPath;
    QString m_platesPath;
    QString m_modCfgPath;
    QString m_defaultAuthorName;
    QString m_defaultAuthorEmail;
    QString m_defaultWebsite;
    QString m_editorTitle;
    QString m_aboutDescription;
    QString m_serverConfigHeader;
    QString m_serverEntryListHeader;
    QString m_showroomConfigDesc;
    QMap<QString, QString> m_formatDescriptions;
    QStringList m_registryKeys;
    QStringList m_defaultSearchPaths;
    QString m_steamRelativePath;
};
