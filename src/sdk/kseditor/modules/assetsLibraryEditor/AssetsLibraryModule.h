#pragma once

#include "../editor/EditorModule.h"
#include "sdk/kseditor/engine/assets/AssetPreviewWidget.h"
#include "sdk/kseditor/engine/assets/AssetManager.h"
#include "sdk/kseditor/engine/assets/CloudSyncManager.h"
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QProgressBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

namespace ks {

class AssetsLibraryModule : public EditorModule {
    Q_OBJECT
public:
    explicit AssetsLibraryModule(QWidget* parent = nullptr);
    ~AssetsLibraryModule() override = default;

    bool initialize() override;
    void shutdown() override;
    QString moduleName() const override { return "Assets Library"; }
    QString moduleId() const override { return "assetsLibrary"; }
    int getModulePriority() const override { return 10; }

    void importFile(const QString& filePath) override;
    void exportFile(const QString& filePath) override;

    // Batch import
    Q_SLOT void importDirectory(const QString& dirPath);
    Q_SLOT void importDirectoryWithOptions(const QString& dirPath, const AssetManager::ImportOptions& options);
    void setImportOptions(const AssetManager::ImportOptions& options) { m_importOptions = options; }
    AssetManager::ImportOptions importOptions() const { return m_importOptions; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onLoadAssetClicked();
    void onRefreshClicked();
    void onSearchTextChanged(const QString& text);
    void onTypeFilterChanged(int index);
    void onAssetSelected(QListWidgetItem* item);
    void onBatchImportClicked();
    void onBatchImportProgress(int current, int total, const QString& currentFile);
    void onBatchImportCompleted(const QVariantMap& summary);
    void onImportOptionsClicked();
    void onSyncClicked();
    void onSyncCompleted(bool success, const QString& message);
    void onConfigureCloudClicked();

private:
    void refreshAssetList();
    QString formatFileSize(qint64 bytes) const;

    // UI
    QSplitter* m_splitter;
    QListWidget* m_assetList;
    QLineEdit* m_searchEdit;
    QComboBox* m_typeFilter;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    AssetPreviewWidget* m_previewWidget;
    QLabel* m_detailName;
    QLabel* m_detailType;
    QLabel* m_detailSize;
    QLabel* m_detailPath;

    CloudSyncManager* m_cloudSync = nullptr;
    AssetManager::ImportOptions m_importOptions;
};

} // namespace ks
