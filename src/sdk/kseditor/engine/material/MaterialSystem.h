#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QVector3D>
#include <QVector2D>
#include <QImage>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>

namespace ks {

enum class TextureType {
    BaseColor,
    Normal,
    Roughness,
    Metallic,
    Specular,
    Emission,
    Alpha,
    AmbientOcclusion,
    Height,
    Clearcoat,
    ClearcoatRoughness,
    Sheen,
    Anisotropy,
    Transmission,
    Subsurface,
    SubsurfaceRadius,
    SpecularTint
};

struct TextureSlot {
    TextureType type = TextureType::BaseColor;
    QString texturePath;
};

struct PBRParams {
    QVector3D baseColor = QVector3D(0.8f, 0.8f, 0.8f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float alpha = 1.0f;
    QVector3D emissive = QVector3D(0.0f, 0.0f, 0.0f);

    // Extended PBR features (Disney Principled BSDF)
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.03f;
    float sheen = 0.0f;
    QVector3D sheenColor = QVector3D(1.0f, 1.0f, 1.0f);
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float thickness = 1.0f;
    float subsurface = 0.0f;
    QVector3D subsurfaceColor = QVector3D(1.0f, 1.0f, 1.0f);
    float subsurfaceScale = 1.0f;
    QVector3D subsurfaceRadius = QVector3D(1.0f, 1.0f, 1.0f);
    float specular = 0.5f;
    QVector3D specularTint = QVector3D(1.0f, 1.0f, 1.0f);
};

struct Material {
    QString name;
    QString category;
    bool isTransparent = false;
    bool twoSided = false;
    QMap<TextureType, TextureSlot> textures;
    PBRParams params;

    Material();
    explicit Material(const QString& n);

    void setTexture(TextureType type, const QString& path);
    TextureSlot* getTexture(TextureType type);
    void removeTexture(TextureType type);
    void clearTextures();

    void setBaseColor(const QVector3D& color);
    void setMetallic(float value);
    void setRoughness(float value);
    void setAlpha(float value);

    // Extended PBR setters
    void setClearcoat(float value);
    void setClearcoatRoughness(float value);
    void setSheen(float value);
    void setSheenColor(const QVector3D& color);
    void setAnisotropy(float value);
    void setAnisotropyRotation(float value);
    void setTransmission(float value);
    void setIor(float value);
    void setThickness(float value);
    void setSubsurface(float value);
    void setSubsurfaceColor(const QVector3D& color);
    void setSubsurfaceScale(float value);
    void setSubsurfaceRadius(const QVector3D& radius);
    void setSpecular(float value);
    void setSpecularTint(const QVector3D& color);

    QString toJson() const;
    static Material fromJson(const QString& json);
};

struct MaterialPreset {
    QString name;
    PBRParams params;

    static MaterialPreset carbon();
    static MaterialPreset leather();
    static MaterialPreset glass();
    static MaterialPreset metal();
    static MaterialPreset plastic();
    static MaterialPreset rubber();
    static MaterialPreset wood();
    static MaterialPreset fabric();
    static MaterialPreset paint();
    static MaterialPreset chrome();
    static MaterialPreset gold();
    static MaterialPreset brushedMetal();
};

class TextureManager {
public:
    static TextureManager& instance();

    int loadTexture(const QString& path, bool srgb = false);
    int loadTexture(const QImage& image, const QString& name = QString());
    void releaseTexture(int id);
    void releaseAll();
    QOpenGLTexture* getTexture(int id);
    int getTextureId(const QString& path);

    ~TextureManager();

    QMap<QString, int> loadedTextures;

private:
    TextureManager() = default;
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    int m_NextId = 1;
    QMap<int, QOpenGLTexture*> m_Textures;
};

struct UVEditor {
    struct UVIsland {
        QVector<int> faceIndices;
        QVector<QVector2D> uvs;
    };

    static QVector<UVIsland> findIslands(const QVector<QVector<int>>& faces, const QVector<QVector2D>& uvs);
    static void packIslands(QVector<UVIsland>& islands, float padding = 0.01f);
    static void unwrapMesh(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                          QVector<QVector2D>& uvs, QVector<int>& seams);
    static void smartUVProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                              QVector<QVector2D>& uvs, float angleDistortion = 45.0f);
    static void stitchIslands(const QVector<UVIsland>& islands, float tolerance = 0.001f);
    static void minimizeStretch(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                               QVector<QVector2D>& uvs, int iterations = 100);
};

class ShaderManager {
public:
    static ShaderManager& instance();

    int compileShader(const QString& name, const QString& vertexSource, const QString& fragmentSource);
    int compileShader(const QString& name, const QString& vertexSource, const QString& fragmentSource,
                     const QString& geometrySource);
    void useShader(int programId);
    void bindMaterial(const Material& material);
    void unbindMaterial();
    int getProgram(const QString& name);
    int getBuiltinShader(const QString& type);

    QString getDefaultVertexShader();
    QString getDefaultFragmentShader();
    QString getPBRFragmentShader();
    QString getWireframeFragmentShader();
    QString getNormalVisualizationShader();
    QString getUVVisualizationShader();

    QMap<QString, int> compiledShaders;

private:
    ShaderManager() = default;
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    QMap<int, QOpenGLShaderProgram*> m_programs;
    int m_nextProgramId = 1;
};

} // namespace ks
