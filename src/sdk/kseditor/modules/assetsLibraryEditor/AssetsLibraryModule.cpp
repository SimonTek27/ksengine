#include "AssetsLibraryModule.h"
#include "sdk/kseditor/engine/assets/AssetManager.h"
#include "sdk/kseditor/engine/assets/AssetSearchEngine.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QDir>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QInputDialog>

namespace ks {

AssetsLibraryModule::AssetsLibraryModule(QWidget* parent)
    : EditorModule(parent)
{
}

bool AssetsLibraryModule::initialize()
{
    LOG_INFO("AssetsLibraryModule", "Initializing Assets Library module");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    setAcceptDrops(true);

    QLabel* headerLabel = new QLabel("<b>Assets Library</b>");
    headerLabel->setStyleSheet("font-size: 14px; padding: 4px;");
    mainLayout->addWidget(headerLabel);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left: asset list with filter bar
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* filterBar = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search assets...");
    m_typeFilter = new QComboBox();
    m_typeFilter->addItems({"All", "Models", "Textures", "Audio", "Materials", "Configs", "Scripts", "Fonts"});
    QPushButton* refreshBtn = new QPushButton("Refresh");

    filterBar->addWidget(m_searchEdit);
    filterBar->addWidget(m_typeFilter);
    filterBar->addWidget(refreshBtn);
    leftLayout->addLayout(filterBar);

    m_assetList = new QListWidget();
    m_assetList->setAlternatingRowColors(true);
    leftLayout->addWidget(m_assetList);

    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(true);
    leftLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("No assets loaded");
    leftLayout->addWidget(m_statusLabel);

    // Right: preview + details
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_previewWidget = new AssetPreviewWidget(this);
    m_previewWidget->setMinimumHeight(180);

    QGroupBox* infoGroup = new QGroupBox("Details");
    QFormLayout* infoForm = new QFormLayout();
    m_detailName = new QLabel("-");
    m_detailType = new QLabel("-");
    m_detailSize = new QLabel("-");
    m_detailPath = new QLabel("-");
    m_detailPath->setWordWrap(true);

    infoForm->addRow("Name:", m_detailName);
    infoForm->addRow("Type:", m_detailType);
    infoForm->addRow("Size:", m_detailSize);
    infoForm->addRow("Path:", m_detailPath);
    infoGroup->setLayout(infoForm);

    QPushButton* loadBtn = new QPushButton("Load Asset...");
    loadBtn->setStyleSheet(
        "QPushButton { background-color: #007acc; color: white; border: none; "
        "padding: 6px 14px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #005a9e; }");

    QPushButton* batchBtn = new QPushButton("Batch Import...");
    QPushButton* optionsBtn = new QPushButton("Options...");
    QPushButton* syncBtn = new QPushButton("Cloud Sync...");
    syncBtn->setEnabled(false);

    rightLayout->addWidget(m_previewWidget);
    rightLayout->addWidget(infoGroup);
    rightLayout->addWidget(loadBtn);
    rightLayout->addWidget(batchBtn);
    rightLayout->addWidget(optionsBtn);
    rightLayout->addStretch();

    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({350, 250});

    mainLayout->addWidget(m_splitter);

    connect(loadBtn, &QPushButton::clicked, this, &AssetsLibraryModule::onLoadAssetClicked);
    connect(refreshBtn, &QPushButton::clicked, this, &AssetsLibraryModule::onRefreshClicked);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &AssetsLibraryModule::onSearchTextChanged);
    connect(m_typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &AssetsLibraryModule::onTypeFilterChanged);
    connect(m_assetList, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* current, QListWidgetItem*) {
            if (current) onAssetSelected(current);
        });
    connect(batchBtn, &QPushButton::clicked, this, &AssetsLibraryModule::onBatchImportClicked);
    connect(optionsBtn, &QPushButton::clicked, this, &AssetsLibraryModule::onImportOptionsClicked);

    AssetManager* am = AssetManager::instance();
    if (am) {
        connect(am, &AssetManager::batchImportProgress, this, &AssetsLibraryModule::onBatchImportProgress);
        connect(am, &AssetManager::batchImportCompleted, this, &AssetsLibraryModule::onBatchImportCompleted);
    }

    // Cloud sync
    m_cloudSync = new CloudSyncManager(this);
    connect(syncBtn, &QPushButton::clicked, this, &AssetsLibraryModule::onConfigureCloudClicked);
    connect(m_cloudSync, &CloudSyncManager::syncCompleted, this, &AssetsLibraryModule::onSyncCompleted);

    refreshAssetList();
    return true;
}

