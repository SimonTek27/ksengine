// ============================================================================
// VegetationScatter.cpp
// Instanced vegetation placement system implementation.
// ============================================================================

#include "VegetationScatter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QBuffer>
#include <QDebug>
#include <cmath>

namespace ks {

// ============================================================================
// VegetationPreset serialization
// ============================================================================
QJsonObject VegetationPreset::toJson() const
{
    QJsonObject json;
    json["name"] = name;
    json["type"] = static_cast<int>(type);
    json["meshPath"] = meshPath;
    json["texturePath"] = texturePath;
    json["normalPath"] = normalPath;
    json["baseScale"] = static_cast<double>(baseScale);
    json["scaleVariance"] = static_cast<double>(scaleVariance);
    json["density"] = static_cast<double>(density);
    json["minHeight"] = static_cast<double>(minHeight);
    json["maxHeight"] = static_cast<double>(maxHeight);
    json["minSlope"] = static_cast<double>(minSlope);
    json["maxSlope"] = static_cast<double>(maxSlope);
    json["alignToNormal"] = alignToNormal;
    json["castShadows"] = castShadows;
    json["windResponse"] = static_cast<double>(windResponse);
    json["useBillboard"] = useBillboard;
    json["billboardDistance"] = static_cast<double>(billboardDistance);
    return json;
}

void VegetationPreset::fromJson(const QJsonObject& json)
{
    name = json["name"].toString();
    type = static_cast<VegetationType>(json["type"].toInt());
    meshPath = json["meshPath"].toString();
    texturePath = json["texturePath"].toString();
    normalPath = json["normalPath"].toString();
    baseScale = static_cast<float>(json["baseScale"].toDouble(1.0));
    scaleVariance = static_cast<float>(json["scaleVariance"].toDouble(0.2));
    density = static_cast<float>(json["density"].toDouble(0.5));
    minHeight = static_cast<float>(json["minHeight"].toDouble());
    maxHeight = static_cast<float>(json["maxHeight"].toDouble(1000.0));
    minSlope = static_cast<float>(json["minSlope"].toDouble());
    maxSlope = static_cast<float>(json["maxSlope"].toDouble(45.0));
    alignToNormal = json["alignToNormal"].toBool();
    castShadows = json["castShadows"].toBool(true);
    windResponse = static_cast<float>(json["windResponse"].toDouble(0.5));
    useBillboard = json["useBillboard"].toBool(true);
    billboardDistance = static_cast<float>(json["billboardDistance"].toDouble(100.0));
}

// ============================================================================
// VegetationLayer serialization
// ============================================================================
QJsonObject VegetationLayer::toJson() const
{
    QJsonObject json;
    json["id"] = id.toString();
    json["name"] = name;
    json["preset"] = preset.toJson();
    json["enabled"] = enabled;
    json["locked"] = locked;
    json["densityMultiplier"] = static_cast<double>(densityMultiplier);
    json["randomSeed"] = randomSeed;
    json["densityMapResolution"] = densityMapResolution;

    // Encode density map as base64 PNG
    if (!densityMap.isNull()) {
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        densityMap.save(&buffer, "PNG");
        json["densityMap"] = QString::fromLatin1(ba.toBase64());
    }

    return json;
}

void VegetationLayer::fromJson(const QJsonObject& json)
{
    id = QUuid(json["id"].toString());
    name = json["name"].toString();
    preset.fromJson(json["preset"].toObject());
    enabled = json["enabled"].toBool(true);
    locked = json["locked"].toBool();
    densityMultiplier = static_cast<float>(json["densityMultiplier"].toDouble(1.0));
    randomSeed = json["randomSeed"].toInt(42);
    densityMapResolution = json["densityMapResolution"].toInt(512);

    // Decode density map
    if (json.contains("densityMap")) {
        QByteArray ba = QByteArray::fromBase64(json["densityMap"].toString().toLatin1());
        densityMap.loadFromData(ba, "PNG");
    }
}

// ============================================================================
// VegetationScatter - Construction / Destruction
// ============================================================================
VegetationScatter::VegetationScatter(QObject* parent)
    : QObject(parent)
{
}

VegetationScatter::~VegetationScatter()
{
}

// ============================================================================
// Layer management
// ============================================================================
int VegetationScatter::addLayer(const QString& name, const VegetationPreset& preset)
{
    VegetationLayer layer;
    layer.id = QUuid::createUuid();
    layer.name = name;
    layer.preset = preset;
    layer.densityMap = QImage(layer.densityMapResolution, layer.densityMapResolution,
                             QImage::Format_Grayscale8);
    layer.densityMap.fill(128);  // Default 50% density

    m_layers.append(layer);
    emit layerAdded(m_layers.size() - 1);
    return m_layers.size() - 1;
}

void VegetationScatter::removeLayer(int index)
{
    if (index >= 0 && index < m_layers.size()) {
        m_layers.removeAt(index);
        emit layerRemoved(index);
    }
}

void VegetationScatter::moveLayer(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_layers.size()) return;
    if (toIndex < 0 || toIndex >= m_layers.size()) return;

