#include "StartupDialog.h"
#include "sys/LogManager.h"
#include "sys/SettingsManager.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/KsAssettoCorsaContentPath.h"

#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStyle>
#include <QTimer>

namespace ks {

// ==================== Constructor / Destructor ====================

StartupDialog::StartupDialog(QWidget* parent)
    : QDialog(parent)
    , m_settings(new QSettings("ksEditor", "StartupDialog", this))
{
    setWindowTitle(tr("Assetto Corsa Dev Mode"));
    setMinimumSize(DIALOG_WIDTH, DIALOG_HEIGHT);
    setModal(true);
    
    // Remove help button but keep close button
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    setupUI();
    applyStylesheet();
    loadRecentProjects();
    
    LOG_INFO("StartupDialog", "Startup dialog initialized");
}

StartupDialog::~StartupDialog()
{
    if (m_carSubMenu) {
        m_carSubMenu->close();
    }
    LOG_INFO("StartupDialog", "Startup dialog destroyed");
}

// ==================== UI Setup ====================

void StartupDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(32, 32, 32, 32);

    setupTitleSection();
    mainLayout->addWidget(m_titleLabel);
    
    auto* subtitleLabel = new QLabel(tr("Create a new project or open an existing one"), this);
    subtitleLabel->setObjectName("subtitleLabel");
    mainLayout->addWidget(subtitleLabel);
    
    mainLayout->addSpacing(8);
    
    setupCreateButtonsSection();
    mainLayout->addLayout(m_createButtonsLayout);  // Will be created in setupCreateButtonsSection
    
    mainLayout->addSpacing(16);
    
    setupRecentProjectsSection();
    mainLayout->addWidget(m_recentProjectsLabel);  // Created in setupRecentProjectsSection
    mainLayout->addWidget(m_recentList);
    
    mainLayout->addSpacing(16);
    
    setupBottomButtonsSection();
    mainLayout->addLayout(m_bottomLayout);  // Created in setupBottomButtonsSection
}

void StartupDialog::setupTitleSection()
{
    m_titleLabel = new QLabel(tr("Assetto Corsa Dev Mode"), this);
    m_titleLabel->setObjectName("titleLabel");
    m_titleLabel->setAlignment(Qt::AlignCenter);
}

void StartupDialog::setupCreateButtonsSection()
{
    auto* createLabel = new QLabel(tr("Create New Project:"), this);
    createLabel->setObjectName("sectionLabel");
    
    m_createButtonsLayout = new QVBoxLayout();
    m_createButtonsLayout->setSpacing(12);
    m_createButtonsLayout->addWidget(createLabel);
    
    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setSpacing(16);
    
    // Helper lambda for button creation
    auto createButton = [this](const QString& text, const QString& iconName) -> QPushButton* {
        auto* btn = new QPushButton(text, this);
        btn->setMinimumSize(BUTTON_MIN_WIDTH, BUTTON_MIN_HEIGHT);
        btn->setProperty("actionButton", true);
        
        if (!iconName.isEmpty()) {
            QIcon icon(QString(":/icons/%1").arg(iconName));
            if (!icon.isNull()) {
                btn->setIcon(icon);
                btn->setIconSize(QSize(32, 32));
            }
        }
        return btn;
    };
    
    m_carButton = createButton(tr("Car"), "car-model.svg");
    m_trackButton = createButton(tr("Track"), "track.svg");
    m_characterButton = createButton(tr("Character"), "character.svg");
    m_soundButton = createButton(tr("Sound"), "audio.svg");
    
    connect(m_carButton, &QPushButton::clicked, this, &StartupDialog::onCreateCarClicked);
    connect(m_trackButton, &QPushButton::clicked, this, &StartupDialog::onCreateTrackClicked);
    connect(m_characterButton, &QPushButton::clicked, this, &StartupDialog::onCreateCharacterClicked);
    connect(m_soundButton, &QPushButton::clicked, this, &StartupDialog::onCreateSoundClicked);
    
    buttonsLayout->addWidget(m_carButton);
    buttonsLayout->addWidget(m_trackButton);
    buttonsLayout->addWidget(m_characterButton);
    buttonsLayout->addWidget(m_soundButton);
    
    m_createButtonsLayout->addLayout(buttonsLayout);
}

