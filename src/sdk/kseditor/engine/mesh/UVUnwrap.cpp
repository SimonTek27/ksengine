#include "UVUnwrap.h"

#include <algorithm>
#include <QQueue>
#include <cfloat>

namespace ks {

QVector3D UVMapper::computeFaceNormal(const QVector<QVector3D>& v, int i0, int i1, int i2) {
    QVector3D e1 = v[i1] - v[i0];
    QVector3D e2 = v[i2] - v[i0];
    QVector3D n = QVector3D::crossProduct(e1, e2);
    if (n.length() > 0.0001f) n.normalize();
    return n;
}

float UVMapper::computeFaceArea(const QVector<QVector3D>& v, int i0, int i1, int i2) {
    QVector3D e1 = v[i1] - v[i0];
    QVector3D e2 = v[i2] - v[i0];
    return QVector3D::crossProduct(e1, e2).length() * 0.5f;
}

QVector<QVector2D> UVMapper::planarProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                           const QVector3D& direction) {
    QVector<QVector2D> uvs;
    uvs.resize(vertices.size());

    QVector3D n = direction.normalized();
    float nx = qAbs(n.x()), ny = qAbs(n.y()), nz = qAbs(n.z());

    for (int i = 0; i < vertices.size(); ++i) {
        const QVector3D& v = vertices[i];
        if (nx > ny && nx > nz) {
            uvs[i] = QVector2D(v.y(), v.z());
        } else if (ny > nz) {
            uvs[i] = QVector2D(v.x(), v.z());
        } else {
            uvs[i] = QVector2D(v.x(), v.y());
        }
    }

    return uvs;
}

QVector<QVector2D> UVMapper::sphericalProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces) {
    QVector<QVector2D> uvs;
    uvs.resize(vertices.size());

    QVector3D center(0, 0, 0);
    for (const auto& v : vertices) center += v;
    if (!vertices.isEmpty()) center /= vertices.size();

    float radius = 0;
    for (const auto& v : vertices) {
        radius = qMax(radius, (v - center).length());
    }

    for (int i = 0; i < vertices.size(); ++i) {
        QVector3D v = (vertices[i] - center) / radius;
        float u = 0.5f + qAtan2(v.z(), v.x()) / (2.0f * M_PI);
        float v_coord = 0.5f - qAsin(qBound(-1.0f, v.y(), 1.0f)) / M_PI;
        uvs[i] = QVector2D(u, v_coord);
    }

    return uvs;
}

QVector<QVector2D> UVMapper::cylindricalProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces) {
    QVector<QVector2D> uvs;
    uvs.resize(vertices.size());

    float minY = vertices.isEmpty() ? 0 : vertices[0].y();
    float maxY = minY;
    for (const auto& v : vertices) {
        minY = qMin(minY, v.y());
        maxY = qMax(maxY, v.y());
    }
    float range = qMax(0.001f, maxY - minY);

    QVector3D center(0, 0, 0);
    for (const auto& v : vertices) center += v;
    if (!vertices.isEmpty()) center /= vertices.size();

    for (int i = 0; i < vertices.size(); ++i) {
        QVector3D v = vertices[i] - center;
        float u = 0.5f + qAtan2(v.z(), v.x()) / (2.0f * M_PI);
        float v_coord = (vertices[i].y() - minY) / range;
        uvs[i] = QVector2D(u, v_coord);
    }

    return uvs;
}

QVector<QVector2D> UVMapper::cubeProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces) {
    QVector<QVector2D> uvs;
    uvs.resize(vertices.size());

    QVector3D bbMin, bbMax;
    if (!vertices.isEmpty()) {
        bbMin = bbMax = vertices[0];
        for (const auto& v : vertices) {
            bbMin = QVector3D(qMin(bbMin.x(), v.x()), qMin(bbMin.y(), v.y()), qMin(bbMin.z(), v.z()));
            bbMax = QVector3D(qMax(bbMax.x(), v.x()), qMax(bbMax.y(), v.y()), qMax(bbMax.z(), v.z()));
        }
    }

    QVector3D size = bbMax - bbMin;
    int primaryAxis = 0;
    if (size.y() > size.x() && size.y() > size.z()) primaryAxis = 1;
    else if (size.z() > size.x() && size.z() > size.y()) primaryAxis = 2;

    for (int i = 0; i < vertices.size(); ++i) {
        const QVector3D& v = vertices[i];
        QVector3D p = (v - bbMin) / size;

        float u, v_coord;
        switch (primaryAxis) {
            case 0: u = p.y(); v_coord = p.z(); break;
            case 1: u = p.x(); v_coord = p.z(); break;
            default: u = p.x(); v_coord = p.y(); break;
        }
        uvs[i] = QVector2D(u, v_coord);
    }

    return uvs;
}

QVector<QVector2D> UVMapper::smartProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                          const UVUnwrapConfig& config) {
    QSet<QPair<int, int>> seams = config.seams;
    if (seams.isEmpty()) {
        auto seamList = UVIslandDetector::findSeamsFromAngle(vertices, faces, qDegreesToRadians(60.0f));
        seams = QSet<QPair<int, int>>(seamList.begin(), seamList.end());
    }

    QVector<QVector2D> uvs;
    if (LSCMUnwrapper::unwrap(vertices, faces, seams, uvs)) {
        return uvs;
    }

    return planarProject(vertices, faces, QVector3D(0, 0, 1));
}

