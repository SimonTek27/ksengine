#include "SoundEditorModule.h"
#include "AudioEditorModule.h"
#include "sdk/kseditor/engine/Audio/AudioStudioTypes.h"
#include "sdk/kseditor/qmlbridges/Audio/AudioWaveformBridge.h"
#include "sdk/kseditor/engine/Audio/KsACSndEventBridge.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QUrl>
#include <QQmlContext>
#include <QJsonObject>

namespace ks {

SoundEditorModule::SoundEditorModule(QWidget* parent)
    : EditorModule(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    layout->addWidget(m_quickWidget);

    setLayout(layout);
}

SoundEditorModule::~SoundEditorModule()
{
    delete m_waveformBridge;
    delete m_eventBridge;
}

bool SoundEditorModule::initialize()
{
    if (!m_initialized) {
        if (!AudioEditorModule::instance())
            new AudioEditorModule(this);

        m_waveformBridge = new ks::audio::AudioWaveformBridge(this);
        m_eventBridge = new ks::audio::KsACSndEventBridge(this);
        auto* ctx = m_quickWidget->rootContext();
        ctx->setContextProperty("waveformBridge", m_waveformBridge);
        ctx->setContextProperty("eventDefs", m_eventBridge);
        m_quickWidget->setSource(QUrl("qrc:///qml/pages/page_ksAudioEditor.qml"));
        m_initialized = true;
    }

    LOG_INFO("SoundEditorModule", "Sound Editor module initialized");
    return true;
}

void SoundEditorModule::shutdown()
{
    LOG_INFO("SoundEditorModule", "Shutting down Sound Editor module");
    if (m_initialized) {
        delete m_waveformBridge;
        m_waveformBridge = nullptr;
        delete m_eventBridge;
        m_eventBridge = nullptr;
        m_quickWidget->setSource(QUrl());
        m_initialized = false;
    }
}

void SoundEditorModule::loadAudioFile(const QString& path)
{
    if (m_waveformBridge) {
        m_waveformBridge->loadFile(path);
    }
}

void SoundEditorModule::exportFile(const QString& filePath)
{
    if (auto* audio = AudioEditorModule::instance()) {
        audio->onExportAsset();
    } else {
        LOG_WARNING("SoundEditorModule", "AudioEditorModule not available for export");
    }
    loadAudioFile(filePath);
}

void SoundEditorModule::importFile(const QString& filePath)
{
    if (auto* audio = AudioEditorModule::instance()) {
        audio->onImportAsset();
    } else {
        LOG_WARNING("SoundEditorModule", "AudioEditorModule not available for import");
    }
    loadAudioFile(filePath);
}

void SoundEditorModule::onActivation()
{
    if (!m_initialized) {
        initialize();
    }
    EditorModule::onActivation();
}

void SoundEditorModule::onDeactivation()
{
    EditorModule::onDeactivation();
}

QJsonObject SoundEditorModule::serializeProject() const
{
    QJsonObject data;
    if (m_waveformBridge && m_waveformBridge->hasData()) {
        data["hasAudio"] = true;
    }
    return data;
}

void SoundEditorModule::deserializeProject(const QJsonObject& data)
{
    if (data.contains("hasAudio") && data["hasAudio"].toBool()) {
        if (!m_initialized) initialize();
        if (m_waveformBridge) {
            if (data.contains("filePath")) {
                m_waveformBridge->loadFile(data["filePath"].toString());
            }
        }
    }
}

} // namespace ks