void StartupDialog::setupRecentProjectsSection()
{
    m_recentProjectsLabel = new QLabel(tr("Recent Projects:"), this);
    m_recentProjectsLabel->setObjectName("sectionLabel");
    
    m_recentList = new QListWidget(this);
    m_recentList->setObjectName("recentList");
    m_recentList->setMinimumHeight(RECENT_LIST_MIN_HEIGHT);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    
    connect(m_recentList, &QListWidget::itemDoubleClicked, 
            this, &StartupDialog::onRecentItemDoubleClicked);
}

void StartupDialog::setupBottomButtonsSection()
{
    m_bottomLayout = new QHBoxLayout();
    m_bottomLayout->setSpacing(12);
    
    auto* editReleasedBtn = new QPushButton(tr("Edit Released Content"), this);
    editReleasedBtn->setObjectName("secondaryButton");
    connect(editReleasedBtn, &QPushButton::clicked, this, &StartupDialog::onEditReleasedClicked);
    m_bottomLayout->addWidget(editReleasedBtn);
    
    auto* encryptBtn = new QPushButton(tr("Encrypt Content"), this);
    encryptBtn->setObjectName("secondaryButton");
    connect(encryptBtn, &QPushButton::clicked, this, &StartupDialog::onEncryptClicked);
    m_bottomLayout->addWidget(encryptBtn);
    
    m_bottomLayout->addStretch();
    
    auto* openFolderBtn = new QPushButton(tr("Browse Folder..."), this);
    openFolderBtn->setObjectName("primaryButton");
    connect(openFolderBtn, &QPushButton::clicked, this, &StartupDialog::onBrowseFolderClicked);
    m_bottomLayout->addWidget(openFolderBtn);
    
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName("secondaryButton");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    m_bottomLayout->addWidget(cancelBtn);
}

void StartupDialog::applyStylesheet()
{
    setStyleSheet(R"(
        StartupDialog {
            background-color: #1e1e2e;
        }
        
        #titleLabel {
            font-size: 28px;
            font-weight: bold;
            color: #cba6f7;
            margin-bottom: 8px;
        }
        
        #subtitleLabel {
            font-size: 14px;
            color: #9399b2;
            margin-bottom: 16px;
        }
        
        #sectionLabel {
            font-size: 13px;
            font-weight: bold;
            color: #cdd6f4;
            margin-bottom: 8px;
        }
        
        QPushButton[actionButton="true"] {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 8px;
            padding: 12px;
            font-size: 14px;
            font-weight: 500;
        }
        
        QPushButton[actionButton="true"]:hover {
            background-color: #45475a;
            border-color: #585b70;
        }
        
        QPushButton[actionButton="true"]:pressed {
            background-color: #585b70;
        }
        
        QPushButton#primaryButton {
            background-color: #89b4fa;
            color: #1e1e2e;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-weight: bold;
        }
        
        QPushButton#primaryButton:hover {
            background-color: #b4befe;
        }
        
        QPushButton#secondaryButton {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 8px 16px;
        }
        
        QPushButton#secondaryButton:hover {
            background-color: #45475a;
        }
        
        QListWidget#recentList {
            background-color: #181825;
            color: #cdd6f4;
            border: 1px solid #313244;
            border-radius: 8px;
            outline: none;
        }
        
        QListWidget#recentList::item {
            padding: 10px 12px;
            border-bottom: 1px solid #313244;
        }
        
        QListWidget#recentList::item:selected {
            background-color: #89b4fa;
            color: #1e1e2e;
        }
        
        QListWidget#recentList::item:hover {
            background-color: #313244;
        }
    )");
}

// ==================== Project Creation Handlers ====================

void StartupDialog::onCreateCarClicked()
{
    showCarSubMenu();
}

void StartupDialog::onCreateTrackClicked()
{
    createProject(ProjectType::Track);
}

void StartupDialog::onCreateCharacterClicked()
{
    createProject(ProjectType::Character);
}

void StartupDialog::onCreateSoundClicked()
{
    createProject(ProjectType::Sound);
}