QVector<QVector2D> UVMapper::followActiveProject(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                                 const QVector2D& activeUV) {
    QVector<QVector2D> uvs;
    return uvs;
}

void UVMapper::correctAspect(QVector<QVector2D>& uvs, const QVector<QVector3D>& vertices,
                            const QVector<QVector<int>>& faces, bool useScale) {
    if (uvs.isEmpty() || vertices.isEmpty() || faces.isEmpty()) return;
    
    // Calculate bounding box of UVs
    QVector2D minUV(uvs[0]), maxUV(uvs[0]);
    for (const auto& uv : uvs) {
        minUV.setX(qMin(minUV.x(), uv.x()));
        minUV.setY(qMin(minUV.y(), uv.y()));
        maxUV.setX(qMax(maxUV.x(), uv.x()));
        maxUV.setY(qMax(maxUV.y(), uv.y()));
    }
    
    QVector2D size = maxUV - minUV;
    if (size.x() > 0.0001f && size.y() > 0.0001f) {
        float aspectRatio = size.x() / size.y();
        float targetAspectRatio = useScale ? 16.0f / 9.0f : 1.0f; // Default to square if not using scale
        
        if (qAbs(aspectRatio - targetAspectRatio) > 0.01f) {
            // Adjust UVs to match target aspect ratio
            if (aspectRatio > targetAspectRatio) {
                // UV is too wide, adjust height
                float newHeight = size.x() / targetAspectRatio;
                float heightDiff = newHeight - size.y();
                float offsetY = heightDiff * 0.5f;
                
                for (auto& uv : uvs) {
                    uv.setY(uv.y() - offsetY);
                }
            } else {
                // UV is too tall, adjust width
                float newWidth = size.y() * targetAspectRatio;
                float widthDiff = newWidth - size.x();
                float offsetX = widthDiff * 0.5f;
                
                for (auto& uv : uvs) {
                    uv.setX(uv.x() - offsetX);
                }
            }
        }
    }
}

void UVMapper::scaleToFit(QVector<QVector2D>& uvs, float margin) {
    if (uvs.isEmpty()) return;

    QVector2D minUV = uvs[0], maxUV = uvs[0];
    for (const auto& uv : uvs) {
        minUV.setX(qMin(minUV.x(), uv.x()));
        minUV.setY(qMin(minUV.y(), uv.y()));
        maxUV.setX(qMax(maxUV.x(), uv.x()));
        maxUV.setY(qMax(maxUV.y(), uv.y()));
    }

    QVector2D size = maxUV - minUV;
    if (size.x() > 0.0001f && size.y() > 0.0001f) {
        float scale = (1.0f - 2.0f * margin) / qMax(size.x(), size.y());
        QVector2D center = (minUV + maxUV) * 0.5f;
        QVector2D centerUV = QVector2D(0.5f, 0.5f);

        for (auto& uv : uvs) {
            uv = centerUV + (uv - center) * scale;
        }
    }
}

void UVMapper::centerUVs(QVector<QVector2D>& uvs) {
    if (uvs.isEmpty()) return;

    QVector2D center(0, 0);
    for (const auto& uv : uvs) center += uv;
    center /= uvs.size();

    for (auto& uv : uvs) {
        uv = uv - center + QVector2D(0.5f, 0.5f);
    }
}

void UVMapper::alignIslands(QVector<UVIsland>& islands) {
    float offsetX = 0;
    for (auto& island : islands) {
        QVector2D shift = -island.center + QVector2D(offsetX, 0);
        island.center += shift;
        offsetX += island.boundingBox.x() + 0.01f;
    }
}

QVector3D ConformalUnwrapper::computeFaceNormal(const QVector<QVector3D>& v, const QVector<int>& face, int idx) {
    int i0 = face[idx];
    int i1 = face[(idx + 1) % face.size()];
    int i2 = face[(idx + 2) % face.size()];
    QVector3D e1 = v[i1] - v[i0];
    QVector3D e2 = v[i2] - v[i0];
    return QVector3D::crossProduct(e1, e2);
}

void ConformalUnwrapper::computeCotanWeights(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                             QMap<QPair<int, int>, float>& weights) {
    for (const auto& face : faces) {
        for (int i = 0; i < face.size(); ++i) {
            int v0 = face[i];
            int v1 = face[(i + 1) % face.size()];
            int v2 = face[(i + 2) % face.size()];

            QVector3D e0 = vertices[v2] - vertices[v1];
            QVector3D e1 = vertices[v0] - vertices[v2];
            QVector3D e2 = vertices[v1] - vertices[v0];

            float cotanA = QVector3D::dotProduct(e1, e2) / QVector3D::crossProduct(e1, e2).length();
            float cotanB = QVector3D::dotProduct(e0, e2) / QVector3D::crossProduct(e0, e2).length();

            QPair<int, int> edge1 = std::make_pair(qMin(v0, v1), qMax(v0, v1));
            QPair<int, int> edge2 = std::make_pair(qMin(v1, v2), qMax(v1, v2));

            weights[edge1] = weights.value(edge1, 0.0f) + cotanA * 0.5f;
            weights[edge2] = weights.value(edge2, 0.0f) + cotanB * 0.5f;
        }
    }
}

