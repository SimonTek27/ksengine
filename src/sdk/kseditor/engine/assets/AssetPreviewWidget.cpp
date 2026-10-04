#include "AssetPreviewWidget.h"
#include <QPainter>
#include <QVBoxLayout>
#include <QDebug>

namespace ks {

AssetPreviewWidget::AssetPreviewWidget(QWidget* parent)
    : QLabel(parent), m_previewWidth(200), m_previewHeight(200) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(100, 100);
    setScaledContents(false);
    setStyleSheet("QLabel { background-color: #2d2d30; border: 1px solid #3e3e42; border-radius: 4px; }");
    clearPreview();
}

void AssetPreviewWidget::loadAsset(const QString& filePath, int width, int height) {
    if (filePath.isEmpty()) {
        clearPreview();
        return;
    }

    m_currentFilePath = filePath;
    m_previewWidth = width;
    m_previewHeight = height;

    // Show loading state
    setText("Loading...");
    setPixmap(QPixmap());

    // Create and start preview task
    // Use QueuedConnection to safely invoke onPreviewReady from the main thread
    // even though the callback fires from the thread pool
    auto task = new AssetPreviewTask(filePath, width, height,
        [this](const QPixmap& preview) {
            QMetaObject::invokeMethod(this, [this, preview]() {
                onPreviewReady(preview);
            }, Qt::QueuedConnection);
        });

    m_threadPool.start(task);
}

void AssetPreviewWidget::clearPreview() {
    m_currentFilePath.clear();
    setText("No Preview");
    setPixmap(QPixmap());
}

void AssetPreviewWidget::onPreviewReady(const QPixmap& preview) {
    if (!m_currentFilePath.isEmpty()) {
        setPixmap(preview);
        emit previewLoaded(m_currentFilePath);
    }
}

} // namespace ks