#include "AdvancedMeshOps.h"
#include <QSet>
#include <QDebug>
#include <cmath>
#include <algorithm>


namespace ks {

QVector3D BooleanCsg::computeCentroid(const QVector<QVector3D>& vertices) {
    QVector3D centroid(0, 0, 0);
    for (const auto& v : vertices) centroid += v;
    if (!vertices.isEmpty()) centroid /= vertices.size();
    return centroid;
}

bool BooleanCsg::isPointInsidePolygon(const QVector3D& point, const QVector<int>& indices, const QVector<QVector3D>& vertices) {
    if (indices.size() < 3) return false;

    // Project onto best-fit plane
    QVector3D centroid;
    for (int idx : indices) centroid += vertices[idx];
    centroid /= indices.size();

    QVector3D normal;
    for (int i = 0; i < indices.size(); ++i) {
        const QVector3D& p0 = vertices[indices[i]];
        const QVector3D& p1 = vertices[indices[(i + 1) % indices.size()]];
        normal += QVector3D::crossProduct(p0 - centroid, p1 - centroid);
    }
    normal.normalize();
    if (normal.length() < 0.0001f) return false;

    // Find best projection axis
    QVector3D absN(qAbs(normal.x()), qAbs(normal.y()), qAbs(normal.z()));
    int u = 0, v = 1;
    if (absN.x() > absN.y() && absN.x() > absN.z()) u = 1, v = 2;
    else if (absN.y() > absN.z()) u = 0, v = 2;

    // Ray casting in 2D projection
    float px = point[u], py = point[v];
    bool inside = false;
    for (int i = 0, j = indices.size() - 1; i < indices.size(); j = i++) {
        float xi = vertices[indices[i]][u], yi = vertices[indices[i]][v];
        float xj = vertices[indices[j]][u], yj = vertices[indices[j]][v];
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

QVector3D BooleanCsg::lineIntersection(const QVector3D& p1, const QVector3D& p2, const QVector3D& planeNormal, float planeDist) {
    QVector3D dir = p2 - p1;
    float denom = QVector3D::dotProduct(planeNormal, dir);
    if (qAbs(denom) < 0.0001f) return p1;
    float t = (planeDist - QVector3D::dotProduct(planeNormal, p1)) / denom;
    return p1 + dir * t;
}

QVector<BooleanCsg::EdgePair> BooleanCsg::findEdgeIntersections(const MeshData& a, const MeshData& b) {
    QVector<EdgePair> intersections;

    for (const auto& faceA : a.faces) {
        for (int i = 0; i < faceA.indices.size(); ++i) {
            QVector3D p1 = a.vertices[faceA[i]].position;
            QVector3D p2 = a.vertices[faceA[(i + 1) % faceA.indices.size()]].position;

            for (const auto& faceB : b.faces) {
                for (int j = 0; j < faceB.indices.size(); ++j) {
                    QVector3D q1 = b.vertices[faceB[j]].position;
                    QVector3D q2 = b.vertices[faceB[(j + 1) % faceB.indices.size()]].position;

                    QVector3D r = p2 - p1;
                    QVector3D s = q2 - q1;
                    QVector3D n = QVector3D::crossProduct(r, s);

                    if (n.length() < 0.0001f) continue;

                    QVector3D diff = q1 - p1;
                    float t = QVector3D::dotProduct(QVector3D::crossProduct(diff, s), n) / n.length();
                    float u = QVector3D::dotProduct(QVector3D::crossProduct(diff, r), n) / n.length();

                    if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
                        EdgePair ep;
                        ep.point = p1 + r * t;
                        ep.paramA = t;
                        ep.paramB = u;
                        intersections.append(ep);
                    }
                }
            }
        }
    }

    return intersections;
}

QVector<BooleanCsg::Polygon> BooleanCsg::extractPolygons(const MeshData& mesh) {
    QVector<Polygon> polygons;
    for (const auto& face : mesh.faces) {
        Polygon poly;
        for (int idx : face.indices) {
            poly.indices.append(idx);
        }
        poly.normal = QVector3D::crossProduct(
            mesh.vertices[face[1]].position - mesh.vertices[face[0]].position,
            mesh.vertices[face[2]].position - mesh.vertices[face[0]].position
        ).normalized();
        poly.meshId = 0;
        polygons.append(poly);
    }
    return polygons;
}

void BooleanCsg::clipPolygons(QVector<Polygon>& polygons, const QVector3D& planeNormal, float planeDistance, bool keepPositive) {
    QVector<Polygon> newPolygons;

    for (const Polygon& poly : polygons) {
        QVector<QVector3D> positions;
        QVector<float> distances;

        for (int idx : poly.indices) {
            positions.append(QVector3D(0, 0, 0));
        }

        for (const auto& p : positions) {
            distances.append(QVector3D::dotProduct(planeNormal, p) - planeDistance);
        }

        QVector<Polygon> clipped;
        for (int i = 0; i < poly.indices.size(); ++i) {
            int next = (i + 1) % poly.indices.size();
            float d1 = distances[i];
            float d2 = distances[next];

            if ((keepPositive && d1 >= 0) || (!keepPositive && d1 <= 0)) {
                Polygon newPoly = poly;
                newPoly.indices = {poly.indices[i], poly.indices[next]};
                clipped.append(newPoly);
            }
        }

        newPolygons.append(clipped);
    }

    polygons = newPolygons;
}

void BooleanCsg::addPolygonToMesh(MeshData& result, const Polygon& poly, int meshId) {
    Face face;
    face.materialId = meshId; // Store origin mesh ID for material tracking
    for (int idx : poly.indices) {
        face.indices.append(idx);
    }
    result.faces.append(face);

    // Recalculate face normal
    if (face.indices.size() >= 3 && result.vertices.size() > face.indices[2]) {
        QVector3D v1 = result.vertices[face.indices[1]].position - result.vertices[face.indices[0]].position;
        QVector3D v2 = result.vertices[face.indices[2]].position - result.vertices[face.indices[0]].position;
        face.normal = QVector3D::crossProduct(v1, v2).normalized();
    }
}

QVector3D BooleanCsg::polygonNormal(const Polygon& poly, const QVector<QVector3D>& vertices) {
    if (poly.indices.size() < 3) return QVector3D(0, 1, 0);
    QVector3D v1 = vertices[poly.indices[1]] - vertices[poly.indices[0]];
    QVector3D v2 = vertices[poly.indices[2]] - vertices[poly.indices[0]];
    return QVector3D::crossProduct(v1, v2).normalized();
}

float BooleanCsg::planeDistance(const QVector3D& normal, const QVector3D& point) {
    return QVector3D::dotProduct(normal, point);
}

void BooleanCsg::mergeDuplicateVertices(MeshData& mesh, float tolerance) {
    QMap<QString, int> vertexMap;
    QVector<Vertex> newVertices;
    QVector<Face> newFaces;

    for (int i = 0; i < mesh.vertices.size(); ++i) {
        const auto& v = mesh.vertices[i];
        QString key = QString("%1,%2,%3").arg(v.position.x()).arg(v.position.y()).arg(v.position.z());
        QString roundedKey = QString("%1,%2,%3").arg(v.position.x(), 0, 'f', 4).arg(v.position.y(), 0, 'f', 4).arg(v.position.z(), 0, 'f', 4);

        if (vertexMap.contains(roundedKey)) {
            int existingIdx = vertexMap[roundedKey];
            for (auto& face : mesh.faces) {
                for (int j = 0; j < face.indices.size(); ++j) {
                    if (face.indices[j] == i) {
                        face.indices[j] = existingIdx;
                    }
                }
            }
        } else {
            vertexMap[roundedKey] = newVertices.size();
            newVertices.append(v);
            for (auto& face : mesh.faces) {
                for (int j = 0; j < face.indices.size(); ++j) {
                    if (face.indices[j] == i) {
                        face.indices[j] = newVertices.size() - 1;
                    }
                }
            }
        }
    }

    QSet<QString> uniqueFaces;
    for (const auto& face : mesh.faces) {
        QString faceKey;
        QList<int> sortedIndices = face.indices;
        std::sort(sortedIndices.begin(), sortedIndices.end());
        for (int idx : sortedIndices) {
            faceKey += QString::number(idx) + ",";
        }
        if (!uniqueFaces.contains(faceKey)) {
            uniqueFaces.insert(faceKey);
            newFaces.append(face);
        }
    }

    mesh.vertices = newVertices;
    mesh.faces = newFaces;
}

void BooleanCsg::splitPolygonAtEdge(const Polygon& poly, const EdgePair& edge, QVector<Polygon>& front, QVector<Polygon>& back) {
    Polygon frontPoly, backPoly;
    frontPoly.normal = poly.normal;
    backPoly.normal = poly.normal;
    frontPoly.material = poly.material;
    backPoly.material = poly.material;
    frontPoly.meshId = poly.meshId;
    backPoly.meshId = poly.meshId;

    QVector3D splitNormal = edge.normal.lengthSquared() > 0.001f
        ? edge.normal
        : QVector3D::crossProduct(poly.normal, QVector3D(1, 0, 0));
    if (splitNormal.lengthSquared() < 0.001f) splitNormal = QVector3D(0, 0, 1);
    splitNormal.normalize();

    for (int i = 0; i < poly.indices.size(); ++i) {
        int idx = poly.indices[i];

        float dist = planeDistance(splitNormal, edge.point);

        if (dist >= 0) frontPoly.indices.append(idx);
        else backPoly.indices.append(idx);
    }

    if (!frontPoly.indices.isEmpty()) front.append(frontPoly);
    if (!backPoly.indices.isEmpty()) back.append(backPoly);
}

MeshData BooleanCsg::booleanOperation(const MeshData& a, const MeshData& b, int operationType, const BooleanConfig& config) {
    MeshData result;

    QVector<QVector3D> positionsA, positionsB;
    for (const auto& v : a.vertices) positionsA.append(v.position);
    for (const auto& v : b.vertices) positionsB.append(v.position);

    QVector3D centerA = computeCentroid(positionsA);
    QVector3D centerB = computeCentroid(positionsB);

    QVector<Polygon> allPolygons;

    for (const auto& face : a.faces) {
        Polygon poly;
        for (int idx : face.indices) {
            poly.indices.append(idx);
            poly.meshId = 0;
        }
        allPolygons.append(poly);
    }

    for (const auto& face : b.faces) {
        Polygon poly;
        for (int idx : face.indices) {
            poly.indices.append(idx + a.vertices.size());
            poly.meshId = 1;
        }
        allPolygons.append(poly);
    }

    for (const auto& v : a.vertices) {
        result.vertices.append(v);
    }
    for (const auto& v : b.vertices) {
        result.vertices.append(v);
    }

    QVector3D sepAxis = (centerB - centerA).normalized();
    if (sepAxis.length() < 0.0001f) sepAxis = QVector3D(1, 0, 0);

    float distA = QVector3D::dotProduct(sepAxis, centerA);
    float distB = QVector3D::dotProduct(sepAxis, centerB);

    for (const auto& face : a.faces) {
        bool keep = false;
        bool hasInside = false;
        bool hasOutside = false;

        for (int idx : face.indices) {
            float dist = QVector3D::dotProduct(sepAxis, result.vertices[idx].position);
            if (dist < distB) hasInside = true;
            else hasOutside = true;
        }

        if (operationType == 0) {
            keep = !hasOutside;
        } else if (operationType == 1) {
            keep = hasInside && !hasOutside;
        } else {
            keep = (hasInside != hasOutside);
        }

        if (keep) {
            Face newFace = face;
            result.faces.append(newFace);
        }
    }

    for (const auto& face : b.faces) {
        bool keep = false;

        if (operationType == 0) {
            keep = true;
        } else if (operationType == 1) {
            keep = false;
        } else {
            keep = true;
        }

        if (keep) {
            Face newFace;
            for (int idx : face.indices) {
                newFace.indices.append(idx);
            }
            result.faces.append(newFace);
        }
    }

    mergeDuplicateVertices(result, config.dissolveDistance);
    result.computeNormals();
    result.computeBoundingBox();

    return result;
}

MeshData BooleanCsg::unite(const MeshData& a, const MeshData& b, const BooleanConfig& config) {
    MeshData result = booleanOperation(a, b, 0, config);
    return result;
}

MeshData BooleanCsg::intersect(const MeshData& a, const MeshData& b, const BooleanConfig& config) {
    MeshData result = booleanOperation(a, b, 1, config);
    return result;
}

MeshData BooleanCsg::subtract(const MeshData& a, const MeshData& b, const BooleanConfig& config) {
    MeshData result = booleanOperation(a, b, 2, config);
    return result;
}

MeshData BooleanCsg::slice(const MeshData& a, const MeshData& b, const BooleanConfig& config) {
    MeshData result;
    result.vertices = a.vertices;

    // Create slice plane from b's first face normal
    QVector3D sliceNormal(0, 1, 0);
    if (!b.faces.isEmpty() && b.vertices.size() >= 3) {
        int i1 = b.faces[0].indices[0];
        int i2 = b.faces[0].indices[1];
        int i3 = b.faces[0].indices[2];
        if (i1 < b.vertices.size() && i2 < b.vertices.size() && i3 < b.vertices.size()) {
            QVector3D v1 = b.vertices[i2].position - b.vertices[i1].position;
            QVector3D v2 = b.vertices[i3].position - b.vertices[i1].position;
            sliceNormal = QVector3D::crossProduct(v1, v2).normalized();
        }
    }

    float sliceOffset = config.sliceOffset;
    for (Vertex& v : result.vertices) {
        float dist = QVector3D::dotProduct(sliceNormal, v.position);
        if (qAbs(dist - sliceOffset) < config.dissolveDistance) {
            v.position -= sliceNormal * (dist - sliceOffset);
        }
    }

    result.faces = a.faces;
    result.computeNormals();
    return result;
}

bool ConvexHull::isVisible(const Face& face, const QVector3D& point, const QVector<QVector3D>& vertices) {
    QVector3D v1 = vertices[face.v2] - vertices[face.v1];
    QVector3D v2 = vertices[face.v3] - vertices[face.v1];
    QVector3D normal = QVector3D::crossProduct(v1, v2).normalized();
    return QVector3D::dotProduct(normal, point - vertices[face.v1]) > 0.0001f;
}

QVector<ConvexHull::Face> ConvexHull::createHull(const QVector<QVector3D>& points) {
    if (points.size() < 4) return {};

    int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    for (int i = 1; i < points.size(); ++i) {
        if (points[i].x() < points[minX].x()) minX = i;
        if (points[i].x() > points[maxX].x()) maxX = i;
        if (points[i].y() < points[minY].y()) minY = i;
        if (points[i].y() > points[maxY].y()) maxY = i;
        if (points[i].z() < points[minZ].z()) minZ = i;
        if (points[i].z() > points[maxZ].z()) maxZ = i;
    }

    QVector<Face> hull;

    auto addFace = [&](int a, int b, int c) {
        Face f;
        f.v1 = a; f.v2 = b; f.v3 = c;
        f.visible = false;
        hull.append(f);
    };

    addFace(0, 1, 2);
    addFace(0, 2, 1);

    QSet<int> processed;
    processed.insert(0);
    processed.insert(1);
    processed.insert(2);

    for (int i = 0; i < points.size(); ++i) {
        if (processed.contains(i)) continue;

        QVector<int> visibleFaces;
        for (int fi = 0; fi < hull.size(); ++fi) {
            if (isVisible(hull[fi], points[i], points)) {
                visibleFaces.append(fi);
            }
        }

        if (visibleFaces.isEmpty()) continue;

        QSet<QPair<int,int>> horizonEdges;
        QSet<int> visibleSet(visibleFaces.begin(), visibleFaces.end());

        for (int fi : visibleFaces) {
            const Face& f = hull[fi];
            int verts[3] = {f.v1, f.v2, f.v3};
            for (int e = 0; e < 3; ++e) {
                int a = verts[e], b = verts[(e + 1) % 3];
                QPair<int,int> edge(qMin(a,b), qMax(a,b));
                bool shared = false;
                for (int fi2 : visibleFaces) {
                    if (fi2 == fi) continue;
                    const Face& f2 = hull[fi2];
                    int verts2[3] = {f2.v1, f2.v2, f2.v3};
                    for (int e2 = 0; e2 < 3; ++e2) {
                        int a2 = verts2[e2], b2 = verts2[(e2 + 1) % 3];
                        if ((a2 == a && b2 == b) || (a2 == b && b2 == a)) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared) break;
                }
                if (!shared) {
                    if (horizonEdges.contains(QPair<int,int>(qMin(b,a), qMax(b,a)))) {
                        horizonEdges.remove(QPair<int,int>(qMin(b,a), qMax(b,a)));
                    } else {
                        horizonEdges.insert(edge);
                    }
                }
            }
        }

        QList<int> sortedVisible = visibleFaces;
        std::sort(sortedVisible.begin(), sortedVisible.end(), std::greater<int>());
        for (int fi : sortedVisible) {
            hull.removeAt(fi);
        }

        for (const auto& edge : horizonEdges) {
            addFace(edge.second, edge.first, i);
        }

        processed.insert(i);
    }

    return hull;
}

MeshData ConvexHull::compute(const QVector<QVector3D>& points) {
    if (points.size() < 4) return MeshData();

    QVector<Face> hullFaces = createHull(points);

    MeshData mesh;
    mesh.vertices.resize(points.size());
    for (int i = 0; i < points.size(); ++i) {
        mesh.vertices[i].position = points[i];
    }

    for (const auto& face : hullFaces) {
        mesh.faces.push_back(ks::Face({face.v1, face.v2, face.v3}));
    }

    mesh.computeNormals();
    mesh.computeBoundingBox();
    return mesh;
}

MeshData ConvexHull::compute(const MeshData& mesh) {
    QVector<QVector3D> points;
    for (const auto& v : mesh.vertices) {
        points.append(v.position);
    }
    return compute(points);
}

QVector3D PolygonOperations::triangleArea(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3) {
    return QVector3D::crossProduct(p2 - p1, p3 - p1) * 0.5f;
}

float PolygonOperations::triangleAspectRatio(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3) {
    float a = (p2 - p1).length();
    float b = (p3 - p2).length();
    float c = (p3 - p1).length();
    float s = (a + b + c) / 2.0f;
    float area = sqrt(s * (s - a) * (s - b) * (s - c));
    float maxEdge = qMax(a, qMax(b, c));
    return maxEdge > 0 ? (2.0f * area / maxEdge) : 0;
}

float PolygonOperations::triangleQuality(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3) {
    float a = (p2 - p1).length();
    float b = (p3 - p2).length();
    float c = (p3 - p1).length();
    float s = (a + b + c) / 2.0f;
    float area = sqrt(s * (s - a) * (s - b) * (s - c));
    float circumradius = (a * b * c) / (4.0f * area);
    return circumradius > 0 ? (2.0f * area / (circumradius * circumradius)) : 0;
}

int PolygonOperations::findSharedEdge(const Face& f1, const Face& f2) {
    for (int i = 0; i < f1.indices.size(); ++i) {
        int prev = (i - 1 + f1.indices.size()) % f1.indices.size();
        int v1 = f1.indices[i];
        int v2 = f1.indices[prev];

        for (int j = 0; j < f2.indices.size(); ++j) {
            int prevJ = (j - 1 + f2.indices.size()) % f2.indices.size();
            if ((f2.indices[j] == v1 && f2.indices[prevJ] == v2) ||
                (f2.indices[j] == v2 && f2.indices[prevJ] == v1)) {
                return i;
            }
        }
    }
    return -1;
}

bool PolygonOperations::shareEdge(const Face& f1, const Face& f2) {
    return findSharedEdge(f1, f2) >= 0;
}

QVector<int> PolygonOperations::getAdjacentFaces(const MeshData& mesh, int faceIndex) {
    QVector<int> adjacent;
    const Face& target = mesh.faces[faceIndex];

    for (int i = 0; i < mesh.faces.size(); ++i) {
        if (i != faceIndex && shareEdge(target, mesh.faces[i])) {
            adjacent.append(i);
        }
    }

    return adjacent;
}

int PolygonOperations::getEdgeFaceCount(const MeshData& mesh, int v1, int v2) {
    int count = 0;
    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int next = (i + 1) % face.indices.size();
            if ((face.indices[i] == v1 && face.indices[next] == v2) ||
                (face.indices[i] == v2 && face.indices[next] == v1)) {
                count++;
                break;
            }
        }
    }
    return count;
}