bool ConformalUnwrapper::solveHarmonicMap(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                          const QMap<QPair<int, int>, float>& weights, QVector<QVector2D>& uvs) {
    uvs.resize(vertices.size());

    for (int i = 0; i < vertices.size(); ++i) {
        uvs[i] = QVector2D(0.5f, 0.5f);
    }

    for (int iter = 0; iter < 50; ++iter) {
        QVector<QVector2D> newUVs = uvs;

        for (int i = 0; i < vertices.size(); ++i) {
            QVector2D sum(0, 0);
            float weightSum = 0;

            for (const auto& face : faces) {
                for (int j = 0; j < face.size(); ++j) {
                    if (face[j] == i) {
                        int prev = face[(j - 1 + face.size()) % face.size()];
                        int next = face[(j + 1) % face.size()];

                        QPair<int, int> e1 = std::make_pair(qMin(i, prev), qMax(i, prev));
                        QPair<int, int> e2 = std::make_pair(qMin(i, next), qMax(i, next));

                        float w1 = weights.value(e1, 1.0f);
                        float w2 = weights.value(e2, 1.0f);

                        sum += uvs[prev] * w1 + uvs[next] * w2;
                        weightSum += w1 + w2;
                    }
                }
            }

            if (weightSum > 0.0001f) {
                newUVs[i] = sum / weightSum;
            }
        }

        float maxDiff = 0;
        for (int i = 0; i < vertices.size(); ++i) {
            QVector2D diff = newUVs[i] - uvs[i];
            maxDiff = qMax(maxDiff, qMax(qAbs(diff.x()), qAbs(diff.y())));
            uvs[i] = newUVs[i];
        }

        if (maxDiff < 0.0001f) break;
    }

    UVMapper::scaleToFit(uvs, 0.01f);

    return true;
}

void ConformalUnwrapper::applyAreaPreservation(QVector<QVector2D>& uvs, const QVector<QVector3D>& vertices,
                                                const QVector<QVector<int>>& faces) {
    if (uvs.isEmpty() || vertices.isEmpty() || faces.isEmpty()) return;
    
    // Apply area preservation to prevent UV distortion
    // This is a simplified implementation - in production, you'd use more sophisticated techniques
    for (int i = 0; i < uvs.size(); ++i) {
        // Calculate average edge length in 3D for this vertex
        float avgEdgeLength3D = 0.0f;
        int edgeCount3D = 0;
        
        // Calculate average edge length in UV space for this vertex
        float avgEdgeLengthUV = 0.0f;
        int edgeCountUV = 0;
        
        // Find all faces that use this vertex
        for (const auto& face : faces) {
            for (int j = 0; j < face.size(); ++j) {
                if (face[j] == i) {
                    int prev = face[(j - 1 + face.size()) % face.size()];
                    int next = face[(j + 1) % face.size()];
                    
                    // 3D edges
                    avgEdgeLength3D += (vertices[i] - vertices[prev]).length();
                    avgEdgeLength3D += (vertices[i] - vertices[next]).length();
                    edgeCount3D += 2;
                    
                    // UV edges
                    avgEdgeLengthUV += (uvs[i] - uvs[prev]).length();
                    avgEdgeLengthUV += (uvs[i] - uvs[next]).length();
                    edgeCountUV += 2;
                }
            }
        }
        
        if (edgeCount3D > 0 && edgeCountUV > 0) {
            avgEdgeLength3D /= edgeCount3D;
            avgEdgeLengthUV /= edgeCountUV;
            
            // If UV edges are significantly different from 3D edges, adjust
            if (avgEdgeLengthUV > 0.0001f) {
                float scaleFactor = avgEdgeLength3D / avgEdgeLengthUV;
                // Only apply correction if significantly different
                if (qAbs(scaleFactor - 1.0f) > 0.1f) {
                    // Move UV towards center based on scale difference
                    QVector2D uvCenter(0.5f, 0.5f); // Assuming UVs are in [0,1] range
                    uvs[i] = uvCenter + (uvs[i] - uvCenter) * scaleFactor;
                }
            }
        }
    }
}

bool ConformalUnwrapper::unwrap(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                               QVector<QVector2D>& uvs, const UVUnwrapConfig& config) {
    QMap<QPair<int, int>, float> weights;
    computeCotanWeights(vertices, faces, weights);
    return solveHarmonicMap(vertices, faces, weights, uvs);
}