void StartupDialog::showCarSubMenu()
{
    // Clean up previous menu if it exists
    if (m_carSubMenu) {
        m_carSubMenu->deleteLater();
    }
    
    m_carSubMenu = new QDialog(this);
    m_carSubMenu->setWindowTitle(tr("Select Car Type"));
    m_carSubMenu->setMinimumSize(400, 350);
    m_carSubMenu->setModal(true);
    
    auto* layout = new QVBoxLayout(m_carSubMenu);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);
    
    auto* titleLabel = new QLabel(tr("Select Car Type"), m_carSubMenu);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #cba6f7;");
    layout->addWidget(titleLabel);
    
    auto* descriptionLabel = new QLabel(
        tr("What aspect of the car do you want to create?"), m_carSubMenu);
    descriptionLabel->setStyleSheet("color: #9399b2; margin-bottom: 8px;");
    layout->addWidget(descriptionLabel);
    
    layout->addSpacing(8);
    
    // Helper for creating car type buttons
    auto addCarTypeButton = [this, layout](const QString& title, const QString& description, CarType type) {
        auto* btn = new QPushButton(m_carSubMenu);
        btn->setMinimumHeight(60);
        btn->setStyleSheet(R"(
            QPushButton {
                background-color: #313244;
                color: #cdd6f4;
                border: 1px solid #45475a;
                border-radius: 8px;
                padding: 10px;
                text-align: left;
            }
            QPushButton:hover {
                background-color: #45475a;
            }
        )");
        
        auto* btnLayout = new QVBoxLayout(btn);
        auto* titleLabelBtn = new QLabel(title, btn);
        titleLabelBtn->setStyleSheet("font-weight: bold; font-size: 14px; color: #cdd6f4; background: transparent;");
        auto* descLabel = new QLabel(description, btn);
        descLabel->setStyleSheet("font-size: 11px; color: #9399b2; background: transparent;");
        
        btnLayout->addWidget(titleLabelBtn);
        btnLayout->addWidget(descLabel);
        
        connect(btn, &QPushButton::clicked, this, [this, type]() {
            onCarSubTypeSelected(type);
        });
        
        layout->addWidget(btn);
    };
    
    addCarTypeButton(tr("3D Model"), tr("Create a 3D car model for visual customization"), CarType::Model);
    addCarTypeButton(tr("Physics"), tr("Create suspension, brakes, and aerodynamics"), CarType::Physics);
    addCarTypeButton(tr("Livery / Skin"), tr("Create paint schemes and decals"), CarType::Livery);
    addCarTypeButton(tr("Sound"), tr("Create engine and exhaust audio"), CarType::Sound);
    
    layout->addStretch();
    
    auto* cancelBtn = new QPushButton(tr("Cancel"), m_carSubMenu);
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #45475a;
            color: #cdd6f4;
            border-radius: 6px;
            padding: 8px 16px;
        }
        QPushButton:hover {
            background-color: #585b70;
        }
    )");
    connect(cancelBtn, &QPushButton::clicked, m_carSubMenu, &QDialog::reject);
    layout->addWidget(cancelBtn);
    
    m_carSubMenu->exec();
}

void StartupDialog::onCarSubTypeSelected(CarType type)
{
    if (m_carSubMenu) {
        m_carSubMenu->accept();
    }
    createProject(ProjectType::Car, type);
}

