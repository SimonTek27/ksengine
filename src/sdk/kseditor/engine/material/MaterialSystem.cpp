#include "MaterialSystem.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QtMath>

namespace ks {

Material::Material() {}

Material::Material(const QString& n) : name(n) {}

void Material::setTexture(TextureType type, const QString& path) {
    TextureSlot slot;
    slot.type = type;
    slot.texturePath = path;
    textures[type] = slot;
}

TextureSlot* Material::getTexture(TextureType type) {
    if (textures.contains(type)) return &textures[type];
    return nullptr;
}

void Material::removeTexture(TextureType type) {
    textures.remove(type);
}

void Material::clearTextures() {
    textures.clear();
}

void Material::setBaseColor(const QVector3D& color) {
    params.baseColor = color;
}

void Material::setMetallic(float value) {
    params.metallic = qBound(0.0f, value, 1.0f);
}

void Material::setRoughness(float value) {
    params.roughness = qBound(0.0f, value, 1.0f);
}

void Material::setAlpha(float value) {
    params.alpha = qBound(0.0f, value, 1.0f);
    isTransparent = (value < 1.0f);
}

void Material::setClearcoat(float value) {
    params.clearcoat = qBound(0.0f, value, 1.0f);
}

void Material::setClearcoatRoughness(float value) {
    params.clearcoatRoughness = qBound(0.0f, value, 1.0f);
}

void Material::setSheen(float value) {
    params.sheen = qBound(0.0f, value, 1.0f);
}

void Material::setSheenColor(const QVector3D& color) {
    params.sheenColor = QVector3D(
        qBound(0.0f, color.x(), 1.0f),
        qBound(0.0f, color.y(), 1.0f),
        qBound(0.0f, color.z(), 1.0f)
    );
}

void Material::setAnisotropy(float value) {
    params.anisotropy = qBound(0.0f, value, 1.0f);
}

void Material::setAnisotropyRotation(float value) {
    params.anisotropyRotation = value;
    if (params.anisotropyRotation < 0) params.anisotropyRotation += 1.0f;
    if (params.anisotropyRotation > 1.0f) params.anisotropyRotation -= 1.0f;
}

void Material::setTransmission(float value) {
    params.transmission = qBound(0.0f, value, 1.0f);
}

void Material::setIor(float value) {
    params.ior = qMax(1.0f, value);
}

void Material::setThickness(float value) {
    params.thickness = qMax(0.0f, value);
}

void Material::setSubsurface(float value) {
    params.subsurface = qBound(0.0f, value, 1.0f);
}

void Material::setSubsurfaceColor(const QVector3D& color) {
    params.subsurfaceColor = QVector3D(
        qBound(0.0f, color.x(), 1.0f),
        qBound(0.0f, color.y(), 1.0f),
        qBound(0.0f, color.z(), 1.0f)
    );
}

void Material::setSubsurfaceScale(float value) {
    params.subsurfaceScale = qMax(0.0f, value);
}

void Material::setSubsurfaceRadius(const QVector3D& radius) {
    params.subsurfaceRadius = QVector3D(
        qMax(0.0f, radius.x()),
        qMax(0.0f, radius.y()),
        qMax(0.0f, radius.z())
    );
}

void Material::setSpecular(float value) {
    params.specular = qBound(0.0f, value, 1.0f);
}

void Material::setSpecularTint(const QVector3D& color) {
    params.specularTint = QVector3D(
        qBound(0.0f, color.x(), 1.0f),
        qBound(0.0f, color.y(), 1.0f),
        qBound(0.0f, color.z(), 1.0f)
    );
}

QString Material::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["category"] = category;
    obj["isTransparent"] = isTransparent;
    obj["twoSided"] = twoSided;

    QJsonObject paramsObj;
    paramsObj["baseColor"] = QJsonArray({params.baseColor.x(), params.baseColor.y(), params.baseColor.z()});
    paramsObj["metallic"] = params.metallic;
    paramsObj["roughness"] = params.roughness;
    paramsObj["alpha"] = params.alpha;
    paramsObj["emissive"] = QJsonArray({params.emissive.x(), params.emissive.y(), params.emissive.z()});

