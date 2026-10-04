#include "EditorConfig.h"
#include <QDir>
#include <QStandardPaths>

EditorConfig& EditorConfig::instance() {
    static EditorConfig config;
    return config;
}

void EditorConfig::resetToDefaults() {
    m_simInstallPath.clear();
    m_simDocumentsPath = QDir::homePath() + "/Documents";
    m_simExeName = "simulator.exe";
    m_simContentCarsPath.clear();
    m_ppFiltersPath.clear();
    m_materialLibraryPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Editor/Materials Library/materials.json";
    m_platesPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Editor/plates";
    m_modCfgPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/cfg";
    m_defaultAuthorName = "User";
    m_defaultAuthorEmail = "user@example.com";
    m_defaultWebsite = "www.example.com";
    m_editorTitle = "Editor";
    m_aboutDescription = "<p>A unified modding toolkit.</p>";
    m_serverConfigHeader = "; Server Configuration\n";
    m_serverEntryListHeader = "; Server Entry List\n";
    m_showroomConfigDesc = "Default showroom configuration";
    m_registryKeys.clear();
    m_defaultSearchPaths.clear();
    m_steamRelativePath.clear();
}