void StartupDialog::createProject(ProjectType type, CarType carType)
{
    QString projectName;
    bool confirmed = false;
    
    while (!confirmed) {
        projectName = promptForProjectName(confirmed);
        
        if (!confirmed || projectName.isEmpty()) {
            return;  // User cancelled or back button
        }
        
        if (!validateProjectName(projectName)) {
            QMessageBox::warning(this, tr("Invalid Project Name"),
                tr("Project name can only contain letters, numbers, spaces, hyphens, and underscores."));
            confirmed = false;
            continue;
        }
        
        break;
    }
    
    // Sanitize and clean the project name
    projectName = sanitizeProjectName(projectName);
    
    // Get AC content path
    QString acContentPath = resolveACContentPath();
    if (acContentPath.isEmpty()) {
        QMessageBox::warning(this, tr("Assetto Corsa Not Found"),
            tr("Assetto Corsa installation not found. Please configure the AC path in Settings."));
        return;
    }
    
    // Determine subfolder based on project type
    QString subfolder;
    switch (type) {
        case ProjectType::Car: subfolder = "cars"; break;
        case ProjectType::Track: subfolder = "tracks"; break;
        case ProjectType::Character: subfolder = "driver"; break;
        case ProjectType::Sound: subfolder = "sfx"; break;
        default: subfolder = "misc";
    }
    
    QString fullPath = QDir::cleanPath(acContentPath + "/" + subfolder + "/" + projectName);
    
    // Check if directory already exists
    if (QDir(fullPath).exists()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Project Exists"),
            tr("A project with this name already exists. Do you want to open it instead?"),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            m_result.projectPath = fullPath;
            m_result.projectType = type;
            m_result.carType = carType;
            addToRecentProjects(fullPath);
            accept();
        }
        return;
    }
    
    // Create directory structure
    if (!createProjectDirectory(fullPath)) {
        QMessageBox::warning(this, tr("Creation Failed"),
            tr("Failed to create project directory: %1").arg(fullPath));
        return;
    }
    
    // Create type-specific folder structure
    switch (type) {
        case ProjectType::Car:
            createCarFolderStructure(fullPath, carType);
            break;
        case ProjectType::Track:
            createTrackFolderStructure(fullPath);
            break;
        case ProjectType::Character:
            createCharacterFolderStructure(fullPath);
            break;
        case ProjectType::Sound:
            createSoundFolderStructure(fullPath);
            break;
        default:
            break;
    }
    
    // Store result
    m_result.projectPath = fullPath;
    m_result.projectType = type;
    m_result.carType = carType;
    
    // Add to recent projects
    addToRecentProjects(fullPath);
    
    LOG_INFO("StartupDialog", QString("Created new %1 project at: %2")
             .arg(getProjectTypeDisplayName(type))
             .arg(fullPath));
    
    accept();
}

// ==================== Recent Projects ====================

void StartupDialog::loadRecentProjects()
{
    m_recentList->clear();
    
    QStringList recentPaths = m_settings->value("RecentProjects", QStringList()).toStringList();
    
    for (const QString& path : recentPaths) {
        QFileInfo info(path);
        if (!info.exists()) {
            continue;  // Skip non-existent paths
        }
        
        QString displayName = info.baseName();
        
        // Add context from parent folder
        QDir parentDir = info.dir();
        QString parentName = parentDir.dirName();
        if (parentName == "cars") {
            displayName = QString("%1 [Car]").arg(info.baseName());
        } else if (parentName == "tracks") {
            displayName = QString("%1 [Track]").arg(info.baseName());
        } else if (parentName == "driver") {
            displayName = QString("%1 [Character]").arg(info.baseName());
        } else if (parentName == "sfx") {
            displayName = QString("%1 [Sound]").arg(info.baseName());
        }
        
        auto* item = new QListWidgetItem(displayName, m_recentList);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QString("%1\nSize: %2")
                         .arg(path)
                         .arg(formatFileSize(calculateFolderSize(path))));
        item->setIcon(getIconForPath(path));
    }
    
    // Show message if no recent projects
    if (m_recentList->count() == 0) {
        auto* item = new QListWidgetItem(tr("No recent projects"), m_recentList);
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor("#9399b2"));
    }
}

void StartupDialog::saveRecentProjects() const
{
    QStringList recentPaths;
    for (int i = 0; i < m_recentList->count(); ++i) {
        QListWidgetItem* item = m_recentList->item(i);
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            recentPaths.append(path);
        }
    }
    m_settings->setValue("RecentProjects", recentPaths);
}