    // Extended PBR
    paramsObj["clearcoat"] = params.clearcoat;
    paramsObj["clearcoatRoughness"] = params.clearcoatRoughness;
    paramsObj["sheen"] = params.sheen;
    paramsObj["sheenColor"] = QJsonArray({params.sheenColor.x(), params.sheenColor.y(), params.sheenColor.z()});
    paramsObj["anisotropy"] = params.anisotropy;
    paramsObj["anisotropyRotation"] = params.anisotropyRotation;
    paramsObj["transmission"] = params.transmission;
    paramsObj["ior"] = params.ior;
    paramsObj["thickness"] = params.thickness;
    paramsObj["subsurface"] = params.subsurface;
    paramsObj["subsurfaceColor"] = QJsonArray({params.subsurfaceColor.x(), params.subsurfaceColor.y(), params.subsurfaceColor.z()});
    paramsObj["subsurfaceScale"] = params.subsurfaceScale;
    paramsObj["subsurfaceRadius"] = QJsonArray({params.subsurfaceRadius.x(), params.subsurfaceRadius.y(), params.subsurfaceRadius.z()});
    paramsObj["specular"] = params.specular;
    paramsObj["specularTint"] = QJsonArray({params.specularTint.x(), params.specularTint.y(), params.specularTint.z()});

    obj["params"] = paramsObj;

    return QJsonDocument(obj).toJson();
}

Material Material::fromJson(const QString& json) {
    Material mat;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "Material::fromJson: Invalid JSON";
        return mat;
    }
    QJsonObject obj = doc.object();

    mat.name = obj["name"].toString();
    mat.category = obj["category"].toString();
    mat.isTransparent = obj["isTransparent"].toBool();
    mat.twoSided = obj["twoSided"].toBool();

    if (obj.contains("params")) {
        QJsonObject p = obj["params"].toObject();
        QJsonArray col = p["baseColor"].toArray();
        mat.params.baseColor = QVector3D(col[0].toDouble(), col[1].toDouble(), col[2].toDouble());
        mat.params.metallic = p["metallic"].toDouble();
        mat.params.roughness = p["roughness"].toDouble();
        mat.params.alpha = p["alpha"].toDouble();

        // Extended PBR
        mat.params.clearcoat = p["clearcoat"].toDouble(0.0f);
        mat.params.clearcoatRoughness = p["clearcoatRoughness"].toDouble(0.03f);
        mat.params.sheen = p["sheen"].toDouble(0.0f);
        QJsonArray sc = p["sheenColor"].toArray();
        if (sc.size() == 3) mat.params.sheenColor = QVector3D(sc[0].toDouble(), sc[1].toDouble(), sc[2].toDouble());
        mat.params.anisotropy = p["anisotropy"].toDouble(0.0f);
        mat.params.anisotropyRotation = p["anisotropyRotation"].toDouble(0.0f);
        mat.params.transmission = p["transmission"].toDouble(0.0f);
        mat.params.ior = p["ior"].toDouble(1.5f);
        mat.params.thickness = p["thickness"].toDouble(1.0f);
        mat.params.subsurface = p["subsurface"].toDouble(0.0f);
        QJsonArray ssc = p["subsurfaceColor"].toArray();
        if (ssc.size() == 3) mat.params.subsurfaceColor = QVector3D(ssc[0].toDouble(), ssc[1].toDouble(), ssc[2].toDouble());
        mat.params.subsurfaceScale = p["subsurfaceScale"].toDouble(1.0f);
        QJsonArray ssr = p["subsurfaceRadius"].toArray();
        if (ssr.size() == 3) mat.params.subsurfaceRadius = QVector3D(ssr[0].toDouble(), ssr[1].toDouble(), ssr[2].toDouble());
        mat.params.specular = p["specular"].toDouble(0.5f);
        QJsonArray spt = p["specularTint"].toArray();
        if (spt.size() == 3) mat.params.specularTint = QVector3D(spt[0].toDouble(), spt[1].toDouble(), spt[2].toDouble());
    }

    return mat;
}

MaterialPreset MaterialPreset::carbon() {
    MaterialPreset preset;
    preset.name = "Carbon";
    preset.params.baseColor = QVector3D(0.02f, 0.02f, 0.02f);
    preset.params.metallic = 0.4f;
    preset.params.roughness = 0.3f;
    return preset;
}

MaterialPreset MaterialPreset::leather() {
    MaterialPreset preset;
    preset.name = "Leather";
    preset.params.baseColor = QVector3D(0.3f, 0.15f, 0.05f);
    preset.params.metallic = 0.0f;
    preset.params.roughness = 0.7f;
    return preset;
}

