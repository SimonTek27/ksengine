#include "FontCreatorEditorModule.h"
#include "FontCreatorQmlBridge.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QUrl>
#include <QQmlContext>
#include <QJsonObject>

namespace ks {

FontCreatorEditorModule::FontCreatorEditorModule(QWidget* parent)
    : EditorModule(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    layout->addWidget(m_quickWidget);

    setLayout(layout);
}

FontCreatorEditorModule::~FontCreatorEditorModule()
{
}

bool FontCreatorEditorModule::initialize()
{
    if (!m_initialized) {
        m_bridge = FontCreatorQmlBridge::instance();
        auto* ctx = m_quickWidget->rootContext();
        ctx->setContextProperty("fontCreatorBridge", m_bridge);
        m_quickWidget->setSource(QUrl("qrc:///qml/modules/font_KSFontCreator.qml"));
        m_initialized = true;
    }

    LOG_INFO("FontCreatorEditorModule", "Font Creator module initialized");
    return true;
}

void FontCreatorEditorModule::shutdown()
{
    LOG_INFO("FontCreatorEditorModule", "Shutting down Font Creator module");
    if (m_initialized) {
        m_bridge = nullptr;
        m_quickWidget->setSource(QUrl());
        m_initialized = false;
    }
}

void FontCreatorEditorModule::exportFile(const QString& filePath)
{
    if (m_bridge) {
        if (filePath.endsWith(".acf", Qt::CaseInsensitive))
            m_bridge->savePreset(filePath);
        else if (filePath.endsWith(".json", Qt::CaseInsensitive))
            m_bridge->exportToJSON(filePath);
        else
            m_bridge->generateAtlas(filePath);
    }
}

void FontCreatorEditorModule::importFile(const QString& filePath)
{
    if (m_bridge) {
        if (filePath.endsWith(".acf", Qt::CaseInsensitive))
            m_bridge->loadPreset(filePath);
        else if (filePath.endsWith(".json", Qt::CaseInsensitive))
            m_bridge->importFromJSON(filePath);
    }
}

void FontCreatorEditorModule::onActivation()
{
    if (!m_initialized) {
        initialize();
    }
    EditorModule::onActivation();
}

void FontCreatorEditorModule::onDeactivation()
{
    EditorModule::onDeactivation();
}

QJsonObject FontCreatorEditorModule::serializeProject() const
{
    QJsonObject data;
    if (m_bridge) {
        data["currentFont"] = m_bridge->currentFont();
        data["fontSize"] = m_bridge->fontSize();
        data["atlasWidth"] = m_bridge->atlasWidth();
        data["atlasHeight"] = m_bridge->atlasHeight();
    }
    return data;
}

void FontCreatorEditorModule::deserializeProject(const QJsonObject& data)
{
    if (data.contains("currentFont")) {
        if (!m_initialized) initialize();
        if (m_bridge) {
            m_bridge->setFontFamily(data["currentFont"].toString());
            if (data.contains("fontSize"))
                m_bridge->setFontSize(data["fontSize"].toInt());
        }
    }
}

} // namespace ks