void StartupDialog::addToRecentProjects(const QString& path)
{
    // Remove if already exists
    for (int i = 0; i < m_recentList->count(); ++i) {
        if (m_recentList->item(i)->data(Qt::UserRole).toString() == path) {
            delete m_recentList->takeItem(i);
            break;
        }
    }
    
    // Add to top
    QFileInfo info(path);
    auto* item = new QListWidgetItem(info.baseName(), m_recentList);
    item->setData(Qt::UserRole, path);
    item->setIcon(getIconForPath(path));
    m_recentList->insertItem(0, item);
    
    // Trim to max
    while (m_recentList->count() > MAX_RECENT_PROJECTS) {
        delete m_recentList->takeItem(m_recentList->count() - 1);
    }
    
    saveRecentProjects();
}

// ==================== Folder Structure Creation ====================

void StartupDialog::createCarFolderStructure(const QString& basePath, CarType carType)
{
    QDir dir(basePath);
    
    // Create base directories
    dir.mkpath("data");
    dir.mkpath("skins/default");
    dir.mkpath("ui");
    dir.mkpath("sfx");
    
    // Create data.acd with type-specific content
    QFile dataFile(basePath + "/data.acd");
    if (dataFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&dataFile);
        
        switch (carType) {
            case CarType::Model:
                stream << "[CAR]\nMODEL=model.kn5\n\n[GEOMETRY]\n";
                break;
            case CarType::Physics:
                stream << "[PHYSICS]\nSHIFT_UP=7000\nSHIFT_DOWN=3000\n\n[SUSPENSION]\n\n[BRAKES]\n\n[AERODYNAMICS]\n";
                break;
            case CarType::Livery:
                stream << "[SKIN]\nNAME=Default\nAUTHOR=\n\n[LIVERY]\n";
                break;
            case CarType::Sound:
                stream << "[SOUND]\nENGINE=engine.bank\nEXHAUST=exhaust.bank\n\n[TURBO]\n";
                break;
        }
        dataFile.close();
    }
    
    // Create a basic ui_car.json
    QFile uiFile(basePath + "/ui/ui_car.json");
    if (uiFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&uiFile);
        stream << "{\n";
        stream << "  \"name\": \"" << QFileInfo(basePath).baseName() << "\",\n";
        stream << "  \"description\": \"\",\n";
        stream << "  \"tags\": [\"mod\"]\n";
        stream << "}\n";
        uiFile.close();
    }
    
    LOG_DEBUG("StartupDialog", "Created car folder structure at: " + basePath);
}

void StartupDialog::createTrackFolderStructure(const QString& basePath)
{
    QDir dir(basePath);
    
    dir.mkpath("ai");
    dir.mkpath("data");
    dir.mkpath("skins/default");
    dir.mkpath("ui");
    dir.mkpath("texture");
    dir.mkpath("models");
    
    // Create track_params.ini
    QFile paramsFile(basePath + "/data/track_params.ini");
    if (paramsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&paramsFile);
        stream << "[TRACK]\n";
        stream << "NAME=" << QFileInfo(basePath).baseName() << "\n";
        stream << "LENGTH=1000\n";
        stream << "WIDTH=10\n";
        stream << "\n[PIT]\n";
        stream << "COUNT=0\n";
        paramsFile.close();
    }
    
    // Create surfaces.ini
    QFile surfacesFile(basePath + "/data/surfaces.ini");
    if (surfacesFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&surfacesFile);
        stream << "[SURFACE_0]\n";
        stream << "NAME=asphalt\n";
        stream << "FRICTION=0.98\n";
        surfacesFile.close();
    }
    
    LOG_DEBUG("StartupDialog", "Created track folder structure at: " + basePath);
}

void StartupDialog::createCharacterFolderStructure(const QString& basePath)
{
    QDir dir(basePath);
    
    dir.mkpath("meshes");
    dir.mkpath("texture");
    dir.mkpath("ui");
    dir.mkpath("animation");
    
    // Create driver.ini
    QFile driverFile(basePath + "/driver.ini");
    if (driverFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&driverFile);
        stream << "[DRIVER]\n";
        stream << "NAME=" << QFileInfo(basePath).baseName() << "\n";
        stream << "MESH=driver.kn5\n";
        stream << "SUIT_TEXTURE=suit.dds\n";
        stream << "HELMET_TEXTURE=helmet.dds\n";
        driverFile.close();
    }
    
    LOG_DEBUG("StartupDialog", "Created character folder structure at: " + basePath);
}

