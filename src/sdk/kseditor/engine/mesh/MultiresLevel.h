#pragma once

#include <QObject>
#include <QVector3D>
#include <QVector>
#include <QMap>

struct SubdivisionLevel {
    QVector<QVector3D> vertices;
    QVector<int> faces; // face indices into vertices
    QVector<float> edgeCreases; // crease weight per edge (0-1)
    QVector<QVector3D> faceNormals;
    
    SubdivisionLevel() {}
    SubdivisionLevel(const QVector<QVector3D>& verts, const QVector<int>& faces,
                     const QVector<float>& creases = QVector<float>(),
                     const QVector<QVector3D>& norms = QVector<QVector3D>())
        : vertices(verts), faces(faces), edgeCreases(creases), faceNormals(norms) {}
};

class MultiresManager : public QObject
{
    Q_OBJECT
public:
    explicit MultiresManager(QObject* parent = nullptr);
    ~MultiresManager();
    
    // Create initial level from mesh data
    void createLevel(const QVector<QVector3D>& vertices, const QVector<int>& faces);
    
    // Get current level
    int currentLevel() const { return m_levels.size() > 0 ? m_currentLevel : 0; }
    const SubdivisionLevel& currentLevelData() const { return m_levels[m_currentLevel]; }
    const QVector<SubdivisionLevel>& allLevels() const { return m_levels; }
    
    // Navigate levels
    void setCurrentLevel(int level);
    void addLevel(); // add new higher subdivision level
    void removeLevel();
    
    // Subdivision operations
    void subdivideCurrentLevel(); // Catmull-Clark subdivision
    void limitCurrentLevel(); // Limit surface (shape preservation)
    
    // Sculpting on current level
    int sculptBrush(const QVector3D& center, float radius, float strength, int mode,
                    const QVector3D& drag, const QVector3D& previousCenter,
                    float falloffPower, const QSet<int>* pinned);
    
    // Bake/freeze current level to base mesh
    void bakeCurrentLevel();
    
    // Signals
signals:
    void levelChanged(int level);
    void levelsChanged(int count);
    void sculptUpdated();
    void baked();
    
private:
    QVector<SubdivisionLevel> m_levels;
    int m_currentLevel = 0;
    QVector<QVector3D> m_previousVertices; // Stored before subdivision for limit surface
    
    // Catmull-Clark subdivision
    QVector<QVector3D> catmullClarkSubdivide(const QVector<QVector3D>& verts, const QVector<int>& faces);
    QVector<int> catmullClarkFaceIndices(const QVector<int>& faces);
    QVector<QVector3D> computeVertexNormals(const QVector<QVector3D>& verts, const QVector<int>& faces);
    
    // Limit surface preservation
    void preserveShape(QVector<QVector3D>& newVerts, const QVector<QVector3D>& oldVerts);
};