MaterialPreset MaterialPreset::glass() {
    MaterialPreset preset;
    preset.name = "Glass";
    preset.params.baseColor = QVector3D(1.0f, 1.0f, 1.0f);
    preset.params.metallic = 0.0f;
    preset.params.roughness = 0.0f;
    preset.params.alpha = 0.3f;
    return preset;
}

MaterialPreset MaterialPreset::metal() {
    MaterialPreset preset;
    preset.name = "Metal";
    preset.params.metallic = 1.0f;
    preset.params.roughness = 0.4f;
    return preset;
}

MaterialPreset MaterialPreset::plastic() {
    MaterialPreset preset;
    preset.name = "Plastic";
    preset.params.metallic = 0.0f;
    preset.params.roughness = 0.5f;
    return preset;
}

MaterialPreset MaterialPreset::rubber() {
    MaterialPreset preset;
    preset.name = "Rubber";
    preset.params.metallic = 0.0f;
    preset.params.roughness = 0.9f;
    return preset;
}

MaterialPreset MaterialPreset::wood() {
    MaterialPreset preset;
    preset.name = "Wood";
    preset.params.baseColor = QVector3D(0.4f, 0.25f, 0.1f);
    preset.params.metallic = 0.0f;
    preset.params.roughness = 0.7f;
    return preset;
}

MaterialPreset MaterialPreset::fabric() {
    MaterialPreset preset;
    preset.name = "Fabric";
    preset.params.metallic = 0.0f;
    preset.params.roughness = 0.8f;
    return preset;
}

MaterialPreset MaterialPreset::paint() {
    MaterialPreset preset;
    preset.name = "Paint";
    preset.params.metallic = 0.3f;
    preset.params.roughness = 0.3f;
    return preset;
}

MaterialPreset MaterialPreset::chrome() {
    MaterialPreset preset;
    preset.name = "Chrome";
    preset.params.metallic = 1.0f;
    preset.params.roughness = 0.05f;
    return preset;
}

MaterialPreset MaterialPreset::gold() {
    MaterialPreset preset;
    preset.name = "Gold";
    preset.params.baseColor = QVector3D(1.0f, 0.8f, 0.3f);
    preset.params.metallic = 1.0f;
    preset.params.roughness = 0.2f;
    return preset;
}

MaterialPreset MaterialPreset::brushedMetal() {
    MaterialPreset preset;
    preset.name = "Brushed Metal";
    preset.params.metallic = 1.0f;
    preset.params.roughness = 0.4f;
    return preset;
}

TextureManager& TextureManager::instance() {
    static TextureManager instance;
    return instance;
}

int TextureManager::loadTexture(const QString& path, bool srgb) {
    if (loadedTextures.contains(path)) {
        return loadedTextures[path];
    }

    QImage image(path);
    if (image.isNull()) return -1;

    int id = loadTexture(image, path);
    loadedTextures[path] = id;
    return id;
}

int TextureManager::loadTexture(const QImage& image, const QString& name) {
    int id = m_NextId++;

    auto* texture = new QOpenGLTexture(image.flipped(Qt::Vertical));
    texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::Repeat);

    m_Textures[id] = texture;
    return id;
}

void TextureManager::releaseTexture(int id) {
    if (m_Textures.contains(id)) {
        delete m_Textures[id];
        m_Textures.remove(id);
    }
}

void TextureManager::releaseAll() {
    for (auto* tex : m_Textures.values()) {
        delete tex;
    }
    m_Textures.clear();
    loadedTextures.clear();
}

QOpenGLTexture* TextureManager::getTexture(int id) {
    return m_Textures.value(id, nullptr);
}

int TextureManager::getTextureId(const QString& path) {
    return loadedTextures.value(path, -1);
}

TextureManager::~TextureManager() {
    releaseAll();
}

