#include "XSIImporter.h"

#include <QTextStream>
#include <QRegularExpression>
#include <QStringList>

namespace ks {
namespace fileformat {

// ============================================================================
// XSI Scene (.scn) Import
// ============================================================================

bool importXSIScene(const QByteArray& data, ks::MeshData& out, QString* error) {
    if (data.isEmpty()) {
        if (error) *error = "Empty XSI scene data";
        return false;
    }

    QTextStream stream(data);
    stream.setEncoding(QStringConverter::Latin1);
    
    QVector<QVector3D> positions;
    QVector<QVector2D> uvs;
    QVector<int> indices;
    
    QString line;
    bool inMesh = false;
    bool inPoints = false;
    bool inPolygons = false;
    
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        
        // Skip comments
        if (line.startsWith("//") || line.startsWith("#")) continue;
        
        // Mesh begin
        if (line.contains(QRegularExpression("^Mesh\\s*\\{")) || 
            line.contains(QRegularExpression("^Geometry\\s*\\{"))) {
            inMesh = true;
            continue;
        }
        
        // Points section
        if (line.contains(QRegularExpression("^Points\\s*\\{")) ||
            line.contains(QRegularExpression("^PointList\\s*\\{"))) {
            inPoints = true;
            inPolygons = false;
            continue;
        }
        
        // Polygons section
        if (line.contains(QRegularExpression("^PolygonList\\s*\\{")) ||
            line.contains(QRegularExpression("^Polygons\\s*\\{"))) {
            inPolygons = true;
            inPoints = false;
            continue;
        }
        
        // End of section
        if (line == "}") {
            inPoints = false;
            inPolygons = false;
            inMesh = false;
            continue;
        }
        
        // Parse point data (x y z)
        if (inPoints) {
            QRegularExpression re("(-?\\d+\\.?\\d*)\\s+(-?\\d+\\.?\\d*)\\s+(-?\\d+\\.?\\d*)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
                float x = match.captured(1).toFloat();
                float y = match.captured(2).toFloat();
                float z = match.captured(3).toFloat();
                positions.append(QVector3D(x, y, z));
            }
        }
        
        // Parse polygon data (v1 v2 v3 ...)
        if (inPolygons) {
            QStringList tokens = line.split(QRegularExpression("\\s+"));
            if (tokens.size() >= 3) {
                for (int i = 0; i < tokens.size() - 2; ++i) {
                    int v0 = tokens[0].toInt();
                    int v1 = tokens[i + 1].toInt();
                    int v2 = tokens[i + 2].toInt();
                    
                    if (v0 >= 0 && v0 < positions.size() &&
                        v1 >= 0 && v1 < positions.size() &&
                        v2 >= 0 && v2 < positions.size()) {
                        indices.append(v0);
                        indices.append(v1);
                        indices.append(v2);
                    }
                }
            }
        }
    }
    
    // Build output mesh
    if (positions.isEmpty()) {
        if (error) *error = "No mesh data found in XSI scene";
        return false;
    }
    
    out = ks::createEmpty();
    for (const auto& pos : positions) {
        Vertex v;
        v.position = pos;
        out.vertices.append(v);
    }
    out.indices = indices;
    
    // Generate normals
    out.normals.resize(positions.size(), QVector3D(0, 1, 0));
    for (int i = 0; i < indices.size(); i += 3) {
        int i0 = indices[i];
        int i1 = indices[i + 1];
        int i2 = indices[i + 2];
        
        QVector3D v0 = positions[i0];
        QVector3D v1 = positions[i1];
        QVector3D v2 = positions[i2];
        
        QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
        out.normals[i0] += normal;
        out.normals[i1] += normal;
        out.normals[i2] += normal;
    }
    
    for (auto& n : out.normals) {
        n.normalize();
    }
    
    return true;
}

// ============================================================================
// XSI Export (.exp) Import - similar to .scn but different header
// ============================================================================

bool importXSIExport(const QByteArray& data, ks::MeshData& out, QString* error) {
    // .exp files are similar to .scn but may have different header
    // Try to parse as scene first
    return importXSIScene(data, out, error);
}

// ============================================================================
// XSI Emodel (.emdl) Import - binary format
// ============================================================================

bool importXSIEmodel(const QByteArray& data, ks::MeshData& out, QString* error) {
    if (data.size() < 16) {
        if (error) *error = "XSI emodel file too small";
        return false;
    }
    
    const char* ptr = data.constData();
    const char* end = ptr + data.size();
    
    // Check for magic number (XSI emodel header)
    if (data.size() >= 4 && 
        ptr[0] == 'X' && ptr[1] == 'S' && ptr[2] == 'I' && ptr[3] == 'M') {
        // Binary XSI emodel format
        ptr += 4; // Skip magic
        
        // Read mesh count
        if (ptr + 4 > end) {
            if (error) *error = "Truncated XSI emodel header";
            return false;
        }
        
        uint32_t meshCount = *reinterpret_cast<const uint32_t*>(ptr);
        ptr += 4;
        
        QVector<QVector3D> allPositions;
        QVector<int> allIndices;
        
        for (uint32_t m = 0; m < meshCount; ++m) {
            if (ptr + 8 > end) break;
            
            uint32_t vertexCount = *reinterpret_cast<const uint32_t*>(ptr);
            ptr += 4;
            uint32_t faceCount = *reinterpret_cast<const uint32_t*>(ptr);
            ptr += 4;
            
            // Read vertices
            if (ptr + vertexCount * 12 > end) break;
            
            int baseVertex = allPositions.size();
            for (uint32_t v = 0; v < vertexCount; ++v) {
                float x = *reinterpret_cast<const float*>(ptr); ptr += 4;
                float y = *reinterpret_cast<const float*>(ptr); ptr += 4;
                float z = *reinterpret_cast<const float*>(ptr); ptr += 4;
                allPositions.append(QVector3D(x, y, z));
            }
            
            // Read faces (triangle count * 3 indices)
            if (ptr + faceCount * 12 > end) break;
            
            for (uint32_t f = 0; f < faceCount; ++f) {
                uint32_t i0 = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;
                uint32_t i1 = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;
                uint32_t i2 = *reinterpret_cast<const uint32_t*>(ptr); ptr += 4;
                
                allIndices.append(baseVertex + i0);
                allIndices.append(baseVertex + i1);
                allIndices.append(baseVertex + i2);
            }
        }
        
        if (allPositions.isEmpty()) {
            if (error) *error = "No mesh data in XSI emodel";
            return false;
        }
        
        out = ks::createEmpty();
        for (const auto& pos : allPositions) {
            Vertex v;
            v.position = pos;
            out.vertices.append(v);
        }
        out.indices = allIndices;
        
        // Generate normals
        out.normals.resize(allPositions.size(), QVector3D(0, 1, 0));
        for (int i = 0; i < allIndices.size(); i += 3) {
            int i0 = allIndices[i];
            int i1 = allIndices[i + 1];
            int i2 = allIndices[i + 2];
            
            QVector3D v0 = allPositions[i0];
            QVector3D v1 = allPositions[i1];
            QVector3D v2 = allPositions[i2];
            
            QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
            out.normals[i0] += normal;
            out.normals[i1] += normal;
            out.normals[i2] += normal;
        }
        
        for (auto& n : out.normals) {
            n.normalize();
        }
        
        return true;
    }
    
    // Not a recognized XSI format - try parsing as text
    return importXSIScene(data, out, error);
}

} // namespace fileformat
} // namespace ks