QVector<QVector<int>> PolygonOperations::findHoles(const MeshData& mesh) {
    QVector<QVector<int>> holes;
    QSet<QPair<int, int>> boundaryEdges;

    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int next = (i + 1) % face.indices.size();
            int v1 = face.indices[i];
            int v2 = face.indices[next];

            if (getEdgeFaceCount(mesh, v1, v2) == 1) {
                boundaryEdges.insert(QPair<int, int>(qMin(v1, v2), qMax(v1, v2)));
            }
        }
    }

    QSet<QPair<int, int>> usedEdges;
    for (const auto& edge : boundaryEdges) {
        if (usedEdges.contains(edge)) continue;

        QVector<int> hole;
        int current = edge.first;
        int next = edge.second;

        while (true) {
            hole.append(current);
            usedEdges.insert(QPair<int, int>(qMin(current, next), qMax(current, next)));

            int foundNext = -1;
            for (const auto& e : boundaryEdges) {
                if ((e.first == current || e.second == current) && e.first != next && e.second != next) {
                    foundNext = (e.first == current) ? e.second : e.first;
                    break;
                }
            }

            if (foundNext < 0 || hole.contains(foundNext)) break;
            current = next;
            next = foundNext;
        }

        if (hole.size() >= 3) {
            holes.append(hole);
        }
    }

    return holes;
}