    m_layers.move(fromIndex, toIndex);
}

// ============================================================================
// Density painting
// ============================================================================
void VegetationScatter::paintDensity(int layerIndex, float worldX, float worldZ,
                                     float radius, float opacity)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;

    VegetationLayer& layer = m_layers[layerIndex];
    if (layer.locked) return;

    if (layer.densityMap.isNull()) {
        layer.densityMap = QImage(layer.densityMapResolution, layer.densityMapResolution,
                                  QImage::Format_Grayscale8);
        layer.densityMap.fill(0);
    }

    // Convert world coords to density map pixel coords
    float u = worldX / m_worldW;
    float v = worldZ / m_worldH;

    int mapW = layer.densityMap.width();
    int mapH = layer.densityMap.height();

    int cx = static_cast<int>(u * mapW);
    int cy = static_cast<int>(v * mapH);
    int r = static_cast<int>(radius / m_worldW * mapW);

    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            if (x < 0 || x >= mapW || y < 0 || y >= mapH) continue;

            float dx = static_cast<float>(x - cx);
            float dy = static_cast<float>(y - cy);
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > r) continue;

            float falloff = 1.0f - (dist / r);
            falloff = falloff * falloff;  // Quadratic falloff
            float paint = opacity * falloff * 255.0f;

            int oldVal = layer.densityMap.pixelColor(x, y).red();
            int newVal = qBound(0, oldVal + static_cast<int>(paint), 255);

            layer.densityMap.setPixelColor(x, y, QColor(newVal, newVal, newVal));
        }
    }

    emit densityPainted(layerIndex);
}

void VegetationScatter::clearDensity(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;
    m_layers[layerIndex].densityMap.fill(0);
    emit densityPainted(layerIndex);
}

void VegetationScatter::autoMaskBySlope(int layerIndex, float minSlope, float maxSlope)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;
    if (m_normals.isEmpty()) return;

    VegetationLayer& layer = m_layers[layerIndex];
    int mapW = layer.densityMap.width();
    int mapH = layer.densityMap.height();

    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            float wx = static_cast<float>(x) / mapW * m_worldW;
            float wz = static_cast<float>(y) / mapH * m_worldH;

            QVector3D normal = sampleNormal(wx, wz);
            float slopeAngle = std::acos(qBound(-1.0f, normal.y(), 1.0f)) * 180.0f / M_PI;

            uint8_t val = 0;
            if (slopeAngle >= minSlope && slopeAngle <= maxSlope) {
                val = 255;
            }
            layer.densityMap.setPixelColor(x, y, QColor(val, val, val));
        }
    }

    emit densityPainted(layerIndex);
}

void VegetationScatter::autoMaskByHeight(int layerIndex, float minHeight, float maxHeight)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;

    VegetationLayer& layer = m_layers[layerIndex];
    int mapW = layer.densityMap.width();
    int mapH = layer.densityMap.height();

    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            float wx = static_cast<float>(x) / mapW * m_worldW;
            float wz = static_cast<float>(y) / mapH * m_worldH;

            float h = sampleHeight(wx, wz);
            uint8_t val = 0;
            if (h >= minHeight && h <= maxHeight) {
                val = 255;
            }
            layer.densityMap.setPixelColor(x, y, QColor(val, val, val));
        }
    }

    emit densityPainted(layerIndex);
}

