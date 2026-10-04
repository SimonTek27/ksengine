#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>
#include <QQuickItem>
#include "sdk/kseditor/engine/mesh/MeshRenderer.h"
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "sdk/kseditor/engine/mesh/AdvancedMeshOps.h"
#include "acCSP/CspConfigParser.h"
#include "KsAssettoCorsaContentPath.h"
#include "sdk/kseditor/modules/PaintEditor/PaintSystem.h"

namespace ks {

class ContentQmlBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isKsInstalled READ isKsInstalled NOTIFY contentChanged)
    Q_PROPERTY(QString ksRoot READ getKsRoot NOTIFY contentChanged)
    Q_PROPERTY(QStringList cars READ getCars NOTIFY contentChanged)
    Q_PROPERTY(QStringList tracks READ getTracks NOTIFY contentChanged)
    Q_PROPERTY(QStringList sounds READ getSounds NOTIFY contentChanged)

public:
    explicit ContentQmlBridge(QObject *parent = nullptr)
        : QObject(parent)
        , m_finder(new KsContentFinder(this))
    {
        connect(m_finder, &KsContentFinder::contentChanged, this, &ContentQmlBridge::contentChanged);
    }

    bool isKsInstalled() const { return m_finder->isKsInstalled(); }
    QString getKsRoot() const { return m_finder->findKsRoot(); }
    QStringList getCars() const { return m_finder->findCars(); }
    QStringList getTracks() const { return m_finder->findTracks(); }
    QStringList getSounds() const { return m_finder->findSounds(); }

    Q_INVOKABLE void setCustomPath(const QString &path) { m_finder->setCustomPath(path); }
    Q_INVOKABLE void refresh() { emit contentChanged(); }

    Q_INVOKABLE QString getContentFolder() const { return m_finder->findContentFolder(); }
    Q_INVOKABLE QString getAppsFolder() const { return m_finder->findAppsFolder(); }
    Q_INVOKABLE QString getSkiesFolder() const { return m_finder->findSkiesFolder(); }

    Q_INVOKABLE QStringList getSkins() const { return m_finder->findSkins(); }

    Q_INVOKABLE QStringList getSkinList(const QString &carName) const {
        if (carName.isEmpty()) return QStringList();

        QStringList skins;
        QString contentPath = m_finder->findContentFolder();
        if (contentPath.isEmpty()) return skins;

        QString skinsPath = contentPath + "/cars/" + carName + "/skins";
        QDir skinsDir(skinsPath);

        if (skinsDir.exists()) {
            skins = skinsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        }

        return skins;
    }

    Q_INVOKABLE QString getMeshInfo(const QString &carOrTrack) const {
        if (carOrTrack.isEmpty()) return QString();

        QString contentPath = m_finder->findContentFolder();
        if (contentPath.isEmpty()) return QString();

        QString carPath = contentPath + "/cars/" + carOrTrack;
        QString trackPath = contentPath + "/tracks/" + carOrTrack;

        QDir carDir(carPath);
        QDir trackDir(trackPath);

        QString kn5Path;
        if (carDir.exists()) {
            QStringList kn5Files = carDir.entryList(QStringList() << "*.kn5", QDir::Files);
            if (!kn5Files.isEmpty()) {
                kn5Path = carPath + "/" + kn5Files.first();
            }
        } else if (trackDir.exists()) {
            QStringList kn5Files = trackDir.entryList(QStringList() << "*.kn5", QDir::Files);
            if (!kn5Files.isEmpty()) {
                kn5Path = trackPath + "/" + kn5Files.first();
            }
        }

        if (!kn5Path.isEmpty()) {
            return tr("KN5: %1").arg(QFileInfo(kn5Path).fileName());
        }

        return QString();
    }

    Q_INVOKABLE QString getCarKn5Path(const QString &carName) const {
        if (carName.isEmpty()) return QString();

        QString contentPath = m_finder->findContentFolder();
        if (contentPath.isEmpty()) return QString();

        QString carPath = contentPath + "/cars/" + carName;
        QDir dir(carPath);

        if (dir.exists()) {
            QStringList kn5Files = dir.entryList(QStringList() << "*.kn5", QDir::Files);
            if (!kn5Files.isEmpty()) {
                return carPath + "/" + kn5Files.first();
            }
        }

        return QString();
    }

    Q_INVOKABLE QString getTrackKn5Path(const QString &trackName) const {
        if (trackName.isEmpty()) return QString();

        QString contentPath = m_finder->findContentFolder();
        if (contentPath.isEmpty()) return QString();

        QString trackPath = contentPath + "/tracks/" + trackName;
        QDir dir(trackPath);

        if (dir.exists()) {
            QStringList kn5Files = dir.entryList(QStringList() << "*.kn5", QDir::Files);
            if (!kn5Files.isEmpty()) {
                return trackPath + "/" + kn5Files.first();
            }
        }

        return QString();
    }

