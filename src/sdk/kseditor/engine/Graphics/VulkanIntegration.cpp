#include "VulkanIntegration.h"
#include "../sys/LogManager.h"
#include <QDir>
#include <QFileInfo>

using namespace ks;

KsVulkanIntegration* KsVulkanIntegration::s_instance = nullptr;

KsVulkanIntegration::KsVulkanIntegration(QObject* parent)
    : QObject(parent)
{
}

KsVulkanIntegration::~KsVulkanIntegration()
{
    if (m_ppFilterRenderer) {
        delete m_ppFilterRenderer;
    }
    if (m_renderer) {
        delete m_renderer;
    }
    qDeleteAll(m_ppPresets);
    m_ppPresets.clear();
    s_instance = nullptr;
}

KsVulkanIntegration* KsVulkanIntegration::instance()
{
    if (!s_instance) {
        s_instance = new KsVulkanIntegration();
    }
    return s_instance;
}

bool KsVulkanIntegration::initialize(const QString& systemPath)
{
    if (m_initialized) {
        LOG_WARNING("KsVulkanIntegration", "Already initialized");
        return true;
    }

    m_systemPath = systemPath;
    if (!QFileInfo::exists(systemPath)) {
        LOG_ERROR("KsVulkanIntegration", "System path does not exist: " + systemPath);
        emit initialized(false);
        return false;
    }

    LOG_INFO("KsVulkanIntegration", "Initializing with system path: " + systemPath);

    KsConfigLoader::instance().loadAll();

    ShaderParamRegistry::instance().registerBuiltInShaders();

    if (!setupShaderDirectories()) {
        LOG_ERROR("KsVulkanIntegration", "Failed to setup shader directories");
        emit initialized(false);
        return false;
    }

    if (!loadPPFilterPresets()) {
        LOG_WARNING("KsVulkanIntegration", "Failed to load PP filter presets");
    }

    m_renderer = new ks::VulkanRenderer();

    if (m_renderer->physicalDevice() != VK_NULL_HANDLE) {
        m_ppFilterRenderer = new ks::PPFilterRenderer(
            m_renderer->physicalDevice(),
            m_renderer->device(),
            m_renderer->graphicsQueue(),
            m_renderer->commandPool()
        );
        VkPipelineVertexInputStateCreateInfo ppVertexInput{};
        ppVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        m_ppFilterRenderer->initialize(m_spirvShadersDir, VK_NULL_HANDLE, ppVertexInput);
    }

    m_initialized = true;
    LOG_INFO("KsVulkanIntegration", "KS Vulkan integration initialized successfully");

    emit initialized(true);
    return true;
}

bool KsVulkanIntegration::setupShaderDirectories()
{
    m_shadersDir = m_systemPath + "/shaders";
    m_spirvShadersDir = m_systemPath + "/shaders/vulkan";

    QDir spirvDir(m_spirvShadersDir);
    if (!spirvDir.exists()) {
        spirvDir.mkpath(".");
    }

    m_shaderPathMap = {
        {"ksPerPixel", m_spirvShadersDir + "/ksPerPixel.vert.spv"},
        {"ksPerPixelAT", m_spirvShadersDir + "/ksPerPixelAT.vert.spv"},
        {"ksMultilayer", m_spirvShadersDir + "/ksMultilayer.vert.spv"},
        {"ksCarPaintSimple", m_spirvShadersDir + "/ksCarPaintSimple.vert.spv"},
        {"ksWindscreen", m_spirvShadersDir + "/ksWindscreen.vert.spv"},
        {"ksSky", m_spirvShadersDir + "/ksSky.vert.spv"},
        {"ksClouds", m_spirvShadersDir + "/ksClouds.vert.spv"},
        {"ksShadowGen", m_spirvShadersDir + "/ksShadowGen.vert.spv"},
        {"ksPostProcess", m_spirvShadersDir + "/ksPostProcess.vert.spv"}
    };

    return true;
}

bool KsVulkanIntegration::loadPPFilterPresets()
{
    QDir ppDir(ppFiltersDir());
    if (!ppDir.exists()) {
        LOG_WARNING("KsVulkanIntegration", "PP filters directory not found");
        return false;
    }

    QStringList filters = {"*.ini"};
    QFileInfoList files = ppDir.entryInfoList(filters, QDir::Files);

    LOG_INFO("KsVulkanIntegration", QString("Found %1 PP filter presets").arg(files.size()));

    for (const QFileInfo& file : files) {
        QString presetName = file.baseName();
        PPFilterPreset* preset = new PPFilterPreset(presetName);
        QSettings settings(file.absoluteFilePath(), QSettings::IniFormat);
        preset->loadFromSettings(&settings);
        m_ppPresets[presetName] = preset;
        LOG_INFO("KsVulkanIntegration", QString("Loaded PP preset: %1").arg(presetName));
    }

    if (m_ppPresets.contains("default")) {
        m_currentPPPreset = m_ppPresets["default"];
    } else if (!m_ppPresets.isEmpty()) {
        m_currentPPPreset = m_ppPresets.first();
    }

    return !m_ppPresets.isEmpty();
}