MeshData PolygonOperations::fillHoles(const MeshData& mesh, int maxHoleSize) {
    MeshData result = mesh;
    QVector<QVector<int>> holes = findHoles(mesh);

    for (const auto& hole : holes) {
        if (hole.size() <= maxHoleSize) {
            if (hole.size() == 3) {
                result.faces.append(Face{hole});
            } else {
                int center = result.vertices.size();
                Vertex centerVert;
                centerVert.position = QVector3D(0, 0, 0);
                for (int idx : hole) {
                    centerVert.position += result.vertices[idx].position;
                }
                centerVert.position /= hole.size();
                result.vertices.append(centerVert);

                for (int i = 0; i < hole.size(); ++i) {
                    int next = (i + 1) % hole.size();
                    result.faces.append(Face{hole[i], hole[next], center});
                }
            }
        }
    }

    result.computeNormals();
    return result;
}

MeshData PolygonOperations::triangulateQuads(const MeshData& mesh, bool beauty) {
    MeshData result = mesh;

    for (int i = result.faces.size() - 1; i >= 0; --i) {
        auto& face = result.faces[i];
        if (face.indices.size() == 4) {
            if (beauty) {
                QVector3D d1 = result.vertices[face[0]].position - result.vertices[face[2]].position;
                QVector3D d2 = result.vertices[face[1]].position - result.vertices[face[3]].position;
                float diagonal1 = d1.length();
                float diagonal2 = d2.length();

                if (diagonal1 < diagonal2) {
                    result.faces.append(Face{face[0], face[1], face[2]});
                    result.faces.append(Face{face[0], face[2], face[3]});
                } else {
                    result.faces.append(Face{face[0], face[1], face[3]});
                    result.faces.append(Face{face[1], face[2], face[3]});
                }
            } else {
                result.faces.append(Face{face[0], face[1], face[2]});
                result.faces.append(Face{face[0], face[2], face[3]});
            }
            result.faces.removeAt(i);
        }
    }

    result.computeNormals();
    return result;
}

MeshData PolygonOperations::convertToQuads(const MeshData& mesh, float angleThreshold) {
    MeshData result = mesh;

    // Build edge-to-face adjacency
    QMap<QPair<int,int>, QVector<int>> edgeFaces;
    for (int fi = 0; fi < result.faces.size(); ++fi) {
        const Face& f = result.faces[fi];
        for (int i = 0; i < f.indices.size(); ++i) {
            int a = f.indices[i];
            int b = f.indices[(i + 1) % f.indices.size()];
            edgeFaces[QPair<int,int>(qMin(a,b), qMax(a,b))].append(fi);
        }
    }

    QSet<int> processed;
    QVector<Face> newFaces;

    for (int fi = 0; fi < result.faces.size(); ++fi) {
        if (processed.contains(fi)) continue;
        const Face& f = result.faces[fi];
        if (f.indices.size() != 3) continue;

        // Try to merge with an adjacent triangle to form a quad
        for (int i = 0; i < 3; ++i) {
            int a = f.indices[i];
            int b = f.indices[(i + 1) % 3];
            QPair<int,int> edge(qMin(a,b), qMax(a,b));

            const QVector<int>& adj = edgeFaces[edge];
            for (int adjFi : adj) {
                if (adjFi == fi || processed.contains(adjFi)) continue;
                const Face& adjF = result.faces[adjFi];
                if (adjF.indices.size() != 3) continue;

                // Check angle between faces
                QVector3D n1 = QVector3D::crossProduct(
                    result.vertices[f[1]].position - result.vertices[f[0]].position,
                    result.vertices[f[2]].position - result.vertices[f[0]].position).normalized();
                QVector3D n2 = QVector3D::crossProduct(
                    result.vertices[adjF[1]].position - result.vertices[adjF[0]].position,
                    result.vertices[adjF[2]].position - result.vertices[adjF[0]].position).normalized();
                float angle = qAcos(qBound(-1.0f, QVector3D::dotProduct(n1, n2), 1.0f));
                if (angle > angleThreshold) continue;

                // Collect unique vertices from both triangles
                QSet<int> verts;
                for (int idx : f.indices) verts.insert(idx);
                for (int idx : adjF.indices) verts.insert(idx);

                if (verts.size() == 4) {
                    Face quad;
                    // Order vertices to form a proper quad
                    QVector<int> ordered;
                    // Find the two vertices that are NOT on the shared edge
                    for (int idx : f.indices) {
                        if (idx != a && idx != b) ordered.append(idx);
                    }
                    ordered.append(a);
                    for (int idx : adjF.indices) {
                        if (idx != a && idx != b && !ordered.contains(idx)) ordered.append(idx);
                    }
                    ordered.append(b);

                    if (ordered.size() == 4) {
                        quad.indices = ordered;
                        newFaces.append(quad);
                        processed.insert(fi);
                        processed.insert(adjFi);
                        goto nextFace;
                    }
                }
            }
        }
        // If no merge found, keep original triangle
        newFaces.append(f);
        processed.insert(fi);
        nextFace:;
    }

    result.faces = newFaces;
    result.computeNormals();
    return result;
}

MeshData PolygonOperations::splitNonPlanarFaces(const MeshData& mesh, float threshold) {
    MeshData result = mesh;
    QVector<Face> newFaces;

    for (const auto& face : result.faces) {
        if (face.indices.size() < 4) {
            newFaces.append(face);
            continue;
        }

        // Check if face is planar by computing distances from the plane of first 3 vertices
        QVector3D v0 = result.vertices[face[0]].position;
        QVector3D v1 = result.vertices[face[1]].position;
        QVector3D v2 = result.vertices[face[2]].position;
        QVector3D edge1 = v1 - v0;
        QVector3D edge2 = v2 - v0;
        QVector3D faceNormal = QVector3D::crossProduct(edge1, edge2).normalized();

        bool isPlanar = true;
        for (int i = 3; i < face.indices.size(); ++i) {
            float dist = qAbs(QVector3D::dotProduct(faceNormal, result.vertices[face[i]].position - v0));
            if (dist > threshold) {
                isPlanar = false;
                break;
            }
        }

        if (isPlanar) {
            newFaces.append(face);
        } else {
            // Fan triangulation from first vertex
            for (int i = 1; i < face.indices.size() - 1; ++i) {
                newFaces.append(Face{face[0], face[i], face[i + 1]});
            }
        }
    }

    result.faces = newFaces;
    result.computeNormals();
    return result;
}

MeshData PolygonOperations::planarFaces(const MeshData& mesh, float threshold) {
    MeshData result = mesh;

    for (auto& face : result.faces) {
        if (face.indices.size() < 3) continue;

        QVector3D v0 = result.vertices[face[0]].position;
        QVector3D v1 = result.vertices[face[1]].position;
        QVector3D v2 = result.vertices[face[2]].position;
        QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0).normalized();

        // Project all vertices onto the plane and snap
        for (int i = 0; i < face.indices.size(); ++i) {
            float dist = QVector3D::dotProduct(normal, result.vertices[face[i]].position - v0);
            if (qAbs(dist) < threshold) {
                result.vertices[face[i]].position -= normal * dist;
            }
        }
    }

    result.computeNormals();
    return result;
}

MeshData PolygonOperations::mergeFaces(const MeshData& mesh, const QVector<int>& faceIndices) {
    if (faceIndices.size() < 2) return mesh;

    MeshData result = mesh;
    QSet<int> toMerge(faceIndices.begin(), faceIndices.end());

    // Find all shared edges between the faces to merge
    QSet<QPair<int,int>> sharedEdges;
    QVector<int> sortedIndices = faceIndices;

    for (int i = 0; i < sortedIndices.size(); ++i) {
        for (int j = i + 1; j < sortedIndices.size(); ++j) {
            int fi = sortedIndices[i];
            int fj = sortedIndices[j];
            if (fi < result.faces.size() && fj < result.faces.size()) {
                int edge = PolygonOperations::findSharedEdge(result.faces[fi], result.faces[fj]);
                if (edge >= 0) {
                    const Face& f = result.faces[fi];
                    int a = f.indices[edge];
                    int b = f.indices[(edge + 1) % f.indices.size()];
                    sharedEdges.insert(QPair<int,int>(qMin(a,b), qMax(a,b)));
                }
            }
        }
    }

    // Collect all vertices from merged faces, excluding shared edges
    QSet<int> allVerts;
    for (int fi : sortedIndices) {
        if (fi >= result.faces.size()) continue;
        for (int idx : result.faces[fi].indices) {
            allVerts.insert(idx);
        }
    }

    // Build merged face by walking the boundary
    QVector<int> merged;
    if (!sortedIndices.isEmpty() && sortedIndices[0] < result.faces.size()) {
        const Face& startFace = result.faces[sortedIndices[0]];
        for (int idx : startFace.indices) {
            merged.append(idx);
        }
    }

    // Remove old faces (in reverse order)
    QList<int> sortedRemove = sortedIndices;
    std::sort(sortedRemove.begin(), sortedRemove.end(), std::greater<int>());
    for (int fi : sortedRemove) {
        if (fi < result.faces.size()) {
            result.faces.removeAt(fi);
        }
    }

    // Add merged face
    if (merged.size() >= 3) {
        Face mergedFace;
        mergedFace.indices = merged;
        result.faces.append(mergedFace);
    }

    result.computeNormals();
    return result;
}

