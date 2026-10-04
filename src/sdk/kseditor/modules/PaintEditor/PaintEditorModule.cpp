#include "PaintEditorModule.h"
#include "PaintEditorWidget.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QMainWindow>
#include <QFileInfo>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QVBoxLayout>

namespace ks {

// ══════════════════════════════════════════════════════════════════════
// PaintEditor Implementation
// ══════════════════════════════════════════════════════════════════════

PaintEditor* PaintEditor::s_instance = nullptr;

PaintEditor::PaintEditor(QObject* parent)
    : QObject(parent)
{
}

PaintEditor* PaintEditor::instance()
{
    if (!s_instance) {
        s_instance = new PaintEditor();
    }
    return s_instance;
}

void PaintEditor::setCarPath(const QString& path)
{
    if (m_carPath == path) return;
    m_carPath = path;

    if (m_paintManager) {
        delete m_paintManager;
        m_paintManager = nullptr;
    }

    m_paintManager = new PaintManager(path);
    m_paintManager->loadSkins();

    m_currentSkin.clear();
    m_config = PaintSystem::SkinConfig();

    emit skinListChanged();
}

bool PaintEditor::loadSkins()
{
    if (!m_paintManager) return false;
    m_paintManager->loadSkins();
    emit skinListChanged();
    return true;
}

bool PaintEditor::createSkin(const QString& name)
{
    if (!m_paintManager) return false;
    bool ok = m_paintManager->createSkin(name);
    if (ok) {
        emit skinListChanged();
    }
    return ok;
}

bool PaintEditor::deleteSkin(const QString& name)
{
    if (!m_paintManager) return false;
    bool ok = m_paintManager->deleteSkin(name);
    if (ok) {
        if (m_currentSkin == name) {
            m_currentSkin.clear();
            m_config = PaintSystem::SkinConfig();
        }
        emit skinListChanged();
    }
    return ok;
}

bool PaintEditor::duplicateSkin(const QString& sourceName, const QString& destName)
{
    if (!m_paintManager) return false;
    bool ok = m_paintManager->duplicateSkin(sourceName, destName);
    if (ok) {
        emit skinListChanged();
    }
    return ok;
}

bool PaintEditor::setCurrentSkin(const QString& skinName)
{
    if (!m_paintManager) return false;
    bool ok = m_paintManager->setCurrentSkin(skinName);
    if (ok) {
        m_currentSkin = skinName;
        m_config = m_paintManager->currentConfig();

        QString skinPath = m_carPath + "/skins/" + skinName;
        loadPaintTexture(skinPath);

        emit skinLoaded(skinName);
    }
    return ok;
}

QStringList PaintEditor::getSkinNames() const
{
    if (!m_paintManager) return QStringList();

    QStringList names;
    const auto skins = m_paintManager->getSkins();
    for (const auto& skin : skins) {
        names.append(skin.name);
    }
    return names;
}

bool PaintEditor::loadPaintTexture(const QString& skinPath)
{
    QString texturePath = skinPath + "/sides_1.png";
    if (!QFile::exists(texturePath)) {
        texturePath = skinPath + "/paint.png";
    }

    if (!QFile::exists(texturePath)) {
        qWarning() << "No paint texture found in:" << skinPath;
        return false;
    }

    QImage texture(texturePath);
    if (texture.isNull()) {
        qWarning() << "Failed to load paint texture:" << texturePath;
        return false;
    }

    m_paintPainter.setTexture(texture);
    emit textureLoaded(texture);
    return true;
}

bool PaintEditor::savePaintTexture(const QImage& texture, const QString& skinPath)
{
    if (texture.isNull()) {
        qWarning() << "Cannot save null texture";
        return false;
    }

    QString texturePath = skinPath + "/sides_1.png";
    bool saved = texture.save(texturePath, "PNG");
    if (!saved) {
        qWarning() << "Failed to save paint texture:" << texturePath;
        return false;
    }

    emit textureSaved(texturePath);
    return true;
}

bool PaintEditor::addLayer(const PaintSystem::PaintLayer& layer)
{
    bool ok = PaintSystem::addLayer(m_config, layer);
    if (ok) emit paintModified();
    return ok;
}

bool PaintEditor::removeLayer(int index)
{
    bool ok = PaintSystem::removeLayer(m_config, index);
    if (ok) emit paintModified();
    return ok;
}

bool PaintEditor::moveLayer(int fromIndex, int toIndex)
{
    bool ok = PaintSystem::moveLayer(m_config, fromIndex, toIndex);
    if (ok) emit paintModified();
    return ok;
}

bool PaintEditor::updateLayer(int index, const PaintSystem::PaintLayer& layer)
{
    bool ok = PaintSystem::updateLayer(m_config, index, layer);
    if (ok) emit paintModified();
    return ok;
}

bool PaintEditor::generateLicensePlate(const QString& text, const QString& country)
{
    if (m_currentSkin.isEmpty()) return false;

    QString skinPath = m_carPath + "/skins/" + m_currentSkin;
    QString outputPath = skinPath + "/license_plate.png";

    bool ok = PaintSystem::generateLicensePlate(text, country, outputPath);
    if (ok) {
        PaintSystem::PaintLayer layer;
        layer.name = "license_plate";
        layer.type = "decal";
        layer.opacity = 1.0f;
        layer.position[0] = 0.7f;
        layer.position[1] = 0.3f;
        layer.size[0] = 0.25f;
        layer.size[1] = 0.1f;
        layer.texturePath = outputPath;
        layer.visible = true;
        m_config.layers.append(layer);

        m_config.licensePlateText = text;
        m_config.licensePlateCountry = country;

        emit paintModified();
    }
    return ok;
}

bool PaintEditor::saveCurrentSkin()
{
    if (m_currentSkin.isEmpty()) return false;

    QString skinPath = m_carPath + "/skins/" + m_currentSkin;
    bool ok = PaintSystem::saveSkinConfig(m_config, skinPath);
    if (ok) {
        emit skinSaved(m_currentSkin);
    }
    return ok;
}

bool PaintEditor::exportSkin(const QString& outputPath)
{
    if (m_currentSkin.isEmpty()) return false;

    QString skinPath = m_carPath + "/skins/" + m_currentSkin;
    return PaintSystem::exportSkin(skinPath, outputPath);
}

bool PaintEditor::importSkin(const QString& importPath)
{
    if (m_carPath.isEmpty()) return false;

    bool ok = PaintSystem::importSkin(importPath, m_carPath);
    if (ok) {
        loadSkins();
    }
    return ok;
}

// ══════════════════════════════════════════════════════════════════════
// PaintEditorModule Implementation
// ══════════════════════════════════════════════════════════════════════

PaintEditorModule::PaintEditorModule(QWidget* parent)
    : EditorModule(parent)
{
}

PaintEditorWidget* PaintEditorModule::ensureWidget()
{
    if (m_editorWidget) return m_editorWidget;
    m_editorWidget = new PaintEditorWidget(this);
    m_editorWidget->setObjectName("paintEditor");
    return m_editorWidget;
}

bool PaintEditorModule::initialize()
{
    LOG_INFO("PaintEditorModule", "Initializing Paint Editor module");
    return true;
}

void PaintEditorModule::shutdown()
{
    LOG_INFO("PaintEditorModule", "Shutting down Paint Editor module");
}

QDockWidget* PaintEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (!m_dockWidget) {
        m_dockWidget = new QDockWidget("Paint Editor", mainWindow);
        m_dockWidget->setAllowedAreas(Qt::AllDockWidgetAreas);
        m_dockWidget->setWidget(ensureWidget());
    }
    return m_dockWidget;
}

void PaintEditorModule::importFile(const QString& filePath)
{
    if (!m_editorWidget) return;

    QFileInfo info(filePath);
    if (info.isDir()) {
        m_editorWidget->setCarPath(filePath);
    } else {
        m_editorWidget->setCarPath(info.absolutePath());
    }

    LOG_INFO("PaintEditorModule", QString("Imported: %1").arg(filePath));
}

void PaintEditorModule::exportFile(const QString& filePath)
{
    if (PaintEditor* editor = PaintEditor::instance()) {
        if (editor->exportSkin(filePath)) {
            LOG_INFO("PaintEditorModule", QString("Exported to: %1").arg(filePath));
        } else {
            LOG_ERROR("PaintEditorModule", QString("Export failed: %1").arg(filePath));
        }
    }
}

void PaintEditorModule::onActivation()
{
    LOG_INFO("PaintEditorModule", "Paint Editor activated");
    ensureWidget();
    if (!layout()) {
        auto* l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_editorWidget);
    }
}

void PaintEditorModule::onDeactivation()
{
    LOG_INFO("PaintEditorModule", "Paint Editor deactivated");
}

} // namespace ks