#pragma once

#include <QString>
#include <QMap>
#include <QVariant>
#include <QVector4D>
#include <QMatrix4x4>

namespace ks {

struct MaterialParams {
    QString name;
    QVector4D ambient;
    QVector4D diffuse;
    QVector4D specular;
    QVector4D emissive;
    QVector4D uvScaleOffset;
    float specularEXP = 32.0f;
    float alpha = 1.0f;
    bool doubleSided = false;
    uint32_t flags = 0;
    QString diffuseTexture;
    QString normalTexture;
    QString specularTexture;
    QString emissiveTexture;
};

struct ShaderParamInfo {
    QString name;
    QString type;
    int location;
    QVariant defaultValue;
};

class ShaderParamRegistry
{
public:
    static ShaderParamRegistry& instance();

    void registerShader(const QString& shaderName);
    void unregisterShader(const QString& shaderName);

    void registerParam(const QString& shaderName, const ShaderParamInfo& param);
    void setParam(const QString& shaderName, const QString& paramName, const QVariant& value);
    QVariant getParam(const QString& shaderName, const QString& paramName) const;

    void registerBuiltInShaders();

    bool hasShader(const QString& shaderName) const { return m_shaders.contains(shaderName); }
    const QMap<QString, ShaderParamInfo>& getParams(const QString& shaderName) const;
    QStringList knownShaders() const { return m_shaders.keys(); }

    // Global parameters — shared across all shaders (e.g. time, camera, environment)
    void setGlobalParam(const QString& name, const QVariant& value);
    QVariant getGlobalParam(const QString& name) const;

    // Validate a material's params against the registered shader definition.
    // Returns a list of unknown / mistyped param names.
    struct ValidationResult {
        QStringList unknownParams;
        QStringList missingRequired;
        QStringList typeMismatches;
        bool isValid() const { return unknownParams.isEmpty() && missingRequired.isEmpty(); }
    };
    ValidationResult validateMaterial(const QString& shaderName,
                                      const QMap<QString,QString>& params) const;

private:
    ShaderParamRegistry() = default;
    ~ShaderParamRegistry() = default;

    static ShaderParamRegistry* s_instance;

    QMap<QString, QMap<QString, ShaderParamInfo>> m_shaders;
    QMap<QString, QMap<QString, QVariant>> m_paramValues;
    QMap<QString, QVariant> m_globalParams;
};
}