signals:
    void contentChanged();

private:
    KsContentFinder *m_finder;
};

class CspConfigBridge : public QObject {
    Q_OBJECT

public:
    explicit CspConfigBridge(QObject* parent = nullptr);

    Q_INVOKABLE QVariantMap loadCarConfig(const QString& path);
    Q_INVOKABLE QVariantMap loadTrackConfig(const QString& path);
    Q_INVOKABLE bool saveCarConfig(const QString& path, const QVariantMap& data);
    Q_INVOKABLE bool saveTrackConfig(const QString& path, const QVariantMap& data);

    Q_INVOKABLE QVariantList parseEmissives(const QVariantMap& sections);
    Q_INVOKABLE QVariantList parseBrakeDiscs(const QVariantMap& sections);
    Q_INVOKABLE QVariantList parseTrackLights(const QVariantMap& sections);
    Q_INVOKABLE QVariantList parseMaterialAdjustments(const QVariantMap& sections);
    Q_INVOKABLE QVariantList parseConditions(const QVariantMap& sections);

    Q_INVOKABLE QVariantMap serializeEmissives(const QVariantList& emissives);
    Q_INVOKABLE QVariantMap serializeBrakeDiscs(const QVariantList& brakeDiscs);
    Q_INVOKABLE QVariantMap serializeTrackLights(const QVariantList& trackLights);
    Q_INVOKABLE QVariantMap serializeMaterialAdjustments(const QVariantList& adjustments);
    Q_INVOKABLE QVariantMap serializeConditions(const QVariantList& conditions);

    Q_INVOKABLE QVariantMap getCarSchema();
    Q_INVOKABLE QVariantMap getTrackSchema();

    Q_INVOKABLE QVariantMap createDefaultCarConfig();
    Q_INVOKABLE QVariantMap createDefaultTrackConfig();

    Q_INVOKABLE QVariantMap addEmissive(QVariantMap sections, const QVariantMap& emissive);
    Q_INVOKABLE QVariantMap removeEmissive(QVariantMap sections, const QString& sectionName);
    Q_INVOKABLE QVariantMap addTrackLight(QVariantMap sections, const QVariantMap& light);
    Q_INVOKABLE QVariantMap removeTrackLight(QVariantMap sections, const QString& sectionName);
    Q_INVOKABLE QVariantMap addMaterialAdjustment(QVariantMap sections, const QVariantMap& adjustment);
    Q_INVOKABLE QVariantMap removeMaterialAdjustment(QVariantMap sections, const QString& sectionName);
    Q_INVOKABLE QVariantMap addCondition(QVariantMap sections, const QVariantMap& condition);
    Q_INVOKABLE QVariantMap removeCondition(QVariantMap sections, const QString& sectionName);

    Q_INVOKABLE QStringList getConditionInputs();
    Q_INVOKABLE QStringList getMaterialKeys();
    Q_INVOKABLE QStringList getConditionSmoothnessOptions();

signals:
    void configLoaded(const QString& path, const QVariantMap& data);
    void configSaved(const QString& path, bool success);

private:
    static QStringList s_conditionInputs;
    static QStringList s_materialKeys;
    static QStringList s_conditionSmoothnessOptions;
};

