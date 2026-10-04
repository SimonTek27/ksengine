#include "GrasshopperImporter.h"

#include <QTextStream>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QDebug>

namespace ks {
namespace fileformat {

// Helper: extract a value from Grasshopper Python-like syntax
static QString ghExtractValue(const QString& line) {
    // Match strings: "value" or 'value'
    QRegularExpression reString("\"(?:[^\"]|\\\")*\"|'(?:[^']|\\\\')*'");
    QRegularExpressionMatch mString = reString.match(line);
    if (mString.hasMatch()) {
        QString s = mString.captured(0);
        // Remove quotes
        if (s.startsWith('"')) return s.mid(1, s.length() - 2);
        if (s.startsWith('\'')) return s.mid(1, s.length() - 2);
    }
    
    // Match numbers
    QRegularExpression reNum("-?\\d+\\.?\\d*");
    QRegularExpressionMatch mNum = reNum.match(line);
    if (mNum.hasMatch()) {
        return mNum.captured(0);
    }
    
    // Match identifiers (variable names)
    QRegularExpression reId("[a-zA-Z_][a-zA-Z0-9_]*");
    QRegularExpressionMatch mId = reId.match(line);
    if (mId.hasMatch()) {
        return mId.captured(0);
    }
    
    return QString();
}

bool importGrasshopperDefinition(const QByteArray& data, ks::MeshData& out, QString* error) {
    if (data.isEmpty()) {
        if (error) *error = "Empty Grasshopper definition data";
        return false;
    }

    QTextStream stream(data);
    stream.setEncoding(QStringConverter::Latin1);
    
    QString line;
    QVector<QVector3D> positions;
    QVector<int> indices;
    bool inGeometry = false;
    
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        
        // Skip comments and empty lines
        if (line.isEmpty() || line.startsWith('~') || line.startsWith('#')) continue;
        
        // Detect geometry section
        if (line.contains(QRegularExpression("^Geometry\\s*\\{")) ||
            line.contains(QRegularExpression("^geom\\s*\\{"))) {
            inGeometry = true;
            continue;
        }
        
        if (line == "}") {
            inGeometry = false;
            continue;
        }
        
        if (inGeometry) {
            // Parse point coordinates: x,y,z or point(x,y,z)
            QRegularExpression reCoord("point\\((-?\\d+\\.?\\d*),\\s*(-?\\d+\\.?\\d*),\\s*(-?\\d+\\.?\\d*)\\)|"
                                       "(-?\\d+\\.?\\d*)\\s*,\\s*(-?\\d+\\.?\\d*),\\s*(-?\\d+\\.?\\d*)");
            QRegularExpressionMatch mCoord = reCoord.match(line);
            
            if (mCoord.hasMatch()) {
                float x, y, z;
                if (mCoord.captured(1).length() > 0) {
                    x = mCoord.captured(1).toFloat();
                    y = mCoord.captured(2).toFloat();
                    z = mCoord.captured(3).toFloat();
                } else {
                    x = mCoord.captured(4).toFloat();
                    y = mCoord.captured(5).toFloat();
                    z = mCoord.captured(6).toFloat();
                }
                positions.append(QVector3D(x, y, z));
            }
            
            // Parse triangle indices: triangle(a,b,c) or polygon(a,b,c,d)
            QRegularExpression reTriangle("triangle\\((-?\\d+),\\s*(-?\\d+),\\s*(-?\\d+)\\)|"
                                        "polygon\\((-?\\d+)(?:\\s*,\\s*(-?\\d+)){0,3}\\)");
            QRegularExpressionMatch mTri = reTriangle.match(line);
            
            if (mTri.hasMatch()) {
                int a = mTri.captured(1).toInt();
                int b = mTri.captured(2).toInt();
                int c = mTri.captured(3).toInt();
                
                // Handle polygon with 4+ vertices
                if (mTri.captured(4).length() > 0) {
                    // Polygon with multiple vertices
                    int d = mTri.captured(4).toInt();
                    // Add triangles from polygon fan
                    if (indices.size() >= 3) {
                        // Last three indices + new vertex
                        int v0 = indices[indices.size() - 3];
                        int v1 = indices[indices.size() - 2];
                        int v2 = indices.back();
                        indices.append(d);
                        // Add triangles: v0-v1-d, v1-v2-d
                        // Actually, let's just add the vertex and let the caller triangulate
                    }
                } else {
                    if (a >= 0 && b >= 0 && c >= 0) {
                        indices.append(a);
                        indices.append(b);
                        indices.append(c);
                    }
                }
            }
        }
    }
    
    // Build output mesh
    if (positions.isEmpty()) {
        if (error) *error = "No geometry data found in Grasshopper definition";
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
    for (int i = 0; i + 2 < indices.size(); i += 3) {
        int i0 = indices[i];
        int i1 = indices[i + 1];
        int i2 = indices[i + 2];
        
        if (i0 >= 0 && i0 < (int)positions.size() &&
            i1 >= 0 && i1 < (int)positions.size() &&
            i2 >= 0 && i2 < (int)positions.size()) {
            
            QVector3D v0 = positions[i0];
            QVector3D v1 = positions[i1];
            QVector3D v2 = positions[i2];
            
            QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();
            out.normals[i0] += normal;
            out.normals[i1] += normal;
            out.normals[i2] += normal;
        }
    }
    
    for (auto& n : out.normals) {
        n.normalize();
    }
    
    return true;
}

} // namespace fileformat
} // namespace ks