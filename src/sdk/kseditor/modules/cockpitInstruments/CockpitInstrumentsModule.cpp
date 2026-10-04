#include "CockpitInstrumentsModule.h"
#include "CockpitInstrumentsQmlBridge.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QUrl>
#include <QQmlContext>
#include <QJsonObject>

namespace ks {

CockpitInstrumentsModule::CockpitInstrumentsModule(QWidget* parent)
    : EditorModule(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    layout->addWidget(m_quickWidget);

    setLayout(layout);
}

CockpitInstrumentsModule::~CockpitInstrumentsModule()
{
}

bool CockpitInstrumentsModule::initialize()
{
    if (!m_initialized) {
        m_bridge = CockpitInstrumentsQmlBridge::instance();
        auto* ctx = m_quickWidget->rootContext();
        ctx->setContextProperty("displayBridge", m_bridge);
        m_quickWidget->setSource(QUrl("qrc:///qml/modules/car_CockpitInstruments.qml"));
        m_initialized = true;
    }

    LOG_INFO("CockpitInstrumentsModule", "Display Editor module initialized");
    return true;
}

void CockpitInstrumentsModule::shutdown()
{
    LOG_INFO("CockpitInstrumentsModule", "Shutting down Display Editor module");
    if (m_initialized) {
        m_bridge = nullptr;
        m_quickWidget->setSource(QUrl());
        m_initialized = false;
    }
}

void CockpitInstrumentsModule::exportFile(const QString& filePath)
{
    if (m_bridge) {
        m_bridge->saveToFile(filePath);
    }
}

void CockpitInstrumentsModule::importFile(const QString& filePath)
{
    if (m_bridge) {
        m_bridge->loadFromFile(filePath);
    }
}

void CockpitInstrumentsModule::onActivation()
{
    if (!m_initialized) {
        initialize();
    }
    EditorModule::onActivation();
}

void CockpitInstrumentsModule::onDeactivation()
{
    EditorModule::onDeactivation();
}

QJsonObject CockpitInstrumentsModule::serializeProject() const
{
    QJsonObject data;
    if (m_bridge) {
        data["displayName"] = m_bridge->displayName();
        data["elementCount"] = m_bridge->elementCount();
        data["currentFile"] = m_bridge->currentFile();
    }
    return data;
}

void CockpitInstrumentsModule::deserializeProject(const QJsonObject& data)
{
    if (data.contains("currentFile")) {
        if (!m_initialized) initialize();
        if (m_bridge) {
            m_bridge->loadFromFile(data["currentFile"].toString());
        }
    }
}

} // namespace ks