class MeshLoaderBridge : public QObject
{
    Q_OBJECT

public:
    explicit MeshLoaderBridge(QObject *parent = nullptr);

    Q_INVOKABLE bool loadKN5(const QString &filePath);
    Q_INVOKABLE bool loadOBJ(const QString &filePath);
    Q_INVOKABLE bool loadGLTF(const QString &filePath);

    Q_INVOKABLE QStringList getAvailableMeshes() const;
    Q_INVOKABLE int getCurrentVertexCount() const;
    Q_INVOKABLE int getCurrentFaceCount() const;
    Q_INVOKABLE QString getCurrentMeshName() const;

    Q_INVOKABLE QString getKsRoot() const;
    Q_INVOKABLE QStringList getCarList() const;
    Q_INVOKABLE QStringList getTrackList() const;
    Q_INVOKABLE QStringList getSkinList(const QString &carName) const;

    Q_INVOKABLE QString getMeshInfo(const QString &carOrTrack) const;
    Q_INVOKABLE QString getCarKn5Path(const QString &carName) const;
    Q_INVOKABLE QString getTrackKn5Path(const QString &trackName) const;

    Q_INVOKABLE QVariantList getVertexData() const;
    Q_INVOKABLE QVariantList getIndexData() const;

signals:
    void meshLoaded(const QString &name, int vertices, int faces);
    void meshLoadError(const QString &error);
    void contentRefreshed();

private:
    MeshRenderer *m_meshRenderer;
    KsContentFinder *m_contentFinder;
};

class MeshOpsBridge : public QObject {
    Q_OBJECT

public:
    Q_INVOKABLE QVariantMap loopCut(const QVariant& meshData, int cuts, const QVariant& center, const QVariant& normal);
    Q_INVOKABLE QVariantMap knifeCut(const QVariant& meshData, const QVariant& start, const QVariant& end, bool snapToVertex = true);
    Q_INVOKABLE QVariant knifeIntersectWithPlane(const QVariant& meshData, const QVariant& point, const QVariant& normal);
    Q_INVOKABLE int splitEdge(QVariant& meshData, int v1, int v2, float t);
    Q_INVOKABLE QVariant splitFace(QVariant& meshData, int faceIndex, const QVariant& point);

    Q_INVOKABLE QVariantList findEdgeLoops(const QVariant& meshData, int startEdge);
    Q_INVOKABLE QVariantList findFaceLoops(const QVariant& meshData, const QVariant& edgeLoop);

    Q_INVOKABLE QVariantList bisect(const QVariant& meshData, const QVariant& planePoint, const QVariant& planeNormal);
    Q_INVOKABLE QVariantMap bisectCut(const QVariant& meshData, const QVariant& planePoint, const QVariant& planeNormal, bool cutCenter = true);

    Q_INVOKABLE QVariantList findConnectedVertices(const QVariant& meshData, int startVertex);
    Q_INVOKABLE QVariantList findVertexRings(const QVariant& meshData, int vertexIndex);
    Q_INVOKABLE int getVertexValence(const QVariant& meshData, int vertexIndex);

    Q_INVOKABLE QVariantMap convexHull(const QVariant& meshData);

    Q_INVOKABLE QVariantList findHoles(const QVariant& meshData);
    Q_INVOKABLE QVariantMap fillHoles(const QVariant& meshData, int maxHoleSize = 100);
    Q_INVOKABLE QVariantMap planarFaces(const QVariant& meshData, float threshold = 0.001f);

    Q_INVOKABLE QVariantMap triRemesh(const QVariant& meshData);
    Q_INVOKABLE QVariantMap quadRemesh(const QVariant& meshData, int targetCount = 1000);
    Q_INVOKABLE QVariantMap decimate(const QVariant& meshData, float targetRatio);