QVector<UVEditor::UVIsland> UVEditor::findIslands(const QVector<QVector<int>>& faces, const QVector<QVector2D>& uvs) {
    QVector<UVIsland> islands;
    if (faces.isEmpty()) return islands;

    // Union-find for connected face components
    QVector<int> parent(faces.size());
    for (int i = 0; i < faces.size(); ++i) parent[i] = i;

    auto find = [&](int x) -> int {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](int a, int b) { parent[find(a)] = find(b); };

    // Build vertex-to-face map and unite faces sharing vertices
    QMap<int, QVector<int>> vertToFaces;
    for (int fi = 0; fi < faces.size(); ++fi) {
        for (int vi : faces[fi]) {
            vertToFaces[vi].append(fi);
        }
    }
    for (const auto& faceList : vertToFaces) {
        for (int i = 1; i < faceList.size(); ++i)
            unite(faceList[0], faceList[i]);
    }

    // Group faces by root
    QMap<int, UVIsland> islandMap;
    for (int fi = 0; fi < faces.size(); ++fi) {
        int root = find(fi);
        if (!islandMap.contains(root)) {
            islandMap[root] = UVIsland();
        }
        islandMap[root].faceIndices.append(fi);
        for (int vi : faces[fi]) {
            if (vi < uvs.size())
                islandMap[root].uvs.append(uvs[vi]);
        }
    }
    return islandMap.values();
}

void UVEditor::packIslands(QVector<UVIsland>& islands, float padding) {
    if (islands.isEmpty()) return;

    // Simple shelf packing: arrange islands left-to-right, top-to-bottom
    float cursorX = 0, cursorY = 0, rowHeight = 0;
    for (auto& island : islands) {
        // Compute bounding box
        if (island.uvs.isEmpty()) continue;
        QVector2D minUV = island.uvs[0], maxUV = island.uvs[0];
        for (const auto& uv : island.uvs) {
            minUV.setX(qMin(minUV.x(), uv.x()));
            minUV.setY(qMin(minUV.y(), uv.y()));
            maxUV.setX(qMax(maxUV.x(), uv.x()));
            maxUV.setY(qMax(maxUV.y(), uv.y()));
        }
        QVector2D size = maxUV - minUV;

        if (cursorX + size.x() + padding > 1.0f) {
            cursorX = 0;
            cursorY += rowHeight + padding;
            rowHeight = 0;
        }

        QVector2D shift(cursorX - minUV.x(), cursorY - minUV.y());
        for (auto& uv : island.uvs) uv += shift;
        cursorX += size.x() + padding;
        rowHeight = qMax(rowHeight, size.y());
    }
}

void UVEditor::unwrapMesh(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                         QVector<QVector2D>& uvs, QVector<int>& seams) {
    uvs.resize(vertices.size());
    seams.clear();
    QVector<bool> assigned(vertices.size(), false);

    for (int fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        if (face.size() < 3) continue;

        QVector3D center;
        QVector3D normal;
        for (int vi : face) {
            center += vertices[vi];
        }
        center /= face.size();

        QVector3D e1 = vertices[face[1]] - vertices[face[0]];
        QVector3D e2 = vertices[face[2]] - vertices[face[0]];
        normal = QVector3D::crossProduct(e1, e2).normalized();

        QVector3D up = (qAbs(normal.y()) < 0.999f) ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
        QVector3D tangent = QVector3D::crossProduct(up, normal).normalized();
        QVector3D bitangent = QVector3D::crossProduct(normal, tangent).normalized();

        float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
        QVector<QVector2D> localUVs(face.size());
        for (int j = 0; j < face.size(); ++j) {
            QVector3D diff = vertices[face[j]] - center;
            float u = QVector3D::dotProduct(diff, tangent);
            float v = QVector3D::dotProduct(diff, bitangent);
            localUVs[j] = QVector2D(u, v);
            if (u < minU) minU = u; if (u > maxU) maxU = u;
            if (v < minV) minV = v; if (v > maxV) maxV = v;
        }
        float rangeU = (maxU - minU > 1e-6f) ? (maxU - minU) : 1.0f;
        float rangeV = (maxV - minV > 1e-6f) ? (maxV - minV) : 1.0f;

        for (int j = 0; j < face.size(); ++j) {
            int vi = face[j];
            if (!assigned[vi]) {
                uvs[vi] = QVector2D((localUVs[j].x() - minU) / rangeU, (localUVs[j].y() - minV) / rangeV);
                assigned[vi] = true;
            }
        }
        if (face.size() >= 3) {
            seams.append(face[0]);
            seams.append(face[face.size() - 1]);
        }
    }
}

void UVEditor::smartUVProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                              QVector<QVector2D>& uvs, float angleDistortion) {
    QVector<int> dummySeams;
    unwrapMesh(vertices, faces, uvs, dummySeams);
}

void UVEditor::stitchIslands(const QVector<UVIsland>& islands, float tolerance) {
    if (islands.size() < 2) return;
}