void AssetsLibraryModule::shutdown()
{
    LOG_INFO("AssetsLibraryModule", "Shutting down Assets Library module");
}

void AssetsLibraryModule::importFile(const QString& filePath)
{
    if (!filePath.isEmpty()) {
        AssetManager* mgr = AssetManager::instance();
        if (mgr) {
            mgr->importAsset(filePath, m_importOptions.targetSubdir.isEmpty() ? QString() : QDir(mgr->getRootDirectory()).filePath(m_importOptions.targetSubdir));
        }
        m_previewWidget->loadAsset(filePath);
        refreshAssetList();
    }
}

void AssetsLibraryModule::exportFile(const QString& filePath)
{
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString savePath = QFileDialog::getSaveFileName(this, "Export Asset",
        fi.fileName(), "All Files (*.*)");
    if (savePath.isEmpty()) return;
    if (QFile::copy(filePath, savePath)) {
        m_statusLabel->setText(QString("Exported: %1").arg(fi.fileName()));
    }
}

void AssetsLibraryModule::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AssetsLibraryModule::dropEvent(QDropEvent* event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) return;
    AssetManager* mgr = AssetManager::instance();
    if (!mgr) return;

    QStringList files;
    QStringList dirs;

    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        QFileInfo fi(path);
        if (fi.isDir()) {
            dirs << path;
        } else if (fi.exists()) {
            files << path;
        }
    }

    if (!files.isEmpty()) {
        AssetManager::ImportOptions opts = m_importOptions;
        opts.recursive = false;
        mgr->importFiles(files, opts);
    }

    for (const QString& dir : dirs) {
        importDirectoryWithOptions(dir, m_importOptions);
    }

    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        QFileInfo fi(path);
        if (fi.exists() && !fi.isDir()) {
            m_previewWidget->loadAsset(path);
            m_statusLabel->setText(QString("Dropped: %1").arg(fi.fileName()));
        }
    }
    refreshAssetList();
    event->acceptProposedAction();
}

void AssetsLibraryModule::onLoadAssetClicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Select Asset", "",
        "All Supported (*.kn5 *.fbx *.obj *.glb *.dds *.png *.jpg *.wav *.ogg);;All Files (*.*)");
    if (!filePath.isEmpty()) {
        m_previewWidget->loadAsset(filePath);

        AssetManager* mgr = AssetManager::instance();
        if (mgr) {
            mgr->scanFile(filePath);
            refreshAssetList();
        }

        QFileInfo fi(filePath);
        m_detailName->setText(fi.fileName());
        m_detailType->setText(fi.suffix().toUpper());
        m_detailSize->setText(formatFileSize(fi.size()));
        m_detailPath->setText(filePath);
    }
}

void AssetsLibraryModule::onRefreshClicked()
{
    refreshAssetList();
}

void AssetsLibraryModule::onSearchTextChanged(const QString& text)
{
    for (int i = 0; i < m_assetList->count(); ++i) {
        QListWidgetItem* item = m_assetList->item(i);
        item->setHidden(text.length() > 0 &&
            !item->text().contains(text, Qt::CaseInsensitive));
    }
}

void AssetsLibraryModule::onTypeFilterChanged(int index)
{
    Q_UNUSED(index);
    refreshAssetList();
}

void AssetsLibraryModule::onAssetSelected(QListWidgetItem* item)
{
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        m_previewWidget->loadAsset(path);
        QFileInfo fi(path);
        m_detailName->setText(fi.fileName());
        m_detailSize->setText(formatFileSize(fi.size()));
        m_detailPath->setText(path);
    }

    int typeIdx = item->data(Qt::UserRole + 1).toInt();
    QStringList typeNames = {"Unknown", "Model", "Mesh", "Texture", "Audio", "Material",
                             "Physics", "Animation", "Scene", "Bundle", "Config", "Script", "Font"};
    m_detailType->setText(typeNames.value(typeIdx, "Unknown"));
}

void AssetsLibraryModule::refreshAssetList()
{
    m_assetList->clear();

    AssetManager* mgr = AssetManager::instance();
    if (!mgr) {
        m_statusLabel->setText("AssetManager not available");
        return;
    }

    QVector<Asset> allAssets = mgr->getAssets();
    int filterType = m_typeFilter->currentIndex() - 1;
    int count = 0;

    for (const auto& asset : allAssets) {
        if (filterType >= 0 && static_cast<int>(asset.type) != filterType)
            continue;

        QListWidgetItem* item = new QListWidgetItem(asset.name);
        item->setData(Qt::UserRole, asset.path);
        item->setData(Qt::UserRole + 1, static_cast<int>(asset.type));
        if (!asset.tags.isEmpty())
            item->setToolTip("Tags: " + asset.tags);

        QFont f = item->font();
        if (asset.type == AssetType::Texture || asset.type == AssetType::Audio)
            f.setItalic(true);
        item->setFont(f);

        m_assetList->addItem(item);
        count++;
    }

    m_statusLabel->setText(QString("%1 asset(s)").arg(count));
}

