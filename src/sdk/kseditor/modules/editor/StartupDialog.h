#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QSettings>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace ks {

/**
 * @brief Project type enumeration for Kinetika modding
 */
enum class ProjectType {
    None,
    Car,
    Track,
    Character,
    Sound
};

/**
 * @brief Car sub-type for more specific project creation
 */
enum class CarType {
    Model,
    Physics,
    Livery,
    Sound
};

/**
 * @brief Result structure returned when dialog accepts
 */
struct StartupResult {
    ProjectType projectType = ProjectType::None;
    CarType carType = CarType::Model;
    QString projectPath;
    QString recentFilePath;
    bool editReleasedMode = false;
    bool encryptMode = false;
    
    bool hasValidProject() const {
        return !projectPath.isEmpty() || !recentFilePath.isEmpty() || editReleasedMode || encryptMode;
    }
};

/**
 * @brief Startup dialog for Kinetika modding tool
 * 
 * Allows users to:
 * - Create new projects (Car, Track, Character, Sound)
 * - Open recent projects
 * - Edit released content
 * - Encrypt content
 */
class StartupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StartupDialog(QWidget* parent = nullptr);
    ~StartupDialog() override;

    /**
     * @brief Get the result after dialog accepts
     */
    StartupResult result() const { return m_result; }

public slots:
    /**
     * @brief Overridden to ensure result is valid
     */
    void accept() override;

    /**
     * @brief Reject with no result
     */
    void reject() override;

private slots:
    void onCreateCarClicked();
    void onCreateTrackClicked();
    void onCreateCharacterClicked();
    void onCreateSoundClicked();
    void onRecentItemDoubleClicked(QListWidgetItem* item);
    void onEditReleasedClicked();
    void onEncryptClicked();
    void onBrowseFolderClicked();
    void onCarSubTypeSelected(CarType type);

private:
    // UI Setup
    void setupUI();
    void setupTitleSection();
    void setupCreateButtonsSection();
    void setupRecentProjectsSection();
    void setupBottomButtonsSection();
    void applyStylesheet();

    // Project Creation
    void showCarSubMenu();
    void createProject(ProjectType type, CarType carType = CarType::Model);
    bool validateProjectName(const QString& name) const;
    QString sanitizeProjectName(const QString& name) const;
    QString promptForProjectName(bool& confirmed);

    // File System Operations
    QString resolveACContentPath() const;
    bool createProjectDirectory(const QString& path);
    void addToRecentProjects(const QString& path);
    void loadRecentProjects();
    void saveRecentProjects() const;

    // Folder Structure Creation
    void createCarFolderStructure(const QString& basePath, CarType carType);
    void createTrackFolderStructure(const QString& basePath);
    void createCharacterFolderStructure(const QString& basePath);
    void createSoundFolderStructure(const QString& basePath);

    // Helper Functions
    static qint64 calculateFolderSize(const QString& path, int maxDepth = 10);
    static QString formatFileSize(qint64 bytes);
    static QString getProjectTypeDisplayName(ProjectType type);
    static QString getProjectIconName(ProjectType type);
    QIcon getIconForPath(const QString& path) const;

    // Constants
    static constexpr int MAX_RECENT_PROJECTS = 10;
    static constexpr int DIALOG_WIDTH = 700;
    static constexpr int DIALOG_HEIGHT = 550;
    static constexpr int BUTTON_MIN_HEIGHT = 64;
    static constexpr int BUTTON_MIN_WIDTH = 140;
    static constexpr int RECENT_LIST_MIN_HEIGHT = 170;

    // Member Variables
    StartupResult m_result;
    QPointer<QDialog> m_carSubMenu;  // For proper cleanup
    QSettings* m_settings = nullptr;

    // UI Components (owned by Qt)
    QLabel* m_titleLabel = nullptr;
    QListWidget* m_recentList = nullptr;
    QPushButton* m_carButton = nullptr;
    QPushButton* m_trackButton = nullptr;
    QPushButton* m_characterButton = nullptr;
    QPushButton* m_soundButton = nullptr;
    QVBoxLayout* m_createButtonsLayout = nullptr;
    QLabel* m_recentProjectsLabel = nullptr;
    QHBoxLayout* m_bottomLayout = nullptr;
};

} // namespace ks