    Q_INVOKABLE static QVariant meshToVariant(const MeshData& mesh);
    Q_INVOKABLE static MeshData variantToMesh(const QVariant& data);

    Q_INVOKABLE QVariant sculptDraw(const QVariant& meshData, const QVariant& brushPos, float strength, float radius);
    Q_INVOKABLE QVariant sculptSmooth(const QVariant& meshData, const QVariant& brushPos, float strength, float radius);
    Q_INVOKABLE QVariant sculptFlatten(const QVariant& meshData, const QVariant& brushPos, float strength, float radius);
    Q_INVOKABLE QVariant sculptInflate(const QVariant& meshData, const QVariant& brushPos, float strength, float radius);

    Q_INVOKABLE QVariantList selectSimilar(const QVariant& meshData, const QVariantList& selected, const QString& property, float tolerance);
    Q_INVOKABLE QVariantList selectByNormal(const QVariant& meshData, const QVariant& normal, float angleDegrees);
    Q_INVOKABLE QVariantList growSelection(const QVariant& meshData, const QVariantList& selected);
    Q_INVOKABLE QVariantList shrinkSelection(const QVariant& meshData, const QVariantList& selected);

    Q_INVOKABLE static QVariant vector3DToVariant(const QVector3D& v);
    Q_INVOKABLE static QVector3D variantToVector3D(const QVariant& v);

private:
    static QVector3D parseVector3D(const QVariant& v);
};

class LiveryEditorBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList skins READ skins NOTIFY skinsChanged)
    Q_PROPERTY(QString currentSkin READ currentSkin NOTIFY currentSkinChanged)
    Q_PROPERTY(QStringList layerTypes READ layerTypes CONSTANT)
    Q_PROPERTY(QStringList supportedCountries READ supportedCountries CONSTANT)

public:
    explicit LiveryEditorBridge(QObject* parent = nullptr);

    QStringList skins() const { return m_skins; }
    QString currentSkin() const { return m_currentSkin; }
    QStringList layerTypes() const { return graphics::PaintSystem::getLayerTypes(); }
    QStringList supportedCountries() const { return graphics::PaintSystem::getSupportedCountries(); }

    Q_INVOKABLE void loadSkins(const QString& carPath);
    Q_INVOKABLE bool createSkin(const QString& carPath, const QString& skinName);
    Q_INVOKABLE bool deleteSkin(const QString& carPath, const QString& skinName);
    Q_INVOKABLE bool duplicateSkin(const QString& carPath, const QString& sourceName, const QString& destName);
    Q_INVOKABLE bool selectSkin(const QString& carPath, const QString& skinName);

    Q_INVOKABLE QVariantMap getSkinConfig(const QString& skinPath);
    Q_INVOKABLE bool saveSkinConfig(const QString& skinPath, const QVariantMap& config);

    Q_INVOKABLE bool addLayer(const QString& skinPath, const QString& name, const QString& type,
                               float opacity = 1.0f, float posX = 0.0f, float posY = 0.0f,
                               float sizeW = 1.0f, float sizeH = 1.0f);
    Q_INVOKABLE bool removeLayer(const QString& skinPath, int index);
    Q_INVOKABLE bool moveLayer(const QString& skinPath, int fromIndex, int toIndex);

    Q_INVOKABLE bool generateLicensePlate(const QString& skinPath, const QString& text, const QString& country);
    Q_INVOKABLE bool generatePreview(const QString& skinPath);

    Q_INVOKABLE bool validateSkin(const QString& skinPath);

    Q_INVOKABLE bool exportSkin(const QString& skinPath, const QString& outputPath);
    Q_INVOKABLE bool importSkin(const QString& importPath, const QString& carPath);

signals:
    void skinsChanged();
    void currentSkinChanged();
    void skinCreated(const QString& skinName);
    void skinDeleted(const QString& skinName);
    void liveryModified();
    void validationResult(bool valid, const QString& message);

private:
    QStringList m_skins;
    QString m_currentSkin;
    graphics::PaintManager* m_manager = nullptr;
};

} // namespace ks