// ============================================================================
// Instance generation
// ============================================================================
void VegetationScatter::generateInstances(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;

    VegetationLayer& layer = m_layers[layerIndex];
    layer.instances.clear();

    if (!layer.enabled || m_heightmap.isEmpty()) return;

    std::mt19937 rng(layer.randomSeed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    int mapW = layer.densityMap.width();
    int mapH = layer.densityMap.height();

    if (mapW <= 0 || mapH <= 0) return;

    // Sample the density map at regular intervals
    float stepX = m_worldW / mapW;
    float stepZ = m_worldH / mapH;

    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            float wx = static_cast<float>(x) / mapW * m_worldW;
            float wz = static_cast<float>(y) / mapH * m_worldH;

            float densityVal = sampleDensity(layerIndex, wx, wz);
            float finalDensity = densityVal * layer.preset.density * layer.densityMultiplier;

            // Decide whether to place an instance here based on density
            float placeChance = finalDensity * stepX * stepZ;
            if (dist(rng) > placeChance) continue;

            float h = sampleHeight(wx, wz);
            QVector3D normal = sampleNormal(wx, wz);
            float slopeAngle = std::acos(qBound(-1.0f, normal.y(), 1.0f)) * 180.0f / M_PI;

            // Check mask constraints
            if (!passesMask(layer.preset, h, slopeAngle)) continue;

            // Add some random offset within the cell
            float offsetX = (dist(rng) - 0.5f) * stepX;
            float offsetZ = (dist(rng) - 0.5f) * stepZ;

            VegetationInstance inst;
            inst.id = QUuid::createUuid();
            inst.position = QVector3D(wx + offsetX, h, wz + offsetZ);
            inst.rotationY = dist(rng) * 360.0f;
            inst.scale = layer.preset.baseScale *
                (1.0f + (dist(rng) - 0.5f) * 2.0f * layer.preset.scaleVariance);
            inst.slopeAngle = slopeAngle;
            inst.heightAboveSea = h;

            layer.instances.append(inst);
        }
    }

    emit instancesGenerated(layerIndex, layer.instances.size());
}

void VegetationScatter::generateAllInstances()
{
    int total = 0;
    for (int i = 0; i < m_layers.size(); ++i) {
        generateInstances(i);
        total += m_layers[i].instances.size();
    }
    emit allInstancesGenerated(total);
}

void VegetationScatter::clearInstances(int layerIndex)
{
    if (layerIndex >= 0 && layerIndex < m_layers.size()) {
        m_layers[layerIndex].instances.clear();
    }
}

QVector<VegetationInstance> VegetationScatter::allInstances() const
{
    QVector<VegetationInstance> all;
    for (const auto& layer : m_layers) {
        all.append(layer.instances);
    }
    return all;
}

const QVector<VegetationInstance>& VegetationScatter::instances(int layerIndex) const
{
    return m_layers[layerIndex].instances;
}

// ============================================================================
// Terrain data
// ============================================================================
void VegetationScatter::setTerrainData(const QVector<float>& heightmap, int gridW, int gridH,
                                       float worldW, float worldH)
{
    m_heightmap = heightmap;
    m_gridW = gridW;
    m_gridH = gridH;
    m_worldW = worldW;
    m_worldH = worldH;
}

void VegetationScatter::setTerrainNormals(const QVector<QVector3D>& normals)
{
    m_normals = normals;
}

// ============================================================================
// Presets
// ============================================================================
VegetationPreset VegetationScatter::defaultPreset(VegetationType type)
{
    VegetationPreset p;

    switch (type) {
        case VegetationType::Tree:
            p.name = "Tree";
            p.type = VegetationType::Tree;
            p.baseScale = 1.0f;
            p.density = 0.001f;       // ~1 tree per 1000 sq meters
            p.minSlope = 0.0f;
            p.maxSlope = 35.0f;
            p.useBillboard = true;
            p.billboardDistance = 150.0f;
            p.windResponse = 0.8f;
            break;

        case VegetationType::Bush:
            p.name = "Bush";
            p.type = VegetationType::Bush;
            p.baseScale = 0.5f;
            p.density = 0.01f;
            p.minSlope = 0.0f;
            p.maxSlope = 50.0f;
            p.useBillboard = false;
            p.windResponse = 0.6f;
            break;

        case VegetationType::Grass:
            p.name = "Grass";
            p.type = VegetationType::Grass;
            p.baseScale = 0.3f;
            p.scaleVariance = 0.3f;
            p.density = 2.0f;         // Dense ground cover
            p.minSlope = 0.0f;
            p.maxSlope = 60.0f;
            p.useBillboard = false;
            p.castShadows = false;
            p.windResponse = 1.0f;
            break;

        case VegetationType::Rock:
            p.name = "Rock";
            p.type = VegetationType::Rock;
            p.baseScale = 0.8f;
            p.scaleVariance = 0.5f;
            p.density = 0.005f;
            p.minSlope = 10.0f;       // Prefer steeper slopes
            p.maxSlope = 80.0f;
            p.useBillboard = false;
            p.windResponse = 0.0f;
            break;

        case VegetationType::Flower:
            p.name = "Flower";
            p.type = VegetationType::Flower;
            p.baseScale = 0.2f;
            p.density = 1.0f;
            p.minSlope = 0.0f;
            p.maxSlope = 30.0f;
            p.useBillboard = false;
            p.castShadows = false;
            p.windResponse = 0.7f;
            break;

        case VegetationType::Custom:
            p.name = "Custom";
            p.type = VegetationType::Custom;
            p.baseScale = 1.0f;
            p.density = 0.1f;
            break;
    }

    return p;
}