bool LSCMUnwrapper::unwrap(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                          const QSet<QPair<int, int>>& seams, QVector<QVector2D>& uvs) {
    if (vertices.isEmpty() || faces.isEmpty()) return false;

    uvs.resize(vertices.size());

#if HAS_EIGEN
    const int n = vertices.size();
    const int numFree = n - 2;

    using SparseMat = Eigen::SparseMatrix<double>;
    using Triplet = Eigen::Triplet<double>;

    SparseMat A(n, n);
    QVector<Triplet> coefficients;
    Eigen::VectorXd bu(n), bv(n);

    bu.setZero();
    bv.setZero();

    // Build Laplacian matrix with cotangent weights
    for (const auto& face : faces) {
        if (face.size() < 3) continue;
        for (int i = 0; i < face.size(); ++i) {
            int v1 = face[i];
            int v2 = face[(i + 1) % face.size()];

            QPair<int, int> edge = std::make_pair(qMin(v1, v2), qMax(v1, v2));
            if (seams.contains(edge)) continue;

            int v0 = face[(i - 1 + face.size()) % face.size()];

            QVector3D p0 = vertices[v0], p1 = vertices[v1], p2 = vertices[v2];

            double cotan = 0.0;
            QVector3D v01 = p0 - p1, v21 = p2 - p1;
            double dot = QVector3D::dotProduct(v01, v21);
            double cross = QVector3D::crossProduct(v01, v21).length();
            if (cross > 1e-8) cotan = dot / cross;

            coefficients.append(Triplet(v1, v1, cotan));
            coefficients.append(Triplet(v2, v2, cotan));
            coefficients.append(Triplet(v1, v2, -cotan));
            coefficients.append(Triplet(v2, v1, -cotan));
        }
    }

    // Pin two vertices to fix the solution (prevent translation/rotation)
    int pin1 = 0, pin2 = qMin(1, n - 1);
    for (int i = 0; i < n && pin2 == pin1; ++i) {
        if (i != pin1) pin2 = i;
    }

    coefficients.append(Triplet(pin1, pin1, 1.0));
    coefficients.append(Triplet(pin2, pin2, 1.0));
    for (int j = 0; j < n; ++j) {
        if (j != pin1) coefficients.append(Triplet(pin1, j, 0.0));
        if (j != pin2) coefficients.append(Triplet(pin2, j, 0.0));
    }

    A.setFromTriplets(coefficients.begin(), coefficients.end());

    // Set boundary conditions: pin UVs to (0,0) and (1,0)
    bu(pin1) = 0.0; bv(pin1) = 0.0;
    bu(pin2) = 1.0; bv(pin2) = 0.0;

    // Solve using sparse Cholesky
    Eigen::SimplicialLDLT<SparseMat> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        return ConformalUnwrapper::unwrap(vertices, faces, uvs);
    }

    Eigen::VectorXd xu = solver.solve(bu);
    Eigen::VectorXd xv = solver.solve(bv);
    if (solver.info() != Eigen::Success) {
        return ConformalUnwrapper::unwrap(vertices, faces, uvs);
    }

    for (int i = 0; i < n; ++i) {
        uvs[i] = QVector2D(static_cast<float>(xu(i)), static_cast<float>(xv(i)));
    }

    normalizeUVs(uvs);
    return true;
#else
    // Fallback: hand-rolled Gauss-Seidel LSCM
    qsizetype n = vertices.size();
    int numFree = static_cast<int>(n) - 2;
    SparseMatrix L;
    QVector<float> b;
    buildMatrix(vertices, faces, seams, L, b);
    if (!solveLSCM(L, b, uvs, numFree)) {
        return ConformalUnwrapper::unwrap(vertices, faces, uvs);
    }
    normalizeUVs(uvs);
    return true;
#endif
}

#if HAS_EIGEN
void LSCMUnwrapper::normalizeUVs(QVector<QVector2D>& uvs) {
    UVMapper::scaleToFit(uvs, 0.01f);
}
#else
void LSCMUnwrapper::normalizeUVs(QVector<QVector2D>& uvs) {
    UVMapper::scaleToFit(uvs, 0.01f);
}

void LSCMUnwrapper::buildMatrix(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                               const QSet<QPair<int, int>>& seams, SparseMatrix& L, QVector<float>& b) {
    if (vertices.isEmpty() || faces.isEmpty()) return;
    
    // Build Laplacian matrix for LSCM
    // This is a simplified implementation
    int n = vertices.size();
    L = SparseMatrix(n, n);
    b.resize(n * 2); // For x and y coordinates
    
    // Initialize to zero
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            L.set(i, j, 0.0f);
        }
        b[2*i] = 0.0f;
        b[2*i + 1] = 0.0f;
    }
    
    // Add Laplacian terms
    for (const auto& face : faces) {
        if (face.size() < 3) continue;
        
        // For each edge in the face
        for (int i = 0; i < face.size(); ++i) {
            int v1 = face[i];
            int v2 = face[(i + 1) % face.size()];
            
            // Check if edge is not a seam
            QPair<int, int> edge1 = std::make_pair(qMin(v1, v2), qMax(v1, v2));
            QPair<int, int> edge2 = std::make_pair(qMin(v2, v1), qMax(v2, v1));
            if (seams.contains(edge1) || seams.contains(edge2)) {
                continue;
            }
            
            // Compute cotan weight
            int v0 = face[(i - 1 + face.size()) % face.size()];
            int v3 = face[(i + 2) % face.size()];
            
            QVector3D p0 = vertices[v0];
            QVector3D p1 = vertices[v1];
            QVector3D p2 = vertices[v2];
            QVector3D p3 = vertices[v3];
            
            float cotan = 0.0f;
            // Simplified cotan calculation
            if ((p0 - p1).length() > 0.0001f && (p2 - p1).length() > 0.0001f) {
                QVector3D v01 = p0 - p1;
                QVector3D v21 = p2 - p1;
                float dot = QVector3D::dotProduct(v01, v21);
                float cross = QVector3D::crossProduct(v01, v21).length();
                if (cross > 0.0001f) {
                    cotan = dot / cross;
                }
            }
            
            // Add to matrix
            L.add(v1, v1, cotan);
            L.add(v2, v2, cotan);
            L.add(v1, v2, -cotan);
            L.add(v2, v1, -cotan);
        }
    }
    
    // Set boundary conditions (seams)
    for (const auto& seam : seams) {
        int v1 = seam.first;
        int v2 = seam.second;
        if (v1 < n && v2 < n) {
            // Fix these vertices
            L.set(v1, v1, 1.0f);
            L.set(v2, v2, 1.0f);
            L.set(v1, v2, 0.0f);
            L.set(v2, v1, 0.0f);

        }
    }
}