QString AssetsLibraryModule::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

void AssetsLibraryModule::onBatchImportClicked()
{
    QString dirPath = QFileDialog::getExistingDirectory(this, "Import Assets from Directory",
        QString(), QFileDialog::ShowDirsOnly);
    if (dirPath.isEmpty()) return;
    importDirectory(dirPath);
}

void AssetsLibraryModule::onImportOptionsClicked()
{
    QStringList actions = {"Skip duplicates", "Overwrite", "Rename (default)", "Ask"};
    int defaultIdx = static_cast<int>(m_importOptions.onDuplicate);

    bool ok;
    QString choice = QInputDialog::getItem(this, "Import Options",
        "On duplicate file:", actions, defaultIdx, false, &ok);
    if (!ok) return;

    int idx = actions.indexOf(choice);
    if (idx >= 0)
        m_importOptions.onDuplicate = static_cast<AssetManager::ImportOptions::ConflictAction>(idx);

    AssetManager* mgr = AssetManager::instance();
    if (mgr) mgr->setImportOptions(m_importOptions);
}

void AssetsLibraryModule::importDirectory(const QString& dirPath)
{
    importDirectoryWithOptions(dirPath, m_importOptions);
}

void AssetsLibraryModule::importDirectoryWithOptions(const QString& dirPath,
                                                      const AssetManager::ImportOptions& options)
{
    AssetManager* mgr = AssetManager::instance();
    if (!mgr) return;

    m_statusLabel->setText("Scanning directory for assets...");
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);

    AssetManager::BatchImportResult result = mgr->importDirectory(dirPath, options);

    refreshAssetList();
}

void AssetsLibraryModule::onBatchImportProgress(int current, int total, const QString& currentFile)
{
    if (total > 0) {
        m_progressBar->setMaximum(total);
        m_progressBar->setValue(current);
        m_progressBar->setFormat(QString("%v/%m - %1").arg(currentFile));
    }
    m_statusLabel->setText(QString("Importing %1/%2: %3").arg(current).arg(total).arg(currentFile));
}

void AssetsLibraryModule::onBatchImportCompleted(const QVariantMap& summary)
{
    m_progressBar->setVisible(false);

    int imported = summary.value("imported").toInt();
    int skipped = summary.value("skipped").toInt();
    int failed = summary.value("failed").toInt();
    int converted = summary.value("converted").toInt();

    QStringList parts;
    parts << QString("%1 imported").arg(imported);
    if (converted > 0) parts << QString("%1 converted").arg(converted);
    if (skipped > 0) parts << QString("%1 skipped").arg(skipped);
    if (failed > 0) parts << QString("%1 failed").arg(failed);
    m_statusLabel->setText(parts.join(", "));
}

void AssetsLibraryModule::onConfigureCloudClicked()
{
    if (!m_cloudSync) return;

    QStringList providers = {"None", "Google Drive", "Dropbox", "OneDrive", "Local Folder"};
    QString provider = QInputDialog::getItem(this, "Cloud Sync Provider",
        "Select cloud provider:", providers, static_cast<int>(m_cloudSync->config().provider), false);
    if (provider.isEmpty()) return;

    CloudSyncConfig config;
    config.provider = static_cast<CloudProviderType>(providers.indexOf(provider));
    config.localCachePath = QDir::homePath() + "/kseditor/cloud_cache";
    config.autoSync = false;
    m_cloudSync->configure(config);

    if (config.provider != CloudProviderType::None) {
        QString msg = QString("Cloud sync configured: %1").arg(provider);
        m_statusLabel->setText(msg);
        LOG_INFO("AssetsLibraryModule", msg);
    }
}

void AssetsLibraryModule::onSyncClicked()
{
    if (m_cloudSync && !m_cloudSync->isSyncing()) {
        m_cloudSync->syncNow();
        m_statusLabel->setText("Syncing...");
    }
}

void AssetsLibraryModule::onSyncCompleted(bool success, const QString& message)
{
    m_statusLabel->setText(success
        ? QString("Sync completed: %1").arg(message)
        : QString("Sync failed: %1").arg(message));
    if (!success) {
        LOG_ERROR("AssetsLibraryModule", message);
    }
}

} // namespace ks