QVector<VegetationPreset> VegetationScatter::builtInPresets()
{
    QVector<VegetationPreset> presets;
    presets.append(defaultPreset(VegetationType::Tree));
    presets.append(defaultPreset(VegetationType::Bush));
    presets.append(defaultPreset(VegetationType::Grass));
    presets.append(defaultPreset(VegetationType::Rock));
    presets.append(defaultPreset(VegetationType::Flower));
    return presets;
}

// ============================================================================
// Serialization
// ============================================================================
QJsonObject VegetationScatter::toJson() const
{
    QJsonObject json;

    QJsonArray layersArr;
    for (const auto& layer : m_layers) {
        layersArr.append(layer.toJson());
    }
    json["layers"] = layersArr;

    return json;
}

void VegetationScatter::fromJson(const QJsonObject& json)
{
    m_layers.clear();
    for (const auto& layerJson : json["layers"].toArray()) {
        VegetationLayer layer;
        layer.fromJson(layerJson.toObject());
        m_layers.append(layer);
    }
}

// ============================================================================
// Private helpers
// ============================================================================
float VegetationScatter::sampleDensity(int layerIndex, float worldX, float worldZ) const
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return 0.0f;

    const VegetationLayer& layer = m_layers[layerIndex];
    if (layer.densityMap.isNull()) return 0.0f;

    float u = worldX / m_worldW;
    float v = worldZ / m_worldH;

    int x = qBound(0, static_cast<int>(u * layer.densityMap.width()),
                    layer.densityMap.width() - 1);
    int y = qBound(0, static_cast<int>(v * layer.densityMap.height()),
                    layer.densityMap.height() - 1);

    return static_cast<float>(layer.densityMap.pixelColor(x, y).red()) / 255.0f;
}

float VegetationScatter::sampleHeight(float worldX, float worldZ) const
{
    if (m_heightmap.isEmpty() || m_gridW <= 0 || m_gridH <= 0) return 0.0f;

    float u = worldX / m_worldW * (m_gridW - 1);
    float v = worldZ / m_worldH * (m_gridH - 1);

    int x0 = qBound(0, static_cast<int>(u), m_gridW - 1);
    int z0 = qBound(0, static_cast<int>(v), m_gridH - 1);
    int x1 = qMin(x0 + 1, m_gridW - 1);
    int z1 = qMin(z0 + 1, m_gridH - 1);

    float fx = u - x0;
    float fz = v - z0;

    float h00 = m_heightmap[z0 * m_gridW + x0];
    float h10 = m_heightmap[z0 * m_gridW + x1];
    float h01 = m_heightmap[z1 * m_gridW + x0];
    float h11 = m_heightmap[z1 * m_gridW + x1];

    // Bilinear interpolation
    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fz;
}

QVector3D VegetationScatter::sampleNormal(float worldX, float worldZ) const
{
    if (m_normals.isEmpty() || m_gridW <= 0 || m_gridH <= 0) {
        return QVector3D(0, 1, 0);
    }

    float u = worldX / m_worldW * (m_gridW - 1);
    float v = worldZ / m_worldH * (m_gridH - 1);

    int x = qBound(0, static_cast<int>(u), m_gridW - 1);
    int z = qBound(0, static_cast<int>(v), m_gridH - 1);

    return m_normals[z * m_gridW + x].normalized();
}

bool VegetationScatter::passesMask(const VegetationPreset& preset, float height,
                                   float slopeAngle) const
{
    if (height < preset.minHeight || height > preset.maxHeight) return false;
    if (slopeAngle < preset.minSlope || slopeAngle > preset.maxSlope) return false;
    return true;
}

} // namespace ks