bool LSCMUnwrapper::solveLSCM(SparseMatrix& L, QVector<float>& b, QVector<QVector2D>& uvs, int numFree) {
    if (uvs.isEmpty() || L.rows == 0 || b.isEmpty()) return false;
    
    // Solve the linear system L * x = b for UV coordinates
    // This is a simplified implementation using Gauss-Seidel iteration
    int n = uvs.size();
    if (n == 0) return false;
    
    // Initial guess
    for (int i = 0; i < n; ++i) {
        uvs[i] = QVector2D(0, 0);
    }
    
    // Gauss-Seidel iteration
    const int maxIterations = 100;
    const float tolerance = 1e-6f;
    
    for (int iter = 0; iter < maxIterations; ++iter) {
        float maxError = 0.0f;
        
        for (int i = 0; i < n; ++i) {
            if (i >= numFree) continue; // Skip fixed vertices
            
            float sum = b[2*i];
            float sumY = b[2*i + 1];
            float diag = L.get(i, i);
            
            if (qAbs(diag) > 0.0001f) {
                for (int j = 0; j < n; ++j) {
                    if (i != j) {
                        sum -= L.get(i, j) * uvs[j].x();
                        sumY -= L.get(i, j) * uvs[j].y();
                    }
                }
                
                float newX = sum / diag;
                float newY = sumY / diag;
                
                float errorX = qAbs(newX - uvs[i].x());
                float errorY = qAbs(newY - uvs[i].y());
                maxError = qMax(maxError, qMax(errorX, errorY));
                
                uvs[i].setX(newX);
                uvs[i].setY(newY);
            }
        }
        
        if (maxError < tolerance) {
            break; // Converged
        }
    }
    
    return true;
}
#endif

QVector2D UVPacker::calculateBounds(const QVector<QVector2D>& uvs) {
    if (uvs.isEmpty()) return QVector2D(0, 0);

    QVector2D minB = uvs[0], maxB = uvs[0];
    for (const auto& uv : uvs) {
        minB.setX(qMin(minB.x(), uv.x()));
        minB.setY(qMin(minB.y(), uv.y()));
        maxB.setX(qMax(maxB.x(), uv.x()));
        maxB.setY(qMax(maxB.y(), uv.y()));
    }

    return maxB - minB;
}

float UVPacker::calculateArea(const QVector<QVector2D>& uvs, const QVector<int>& indices) {
    float area = 0;
    for (int i = 0; i < indices.size(); ++i) {
        int next = (i + 1) % indices.size();
        QVector2D v1 = uvs[indices[i]];
        QVector2D v2 = uvs[indices[next]];
        area += v1.x() * v2.y() - v2.x() * v1.y();
    }
    return qAbs(area) * 0.5f;
}

void UVPacker::rotateIsland(UVIsland& island, float angle) {
    float c = qCos(angle);
    float s = qSin(angle);
    QVector2D p = island.center;
    float x = p.x() * c - p.y() * s;
    float y = p.x() * s + p.y() * c;
    island.center = QVector2D(x, y);
    island.rotation += angle;
}

void UVPacker::fitToBounds(UVIsland& island, const QVector2D& bounds) {
    island.boundingBox = bounds;
}

bool UVPacker::wouldFit(const UVIsland& island, const QVector2D& position, const QVector2D& canvasSize) {
    // Check if island bounding box at position stays within canvas
    float right = position.x() + island.boundingBox.x();
    float bottom = position.y() + island.boundingBox.y();
    return right <= canvasSize.x() && bottom <= canvasSize.y();
}

QVector2D UVPacker::findBestPosition(const UVIsland& island, const QVector<UVIsland>& packed, const QVector2D& canvasSize) {
    // Simple shelf-packing: try positions along x, then y
    float bestX = 0, bestY = 0;
    float rowHeight = 0;
    
    for (const auto& p : packed) {
        float px = p.center.x() + p.boundingBox.x();
        float py = p.center.y() + p.boundingBox.y();
        if (px > bestX) bestX = px;
        if (py > bestY) bestY = py;
    }
    
    // Try to fit on current row or start new one
    if (bestX + island.boundingBox.x() <= canvasSize.x()) {
        return QVector2D(bestX, 0);
    }
    
    return QVector2D(0, bestY);
}

QVector<UVIsland> UVPacker::packIslands(const QVector<UVIsland>& islands, const PackConfig& config) {
    QVector<UVIsland> packed = islands;

    float currentX = 0, currentY = 0;
    float rowHeight = 0;

    for (auto& island : packed) {
        QVector2D bounds = island.boundingBox;

        if (currentX + bounds.x() > 1.0f) {
            currentX = 0;
            currentY += rowHeight + config.padding;
            rowHeight = 0;
        }

        island.center = QVector2D(currentX + bounds.x() * 0.5f, currentY + bounds.y() * 0.5f);
        currentX += bounds.x() + config.padding;
        rowHeight = qMax(rowHeight, bounds.y());
    }

    return packed;
}