MeshData PolygonOperations::separateFaces(const MeshData& mesh, const QVector<int>& faceIndices) {
    if (faceIndices.isEmpty()) {
        return MeshData();
    }

    MeshData separated;
    QSet<int> toSeparate;
    for (int fi : faceIndices) {
        if (fi >= 0 && fi < mesh.faces.size()) {
            toSeparate.insert(fi);
        }
    }

    // Map old vertex indices to new ones
    QMap<int, int> vertexMap;

    // Copy separated faces and their vertices
    for (int fi : faceIndices) {
        if (fi < 0 || fi >= mesh.faces.size()) continue;
        const Face& face = mesh.faces[fi];

        Face newFace;
        for (int idx : face.indices) {
            if (!vertexMap.contains(idx)) {
                vertexMap[idx] = separated.vertices.size();
                separated.vertices.append(mesh.vertices[idx]);
                separated.normals.append(mesh.normals[idx]);
                separated.uvs.append(mesh.uvs[idx]);
            }
            newFace.indices.append(vertexMap[idx]);
        }
        separated.faces.append(newFace);
    }

    separated.computeNormals();
    return separated;
}

MeshData PolygonOperations::symmetricDifference(const MeshData& a, const MeshData& b) {
    // Symmetric difference = (A - B) + (B - A)
    MeshData subAB = BooleanCsg::subtract(a, b);
    MeshData subBA = BooleanCsg::subtract(b, a);

    MeshData result;
    result.vertices = subAB.vertices;
    result.faces = subAB.faces;

    int vertexOffset = subAB.vertices.size();
    for (const auto& v : subBA.vertices) {
        result.vertices.append(v);
    }
    for (const auto& f : subBA.faces) {
        Face newFace;
        for (int idx : f.indices) {
            newFace.indices.append(idx + vertexOffset);
        }
        newFace.materialId = f.materialId;
        result.faces.append(newFace);
    }

    BooleanCsg::mergeDuplicateVertices(result, 0.0001f);
    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

int KnifeTool::splitEdge(MeshData& mesh, int v1, int v2, float t) {
    Vertex newVert = mesh.vertices[v1];
    newVert.position = mesh.vertices[v1].position + (mesh.vertices[v2].position - mesh.vertices[v1].position) * t;

    int newIdx = mesh.vertices.size();
    mesh.vertices.append(newVert);

    return newIdx;
}

QVector<int> KnifeTool::splitFace(MeshData& mesh, int faceIndex, const QVector3D& point) {
    QVector<int> newVertIndices;

    const auto& face = mesh.faces[faceIndex];
    int n = face.indices.size();

    for (int i = 0; i < n; ++i) {
        QVector3D p1 = mesh.vertices[face[i]].position;
        QVector3D p2 = mesh.vertices[face[(i + 1) % n]].position;

        QVector3D edge = p2 - p1;
        QVector3D toPoint = point - p1;

        float edgeLen = edge.length();
        if (edgeLen < 0.0001f) continue;

        float t = QVector3D::dotProduct(toPoint, edge) / (edgeLen * edgeLen);
        if (t >= 0.0f && t <= 1.0f) {
            Vertex newVert;
            newVert.position = p1 + edge * t;
            newVert.normal = (mesh.vertices[face[i]].normal * (1.0f - t) + mesh.vertices[face[(i + 1) % n]].normal * t).normalized();

            int newIdx = mesh.vertices.size();
            mesh.vertices.append(newVert);
            newVertIndices.append(newIdx);
        }
    }

    if (newVertIndices.size() >= 2) {
        for (int i = 0; i < n; ++i) {
            int newV = newVertIndices.value(i, -1);
            if (newV >= 0) {
                Face tri;
                tri.indices = {face[i], newV, newVertIndices[(i + 1) % n]};
                mesh.faces.append(tri);
            }
        }
    }

    return newVertIndices;
}

QVector<KnifeTool::CutPoint> KnifeTool::intersectWithPlane(const MeshData& mesh, const QVector3D& point, const QVector3D& normal) {
    QVector<CutPoint> cutPoints;

    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            int next = (i + 1) % face.indices.size();
            QVector3D p1 = mesh.vertices[face[i]].position;
            QVector3D p2 = mesh.vertices[face[next]].position;

            float d1 = QVector3D::dotProduct(normal, p1 - point);
            float d2 = QVector3D::dotProduct(normal, p2 - point);

            if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) {
                float t = d1 / (d1 - d2);
                CutPoint cp;
                cp.position = p1 + (p2 - p1) * t;
                cp.t = t;
                cp.isNewVertex = true;
                cutPoints.append(cp);
            }
        }
    }

    return cutPoints;
}

MeshData KnifeTool::cut(const MeshData& mesh, const QVector3D& start, const QVector3D& end, bool snapToVertex) {
    MeshData result = mesh;
    QVector3D direction = (end - start).normalized();

    QVector<CutPoint> startCuts = intersectWithPlane(result, start, direction);
    QVector<CutPoint> endCuts = intersectWithPlane(result, end, direction);

    for (const auto& cp : startCuts) {
        if (cp.isNewVertex) {
            Vertex newVert;
            newVert.position = cp.position;
            result.vertices.append(newVert);
        }
    }

    for (const auto& cp : endCuts) {
        if (cp.isNewVertex) {
            Vertex newVert;
            newVert.position = cp.position;
            result.vertices.append(newVert);
        }
    }

    return result;
}

MeshData KnifeTool::cutFaces(const MeshData& mesh, const QVector<CutSegment>& segments) {
    MeshData result = mesh;
    for (const auto& seg : segments) {
        result = cut(result, seg.start.position, seg.end.position);
    }
    return result;
}

QVector<QVector<int>> LoopCut::findEdgeLoops(const MeshData& mesh, int startEdge) {
    QVector<QVector<int>> loops;
    if (mesh.edges.isEmpty() || startEdge < 0 || startEdge >= mesh.edges.size()) return loops;

    QMap<QPair<int,int>, QVector<int>> vertEdges;
    for (int i = 0; i < mesh.edges.size(); ++i) {
        int a = mesh.edges[i].v1, b = mesh.edges[i].v2;
        vertEdges[QPair<int,int>(qMin(a,b), qMax(a,b))].append(i);
    }

    QMap<int, QVector<int>> vertexToEdges;
    for (int i = 0; i < mesh.edges.size(); ++i) {
        vertexToEdges[mesh.edges[i].v1].append(i);
        vertexToEdges[mesh.edges[i].v2].append(i);
    }

    QSet<int> visited;
    QVector<int> currentLoop;

    int currentEdgeIdx = startEdge;
    int currentVert = mesh.edges[startEdge].v2;

    while (!visited.contains(currentEdgeIdx)) {
        visited.insert(currentEdgeIdx);
        currentLoop.append(currentEdgeIdx);

        const QVector<int>& adjEdges = vertexToEdges[currentVert];
        int nextEdge = -1;
        for (int ei : adjEdges) {
            if (!visited.contains(ei)) {
                nextEdge = ei;
                break;
            }
        }

        if (nextEdge < 0) break;
        currentEdgeIdx = nextEdge;
        if (mesh.edges[nextEdge].v1 == currentVert)
            currentVert = mesh.edges[nextEdge].v2;
        else
            currentVert = mesh.edges[nextEdge].v1;
    }

    if (currentLoop.size() >= 3) {
        loops.append(currentLoop);
    }

    return loops;
}

QVector<QVector<int>> LoopCut::findFaceLoops(const MeshData& mesh, const QVector<int>& edgeLoop) {
    QVector<QVector<int>> faceLoops;
    if (edgeLoop.isEmpty()) return faceLoops;

    QSet<int> loopEdgeSet;
    for (int ei : edgeLoop) {
        if (ei >= 0 && ei < mesh.edges.size()) {
            int a = mesh.edges[ei].v1, b = mesh.edges[ei].v2;
            loopEdgeSet.insert(qMin(a, b) * 100000 + qMax(a, b));
        }
    }

    QSet<int> visitedFaces;
    for (int ei : edgeLoop) {
        if (ei < 0 || ei >= mesh.edges.size()) continue;
        int v1 = mesh.edges[ei].v1, v2 = mesh.edges[ei].v2;

        for (int fi = 0; fi < mesh.faces.size(); ++fi) {
            if (visitedFaces.contains(fi)) continue;
            const Face& f = mesh.faces[fi];
            bool hasV1 = false, hasV2 = false;
            for (int idx : f.indices) {
                if (idx == v1) hasV1 = true;
                if (idx == v2) hasV2 = true;
            }
            if (hasV1 && hasV2) {
                QVector<int> faceLoop;
                faceLoop.append(fi);
                faceLoops.append(faceLoop);
                visitedFaces.insert(fi);
            }
        }
    }

    return faceLoops;
}