void StartupDialog::createSoundFolderStructure(const QString& basePath)
{
    QDir dir(basePath);
    
    dir.mkpath("engine");
    dir.mkpath("exhaust");
    dir.mkpath("turbo");
    dir.mkpath("tires");
    dir.mkpath("surface");
    dir.mkpath("ui");
    
    // Create sound_config.ini
    QFile soundFile(basePath + "/sound_config.ini");
    if (soundFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&soundFile);
        stream << "[SOUND]\n";
        stream << "TYPE=car\n";
        stream << "BANK_NAME=" << QFileInfo(basePath).baseName() << ".bank\n";
        stream << "\n[ENGINE]\n";
        stream << "FILE=engine/engine.wav\n";
        stream << "\n[EXHAUST]\n";
        stream << "FILE=exhaust/exhaust.wav\n";
        soundFile.close();
    }
    
    LOG_DEBUG("StartupDialog", "Created sound folder structure at: " + basePath);
}

// ==================== Helper Functions ====================

QString StartupDialog::promptForProjectName(bool& confirmed)
{
    confirmed = false;
    
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Project"));
    dialog.setMinimumWidth(400);
    dialog.setModal(true);
    
    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 20, 20, 20);
    
    auto* label = new QLabel(tr("Project Name:"), &dialog);
    label->setStyleSheet("font-weight: bold; color: #cdd6f4;");
    layout->addWidget(label);
    
    auto* edit = new QLineEdit(&dialog);
    edit->setPlaceholderText(tr("Enter a name for your project"));
    edit->setStyleSheet(R"(
        QLineEdit {
            background-color: #181825;
            color: #cdd6f4;
            border: 1px solid #313244;
            border-radius: 4px;
            padding: 8px;
        }
        QLineEdit:focus {
            border-color: #89b4fa;
        }
    )");
    layout->addWidget(edit);
    
    auto* infoLabel = new QLabel(tr("Letters, numbers, spaces, hyphens, and underscores only."), &dialog);
    infoLabel->setStyleSheet("color: #9399b2; font-size: 11px;");
    layout->addWidget(infoLabel);
    
    layout->addStretch();
    
    auto* buttonLayout = new QHBoxLayout();
    auto* cancelBtn = new QPushButton(tr("Cancel"), &dialog);
    auto* backBtn = new QPushButton(tr("Back"), &dialog);
    auto* createBtn = new QPushButton(tr("Create"), &dialog);
    
    cancelBtn->setStyleSheet("background-color: #45475a; color: #cdd6f4; border-radius: 4px; padding: 6px 12px;");
    backBtn->setStyleSheet("background-color: #45475a; color: #cdd6f4; border-radius: 4px; padding: 6px 12px;");
    createBtn->setStyleSheet("background-color: #89b4fa; color: #1e1e2e; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(backBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(createBtn);
    layout->addLayout(buttonLayout);
    
    bool backPressed = false;
    bool createPressed = false;
    
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(backBtn, &QPushButton::clicked, [&]() { backPressed = true; dialog.reject(); });
    connect(createBtn, &QPushButton::clicked, [&]() {
        if (!edit->text().trimmed().isEmpty()) {
            createPressed = true;
            dialog.accept();
        }
    });
    
    int result = dialog.exec();
    
    if (result == QDialog::Accepted && createPressed) {
        confirmed = true;
        return edit->text().trimmed();
    }
    
    if (backPressed) {
        confirmed = false;  // Back button pressed - go back to car submenu
        return QString();
    }
    
    confirmed = false;  // Cancelled
    return QString();
}

bool StartupDialog::validateProjectName(const QString& name) const
{
    if (name.isEmpty()) return false;
    
    // Allow letters, numbers, spaces, hyphens, underscores
    static QRegularExpression regex(R"(^[a-zA-Z0-9\s\-_]+$)");
    return regex.match(name).hasMatch();
}

QString StartupDialog::sanitizeProjectName(const QString& name) const
{
    QString sanitized = name;
    // Replace spaces with underscores
    sanitized.replace(' ', '_');
    // Remove any remaining invalid characters
    sanitized.remove(QRegularExpression(R"([^a-zA-Z0-9_\-])"));
    return sanitized;
}