QVector<QPair<int, int>> UVIslandDetector::findSeamsFromAngle(const QVector<QVector3D>& vertices,
                                                              const QVector<QVector<int>>& faces, float angleThreshold) {
    QVector<QPair<int, int>> seams;

    for (int i = 0; i < vertices.size(); ++i) {
        QMap<int, float> angles;

        for (const auto& face : faces) {
            for (int j = 0; j < face.size(); ++j) {
                if (face[j] == i) {
                    int prev = face[(j - 1 + face.size()) % face.size()];
                    int next = face[(j + 1) % face.size()];

                    QVector3D e1 = (vertices[i] - vertices[prev]).normalized();
                    QVector3D e2 = (vertices[next] - vertices[i]).normalized();
                    float angle = qAcos(qBound(-1.0f, QVector3D::dotProduct(e1, e2), 1.0f));

                    if (!angles.contains(prev)) angles[prev] = 0;
                    if (!angles.contains(next)) angles[next] = 0;
                    angles[prev] += angle;
                    angles[next] += angle;
                }
            }
        }

        for (auto it = angles.constBegin(); it != angles.constEnd(); ++it) {
            if (it.value() < angleThreshold) {
                int v1 = qMin(i, it.key());
                int v2 = qMax(i, it.key());
                seams.append(std::make_pair(v1, v2));
            }
        }
    }

    return seams;
}

QVector<QPair<int, int>> UVIslandDetector::findSeamsFromUV(const QVector<QVector2D>& uvs, float threshold) {
    QVector<QPair<int, int>> seams;
    return seams;
}

bool UVIslandDetector::isEdgeSeam(const QVector<int>& f1, const QVector<int>& f2,
                                  const QSet<QPair<int, int>>& seamEdges) {
    for (int i = 0; i < f1.size(); ++i) {
        int v1 = f1[i];
        int v2 = f1[(i + 1) % f1.size()];
        QPair<int, int> edge = std::make_pair(qMin(v1, v2), qMax(v1, v2));
        if (seamEdges.contains(edge)) return true;
    }
    return false;
}

bool UVIslandDetector::areFacesConnected(const QVector<int>& f1, const QVector<int>& f2,
                                         const QSet<QPair<int, int>>& seamEdges) {
    if (isEdgeSeam(f1, f2, seamEdges)) return false;

    int shared = 0;
    for (int v1 : f1) {
        for (int v2 : f2) {
            if (v1 == v2) shared++;
        }
    }
    return shared >= 2;
}

QVector<QVector<int>> UVIslandDetector::findConnectedFaces(const QVector<QVector<int>>& faces,
                                                            const QSet<QPair<int, int>>& seamEdges) {
    QVector<QVector<int>> faceIslands;
    QVector<bool> visited(faces.size(), false);

    for (int i = 0; i < faces.size(); ++i) {
        if (visited[i]) continue;

        QVector<int> island;
        QQueue<int> queue;
        queue.enqueue(i);
        visited[i] = true;

        while (!queue.isEmpty()) {
            int current = queue.dequeue();
            island.append(current);

            for (int j = 0; j < faces.size(); ++j) {
                if (!visited[j] && areFacesConnected(faces[current], faces[j], seamEdges)) {
                    visited[j] = true;
                    queue.enqueue(j);
                }
            }
        }

        faceIslands.append(island);
    }

    return faceIslands;
}

QVector<UVIsland> UVIslandDetector::findIslands(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                                const QSet<QPair<int, int>>& seams) {
    QVector<UVIsland> islands;

    QVector<QVector<int>> connectedFaces = findConnectedFaces(faces, seams);

    for (int i = 0; i < connectedFaces.size(); ++i) {
        const auto& faceIndices = connectedFaces[i];
        if (faceIndices.isEmpty()) continue;

        UVIsland island;
        island.id = i;
        island.faceIndices = faceIndices;

        QSet<int> vertexSet;
        for (int fi : faceIndices) {
            for (int vi : faces[fi]) {
                vertexSet.insert(vi);
            }
        }
        island.vertexIndices = QVector<int>(vertexSet.begin(), vertexSet.end());

        islands.append(island);
    }

    return islands;
}

int UVIslandDetector::findIslandId(const QVector<QVector<int>>& faceIslands, int faceIndex) {
    for (int i = 0; i < faceIslands.size(); ++i) {
        if (faceIslands[i].contains(faceIndex)) return i;
    }
    return -1;
}

void UVTransform::translate(QVector<QVector2D>& uvs, const QVector2D& offset) {
    for (auto& uv : uvs) {
        uv += offset;
    }
}

void UVTransform::scale(QVector<QVector2D>& uvs, const QVector2D& center, float scale) {
    for (auto& uv : uvs) {
        uv = center + (uv - center) * scale;
    }
}

void UVTransform::rotate(QVector<QVector2D>& uvs, const QVector2D& center, float angle) {
    float c = qCos(angle);
    float s = qSin(angle);

    for (auto& uv : uvs) {
        QVector2D p = uv - center;
        float x = p.x() * c - p.y() * s;
        float y = p.x() * s + p.y() * c;
        uv = center + QVector2D(x, y);
    }
}

void UVTransform::flip(QVector<QVector2D>& uvs, bool flipHorizontal, bool flipVertical) {
    for (auto& uv : uvs) {
        if (flipHorizontal) uv.setX(1.0f - uv.x());
        if (flipVertical) uv.setY(1.0f - uv.y());
    }
}

void UVTransform::weld(QVector<QVector2D>& uvs, float threshold) {
    for (int i = 0; i < uvs.size(); ++i) {
        for (int j = i + 1; j < uvs.size(); ++j) {
            if ((uvs[i] - uvs[j]).length() < threshold) {
                uvs[j] = uvs[i];
            }
        }
    }
}

QVector2D UVTransform::snapToPixel(const QVector2D& uv, float resolution) {
    return QVector2D(
        qRound(uv.x() * resolution) / resolution,
        qRound(uv.y() * resolution) / resolution
    );
}

