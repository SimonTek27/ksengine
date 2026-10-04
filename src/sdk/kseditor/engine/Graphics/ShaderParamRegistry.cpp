#include "ShaderParamRegistry.h"

namespace ks {

ShaderParamRegistry* ShaderParamRegistry::s_instance = nullptr;

ShaderParamRegistry& ShaderParamRegistry::instance()
{
    if (!s_instance) {
        s_instance = new ShaderParamRegistry();
    }
    return *s_instance;
}

void ShaderParamRegistry::registerShader(const QString& shaderName)
{
    if (!m_shaders.contains(shaderName)) {
        m_shaders[shaderName] = QMap<QString, ShaderParamInfo>();
        m_paramValues[shaderName] = QMap<QString, QVariant>();
    }
}

void ShaderParamRegistry::unregisterShader(const QString& shaderName)
{
    m_shaders.remove(shaderName);
    m_paramValues.remove(shaderName);
}

void ShaderParamRegistry::registerParam(const QString& shaderName, const ShaderParamInfo& param)
{
    if (m_shaders.contains(shaderName)) {
        m_shaders[shaderName][param.name] = param;
        if (param.defaultValue.isValid()) {
            m_paramValues[shaderName][param.name] = param.defaultValue;
        }
    }
}

void ShaderParamRegistry::setParam(const QString& shaderName, const QString& paramName, const QVariant& value)
{
    if (m_paramValues.contains(shaderName)) {
        m_paramValues[shaderName][paramName] = value;
    }
}

QVariant ShaderParamRegistry::getParam(const QString& shaderName, const QString& paramName) const
{
    if (m_paramValues.contains(shaderName) && m_paramValues[shaderName].contains(paramName)) {
        return m_paramValues[shaderName][paramName];
    }
    return QVariant();
}

void ShaderParamRegistry::registerBuiltInShaders()
{
    registerShader("ksPerPixel");
    registerShader("ksMultilayer");
    registerShader("ksPostProcess");
    registerShader("ksSky");

    registerParam("ksPerPixel", { "modelMatrix", "mat4", 0, QVariant() });
    registerParam("ksPerPixel", { "viewMatrix", "mat4", 1, QVariant() });
    registerParam("ksPerPixel", { "projectionMatrix", "mat4", 2, QVariant() });
    registerParam("ksPerPixel", { "normalMatrix", "mat4", 3, QVariant() });

    registerParam("ksPerPixel", { "ambientColor", "vec4", 10, QVariant::fromValue(QVector4D(0.2f, 0.2f, 0.2f, 1.0f)) });
    registerParam("ksPerPixel", { "diffuseColor", "vec4", 11, QVariant::fromValue(QVector4D(0.8f, 0.8f, 0.8f, 1.0f)) });
    registerParam("ksPerPixel", { "specularColor", "vec4", 12, QVariant::fromValue(QVector4D(1.0f, 1.0f, 1.0f, 1.0f)) });
    registerParam("ksPerPixel", { "emissiveColor", "vec4", 13, QVariant::fromValue(QVector4D(0.0f, 0.0f, 0.0f, 1.0f)) });

    registerParam("ksPerPixel", { "lightDirection", "vec3", 20, QVariant::fromValue(QVector3D(0.5f, 1.0f, 0.5f)) });
    registerParam("ksPerPixel", { "lightColor", "vec4", 21, QVariant::fromValue(QVector4D(1.0f, 1.0f, 1.0f, 1.0f)) });

    registerParam("ksPerPixel", { "specularEXP", "float", 30, 32.0f });
    registerParam("ksPerPixel", { "alpha", "float", 31, 1.0f });
    registerParam("ksPerPixel", { "doubleSided", "bool", 32, false });

    registerParam("ksPerPixel", { "diffuseMap", "sampler2D", 40, QVariant() });
    registerParam("ksPerPixel", { "normalMap", "sampler2D", 41, QVariant() });
    registerParam("ksPerPixel", { "specularMap", "sampler2D", 42, QVariant() });

    registerShader("ksMultilayer");
    registerParam("ksMultilayer", { "layerCount", "int", 0, 1 });
    registerParam("ksMultilayer", { "layer0Texture", "sampler2D", 1, QVariant() });
    registerParam("ksMultilayer", { "layer0Normal", "sampler2D", 2, QVariant() });

    registerShader("ksPostProcess");
    registerParam("ksPostProcess", { "exposure", "float", 0, 1.0f });
    registerParam("ksPostProcess", { "contrast", "float", 1, 1.0f });
    registerParam("ksPostProcess", { "saturation", "float", 2, 1.0f });
    registerParam("ksPostProcess", { "vignetteIntensity", "float", 3, 0.0f });
    registerParam("ksPostProcess", { "chromaticAberration", "float", 4, 0.0f });

    registerShader("ksSky");
    registerParam("ksSky", { "skyTexture", "samplerCube", 0, QVariant() });
    registerParam("ksSky", { "horizonColor", "vec4", 1, QVariant::fromValue(QVector4D(0.5f, 0.7f, 1.0f, 1.0f)) });
    registerParam("ksSky", { "zenithColor", "vec4", 2, QVariant::fromValue(QVector4D(0.2f, 0.4f, 0.8f, 1.0f)) });
    registerParam("ksSky", { "groundColor", "vec4", 3, QVariant::fromValue(QVector4D(0.3f, 0.25f, 0.2f, 1.0f)) });

    // ================================================================
    // AC kS* shaders — complete parameter definitions
    // ================================================================
    auto regAC = [&](const QString& sh, QVector<ShaderParamInfo> params){
        registerShader(sh);
        int loc = 0;
        for (auto& p : params) { p.location = loc++; registerParam(sh, p); }
    };

    // ---- ksPerPixelNM (normal map) ---------------------------------
    regAC("ksPerPixelNM", {
        {"diffuseColor",    "vec4",      0, QVariant::fromValue(QVector4D(1,1,1,1))},
        {"specularColor",   "vec4",      0, QVariant::fromValue(QVector4D(1,1,1,1))},
        {"specularEXP",     "float",     0, 50.0f},
        {"normalStrength",  "float",     0, 1.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
    });

    // ---- ksPerPixelMultiMap ----------------------------------------
    regAC("ksPerPixelMultiMap", {
        {"ksAmbient",       "float",     0, 0.4f},
        {"ksDiffuse",       "float",     0, 0.6f},
        {"ksSpecular",      "float",     0, 1.0f},
        {"ksSpecularEXP",   "float",     0, 50.0f},
        {"useDetail",       "float",     0, 0.0f},
        {"detailUVMultiplier","float",   0, 4.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
        {"txDetail",        "sampler2D", 0, QVariant()},
        {"txMask",          "sampler2D", 0, QVariant()},
    });

    // ---- ksPerPixelMultiMap_NMDetail -------------------------------
    regAC("ksPerPixelMultiMap_NMDetail", {
        {"ksAmbient",        "float",    0, 0.4f},
        {"ksDiffuse",        "float",    0, 0.6f},
        {"ksSpecular",       "float",    0, 1.0f},
        {"ksSpecularEXP",    "float",    0, 50.0f},
        {"detailNMMult",     "float",    0, 2.0f},
        {"detailUVMultiplier","float",   0, 8.0f},
        {"txDiffuse",        "sampler2D",0, QVariant()},
        {"txNormal",         "sampler2D",0, QVariant()},
        {"txDetail",         "sampler2D",0, QVariant()},
        {"txDetailNM",       "sampler2D",0, QVariant()},
        {"txMask",           "sampler2D",0, QVariant()},
    });

    // ---- ksMultilayer_fresnel_nm (CSP extended) --------------------
    regAC("ksMultilayer_fresnel_nm", {
        {"fresnelC",        "float",     0, 0.04f},
        {"fresnelEXP",      "float",     0, 3.0f},
        {"ksSpecular",      "float",     0, 1.0f},
        {"ksSpecularEXP",   "float",     0, 50.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
        {"txMask",          "sampler2D", 0, QVariant()},
        {"txReflection",    "sampler2D", 0, QVariant()},
    });

    // ---- ksBrakeDisc -------------------------------------------
    regAC("ksBrakeDisc", {
        {"ksAmbient",       "float",     0, 0.3f},
        {"ksDiffuse",       "float",     0, 0.6f},
        {"ksSpecular",      "float",     0, 1.0f},
        {"ksSpecularEXP",   "float",     0, 50.0f},
        {"brakeTemp",       "float",     0, 0.0f},
        {"glowBrightness",  "float",     0, 1.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
        {"txGlow",          "sampler2D", 0, QVariant()},
    });

    // ---- ksGlass ------------------------------------------------
    regAC("ksGlass", {
        {"ksSpecular",      "float",     0, 1.0f},
        {"ksSpecularEXP",   "float",     0, 200.0f},
        {"ksReflectance",   "float",     0, 0.8f},
        {"ksFresnelC",      "float",     0, 0.04f},
        {"ksFresnelEXP",    "float",     0, 5.0f},
        {"txBackground",    "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
    });

    // ---- ksTyre ------------------------------------------------
    regAC("ksTyre", {
        {"ksAmbient",       "float",     0, 0.3f},
        {"ksDiffuse",       "float",     0, 0.6f},
        {"ksSpecular",      "float",     0, 0.5f},
        {"ksSpecularEXP",   "float",     0, 30.0f},
        {"tyreTemperature", "float",     0, 0.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
        {"txDirt",          "sampler2D", 0, QVariant()},
    });

    // ---- ksWindscreen ------------------------------------------
    regAC("ksWindscreen", {
        {"transparency",    "float",     0, 0.8f},
        {"ksFresnelC",      "float",     0, 0.04f},
        {"rainIntensity",   "float",     0, 0.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
    });

    // ---- ksEmissive --------------------------------------------
    regAC("ksEmissive", {
        {"ksEmissive",      "float",     0, 1.0f},
        {"ksShadow",        "float",     0, 1.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txEmissive",      "sampler2D", 0, QVariant()},
    });

    // ---- ksTerrain ----------------------------------------------
    regAC("ksTerrain", {
        {"ksAmbient",       "float",     0, 0.5f},
        {"ksDiffuse",       "float",     0, 0.5f},
        {"uvScale",         "float",     0, 1.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
        {"txDetail",        "sampler2D", 0, QVariant()},
        {"txBlendMask",     "sampler2D", 0, QVariant()},
    });

    // ---- ksPerPixelAT (alpha test) -----------------------------
    regAC("ksPerPixelAT", {
        {"ksAlphaRef",      "float",     0, 0.5f},
        {"ksAmbient",       "float",     0, 0.4f},
        {"ksDiffuse",       "float",     0, 0.6f},
        {"ksSpecular",      "float",     0, 0.8f},
        {"ksSpecularEXP",   "float",     0, 50.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
    });

    // ---- ksSimple (unlit / vertex color) -----------------------
    regAC("ksSimple", {
        {"ksAmbient",       "float",     0, 1.0f},
        {"ksDiffuse",       "float",     0, 0.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
    });

    // ---- ksTree ------------------------------------------------
    regAC("ksTree", {
        {"windStrength",    "float",     0, 1.0f},
        {"windSpeed",       "float",     0, 1.0f},
        {"ksAlphaRef",      "float",     0, 0.3f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
        {"txNormal",        "sampler2D", 0, QVariant()},
    });

    // ---- ksLightEmitter ----------------------------------------
    regAC("ksLightEmitter", {
        {"emitterColor",    "vec4",      0, QVariant::fromValue(QVector4D(1,1,0.8f,1))},
        {"emitterStrength", "float",     0, 1.0f},
        {"txDiffuse",       "sampler2D", 0, QVariant()},
    });

    // Validator: validates a KN5 material against registered shader params
    // Returns list of unknown param names for a given shader
    // (used by the material editor to highlight invalid params)

}

const QMap<QString, ShaderParamInfo>& ShaderParamRegistry::getParams(const QString& shaderName) const
{
    static const QMap<QString, ShaderParamInfo> empty;
    auto it = m_shaders.constFind(shaderName);
    if (it != m_shaders.constEnd()) {
        return *it;
    }
    return empty;
}

ShaderParamRegistry::ValidationResult
ShaderParamRegistry::validateMaterial(const QString& shaderName,
                                      const QMap<QString,QString>& params) const
{
    ValidationResult result;
    if (!m_shaders.contains(shaderName)) {
        // Unknown shader — flag all params as unknown
        result.unknownParams = params.keys();
        return result;
    }
    const auto& knownParams = m_shaders[shaderName];

    // Check each material param exists in the shader definition
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        // Skip engine-reserved params (matrix uniforms etc.)
        if (it.key().startsWith("ks") || knownParams.contains(it.key())) continue;
        result.unknownParams << it.key();
    }

    // Check required texture slots are present
    for (auto it = knownParams.constBegin(); it != knownParams.constEnd(); ++it) {
        if (it->type == "sampler2D" && it->name == "txDiffuse") {
            if (!params.contains("txDiffuse") || params["txDiffuse"].isEmpty())
                result.missingRequired << "txDiffuse";
        }
    }
    return result;
}

void ShaderParamRegistry::setGlobalParam(const QString& name, const QVariant& value)
{
    m_globalParams[name] = value;
}

QVariant ShaderParamRegistry::getGlobalParam(const QString& name) const
{
    return m_globalParams.value(name);
}

} // namespace ks