void UVEditor::minimizeStretch(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                              QVector<QVector2D>& uvs, int iterations) {
    if (vertices.isEmpty() || faces.isEmpty()) return;
    for (int iter = 0; iter < iterations; ++iter) {
        for (const auto& face : faces) {
            if (face.size() < 3) continue;
            QVector2D centroid;
            for (int vi : face) centroid += uvs[vi];
            centroid /= face.size();
            float area3D = 0;
            float areaUV = 0;
            for (int j = 0; j < face.size(); ++j) {
                int next = (j + 1) % face.size();
                area3D += QVector3D::crossProduct(
                    vertices[face[next]] - vertices[face[j]],
                    vertices[face[(j + 2) % face.size()]] - vertices[face[j]]).length();
                QVector2D d1 = uvs[face[next]] - uvs[face[j]];
                QVector2D d2 = uvs[face[(j + 2) % face.size()]] - uvs[face[j]];
                areaUV += qAbs(d1.x() * d2.y() - d1.y() * d2.x());
            }
            if (area3D > 1e-6f && areaUV > 1e-6f) {
                float scale = qSqrt(area3D / areaUV) * 0.1f;
                for (int vi : face) {
                    uvs[vi] = uvs[vi] * (1.0f - scale) + centroid * scale;
                }
            }
        }
    }
}

ShaderManager& ShaderManager::instance() {
    static ShaderManager instance;
    return instance;
}

int ShaderManager::compileShader(const QString& name, const QString& vertexSource, const QString& fragmentSource) {
    return compileShader(name, vertexSource, fragmentSource, QString());
}

int ShaderManager::compileShader(const QString& name, const QString& vertexSource, const QString& fragmentSource,
                                const QString& geometrySource) {
    if (compiledShaders.contains(name)) return compiledShaders[name];

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qDebug() << "ShaderManager: No OpenGL context";
        return 0;
    }

    auto* program = new QOpenGLShaderProgram();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)) {
        qDebug() << "Vertex shader error:" << program->log();
        delete program;
        return 0;
    }
    if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        qDebug() << "Fragment shader error:" << program->log();
        delete program;
        return 0;
    }
    if (!geometrySource.isEmpty()) {
        if (!program->addShaderFromSourceCode(QOpenGLShader::Geometry, geometrySource)) {
            qDebug() << "Geometry shader error:" << program->log();
            delete program;
            return 0;
        }
    }
    if (!program->link()) {
        qDebug() << "Shader link error:" << program->log();
        delete program;
        return 0;
    }

    int id = m_nextProgramId++;
    m_programs[id] = program;
    compiledShaders[name] = id;
    return id;
}

void ShaderManager::useShader(int programId) {
    auto* prog = m_programs.value(programId, nullptr);
    if (prog) prog->bind();
}

void ShaderManager::bindMaterial(const Material& material) {
    PBRParams p = material.params;
    int curId = compiledShaders.value("current", 0);
    auto* prog = m_programs.value(curId, nullptr);
    if (!prog) return;
    prog->setUniformValue("baseColor", p.baseColor);
    prog->setUniformValue("metallic", p.metallic);
    prog->setUniformValue("roughness", p.roughness);
    prog->setUniformValue("alpha", p.alpha);
    prog->setUniformValue("emissive", p.emissive);
}

void ShaderManager::unbindMaterial() {
    int curId = compiledShaders.value("current", 0);
    auto* prog = m_programs.value(curId, nullptr);
    if (prog) prog->release();
}

int ShaderManager::getProgram(const QString& name) {
    return compiledShaders.value(name, 0);
}

int ShaderManager::getBuiltinShader(const QString& type) {
    if (compiledShaders.contains(type)) return compiledShaders[type];
    QString vs = getDefaultVertexShader();
    QString fs;
    if (type == "pbr") fs = getPBRFragmentShader();
    else if (type == "wireframe") fs = getWireframeFragmentShader();
    else if (type == "normal") fs = getNormalVisualizationShader();
    else if (type == "uv") fs = getUVVisualizationShader();
    else fs = getDefaultFragmentShader();
    int id = compileShader(type, vs, fs);
    return id;
}

QString ShaderManager::getDefaultVertexShader() {
    return R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec2 uv;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        out vec3 fragPos;
        out vec3 fragNormal;
        out vec2 fragUV;
        void main() {
            fragPos = vec3(model * vec4(position, 1.0));
            fragNormal = mat3(transpose(inverse(model))) * normal;
            fragUV = uv;
            gl_Position = projection * view * vec4(fragPos, 1.0);
        }
    )";
}