void UVTransform::alignToAxis(QVector<QVector2D>& uvs, float threshold) {
    if (uvs.isEmpty() || threshold <= 0.0f) return;
    
    // Snap UVs that are close to 0, 0.5, or 1.0 to those exact values
    for (auto& uv : uvs) {
        if (qAbs(uv.x()) < threshold) uv.setX(0.0f);
        else if (qAbs(uv.x() - 0.5f) < threshold) uv.setX(0.5f);
        else if (qAbs(uv.x() - 1.0f) < threshold) uv.setX(1.0f);
        
        if (qAbs(uv.y()) < threshold) uv.setY(0.0f);
        else if (qAbs(uv.y() - 0.5f) < threshold) uv.setY(0.5f);
        else if (qAbs(uv.y() - 1.0f) < threshold) uv.setY(1.0f);
    }
}

bool UVStitcher::stitchIslands(const UVIsland& island1, const UVIsland& island2,
                              const QVector<QVector2D>& uvs1, const QVector<QVector2D>& uvs2,
                              const StitchConfig& config) {
    // Find matching seam edges between two UV islands
    int seamIdx = findMatchingSeam(island1, island2, uvs1, uvs2);
    if (seamIdx < 0) return false;
    
    // Merge the UVs at the matching seam
    float threshold = config.threshold;
    
    for (int i = 0; i < island1.faceIndices.size(); ++i) {
        for (int j = 0; j < island2.faceIndices.size(); ++j) {
            // Check if face edges match based on distance threshold
            if ((uvs1[i] - uvs2[j]).length() < threshold) {
                // Snap UVs together
                QVector2D avgUV = (uvs1[i] + uvs2[j]) * 0.5f;
                const_cast<QVector<QVector2D>&>(uvs1)[i] = avgUV;
                const_cast<QVector<QVector2D>&>(uvs2)[j] = avgUV;
            }
        }
    }
    
    return true;
}

int UVStitcher::findMatchingSeam(const UVIsland& island1, const UVIsland& island2,
                                 const QVector<QVector2D>& uvs1, const QVector<QVector2D>& uvs2) {
    // Find the index of the matching seam between two UV islands
    float minDist = 1e9f;
    int bestIdx = -1;
    
    for (int i = 0; i < island1.faceIndices.size() && i < uvs1.size(); ++i) {
        for (int j = 0; j < island2.faceIndices.size() && j < uvs2.size(); ++j) {
            float dist = (uvs1[i] - uvs2[j]).length();
            if (dist < minDist) {
                minDist = dist;
                bestIdx = i;
            }
        }
    }
    
    return bestIdx;
}

float MinStretchUnwrapper::computeStretch(const QVector<QVector3D>& verts, const QVector<QVector<int>>& faces,
                                          const QVector<QVector2D>& uvs, int faceIndex) {
    if (faceIndex >= faces.size()) return 0;

    const auto& f = faces[faceIndex];
    if (f.size() < 3) return 0;

    QVector3D e1 = verts[f[1]] - verts[f[0]];
    QVector3D e2 = verts[f[2]] - verts[f[0]];

    QVector2D du1 = uvs[f[1]] - uvs[f[0]];
    QVector2D du2 = uvs[f[2]] - uvs[f[0]];

    float eLen1 = e1.length();
    float eLen2 = e2.length();
    float duLen1 = qMax(0.0001f, du1.length());
    float duLen2 = qMax(0.0001f, du2.length());

    return qMax(eLen1 / duLen1, eLen2 / duLen2);
}

void MinStretchUnwrapper::smoothUVs(QVector<QVector2D>& uvs, float lambda) {
    QVector<QVector2D> newUVs = uvs;
    for (int i = 0; i < uvs.size(); ++i) {
        newUVs[i] = uvs[i] * (1 - lambda);
    }
    uvs = newUVs;
}

bool MinStretchUnwrapper::minimizeStretch(const QVector<QVector3D>& vertices, const QVector<QVector<int>>& faces,
                                         QVector<QVector2D>& uvs, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        smoothUVs(uvs, 0.3f);

        float maxStretch = 0;
        for (int i = 0; i < faces.size(); ++i) {
            maxStretch = qMax(maxStretch, computeStretch(vertices, faces, uvs, i));
        }

        if (maxStretch < 1.1f) break;
    }

    UVMapper::scaleToFit(uvs, 0.01f);
    return true;
}

float UVQualityChecker::computeConformalDistortion(const QVector<QVector3D>& verts, const QVector<QVector2D>& uvs,
                                                   int faceIndex) {
    if (faceIndex < 0 || faceIndex + 2 >= verts.size() || faceIndex + 2 >= uvs.size()) {
        return 0.0f;
    }
    
    // Compute the difference between 3D angles and UV angles for the face
    QVector3D e1_3d = verts[faceIndex + 1] - verts[faceIndex];
    QVector3D e2_3d = verts[faceIndex + 2] - verts[faceIndex];
    QVector2D e1_uv = uvs[faceIndex + 1] - uvs[faceIndex];
    QVector2D e2_uv = uvs[faceIndex + 2] - uvs[faceIndex];
    
    float angle3d = qAcos(QVector3D::dotProduct(e1_3d.normalized(), e2_3d.normalized()));
    float angleUv = qAcos(QVector2D::dotProduct(e1_uv.normalized(), e2_uv.normalized()));
    
    return qAbs(angle3d - angleUv);
}