MeshData LoopCut::cut(const MeshData& mesh, int cuts, const QVector3D& center, const QVector3D& normal) {
    MeshData result = mesh;
    if (cuts <= 0) return result;

    QVector<QVector3D> cutNormals;
    if (normal.lengthSquared() > 0.001f) {
        QVector3D n = normal.normalized();
        QVector3D u = qAbs(QVector3D::dotProduct(n, QVector3D(0, 1, 0))) < 0.99f
            ? QVector3D::crossProduct(n, QVector3D(0, 1, 0)).normalized()
            : QVector3D::crossProduct(n, QVector3D(1, 0, 0)).normalized();
        QVector3D v = QVector3D::crossProduct(n, u).normalized();

        for (int i = 0; i < cuts; ++i) {
            float t = (i + 1.0f) / (cuts + 1.0f) - 0.5f;
            cutNormals.append(n);
        }
    }

    for (int c = 0; c < cuts; ++c) {
        QVector3D cutPlaneNormal = normal.lengthSquared() > 0.001f ? normal.normalized() : QVector3D(0, 0, 1);
        QVector3D cutPlanePoint = center;

        QVector<KnifeTool::CutPoint> cutPoints = KnifeTool::intersectWithPlane(result, cutPlanePoint, cutPlaneNormal);

        for (const auto& cp : cutPoints) {
            Vertex newVert;
            newVert.position = cp.position;
            newVert.normal = QVector3D(0, 1, 0);
            result.vertices.append(newVert);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData Bisect::cut(const MeshData& mesh, const QVector3D& planePoint, const QVector3D& planeNormal, bool cutCenter, bool clearOuter, bool clearInner) {
    MeshData result;
    QVector3D normalizedNormal = planeNormal.normalized();

    for (const auto& v : mesh.vertices) {
        Vertex nv = v;
        float dist = QVector3D::dotProduct(normalizedNormal, v.position - planePoint);
        if (dist < 0 && clearInner) continue;
        if (dist > 0 && clearOuter) continue;
        result.vertices.append(nv);
    }

    for (const auto& face : mesh.faces) {
        QVector3D v1 = mesh.vertices[face[0]].position;
        float d1 = QVector3D::dotProduct(normalizedNormal, v1 - planePoint);

        QVector<int> posIndices, negIndices;
        for (int idx : face.indices) {
            float d = QVector3D::dotProduct(normalizedNormal, mesh.vertices[idx].position - planePoint);
            if (d >= 0) posIndices.append(idx);
            else negIndices.append(idx);
        }

        if (!posIndices.isEmpty() && !negIndices.isEmpty()) {
            if (cutCenter) {
                Vertex centerVert;
                centerVert.position = planePoint;
                int centerIdx = result.vertices.size();
                result.vertices.append(centerVert);

                for (int i = 0; i < posIndices.size(); ++i) {
                    result.faces.append(Face{posIndices[i], posIndices[(i + 1) % posIndices.size()], centerIdx});
                }
            }
        } else if (!posIndices.isEmpty()) {
            result.faces.append(face);
        }
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

QPair<MeshData, MeshData> Bisect::split(const MeshData& mesh, const QVector3D& planePoint, const QVector3D& planeNormal) {
    MeshData positive = cut(mesh, planePoint, planeNormal, false, true, false);
    MeshData negative = cut(mesh, planePoint, planeNormal, false, false, true);
    return std::make_pair(positive, negative);
}

QVector3D Bisect::projectToPlane(const QVector3D& point, const QVector3D& planePoint, const QVector3D& planeNormal) {
    QVector3D n = planeNormal.normalized();
    float dist = QVector3D::dotProduct(n, point - planePoint);
    return point - n * dist;
}

QVector<QVector<int>> VertexConnectivity::findConnectedVertices(const MeshData& mesh, int startVertex) {
    QVector<QVector<int>> components;
    QSet<int> visited;
    QQueue<int> queue;

    queue.enqueue(startVertex);
    visited.insert(startVertex);

    QVector<int> currentComponent;

    while (!queue.isEmpty()) {
        int current = queue.dequeue();
        currentComponent.append(current);

        for (const auto& face : mesh.faces) {
            for (int i = 0; i < face.indices.size(); ++i) {
                if (face.indices[i] == current) {
                    int next = face.indices[(i + 1) % face.indices.size()];
                    int prev = face.indices[(i - 1 + face.indices.size()) % face.indices.size()];

                    if (!visited.contains(next)) {
                        visited.insert(next);
                        queue.enqueue(next);
                    }
                    if (!visited.contains(prev)) {
                        visited.insert(prev);
                        queue.enqueue(prev);
                    }
                }
            }
        }
    }

    if (!currentComponent.isEmpty()) {
        components.append(currentComponent);
    }

    return components;
}

QVector<QVector<int>> VertexConnectivity::findConnectedEdges(const MeshData& mesh, const QVector<int>& vertices) {
    QVector<QVector<int>> edgeGroups;
    if (vertices.isEmpty() || mesh.edges.isEmpty()) return edgeGroups;

    QSet<int> vertSet(vertices.begin(), vertices.end());
    QSet<int> visitedEdges;

    for (int startVi : vertices) {
        QVector<int> group;
        QQueue<int> edgeQueue;

        for (int ei = 0; ei < mesh.edges.size(); ++ei) {
            if (visitedEdges.contains(ei)) continue;
            if (mesh.edges[ei].v1 == startVi || mesh.edges[ei].v2 == startVi) {
                edgeQueue.enqueue(ei);
                visitedEdges.insert(ei);
            }
        }

        while (!edgeQueue.isEmpty()) {
            int ei = edgeQueue.dequeue();
            group.append(ei);
            int other = (mesh.edges[ei].v1 == startVi) ? mesh.edges[ei].v2 : mesh.edges[ei].v1;

            for (int ei2 = 0; ei2 < mesh.edges.size(); ++ei2) {
                if (visitedEdges.contains(ei2)) continue;
                if (mesh.edges[ei2].v1 == other || mesh.edges[ei2].v2 == other) {
                    visitedEdges.insert(ei2);
                    edgeQueue.enqueue(ei2);
                }
            }
        }

        if (!group.isEmpty()) {
            edgeGroups.append(group);
        }
    }

    return edgeGroups;
}

QVector<QVector<int>> VertexConnectivity::findVertexRings(const MeshData& mesh, int vertexIndex) {
    QVector<QVector<int>> rings;
    QSet<int> ringSet;
    QVector<int> ring;

    for (const auto& face : mesh.faces) {
        for (int i = 0; i < face.indices.size(); ++i) {
            if (face.indices[i] == vertexIndex) {
                int next = face.indices[(i + 1) % face.indices.size()];
                if (!ringSet.contains(next)) {
                    ringSet.insert(next);
                    ring.append(next);
                }
                int prev = face.indices[(i - 1 + face.indices.size()) % face.indices.size()];
                if (!ringSet.contains(prev)) {
                    ringSet.insert(prev);
                    ring.append(prev);
                }
            }
        }
    }

    if (!ring.isEmpty()) {
        rings.append(ring);
    }

    return rings;
}

int VertexConnectivity::getVertexValence(const MeshData& mesh, int vertexIndex) {
    return findVertexRings(mesh, vertexIndex).value(0, QVector<int>()).size();
}

QVector<int> VertexConnectivity::findFeatureVertices(const MeshData& mesh, float angleThreshold) {
    QVector<int> featureVerts;

    for (int i = 0; i < mesh.vertices.size(); ++i) {
        QVector<QVector3D> normals;
        for (const auto& face : mesh.faces) {
            for (int j = 0; j < face.indices.size(); ++j) {
                if (face.indices[j] == i) {
                    QVector3D n = QVector3D::crossProduct(
                        mesh.vertices[face[(j + 1) % face.indices.size()]].position - mesh.vertices[face[j]].position,
                        mesh.vertices[face[(j + 2) % face.indices.size()]].position - mesh.vertices[face[(j + 1) % face.indices.size()]].position
                    ).normalized();
                    normals.append(n);
                }
            }
        }

        if (normals.size() >= 2) {
            float maxAngle = 0;
            for (int m = 0; m < normals.size(); ++m) {
                for (int n = m + 1; n < normals.size(); ++n) {
                    float angle = qAcos(qBound(-1.0f, QVector3D::dotProduct(normals[m], normals[n]), 1.0f));
                    maxAngle = qMax(maxAngle, angle);
                }
            }

            if (maxAngle > angleThreshold) {
                featureVerts.append(i);
            }
        }
    }

    return featureVerts;
}

MeshData Decimation::simplify(const MeshData& mesh, float targetRatio, float angleThreshold) {
    if (targetRatio >= 1.0f) return mesh;

    MeshData result = mesh;
    int targetFaces = int(mesh.faces.size() * targetRatio);

    while (result.faces.size() > targetFaces) {
        bool merged = false;

        for (int i = 0; i < result.faces.size() - 1 && result.faces.size() > targetFaces; ++i) {
            auto& face1 = result.faces[i];

            for (int j = i + 1; j < result.faces.size() && result.faces.size() > targetFaces; ++j) {
                auto& face2 = result.faces[j];

                int sharedEdge = PolygonOperations::findSharedEdge(face1, face2);
                if (sharedEdge >= 0) {
                    QVector3D center(0, 0, 0);
                    QSet<int> allIndices;
                    for (int idx : face1.indices) allIndices.insert(idx);
                    for (int idx : face2.indices) allIndices.insert(idx);

                    for (int idx : allIndices) {
                        center += result.vertices[idx].position;
                    }
                    center /= allIndices.size();

                    int centerIdx = result.vertices.size();
                    Vertex centerVert;
                    centerVert.position = center;
                    result.vertices.append(centerVert);

                    QVector<int> newIndices;
                    for (int idx : allIndices) {
                        newIndices.append(idx);
                    }
                    newIndices.append(centerIdx);

                    result.faces.removeAt(j);
                    result.faces.removeAt(i);

                    Face newFace;
                    for (int idx : newIndices) {
                        newFace.indices.append(idx);
                    }
                    result.faces.append(newFace);

                    merged = true;
                    break;
                }
            }
            if (merged) break;
        }

        if (!merged) break;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData Decimation::decimateCluster(const MeshData& mesh, float targetFaceCount) {
    return simplify(mesh, targetFaceCount / mesh.faces.size());
}

MeshData Decimation::decimateQuadric(const MeshData& mesh, float targetRatio) {
    return simplify(mesh, targetRatio);
}

Decimation::Quadric Decimation::computeQuadric(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3) {
    QVector3D v1 = p2 - p1;
    QVector3D v2 = p3 - p1;
    QVector3D normal = QVector3D::crossProduct(v1, v2).normalized();

    Quadric q = {};
    q.a = normal.x() * normal.x();
    q.b = normal.x() * normal.y();
    q.c = normal.x() * normal.z();
    q.d = normal.x() * normal.x() * p1.x() + normal.x() * normal.y() * p1.y() + normal.x() * normal.z() * p1.z();
    q.e = normal.y() * normal.y();
    q.f = normal.y() * normal.z();
    q.g = normal.y() * normal.x() * p1.x() + normal.y() * normal.y() * p1.y() + normal.y() * normal.z() * p1.z();
    q.h = normal.z() * normal.z();
    q.i = normal.z() * normal.x() * p1.x() + normal.z() * normal.y() * p1.y() + normal.z() * normal.z() * p1.z();
    q.j = 1.0f;

    return q;
}

float Decimation::quadricError(const Decimation::Quadric& q, const QVector3D& v) {
    return q.a * v.x() * v.x() + 2 * q.b * v.x() * v.y() + 2 * q.c * v.x() * v.z() +
           2 * q.d * v.x() + q.e * v.y() * v.y() + 2 * q.f * v.y() * v.z() +
           2 * q.g * v.y() + q.h * v.z() * v.z() + 2 * q.i * v.z() + q.j;
}

QVector3D Decimation::optimalVertex(const Decimation::Quadric& q1, const Decimation::Quadric& q2) {
    Quadric combined = q1;
    combined.a += q2.a; combined.b += q2.b; combined.c += q2.c; combined.d += q2.d;
    combined.e += q2.e; combined.f += q2.f; combined.g += q2.g;
    combined.h += q2.h; combined.i += q2.i; combined.j += q2.j;

    // Solve for optimal position using simple gradient descent
    QVector3D v(0, 0, 0);
    float step = 0.1f;
    for (int i = 0; i < 10; ++i) {
        QVector3D grad;
        grad.setX(2 * combined.a * v.x() + 2 * combined.b * v.y() + 2 * combined.c * v.z() + 2 * combined.d);
        grad.setY(2 * combined.b * v.x() + 2 * combined.e * v.y() + 2 * combined.f * v.z() + 2 * combined.g);
        grad.setZ(2 * combined.c * v.x() + 2 * combined.f * v.y() + 2 * combined.h * v.z() + 2 * combined.i);
        v -= grad * step;
    }
    return v;
}

void Decimation::computePairing(QVector<Decimation::MeshVertex>& vertices, const QVector<QPair<int, int>>& edges) {
    for (const auto& edge : edges) {
        if (edge.first < vertices.size() && edge.second < vertices.size()) {
            Decimation::Quadric q1 = computeQuadric(
                vertices[edge.first].position,
                vertices[(edge.first + 1) % vertices.size()].position,
                vertices[(edge.first + 2) % vertices.size()].position
            );
            Decimation::Quadric q2 = computeQuadric(
                vertices[edge.second].position,
                vertices[(edge.second + 1) % vertices.size()].position,
                vertices[(edge.second + 2) % vertices.size()].position
            );
            QVector3D optPos = optimalVertex(q1, q2);
            vertices[edge.first].representative = edge.second;
            // Set the target vertex position to the optimal position
            vertices[edge.second].position = optPos;
        }
    }
}

QVector<Remeshing::Voxel> Remeshing::voxelize(const MeshData& mesh, float resolution) {
    QVector<Voxel> voxels;
    if (mesh.vertices.isEmpty() || resolution <= 0.0f) return voxels;

    const_cast<MeshData&>(mesh).computeBoundingBox();
    QVector3D bmin = mesh.boundingBoxMin;
    QVector3D bmax = mesh.boundingBoxMax;
    QVector3D ext = bmax - bmin;
    float maxExt = qMax(ext.x(), qMax(ext.y(), ext.z()));
    if (maxExt < 0.0001f) return voxels;

    int gridRes = qMax(1, (int)qCeil(maxExt / resolution));
    float cellSize = maxExt / gridRes;

    voxels.resize(gridRes * gridRes * gridRes);
    for (int i = 0; i < voxels.size(); ++i) {
        int z = i / (gridRes * gridRes);
        int y = (i / gridRes) % gridRes;
        int x = i % gridRes;
        voxels[i].position = bmin + QVector3D((x + 0.5f) * cellSize, (y + 0.5f) * cellSize, (z + 0.5f) * cellSize);
        voxels[i].distance = 1e10f;
        voxels[i].occupied = false;
    }

    for (const auto& face : mesh.faces) {
        if (face.indices.size() < 3) continue;
        QVector<QVector3D> triVerts;
        for (int idx : face.indices) {
            if (idx < mesh.vertices.size())
                triVerts.append(mesh.vertices[idx].position);
        }
        if (triVerts.size() < 3) continue;

        QVector3D triMin = triVerts[0], triMax = triVerts[0];
        for (int i = 1; i < triVerts.size(); ++i) {
            triMin = QVector3D(qMin(triMin.x(), triVerts[i].x()), qMin(triMin.y(), triVerts[i].y()), qMin(triMin.z(), triVerts[i].z()));
            triMax = QVector3D(qMax(triMax.x(), triVerts[i].x()), qMax(triMax.y(), triVerts[i].y()), qMax(triMax.z(), triVerts[i].z()));
        }

        int x0 = qMax(0, (int)((triMin.x() - bmin.x()) / cellSize));
        int x1 = qMin(gridRes - 1, (int)((triMax.x() - bmin.x()) / cellSize));
        int y0 = qMax(0, (int)((triMin.y() - bmin.y()) / cellSize));
        int y1 = qMin(gridRes - 1, (int)((triMax.y() - bmin.y()) / cellSize));
        int z0 = qMax(0, (int)((triMin.z() - bmin.z()) / cellSize));
        int z1 = qMin(gridRes - 1, (int)((triMax.z() - bmin.z()) / cellSize));

        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    int idx = z * gridRes * gridRes + y * gridRes + x;
                    QVector3D center = voxels[idx].position;

                    QVector3D faceNormal = QVector3D::crossProduct(triVerts[1] - triVerts[0], triVerts[2] - triVerts[0]);
                    float faceArea = faceNormal.length();
                    if (faceArea < 1e-10f) continue;
                    faceNormal /= faceArea;

                    QVector3D v0p = center - triVerts[0];
                    float dist = qAbs(QVector3D::dotProduct(v0p, faceNormal));
                    if (dist < voxels[idx].distance) {
                        voxels[idx].distance = dist;
                        voxels[idx].occupied = true;
                    }
                }
            }
        }
    }

    return voxels;
}

MeshData Remeshing::extractSurface(const QVector<Voxel>& voxels, float isovalue) {
    MeshData mesh;
    if (voxels.isEmpty()) return mesh;

    int gridRes = 1;
    while (gridRes * gridRes * gridRes < voxels.size()) ++gridRes;
    if (gridRes * gridRes * gridRes != voxels.size()) return mesh;

    auto getIdx = [&](int x, int y, int z) -> int {
        return z * gridRes * gridRes + y * gridRes + x;
    };

    auto getDistance = [&](int x, int y, int z) -> float {
        if (x < 0 || x >= gridRes || y < 0 || y >= gridRes || z < 0 || z >= gridRes)
            return 1e10f;
        return voxels[getIdx(x, y, z)].distance;
    };

    auto lerp = [](const QVector3D& a, const QVector3D& b, float t) -> QVector3D {
        return a + (b - a) * t;
    };

    for (int z = 0; z < gridRes - 1; ++z) {
        for (int y = 0; y < gridRes - 1; ++y) {
            for (int x = 0; x < gridRes - 1; ++x) {
                float d[8];
                QVector3D pos[8];
                int corners[8][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};

                for (int c = 0; c < 8; ++c) {
                    int cx = x + corners[c][0];
                    int cy = y + corners[c][1];
                    int cz = z + corners[c][2];
                    d[c] = getDistance(cx, cy, cz);
                    pos[c] = voxels[getIdx(cx, cy, cz)].position;
                }

                int cubeIndex = 0;
                for (int c = 0; c < 8; ++c) {
                    if (d[c] <= isovalue) cubeIndex |= (1 << c);
                }
                if (cubeIndex == 0 || cubeIndex == 255) continue;

                QVector3D vertList[12];
                int edgeTable[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
                int triTable[256][16] = {
                    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,8,3,9,8,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,3,1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {9,2,10,0,2,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {2,8,3,2,10,8,10,9,8,-1,-1,-1,-1,-1,-1,-1},
                    {3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,11,2,8,11,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,9,0,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,11,2,1,9,11,9,8,11,-1,-1,-1,-1,-1,-1,-1},
                    {3,10,1,11,10,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,10,1,0,8,10,8,11,10,-1,-1,-1,-1,-1,-1,-1},
                    {3,9,0,3,11,9,11,10,9,-1,-1,-1,-1,-1,-1,-1},
                    {9,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,3,0,7,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,1,9,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,1,9,4,7,1,7,3,1,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,10,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {3,4,7,3,0,4,1,2,10,-1,-1,-1,-1,-1,-1,-1},
                    {9,2,10,9,0,2,8,4,7,-1,-1,-1,-1,-1,-1,-1},
                    {2,10,9,2,9,7,2,7,3,7,9,4,-1,-1,-1,-1},
                    {8,4,7,3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {11,4,7,11,2,4,2,0,4,-1,-1,-1,-1,-1,-1,-1},
                    {9,0,1,8,4,7,2,3,11,-1,-1,-1,-1,-1,-1,-1},
                    {4,7,11,9,4,11,9,11,2,9,2,1,-1,-1,-1,-1},
                    {3,10,1,3,11,10,7,8,4,-1,-1,-1,-1,-1,-1,-1},
                    {1,11,10,1,4,11,1,0,4,7,11,4,-1,-1,-1,-1},
                    {4,7,8,9,0,3,9,3,10,10,3,11,-1,-1,-1,-1},
                    {4,7,11,4,11,9,9,11,10,-1,-1,-1,-1,-1,-1,-1},
                    {9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {9,5,4,0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,5,4,1,5,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {8,5,4,8,3,5,3,1,5,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,10,9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {3,0,8,1,2,10,4,9,5,-1,-1,-1,-1,-1,-1,-1},
                    {5,2,10,5,4,2,4,0,2,-1,-1,-1,-1,-1,-1,-1},
                    {2,10,5,3,2,5,3,5,4,3,4,8,-1,-1,-1,-1},
                    {9,5,4,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,11,2,0,8,11,4,9,5,-1,-1,-1,-1,-1,-1,-1},
                    {0,5,4,0,1,5,2,3,11,-1,-1,-1,-1,-1,-1,-1},
                    {2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
                    {10,3,11,10,1,3,9,5,4,-1,-1,-1,-1,-1,-1,-1},
                    {4,9,5,0,8,1,8,10,1,8,11,10,-1,-1,-1,-1},
                    {5,4,0,5,0,11,5,11,10,11,0,3,-1,-1,-1,-1},
                    {5,4,8,5,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1},
                    {9,7,8,5,7,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {9,3,0,9,5,3,5,7,3,-1,-1,-1,-1,-1,-1,-1},
                    {0,7,8,0,1,7,1,5,7,-1,-1,-1,-1,-1,-1,-1},
                    {1,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {9,7,8,9,5,7,10,1,2,-1,-1,-1,-1,-1,-1,-1},
                    {10,1,2,9,5,0,5,3,0,5,7,3,-1,-1,-1,-1},
                    {8,0,2,8,2,5,8,5,7,10,5,2,-1,-1,-1,-1},
                    {2,10,5,2,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1},
                    {7,9,5,7,8,9,3,11,2,-1,-1,-1,-1,-1,-1,-1},
                    {9,5,7,9,7,2,9,2,0,2,7,11,-1,-1,-1,-1},
                    {2,3,11,0,1,8,1,7,8,1,5,7,-1,-1,-1,-1},
                    {11,2,1,11,1,7,7,1,5,-1,-1,-1,-1,-1,-1,-1},
                    {9,5,8,8,5,7,10,1,3,10,3,11,-1,-1,-1,-1},
                    {5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
                    {11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
                    {11,10,5,7,11,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,3,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {9,0,1,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,8,3,1,9,8,5,10,6,-1,-1,-1,-1,-1,-1,-1},
                    {1,6,5,2,6,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,6,5,1,2,6,3,0,8,-1,-1,-1,-1,-1,-1,-1},
                    {9,6,5,9,0,6,0,2,6,-1,-1,-1,-1,-1,-1,-1},
                    {5,9,8,5,8,2,5,2,6,3,2,8,-1,-1,-1,-1},
                    {2,3,11,10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {11,0,8,11,2,0,10,6,5,-1,-1,-1,-1,-1,-1,-1},
                    {0,1,9,2,3,11,5,10,6,-1,-1,-1,-1,-1,-1,-1},
                    {5,10,6,1,9,2,9,11,2,9,8,11,-1,-1,-1,-1},
                    {6,3,11,6,5,3,5,1,3,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,11,0,11,5,0,5,1,5,11,6,-1,-1,-1,-1},
                    {3,11,6,0,3,6,0,6,5,0,5,9,-1,-1,-1,-1},
                    {6,5,9,6,9,11,11,9,8,-1,-1,-1,-1,-1,-1,-1},
                    {5,10,6,4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,3,0,4,7,3,6,5,10,-1,-1,-1,-1,-1,-1,-1},
                    {1,9,0,5,10,6,8,4,7,-1,-1,-1,-1,-1,-1,-1},
                    {10,6,5,1,9,7,1,7,3,7,9,4,-1,-1,-1,-1},
                    {6,1,2,6,5,1,4,7,8,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,5,5,2,6,3,0,4,3,4,7,-1,-1,-1,-1},
                    {8,4,7,9,0,5,0,6,5,0,2,6,-1,-1,-1,-1},
                    {7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
                    {3,11,2,7,8,4,10,6,5,-1,-1,-1,-1,-1,-1,-1},
                    {5,10,6,4,7,2,4,2,0,2,7,11,-1,-1,-1,-1},
                    {0,1,9,4,7,8,2,3,11,5,10,6,-1,-1,-1,-1},
                    {9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
                    {8,4,7,3,11,5,3,5,1,5,11,6,-1,-1,-1,-1},
                    {5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
                    {0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
                    {6,5,9,6,9,11,4,7,9,7,11,9,-1,-1,-1,-1},
                    {10,4,9,6,4,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,10,6,4,9,10,0,8,3,-1,-1,-1,-1,-1,-1,-1},
                    {10,0,1,10,6,0,6,4,0,-1,-1,-1,-1,-1,-1,-1},
                    {8,3,1,8,1,6,8,6,4,6,1,10,-1,-1,-1,-1},
                    {1,4,9,1,2,4,2,6,4,-1,-1,-1,-1,-1,-1,-1},
                    {3,0,8,1,2,9,2,4,9,2,6,4,-1,-1,-1,-1},
                    {0,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {8,3,2,8,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1},
                    {10,4,9,10,6,4,11,2,3,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,2,2,8,11,4,9,10,4,10,6,-1,-1,-1,-1},
                    {3,11,2,0,1,6,0,6,4,6,1,10,-1,-1,-1,-1},
                    {6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
                    {9,6,4,9,3,6,9,1,3,11,6,3,-1,-1,-1,-1},
                    {8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
                    {3,11,6,3,6,0,0,6,4,-1,-1,-1,-1,-1,-1,-1},
                    {6,4,8,11,6,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {7,10,6,7,8,10,8,9,10,-1,-1,-1,-1,-1,-1,-1},
                    {0,7,3,0,10,7,0,9,10,6,7,10,-1,-1,-1,-1},
                    {10,6,7,1,10,7,1,7,8,1,8,0,-1,-1,-1,-1},
                    {10,6,7,10,7,1,1,7,3,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,6,1,6,8,1,8,9,8,6,7,-1,-1,-1,-1},
                    {2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
                    {7,8,0,7,0,6,6,0,2,-1,-1,-1,-1,-1,-1,-1},
                    {7,3,2,6,7,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {2,3,11,10,6,8,10,8,9,8,6,7,-1,-1,-1,-1},
                    {2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
                    {1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
                    {11,2,1,11,1,7,10,6,1,6,7,1,-1,-1,-1,-1},
                    {8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
                    {0,9,1,11,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {7,8,0,7,0,6,3,11,0,11,6,0,-1,-1,-1,-1},
                    {7,11,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {3,0,8,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,1,9,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {8,1,9,8,3,1,11,7,6,-1,-1,-1,-1,-1,-1,-1},
                    {10,1,2,6,11,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,10,3,0,8,6,11,7,-1,-1,-1,-1,-1,-1,-1},
                    {2,9,0,2,10,9,6,11,7,-1,-1,-1,-1,-1,-1,-1},
                    {6,11,7,2,10,3,10,8,3,10,9,8,-1,-1,-1,-1},
                    {7,2,3,6,2,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {7,0,8,7,6,0,6,2,0,-1,-1,-1,-1,-1,-1,-1},
                    {2,7,6,2,3,7,0,1,9,-1,-1,-1,-1,-1,-1,-1},
                    {1,6,2,1,8,6,1,9,8,8,7,6,-1,-1,-1,-1},
                    {10,7,6,10,1,7,1,3,7,-1,-1,-1,-1,-1,-1,-1},
                    {10,7,6,1,7,10,1,8,7,1,0,8,-1,-1,-1,-1},
                    {0,3,7,0,7,10,0,10,9,6,10,7,-1,-1,-1,-1},
                    {7,6,10,7,10,8,8,10,9,-1,-1,-1,-1,-1,-1,-1},
                    {6,8,4,11,8,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {3,6,11,3,0,6,0,4,6,-1,-1,-1,-1,-1,-1,-1},
                    {8,6,11,8,4,6,9,0,1,-1,-1,-1,-1,-1,-1,-1},
                    {9,4,6,9,6,3,9,3,1,11,3,6,-1,-1,-1,-1},
                    {6,8,4,6,11,8,2,10,1,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,10,3,0,11,0,6,11,0,4,6,-1,-1,-1,-1},
                    {4,11,8,4,6,11,0,2,9,2,10,9,-1,-1,-1,-1},
                    {10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
                    {8,2,3,8,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1},
                    {0,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,9,0,2,3,4,2,4,6,4,3,8,-1,-1,-1,-1},
                    {1,9,4,1,4,2,2,4,6,-1,-1,-1,-1,-1,-1,-1},
                    {8,1,3,8,6,1,8,4,6,6,10,1,-1,-1,-1,-1},
                    {10,1,0,10,0,6,6,0,4,-1,-1,-1,-1,-1,-1,-1},
                    {4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
                    {10,9,4,6,10,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,9,5,7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,3,4,9,5,11,7,6,-1,-1,-1,-1,-1,-1,-1},
                    {5,0,1,5,4,0,7,6,11,-1,-1,-1,-1,-1,-1,-1},
                    {11,7,6,8,3,4,3,5,4,3,1,5,-1,-1,-1,-1},
                    {9,5,4,10,1,2,7,6,11,-1,-1,-1,-1,-1,-1,-1},
                    {6,11,7,1,2,10,0,8,3,4,9,5,-1,-1,-1,-1},
                    {7,6,11,5,4,10,4,2,10,4,0,2,-1,-1,-1,-1},
                    {3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
                    {7,2,3,7,6,2,5,4,9,-1,-1,-1,-1,-1,-1,-1},
                    {9,5,4,0,8,6,0,6,2,6,8,7,-1,-1,-1,-1},
                    {3,6,2,3,7,6,1,5,0,5,4,0,-1,-1,-1,-1},
                    {6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
                    {9,5,4,10,1,6,1,7,6,1,3,7,-1,-1,-1,-1},
                    {1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
                    {4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
                    {7,6,10,7,10,8,5,4,10,4,8,10,-1,-1,-1,-1},
                    {6,9,5,6,11,9,11,8,9,-1,-1,-1,-1,-1,-1,-1},
                    {3,6,11,0,6,3,0,5,6,0,9,5,-1,-1,-1,-1},
                    {0,11,8,0,5,11,0,1,5,5,6,11,-1,-1,-1,-1},
                    {6,11,3,6,3,5,5,3,1,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,10,9,5,11,9,11,8,11,5,6,-1,-1,-1,-1},
                    {0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
                    {11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
                    {6,11,3,6,3,5,2,10,3,10,5,3,-1,-1,-1,-1},
                    {5,8,9,5,2,8,5,6,2,3,8,2,-1,-1,-1,-1},
                    {9,5,6,9,6,0,0,6,2,-1,-1,-1,-1,-1,-1,-1},
                    {1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
                    {1,5,6,2,1,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
                    {10,1,0,10,0,6,9,5,0,5,6,0,-1,-1,-1,-1},
                    {0,3,8,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {10,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {11,5,10,7,5,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {11,5,10,11,7,5,8,3,0,-1,-1,-1,-1,-1,-1,-1},
                    {5,11,7,5,10,11,1,9,0,-1,-1,-1,-1,-1,-1,-1},
                    {10,7,5,10,11,7,9,8,1,8,3,1,-1,-1,-1,-1},
                    {11,1,2,11,7,1,7,5,1,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,3,1,2,7,1,7,5,7,2,11,-1,-1,-1,-1},
                    {9,7,5,9,2,7,9,0,2,2,11,7,-1,-1,-1,-1},
                    {7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
                    {2,5,10,2,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1},
                    {8,2,0,8,5,2,8,7,5,10,2,5,-1,-1,-1,-1},
                    {9,0,1,5,10,3,5,3,7,3,10,2,-1,-1,-1,-1},
                    {9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
                    {1,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,7,0,7,1,1,7,5,-1,-1,-1,-1,-1,-1,-1},
                    {9,0,3,9,3,5,5,3,7,-1,-1,-1,-1,-1,-1,-1},
                    {9,8,7,5,9,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {8,5,4,8,11,5,11,10,5,-1,-1,-1,-1,-1,-1,-1},
                    {5,0,4,5,11,0,5,10,11,11,3,0,-1,-1,-1,-1},
                    {0,1,9,8,5,4,8,11,5,11,10,5,-1,-1,-1,-1},
                    {10,5,11,10,11,3,9,1,11,1,5,11,-1,-1,-1,-1},
                    {2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
                    {0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
                    {0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
                    {9,4,5,2,11,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {2,5,10,3,5,2,3,4,5,3,8,4,-1,-1,-1,-1},
                    {5,10,2,5,2,4,4,2,0,-1,-1,-1,-1,-1,-1,-1},
                    {3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
                    {5,10,2,5,2,4,1,9,2,9,4,2,-1,-1,-1,-1},
                    {8,4,5,8,5,3,3,5,1,-1,-1,-1,-1,-1,-1,-1},
                    {0,4,5,1,0,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {8,4,5,8,5,3,9,0,5,0,3,5,-1,-1,-1,-1},
                    {9,4,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,11,7,4,9,11,9,10,11,-1,-1,-1,-1,-1,-1,-1},
                    {0,8,3,4,9,7,9,11,7,9,10,11,-1,-1,-1,-1},
                    {1,10,11,1,11,4,1,4,0,7,4,11,-1,-1,-1,-1},
                    {3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
                    {4,11,7,9,11,4,9,2,11,9,1,2,-1,-1,-1,-1},
                    {9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
                    {11,7,4,11,4,2,2,4,0,-1,-1,-1,-1,-1,-1,-1},
                    {11,7,4,11,4,2,8,3,4,3,2,4,-1,-1,-1,-1},
                    {2,9,10,2,7,9,2,3,7,7,4,9,-1,-1,-1,-1},
                    {9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},
                    {3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
                    {1,10,2,8,7,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,9,1,4,1,7,7,1,3,-1,-1,-1,-1,-1,-1,-1},
                    {4,9,1,4,1,7,0,8,1,8,7,1,-1,-1,-1,-1},
                    {4,0,3,7,4,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {4,8,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {9,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {3,0,9,3,9,11,11,9,10,-1,-1,-1,-1,-1,-1,-1},
                    {0,1,10,0,10,8,8,10,11,-1,-1,-1,-1,-1,-1,-1},
                    {3,1,10,11,3,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,2,11,1,11,9,9,11,8,-1,-1,-1,-1,-1,-1,-1},
                    {3,0,9,3,9,11,1,2,9,2,11,9,-1,-1,-1,-1},
                    {0,2,11,8,0,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {3,2,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {2,3,8,2,8,10,10,8,9,-1,-1,-1,-1,-1,-1,-1},
                    {9,10,2,0,9,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {2,3,8,2,8,10,0,1,8,1,10,8,-1,-1,-1,-1},
                    {1,10,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {1,3,8,9,1,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,9,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {0,3,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
                    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}
                };

                for (int e = 0; e < 12; ++e) {
                    int i0 = edgeTable[e][0], i1 = edgeTable[e][1];
                    if ((d[i0] <= isovalue) != (d[i1] <= isovalue)) {
                        float t = (isovalue - d[i0]) / (d[i1] - d[i0]);
                        t = qBound(0.0f, t, 1.0f);
                        vertList[e] = lerp(pos[i0], pos[i1], t);
                    }
                }

                for (int i = 0; triTable[cubeIndex][i] != -1; i += 3) {
                    QVector<int> tri;
                    for (int j = 0; j < 3; ++j) {
                        int vi = triTable[cubeIndex][i + j];
                        Vertex nv;
                        nv.position = vertList[vi];
                        nv.normal = QVector3D(0, 0, 1);
                        int idx = mesh.vertices.size();
                        mesh.vertices.append(nv);
                        tri.append(idx);
                    }
                    mesh.faces.append(Face(tri));
                }
            }
        }
    }

    mesh.computeNormals();
    mesh.computeBoundingBox();
    return mesh;
}

MeshData Remeshing::quadRemesh(const MeshData& mesh, int targetCount) {
    if (mesh.faces.isEmpty()) return mesh;

    MeshData triMesh = mesh;
    triMesh.triangulate();

    float avgEdgeLen = 0.0f;
    int edgeCount = 0;
    for (const auto& face : triMesh.faces) {
        if (face.indices.size() < 3) continue;
        for (int i = 0; i < 3; ++i) {
            int a = face[i], b = face[(i + 1) % 3];
            if (a < triMesh.vertices.size() && b < triMesh.vertices.size()) {
                avgEdgeLen += (triMesh.vertices[b].position - triMesh.vertices[a].position).length();
                edgeCount++;
            }
        }
    }
    if (edgeCount > 0) avgEdgeLen /= edgeCount;

    float voxelSize = avgEdgeLen * 2.0f;
    QVector<Voxel> voxels = voxelize(triMesh, voxelSize);
    MeshData voxelMesh = extractSurface(voxels, voxelSize * 0.5f);

    if (voxelMesh.faces.size() > targetCount && targetCount > 0) {
        float ratio = (float)targetCount / voxelMesh.faces.size();
        voxelMesh = Decimation::simplify(voxelMesh, ratio);
    }

    voxelMesh.computeNormals();
    voxelMesh.computeBoundingBox();
    return voxelMesh;
}

MeshData Remeshing::triRemesh(const MeshData& mesh) {
    if (mesh.faces.isEmpty()) return mesh;

    MeshData result = mesh;
    result.triangulate();

    float avgEdgeLen = 0.0f;
    int edgeCount = 0;
    for (const auto& face : result.faces) {
        if (face.indices.size() < 3) continue;
        for (int i = 0; i < 3; ++i) {
            int a = face[i], b = face[(i + 1) % 3];
            if (a < result.vertices.size() && b < result.vertices.size()) {
                avgEdgeLen += (result.vertices[b].position - result.vertices[a].position).length();
                edgeCount++;
            }
        }
    }
    if (edgeCount > 0) avgEdgeLen /= edgeCount;

    float voxelSize = avgEdgeLen * 1.5f;
    QVector<Voxel> voxels = voxelize(result, voxelSize);
    result = extractSurface(voxels, voxelSize * 0.5f);

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData Remeshing::pentagonalRemesh(const MeshData& mesh) {
    if (mesh.faces.isEmpty()) return mesh;

    MeshData result = triRemesh(mesh);

    QVector<Face> pentFaces;
    for (int i = 0; i + 2 < result.faces.size(); i += 3) {
        if (i + 2 < result.faces.size()) {
            Face pent;
            QSet<int> verts;
            for (int j = 0; j < 3 && (i + j) < result.faces.size(); ++j) {
                for (int idx : result.faces[i + j].indices) {
                    verts.insert(idx);
                }
            }
            for (int v : verts) {
                pent.indices.append(v);
            }
            if (pent.indices.size() >= 3) {
                pentFaces.append(pent);
            }
        }
    }

    if (!pentFaces.isEmpty()) {
        result.faces = pentFaces;
    }

    result.computeNormals();
    result.computeBoundingBox();
    return result;
}

MeshData Remeshing::isoSurface(const MeshData& mesh, float isovalue) {
    QVector<Voxel> voxels = voxelize(mesh, 0.1f);
    return extractSurface(voxels, isovalue);
}

void Remeshing::refineVertices(MeshData& mesh, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        MeshData temp = mesh;
        for (int i = 0; i < mesh.vertices.size(); ++i) {
            QVector3D avg(0, 0, 0);
            int count = 0;
            for (const auto& edge : mesh.edges) {
                if (edge.v1 == i) { avg += mesh.vertices[edge.v2].position; count++; }
                else if (edge.v2 == i) { avg += mesh.vertices[edge.v1].position; count++; }
            }
            if (count > 0) {
                avg /= count;
                temp.vertices[i].position += (avg - mesh.vertices[i].position) * 0.3f;
            }
        }
        mesh = temp;
    }
}

}