QString ShaderManager::getDefaultFragmentShader() {
    return R"(
        #version 330 core
        in vec3 fragPos;
        in vec3 fragNormal;
        in vec2 fragUV;
        out vec4 fragColor;
        uniform vec3 baseColor;
        uniform float metallic;
        uniform float roughness;
        uniform vec3 lightPos;
        uniform vec3 viewPos;
        void main() {
            vec3 norm = normalize(fragNormal);
            vec3 viewDir = normalize(viewPos - fragPos);
            vec3 lightDir = normalize(lightPos - fragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
            vec3 ambient = 0.1 * baseColor;
            vec3 diffuse = diff * baseColor;
            vec3 specular = spec * vec3(0.3);
            fragColor = vec4(ambient + diffuse + specular, 1.0);
        }
    )";
}

QString ShaderManager::getPBRFragmentShader() {
    return R"(
        #version 330 core
        in vec3 fragPos;
        in vec3 fragNormal;
        in vec2 fragUV;
        out vec4 fragColor;
        uniform vec3 baseColor;
        uniform float metallic;
        uniform float roughness;
        uniform vec3 lightPos;
        uniform vec3 viewPos;
        const float PI = 3.14159265359;
        float distributionGGX(vec3 N, vec3 H, float roughness) {
            float a = roughness * roughness;
            float a2 = a * a;
            float NdotH = max(dot(N, H), 0.0);
            float NdotH2 = NdotH * NdotH;
            float nom = a2;
            float denom = NdotH2 * (a2 - 1.0) + 1.0;
            denom = PI * denom * denom;
            return nom / denom;
        }
        float geometrySchlickGGX(float NdotV, float roughness) {
            float r = roughness + 1.0;
            float k = (r * r) / 8.0;
            float nom = NdotV;
            float denom = NdotV * (1.0 - k) + k;
            return nom / denom;
        }
        float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
            float NdotV = max(dot(N, V), 0.0);
            float NdotL = max(dot(N, L), 0.0);
            float ggx2 = geometrySchlickGGX(NdotV, roughness);
            float ggx1 = geometrySchlickGGX(NdotL, roughness);
            return ggx1 * ggx2;
        }
        vec3 fresnelSchlick(float cosTheta, vec3 F0) {
            return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
        }
        void main() {
            vec3 N = normalize(fragNormal);
            vec3 V = normalize(viewPos - fragPos);
            vec3 F0 = vec3(0.04);
            F0 = mix(F0, baseColor, metallic);
            vec3 Lo = vec3(0.0);
            vec3 L = normalize(lightPos - fragPos);
            vec3 H = normalize(V + L);
            float distance = length(lightPos - fragPos);
            float attenuation = 1.0 / (distance * distance);
            vec3 radiance = vec3(1.0) * attenuation;
            float NDF = distributionGGX(N, H, roughness);
            float G = geometrySmith(N, V, L, roughness);
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;
            vec3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
            vec3 specular = numerator / denominator;
            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * baseColor / PI + specular) * radiance * NdotL;
            vec3 ambient = vec3(0.03) * baseColor;
            vec3 color = ambient + Lo;
            color = color / (color + vec3(1.0));
            color = pow(color, vec3(1.0/2.2));
            fragColor = vec4(color, 1.0);
        }
    )";
}

QString ShaderManager::getWireframeFragmentShader() {
    return R"(
        #version 330 core
        in vec3 fragPos;
        out vec4 fragColor;
        uniform vec3 wireframeColor;
        void main() {
            fragColor = vec4(wireframeColor, 1.0);
        }
    )";
}

QString ShaderManager::getNormalVisualizationShader() {
    return R"(
        #version 330 core
        in vec3 fragPos;
        in vec3 fragNormal;
        out vec4 fragColor;
        void main() {
            vec3 normal = normalize(fragNormal);
            fragColor = vec4(normal * 0.5 + 0.5, 1.0);
        }
    )";
}

QString ShaderManager::getUVVisualizationShader() {
    return R"(
        #version 330 core
        in vec2 fragUV;
        out vec4 fragColor;
        void main() {
            vec3 color = vec3(fragUV, 0.0);
            fragColor = vec4(color, 1.0);
        }
    )";
}

}