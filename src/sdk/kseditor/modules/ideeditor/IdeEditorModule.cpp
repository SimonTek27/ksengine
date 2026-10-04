#include "IdeEditorModule.h"
#include "IdeEditorWidget.h"
#include "sdk/kseditor/engine/sys/LogManager.h"

#include <QVBoxLayout>
#include <QMainWindow>
#include <QFileInfo>
#include <QDir>

namespace ks {

IdeEditorModule::IdeEditorModule(QWidget* parent)
    : EditorModule(parent)
    , m_dockWidget(nullptr)
    , m_editorWidget(nullptr)
{
    setAcceptDrops(true);
}

IdeEditorModule::~IdeEditorModule()
{
    delete m_dockWidget;
}

bool IdeEditorModule::initialize()
{
    if (m_initialized) return true;
    LOG_INFO("IdeEditorModule", "Initializing IDE Editor module");
    m_initialized = true;
    return true;
}

void IdeEditorModule::shutdown()
{
    LOG_INFO("IdeEditorModule", "Shutting down IDE Editor module");
    m_initialized = false;
}

QDockWidget* IdeEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;

    m_dockWidget = new QDockWidget("IDE Editor", mainWindow);
    m_dockWidget->setObjectName("IdeEditorDock");
    m_dockWidget->setAllowedAreas(Qt::AllDockWidgetAreas);

    setupUI();

    m_dockWidget->setWidget(this);
    return m_dockWidget;
}

void IdeEditorModule::setupUI()
{
    if (m_editorWidget) return;

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_editorWidget = new IdeEditorWidget(this);
    layout->addWidget(m_editorWidget, 1);
}

void IdeEditorModule::importFile(const QString& filePath)
{
    if (!m_editorWidget) setupUI();
    QString absPath = QFileInfo(filePath).absoluteFilePath();
    m_editorWidget->openFile(absPath);
}

void IdeEditorModule::exportFile(const QString& filePath)
{
    Q_UNUSED(filePath);
    if (m_editorWidget) m_editorWidget->saveCurrentAs();
}

bool IdeEditorModule::canCut() const { return m_editorWidget && m_editorWidget->canCut(); }
bool IdeEditorModule::canCopy() const { return m_editorWidget && m_editorWidget->canCopy(); }
bool IdeEditorModule::canPaste() const { return m_editorWidget && m_editorWidget->canPaste(); }
bool IdeEditorModule::canDelete() const { return m_editorWidget && m_editorWidget->canDelete(); }

void IdeEditorModule::cut() { if (m_editorWidget) m_editorWidget->cut(); }
void IdeEditorModule::copy() { if (m_editorWidget) m_editorWidget->copy(); }
void IdeEditorModule::paste() { if (m_editorWidget) m_editorWidget->paste(); }
void IdeEditorModule::deleteSelected() { if (m_editorWidget) m_editorWidget->deleteSelected(); }

QJsonObject IdeEditorModule::serializeProject() const
{
    return QJsonObject();
}

void IdeEditorModule::deserializeProject(const QJsonObject& data)
{
    Q_UNUSED(data);
}

void IdeEditorModule::onActivation()
{
    if (!m_editorWidget) setupUI();
    LOG_INFO("IdeEditorModule", "IDE Editor activated");
}

void IdeEditorModule::onDeactivation()
{
    LOG_INFO("IdeEditorModule", "IDE Editor deactivated");
}

} // namespace ks
