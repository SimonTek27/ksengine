#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <QString>
#include <QVector>
#include "../Math/MathCore.h"

namespace ks {

struct FBXMaterial {
    std::string name;
    Vec3 diffuseColor = {0.8f, 0.8f, 0.8f};
    Vec3 specularColor = {0.2f, 0.2f, 0.2f};
    Vec3 ambientColor = {0.2f, 0.2f, 0.2f};
    float opacity = 1.0f;
    float shininess = 0.0f;
    std::string diffuseTexture;
    std::string normalTexture;
};

struct FBXMesh {
    std::string name;
    std::string materialName;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> boneIndices;
    std::vector<float> boneWeights;
};

struct FBXBone {
    std::string name;
    int parentIndex = -1;
    Vec3 position = {0, 0, 0};
    Vec4 rotation = {0, 0, 0, 1};
    Vec3 scale = {1, 1, 1};
    Mat4 offsetMatrix;
};

struct FBXSkeleton {
    std::vector<FBXBone> bones;
};

struct FBXKeyframe {
    float time = 0.0f;
    Vec3 translation = {0, 0, 0};
    Vec4 rotation = {0, 0, 0, 1};
    Vec3 scale = {1, 1, 1};
};

struct FBXAnimationChannel {
    std::string boneName;
    std::vector<FBXKeyframe> keyframes;
};

struct FBXAnimation {
    std::string name;
    float duration = 0.0f;
    float fps = 30.0f;
    std::vector<FBXAnimationChannel> channels;
};

namespace FBX {

struct Vertex {
    Vector3 position;
    Vector3 normal;
    Vector2 uv;
};

struct Polygon {
    QVector<int> indices;
};

struct MeshData {
    QString name;
    QVector<Vertex> vertices;
    QVector<Polygon> polygons;
};

struct Node {
    QString name;
    QVector<MeshData> meshes;
};

struct Scene {
    QString name;
    QVector<MeshData> meshes;
    QVector<Node> nodes;
};

struct File {
    QString version;
    Scene scene;
};

class Parser {
public:
    static bool read(const QString& filePath, File& outFile);
    static QString lastError();
};

} // namespace FBX

struct FBXScene {
    std::vector<FBXMesh> meshes;
    std::map<std::string, FBXMaterial> materials;
    FBXSkeleton skeleton;
    std::vector<FBXAnimation> animations;
    std::string sourcePath;
};

class FBXParser {
public:
    FBXParser() = default;

    bool loadFromFile(const std::string& path);
    const FBXScene& scene() const { return m_scene; }
    const FBXSkeleton& skeleton() const { return m_scene.skeleton; }
    const std::vector<FBXAnimation>& animations() const { return m_scene.animations; }

private:
    bool parseASCII(const std::string& content);
    bool parseBinary(const std::vector<char>& data);
    void parseGeometry(const std::string& section);
    void parseMaterials(const std::string& section);
    void parseConnections(const std::string& section);
    void parseSkeleton(const std::string& section);
    void parseAnimationStack(const std::string& section);

    FBXScene m_scene;
    std::map<std::string, std::string> m_meshGeometry;
    std::map<std::string, std::string> m_materialProps;
};

} // namespace ks