QString StartupDialog::resolveACContentPath() const
{
    // Try from settings first
    QSettings appSettings("ksEditor", "ksEditor");
    QString acRoot = appSettings.value("acPath").toString();
    
    if (!acRoot.isEmpty()) {
        QString contentPath = QDir::cleanPath(acRoot + "/content");
        if (QDir(contentPath).exists()) {
            return contentPath;
        }
    }
    
    // Auto-detect
    QString detectedRoot = ks::KsACPaths::findBestInstallation();
    if (!detectedRoot.isEmpty()) {
        QString contentPath = QDir::cleanPath(detectedRoot + "/content");
        if (QDir(contentPath).exists()) {
            return contentPath;
        }
    }
    
    return QString();
}

bool StartupDialog::createProjectDirectory(const QString& path)
{
    QDir dir;
    return dir.mkpath(path);
}

qint64 StartupDialog::calculateFolderSize(const QString& path, int maxDepth)
{
    if (maxDepth <= 0) return 0;
    
    qint64 size = 0;
    QDir dir(path);
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QFileInfo& info : entries) {
        if (info.isFile()) {
            size += info.size();
        } else if (info.isDir() && maxDepth > 1) {
            size += calculateFolderSize(info.filePath(), maxDepth - 1);
        }
    }
    return size;
}

QString StartupDialog::formatFileSize(qint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

QString StartupDialog::getProjectTypeDisplayName(ProjectType type)
{
    switch (type) {
        case ProjectType::Car: return "Car";
        case ProjectType::Track: return "Track";
        case ProjectType::Character: return "Character";
        case ProjectType::Sound: return "Sound";
        default: return "Unknown";
    }
}

QString StartupDialog::getProjectIconName(ProjectType type)
{
    switch (type) {
        case ProjectType::Car: return "car-model.svg";
        case ProjectType::Track: return "track.svg";
        case ProjectType::Character: return "character.svg";
        case ProjectType::Sound: return "audio.svg";
        default: return "folder-open.svg";
    }
}

QIcon StartupDialog::getIconForPath(const QString& path) const
{
    if (path.contains("/cars/", Qt::CaseInsensitive)) {
        QIcon icon(":/icons/car-model.svg");
        if (!icon.isNull()) return icon;
    } else if (path.contains("/tracks/", Qt::CaseInsensitive)) {
        QIcon icon(":/icons/track.svg");
        if (!icon.isNull()) return icon;
    } else if (path.contains("/driver/", Qt::CaseInsensitive)) {
        QIcon icon(":/icons/character.svg");
        if (!icon.isNull()) return icon;
    } else if (path.contains("/sfx/", Qt::CaseInsensitive)) {
        QIcon icon(":/icons/audio.svg");
        if (!icon.isNull()) return icon;
    }
    
    return style()->standardIcon(QStyle::SP_DirIcon);
}

// ==================== Button Handlers ====================

void StartupDialog::onRecentItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) return;
    
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    
    m_result.recentFilePath = path;
    addToRecentProjects(path);  // Bump to top
    accept();
}

void StartupDialog::onEditReleasedClicked()
{
    m_result.editReleasedMode = true;
    accept();
}

void StartupDialog::onEncryptClicked()
{
    m_result.encryptMode = true;
    accept();
}

void StartupDialog::onBrowseFolderClicked()
{
    QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select Project Folder"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
    );
    
    if (!folder.isEmpty()) {
        m_result.projectPath = folder;
        m_result.projectType = ProjectType::None;  // Unknown type
        addToRecentProjects(folder);
        accept();
    }
}

// ==================== Dialog Lifecycle ====================

void StartupDialog::accept()
{
    if (!m_result.hasValidProject()) {
        // No project selected, but maybe user just wants to open main window?
        // We'll still accept with empty result - main window can handle this
        LOG_DEBUG("StartupDialog", "Accepting with empty result");
    }
    QDialog::accept();
}

void StartupDialog::reject()
{
    m_result = StartupResult();  // Clear result on reject
    QDialog::reject();
}

} // namespace ks