float UVQualityChecker::computeAreaDistortion(const QVector<QVector3D>& verts, const QVector<QVector2D>& uvs,
                                               int faceIndex) {
    if (faceIndex < 0 || faceIndex + 2 >= verts.size() || faceIndex + 2 >= uvs.size()) {
        return 0.0f;
    }
    
    // 3D area (half of cross product magnitude)
    QVector3D e1_3d = verts[faceIndex + 1] - verts[faceIndex];
    QVector3D e2_3d = verts[faceIndex + 2] - verts[faceIndex];
    float area3d = QVector3D::crossProduct(e1_3d, e2_3d).length() * 0.5f;
    
    // UV area
    QVector2D e1_uv = uvs[faceIndex + 1] - uvs[faceIndex];
    QVector2D e2_uv = uvs[faceIndex + 2] - uvs[faceIndex];
    float areaUv = qAbs(e1_uv.x() * e2_uv.y() - e1_uv.y() * e2_uv.x()) * 0.5f;
    
    if (area3d < 0.0001f || areaUv < 0.0001f) return 1.0f;
    return area3d / areaUv;
}

bool UVQualityChecker::checkOverlap(const QVector<UVIsland>& islands) {
    // Simple AABB overlap check between all island pairs
    for (int i = 0; i < islands.size(); ++i) {
        for (int j = i + 1; j < islands.size(); ++j) {
            const auto& a = islands[i];
            const auto& b = islands[j];
            
            // Check AABB overlap
            if (a.center.x() < b.center.x() + b.boundingBox.x() &&
                a.center.x() + a.boundingBox.x() > b.center.x() &&
                a.center.y() < b.center.y() + b.boundingBox.y() &&
                a.center.y() + a.boundingBox.y() > b.center.y()) {
                return true;
            }
        }
    }
    return false;
}

UVQualityChecker::QualityReport UVQualityChecker::analyze(const QVector<QVector3D>& vertices,
                                                          const QVector<QVector<int>>& faces,
                                                          const QVector<QVector2D>& uvs, float texelSize) {
    QualityReport report;
    if (faces.isEmpty() || vertices.isEmpty() || uvs.isEmpty()) return report;

    float totalStretch = 0.0f;
    float totalConformal = 0.0f;
    float totalAreaRatio = 0.0f;
    float maxStretchFace = 0.0f;
    int stretchedFaceCount = 0;

    // Compute global texel density for reference
    float total3DArea = 0.0f;
    float totalUVArea = 0.0f;

    for (int fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        if (face.size() < 3) continue;

        // Gather face vertex positions and UVs into flat arrays for the helper functions
        QVector<QVector3D> faceVerts;
        QVector<QVector2D> faceUVs;
        for (int i = 0; i < face.size(); ++i) {
            int vi = face[i];
            if (vi >= 0 && vi < vertices.size()) faceVerts.append(vertices[vi]);
            if (vi >= 0 && vi < uvs.size()) faceUVs.append(uvs[vi]);
            else faceUVs.append(QVector2D(0, 0));
        }
        if (faceVerts.size() < 3) continue;

        // 3D triangle area
        QVector3D e1 = faceVerts[1] - faceVerts[0];
        QVector3D e2 = faceVerts[2] - faceVerts[0];
        float area3d = QVector3D::crossProduct(e1, e2).length() * 0.5f;
        total3DArea += area3d;

        // UV triangle area
        QVector2D u1 = faceUVs[1] - faceUVs[0];
        QVector2D u2 = faceUVs[2] - faceUVs[0];
        float areaUV = qAbs(u1.x() * u2.y() - u1.y() * u2.x()) * 0.5f;
        totalUVArea += areaUV;

        // Conformal distortion: angle preservation
        float conformal = 0.0f;
        if (face.size() >= 3) {
            // Compare edge length ratios between 3D and UV
            for (int i = 0; i < 3; ++i) {
                int j = (i + 1) % 3;
                float len3D = (faceVerts[j] - faceVerts[i]).length();
                float lenUV = (faceUVs[j] - faceUVs[i]).length();
                if (lenUV > 0.0001f && len3D > 0.0001f) {
                    float ratio3D = len3D / area3d;
                    float ratioUV = lenUV / areaUV;
                    conformal += qAbs(ratio3D - ratioUV);
                }
            }
            conformal /= 3.0f;
        }

        // Area-based stretch: how much the UV area deviates from expected
        float expectedUVArea = (texelSize > 0.0f) ? (area3d / (texelSize * texelSize)) : area3d;
        float stretch = (areaUV > 0.0001f) ? (expectedUVArea / areaUV) : 1.0f;
        stretch = qAbs(stretch - 1.0f) + 1.0f; // normalize around 1.0

        totalStretch += stretch;
        totalConformal += conformal;
        totalAreaRatio += (areaUV > 0.0001f) ? (area3d / areaUV) : 0.0f;

        if (stretch > maxStretchFace) maxStretchFace = stretch;
        if (stretch > 1.1f || stretch < 0.9f) {
            stretchedFaceCount++;
            report.distortedFaces.append(fi);
        }
    }

    int validFaces = faces.size();
    report.avgStretch = (validFaces > 0) ? (totalStretch / validFaces) : 1.0f;
    report.maxStretch = maxStretchFace;
    report.distortion = (validFaces > 0) ? (totalConformal / validFaces) : 0.0f;
    report.uvToTexRatio = texelSize;
    report.islandsOverlap = 0.0f; // Would need island detection to compute

    return report;
}

}