bool KsVulkanIntegration::loadKsShader(const QString& shaderName, VkShaderStageFlagBits stage)
{
    QString path = getShaderPath(shaderName, stage);
    if (path.isEmpty()) {
        LOG_ERROR("KsVulkanIntegration", "No path for shader: " + shaderName);
        emit shaderLoadFailed(shaderName, "Unknown shader name");
        return false;
    }

    if (!QFileInfo::exists(path)) {
        LOG_WARNING("KsVulkanIntegration", 
            QString("SPIR-V shader not found: %1 (shader: %2)").arg(path).arg(shaderName));

        QString glslPath = m_shadersDir + "/" + shaderName.toLower() + 
                         (stage == VK_SHADER_STAGE_VERTEX_BIT ? ".vert" : ".frag");
        if (QFileInfo::exists(glslPath)) {
            LOG_INFO("KsVulkanIntegration", 
                QString("Found GLSL source but no compiled SPIR-V: %1").arg(glslPath));
            emit shaderLoadFailed(shaderName, "SPIR-V not compiled: " + path);
        } else {
            emit shaderLoadFailed(shaderName, "Shader file not found: " + path);
        }
        return false;
    }

    if (m_ppFilterRenderer) {
        if (m_ppFilterRenderer->loadShader(shaderName, stage, path)) {
            emit shaderLoaded(shaderName);
            return true;
        }
    }

    emit shaderLoadFailed(shaderName, "Failed to load shader module");
    return false;
}

QString KsVulkanIntegration::getShaderPath(const QString& shaderName, VkShaderStageFlagBits stage) const
{
    if (m_shaderPathMap.contains(shaderName)) {
        // For fragment shaders, replace .vert.spv with .frag.spv
        QString basePath = m_shaderPathMap[shaderName];
        if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
            QString dir = QFileInfo(basePath).absolutePath();
            return dir + "/" + shaderName + ".frag.spv";
        }
        return basePath;
    }

    QString ext = stage == VK_SHADER_STAGE_VERTEX_BIT ? ".vert.spv" : ".frag.spv";
    return m_spirvShadersDir + "/" + shaderName + ext;
}

void KsVulkanIntegration::applyGraphicsSettings()
{
    const KsConfigLoader::GraphicsSettings& settings = graphicsSettings();

    if (m_ppFilterRenderer) {
        ks::VulkanShaderLoader::GraphicsSettings gfx;
        gfx.mipLodBias = settings.mipLodBias;
        gfx.shadowMapBias = settings.shadowMapBias0;
        m_ppFilterRenderer->applyGraphicsFromConfig(gfx);
    }

    emit graphicsSettingsChanged();
}

void KsVulkanIntegration::applyLightingSettings()
{
    const KsConfigLoader::LightingSettings& settings = lightingSettings();

    if (m_ppFilterRenderer) {
        ks::VulkanShaderLoader::LightingSettings light;
        light.ambientColor[0] = settings.ambientColor.redF();
        light.ambientColor[1] = settings.ambientColor.greenF();
        light.ambientColor[2] = settings.ambientColor.blueF();
        light.ambientIntensity = settings.lightIntensity * 100.0f;
        light.hdrExposure = settings.lightIntensity;
        m_ppFilterRenderer->applyLightingFromConfig(light);
    }

    emit lightingSettingsChanged();
}

void KsVulkanIntegration::applyPPFilterPreset(const PPFilterPreset* preset)
{
    m_currentPPPreset = const_cast<PPFilterPreset*>(preset);

    if (m_ppFilterRenderer && preset) {
        m_ppFilterRenderer->updateFromPreset(*preset);
    }

    if (preset) {
        emit ppFilterPresetChanged(preset->name());
    }
}

void KsVulkanIntegration::bindMaterial(const ks::VulkanShaderLoader::MaterialParams& params)
{
    if (m_ppFilterRenderer) {
        m_ppFilterRenderer->updateMaterialUBO(params);
    }
}

void KsVulkanIntegration::bindCamera(const QMatrix4x4& view, const QMatrix4x4& projection,
                                     const QVector3D& cameraPos, float nearPlane, float farPlane)
{
    if (m_ppFilterRenderer) {
        m_ppFilterRenderer->updateCameraUBO(view, projection, cameraPos, nearPlane, farPlane);
    }
}

QVariant KsVulkanIntegration::getShaderParam(const QString& shader, const QString& param,
                                              VkShaderStageFlagBits stage) const
{
    const char* stagePrefix = "";
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:   stagePrefix = "vs_"; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT: stagePrefix = "fs_"; break;
        case VK_SHADER_STAGE_GEOMETRY_BIT: stagePrefix = "gs_"; break;
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    stagePrefix = "tcs_"; break;
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: stagePrefix = "tes_"; break;
        case VK_SHADER_STAGE_COMPUTE_BIT:  stagePrefix = "cs_"; break;
        default: break;
    }
    QString stageKey = QString("%1%2").arg(stagePrefix, shader);
    QVariant result = ShaderParamRegistry::instance().getParam(stageKey, param);
    if (result.isValid()) return result;
    return ShaderParamRegistry::instance().getParam(shader, param);
}

void KsVulkanIntegration::setGlobalParam(const QString& name, const QVariant& value)
{
    ShaderParamRegistry::instance().setGlobalParam(name, value);
}

QVariant KsVulkanIntegration::getGlobalParam(const QString& name) const
{
    return ShaderParamRegistry::instance().getGlobalParam(name);
}
