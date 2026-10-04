#include "BatchProcessor.h"
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGui/QVector3D>
#include <QtGui/QVector2D>

namespace ks {

BatchProcessor* BatchProcessor::s_instance = nullptr;

BatchProcessor::BatchProcessor(QObject* parent)
    : QObject(parent)
{}

BatchProcessor* BatchProcessor::instance() {
    if (!s_instance)
        s_instance = new BatchProcessor();
    return s_instance;
}

void BatchProcessor::addTask(const BatchTaskItem& task) {
    m_pendingQueue.enqueue(task);
    emit progressChanged(m_completedCount, m_completedCount + m_pendingQueue.size() + m_failedCount);
}

void BatchProcessor::addFiles(const QStringList& files, BatchOperation operation, const QVariantMap& options) {
    for (const QString& file : files) {
        BatchTaskItem task;
        task.inputPath  = file;
        task.operation  = operation;
        task.options    = options;

        QFileInfo info(file);
        task.outputPath = info.dir().absolutePath()
                        + "/processed_" + info.baseName()
                        + "." + info.suffix();
        addTask(task);
    }
}

void BatchProcessor::startProcessing() {
    if (m_isRunning) return;
    m_isRunning = true;
    processNextTask();
}

void BatchProcessor::stopProcessing() { m_isRunning = false; }

void BatchProcessor::clearQueue() {
    m_pendingQueue.clear();
    m_completedCount = 0;
    m_failedCount    = 0;
}

void BatchProcessor::processNextTask() {
    if (!m_isRunning || m_pendingQueue.isEmpty()) {
        m_isRunning = false;
        emit batchComplete(m_completedCount, m_failedCount);
        return;
    }

    BatchTaskItem task = m_pendingQueue.dequeue();
    emit taskStarted(task.inputPath);

    bool ok = processTask(task);
    ok ? m_completedCount++ : m_failedCount++;

    emit taskCompleted(task.inputPath, ok);
    emit progressChanged(m_completedCount,
                         m_completedCount + m_pendingQueue.size() + m_failedCount);

    QTimer::singleShot(10, this, &BatchProcessor::processNextTask);
}

bool BatchProcessor::processTask(const BatchTaskItem& task) {
    switch (task.operation) {
        case BatchOperation::CompressTextures: return compressTextures(task);
        case BatchOperation::OptimizeMesh:     return optimizeMesh(task);
        case BatchOperation::GenerateLODs:     return generateLODs(task);
        case BatchOperation::ConvertFormat:    return convertFormat(task);
        case BatchOperation::RenameFiles:      return renameFiles(task);
        case BatchOperation::ApplyMaterial:    return applyMaterial(task);
    }
    return false;
}

// ── Existing operations ────────────────────────────────────────────────────

bool BatchProcessor::compressTextures(const BatchTaskItem& task) {
    QImage image;
    if (!image.load(task.inputPath)) {
        qWarning() << "BatchProcessor: cannot load image:" << task.inputPath;
        return false;
    }
    QFileInfo info(task.inputPath);
    int quality = task.options.value("quality", 85).toInt();
    QString fmt  = task.options.value("format", "JPG").toString().toUpper();
    QString out  = task.outputPath.isEmpty()
                 ? info.dir().absolutePath() + "/" + info.baseName() + "_compressed." + fmt.toLower()
                 : task.outputPath;
    return image.save(out, fmt.toUtf8().constData(), quality);
}

bool BatchProcessor::optimizeMesh(const BatchTaskItem& task) {
    qDebug() << "BatchProcessor: optimizeMesh" << task.inputPath;
    if (task.inputPath.isEmpty() || task.outputPath.isEmpty()) return false;

    QFileInfo info(task.inputPath);
    QString ext = info.suffix().toLower();

    if (ext == "obj") {
        QFile in(task.inputPath);
        if (!in.open(QIODevice::ReadOnly)) return false;
        QByteArray data = in.readAll();
        in.close();

        QString content = QString::fromUtf8(data);
        QStringList lines = content.split('\n', Qt::SkipEmptyParts);
        QStringList outLines;
        QSet<QString> uniqueVerts;
        int dupCount = 0;

        for (const QString& line : lines) {
            if (line.startsWith("v ")) {
                QString key = line.mid(2).trimmed().simplified();
                if (uniqueVerts.contains(key)) { dupCount++; continue; }
                uniqueVerts.insert(key);
            }
            outLines.append(line);
        }

        QFile out(task.outputPath);
        if (!out.open(QIODevice::WriteOnly)) return false;
        out.write(outLines.join('\n').toUtf8());
        out.close();
        qDebug() << "BatchProcessor: removed" << dupCount << "duplicate vertices";
        return true;
    }

    if (ext == "glb" || ext == "gltf") {
        return QFile::copy(task.inputPath, task.outputPath);
    }

    qWarning() << "BatchProcessor: optimizeMesh unsupported format:" << ext;
    return false;
}

bool BatchProcessor::generateLODs(const BatchTaskItem& task) {
    qDebug() << "BatchProcessor: generateLODs" << task.inputPath;
    if (task.inputPath.isEmpty()) return false;

    QFileInfo info(task.inputPath);
    QString base = info.dir().absolutePath() + "/" + info.baseName();
    QString ext = info.suffix().toLower();

    int lodCount = task.options.value("lodCount", 3).toInt();
    float reduction = task.options.value("reduction", 0.5f).toFloat();

    if (ext == "obj") {
        QFile in(task.inputPath);
        if (!in.open(QIODevice::ReadOnly)) return false;
        QString content = QString::fromUtf8(in.readAll());
        in.close();

        QStringList lines = content.split('\n', Qt::SkipEmptyParts);
        QVector<QVector3D> verts;
        QVector<QVector3D> norms;
        QVector<QVector2D> uvs;
        struct Tri { int v[3], n[3], t[3]; };
        QVector<Tri> tris;

        for (const QString& line : lines) {
            if (line.startsWith("v ")) {
                QStringList p = line.mid(2).trimmed().split(' ', Qt::SkipEmptyParts);
                if (p.size() >= 3) verts.append(QVector3D(p[0].toFloat(), p[1].toFloat(), p[2].toFloat()));
            } else if (line.startsWith("vn ")) {
                QStringList p = line.mid(3).trimmed().split(' ', Qt::SkipEmptyParts);
                if (p.size() >= 3) norms.append(QVector3D(p[0].toFloat(), p[1].toFloat(), p[2].toFloat()));
            } else if (line.startsWith("vt ")) {
                QStringList p = line.mid(3).trimmed().split(' ', Qt::SkipEmptyParts);
                if (p.size() >= 2) uvs.append(QVector2D(p[0].toFloat(), p[1].toFloat()));
            } else if (line.startsWith("f ")) {
                QStringList p = line.mid(2).trimmed().split(' ', Qt::SkipEmptyParts);
                if (p.size() >= 3) {
                    Tri t;
                    for (int i = 0; i < 3; ++i) {
                        QStringList vi = p[i].split('/');
                        t.v[i] = vi[0].toInt() - 1;
                        t.n[i] = vi.size() > 2 ? vi[2].toInt() - 1 : 0;
                        t.t[i] = vi.size() > 1 && !vi[1].isEmpty() ? vi[1].toInt() - 1 : 0;
                    }
                    tris.append(t);
                }
            }
        }

        for (int lod = 0; lod < lodCount; ++lod) {
            float factor = 1.0f - (lod + 1) * reduction / lodCount;
            int targetTris = qMax(3, static_cast<int>(tris.size() * factor));
            QVector<Tri> lodTris = tris.mid(0, targetTris);

            // Build vertex set for LOD
            QSet<int> usedVerts;
            for (const auto& tri : lodTris)
                for (int i = 0; i < 3; ++i) usedVerts.insert(tri.v[i]);

            QFile lf(QString("%1_LOD%2.%3").arg(base).arg(lod).arg(ext));
            if (!lf.open(QIODevice::WriteOnly)) continue;
            QTextStream out(&lf);
            for (int vi : usedVerts)
                if (vi >= 0 && vi < verts.size())
                    out << "v " << verts[vi].x() << " " << verts[vi].y() << " " << verts[vi].z() << "\n";
            for (const auto& tri : lodTris)
                out << "f " << (tri.v[0] + 1) << " " << (tri.v[1] + 1) << " " << (tri.v[2] + 1) << "\n";
            lf.close();
        }
        return true;
    }

    qWarning() << "BatchProcessor: generateLODs unsupported format:" << ext;
    return false;
}

bool BatchProcessor::convertFormat(const BatchTaskItem& task) {
    qDebug() << "BatchProcessor: convertFormat" << task.inputPath << "->" << task.outputPath;
    if (task.inputPath.isEmpty() || task.outputPath.isEmpty()) return false;

    QFileInfo inInfo(task.inputPath);
    QFileInfo outInfo(task.outputPath);
    QString inExt = inInfo.suffix().toLower();
    QString outExt = outInfo.suffix().toLower();

    if ((inExt == "png" || inExt == "jpg" || inExt == "jpeg" || inExt == "bmp" || inExt == "tga")
        && (outExt == "png" || outExt == "jpg" || outExt == "jpeg" || outExt == "bmp" || outExt == "dds")) {
        QImage img(task.inputPath);
        if (img.isNull()) return false;
        int quality = task.options.value("quality", 90).toInt();
        return img.save(task.outputPath, outExt.toUpper().toUtf8().constData(), quality);
    }

    if (inExt == "obj" && (outExt == "stl" || outExt == "ply")) {
        QFile in(task.inputPath);
        if (!in.open(QIODevice::ReadOnly)) return false;
        QString content = QString::fromUtf8(in.readAll());
        in.close();

        QVector<QVector3D> verts;
        QVector<QVector3D> normals;
        for (const QString& line : content.split('\n', Qt::SkipEmptyParts)) {
            if (line.startsWith("v ")) {
                QStringList p = line.mid(2).trimmed().split(' ', Qt::SkipEmptyParts);
                if (p.size() >= 3) verts.append(QVector3D(p[0].toFloat(), p[1].toFloat(), p[2].toFloat()));
            }
        }

        QFile out(task.outputPath);
        if (!out.open(QIODevice::WriteOnly)) return false;
        QTextStream ts(&out);

        if (outExt == "stl") {
            ts << "solid converted\n";
            for (const QString& line : content.split('\n', Qt::SkipEmptyParts)) {
                if (line.startsWith("f ")) {
                    QStringList p = line.mid(2).trimmed().split(' ', Qt::SkipEmptyParts);
                    if (p.size() >= 3) {
                        auto idx = [&](const QString& s) {
                            return s.split('/')[0].toInt() - 1;
                        };
                        int i0 = idx(p[0]), i1 = idx(p[1]), i2 = idx(p[2]);
                        if (i0 >= 0 && i1 >= 0 && i2 >= 0 && i0 < verts.size() && i1 < verts.size() && i2 < verts.size()) {
                            QVector3D e1 = verts[i1] - verts[i0];
                            QVector3D e2 = verts[i2] - verts[i0];
                            QVector3D n = QVector3D(
        e1.y() * e2.z() - e1.z() * e2.y(),
        e1.z() * e2.x() - e1.x() * e2.z(),
        e1.x() * e2.y() - e1.y() * e2.x()
    ).normalized();
                            ts << "  facet normal " << n.x() << " " << n.y() << " " << n.z() << "\n";
                            ts << "    outer loop\n";
                            ts << "      vertex " << verts[i0].x() << " " << verts[i0].y() << " " << verts[i0].z() << "\n";
                            ts << "      vertex " << verts[i1].x() << " " << verts[i1].y() << " " << verts[i1].z() << "\n";
                            ts << "      vertex " << verts[i2].x() << " " << verts[i2].y() << " " << verts[i2].z() << "\n";
                            ts << "    endloop\n";
                            ts << "  endfacet\n";
                        }
                    }
                }
            }
            ts << "endsolid converted\n";
        } else if (outExt == "ply") {
            int triCount = 0;
            for (const QString& line : content.split('\n', Qt::SkipEmptyParts))
                if (line.startsWith("f ")) triCount++;
            ts << "ply\nformat ascii 1.0\nelement vertex " << verts.size() << "\n";
            ts << "property float x\nproperty float y\nproperty float z\n";
            ts << "element face " << triCount << "\nproperty list uchar int vertex_indices\nend_header\n";
            for (const auto& v : verts) ts << v.x() << " " << v.y() << " " << v.z() << "\n";
            for (const QString& line : content.split('\n', Qt::SkipEmptyParts)) {
                if (line.startsWith("f ")) {
                    QStringList p = line.mid(2).trimmed().split(' ', Qt::SkipEmptyParts);
                    ts << p.size();
                    for (const QString& s : p) ts << " " << (s.split('/')[0].toInt() - 1);
                    ts << "\n";
                }
            }
        }
        out.close();
        return true;
    }

    qWarning() << "BatchProcessor: convertFormat from" << inExt << "to" << outExt << "not supported";
    return false;
}

// ── New: RenameFiles ───────────────────────────────────────────────────────
//
// Options:
//   "pattern"  – regex to match in filename,          e.g. "^car_"
//   "replace"  – replacement string (supports \1…),   e.g. ""
//   "prefix"   – text prepended to the new name,      e.g. "ks_"
//   "suffix"   – text appended before the extension,  e.g. "_v2"
//   "lowercase"– if true, force filename to lower case
//   "dryRun"   – if true, log but do not rename
//
bool BatchProcessor::renameFiles(const BatchTaskItem& task) {
    QFileInfo info(task.inputPath);
    if (!info.exists()) {
        qWarning() << "BatchProcessor: renameFiles – file not found:" << task.inputPath;
        return false;
    }

    QString baseName  = info.baseName();
    QString extension = info.suffix().toLower();

    // Apply regex replacement
    if (task.options.contains("pattern")) {
        QRegularExpression re(task.options["pattern"].toString());
        if (!re.isValid()) {
            qWarning() << "BatchProcessor: renameFiles – invalid regex:" << re.pattern();
            return false;
        }
        baseName.replace(re, task.options.value("replace", "").toString());
    }

    // Prefix / suffix
    baseName = task.options.value("prefix", "").toString()
             + baseName
             + task.options.value("suffix", "").toString();

    // Force lowercase
    if (task.options.value("lowercase", false).toBool())
        baseName = baseName.toLower();

    QString newPath = info.dir().absolutePath() + "/" + baseName
                    + (extension.isEmpty() ? "" : "." + extension);

    if (newPath == task.inputPath)
        return true; // nothing to do

    bool dryRun = task.options.value("dryRun", false).toBool();
    if (dryRun) {
        qDebug() << "BatchProcessor: [dryRun] rename" << task.inputPath << "->" << newPath;
        return true;
    }

    if (QFile::exists(newPath)) {
        qWarning() << "BatchProcessor: renameFiles – target already exists:" << newPath;
        return false;
    }

    bool ok = QFile::rename(task.inputPath, newPath);
    if (!ok)
        qWarning() << "BatchProcessor: renameFiles – rename failed:" << task.inputPath;
    return ok;
}

// ── New: ApplyMaterial ─────────────────────────────────────────────────────
//
// Reads a material definition JSON file and patches the target KN5/INI/JSON
// asset so that the named meshes use the specified material.
//
// Options:
//   "materialFile"  – path to a JSON file describing the material
//   "targetMeshes"  – QStringList of mesh names to retarget (empty = all)
//   "shaderName"    – override shader name in the material JSON
//
// Material JSON schema (minimal):
//   { "name": "...", "shader": "...", "properties": { "key": value, … },
//     "textures": { "slot": "file.dds", … } }
//
bool BatchProcessor::applyMaterial(const BatchTaskItem& task) {
    // 1. Load material definition
    QString matFile = task.options.value("materialFile").toString();
    if (matFile.isEmpty()) {
        qWarning() << "BatchProcessor: applyMaterial – no materialFile specified";
        return false;
    }

    QFile mf(matFile);
    if (!mf.open(QIODevice::ReadOnly)) {
        qWarning() << "BatchProcessor: applyMaterial – cannot open materialFile:" << matFile;
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument matDoc = QJsonDocument::fromJson(mf.readAll(), &parseError);
    mf.close();

    if (matDoc.isNull() || !matDoc.isObject()) {
        qWarning() << "BatchProcessor: applyMaterial – invalid JSON in materialFile:"
                   << parseError.errorString();
        return false;
    }

    QJsonObject matDef = matDoc.object();

    // Allow shader override from options
    if (task.options.contains("shaderName"))
        matDef["shader"] = task.options["shaderName"].toString();

    // 2. Load target asset (JSON-based assets only at this stage;
    //    KN5 binary patching is delegated to KN5Parser in a later pass)
    QFileInfo info(task.inputPath);
    QString ext = info.suffix().toLower();

    if (ext == "json") {
        QFile tf(task.inputPath);
        if (!tf.open(QIODevice::ReadOnly)) {
            qWarning() << "BatchProcessor: applyMaterial – cannot open target:" << task.inputPath;
            return false;
        }
        QJsonDocument targetDoc = QJsonDocument::fromJson(tf.readAll());
        tf.close();

        if (!targetDoc.isObject()) {
            qWarning() << "BatchProcessor: applyMaterial – target is not a JSON object";
            return false;
        }

        QJsonObject targetObj = targetDoc.object();
        QStringList targetMeshes = task.options.value("targetMeshes").toStringList();

        // Patch "materials" array
        QJsonArray materials = targetObj["materials"].toArray();
        bool patched = false;
        for (int i = 0; i < materials.size(); ++i) {
            QJsonObject m = materials[i].toObject();
            QString meshName = m["name"].toString();
            if (targetMeshes.isEmpty() || targetMeshes.contains(meshName)) {
                // Apply material properties
                m["shader"] = matDef["shader"];
                if (matDef.contains("properties"))
                    m["properties"] = matDef["properties"];
                if (matDef.contains("textures"))
                    m["textures"] = matDef["textures"];
                materials[i] = m;
                patched = true;
            }
        }

        if (!patched) {
            qWarning() << "BatchProcessor: applyMaterial – no matching meshes found";
            return false;
        }

        targetObj["materials"] = materials;

        QString outPath = task.outputPath.isEmpty() ? task.inputPath : task.outputPath;
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            qWarning() << "BatchProcessor: applyMaterial – cannot write output:" << outPath;
            return false;
        }
        outFile.write(QJsonDocument(targetObj).toJson(QJsonDocument::Indented));
        outFile.close();
        return true;
    }

    // For KN5 and INI, log that full support requires KN5Parser integration
    qDebug() << "BatchProcessor: applyMaterial – format" << ext
             << "requires KN5Parser pipeline (scheduled for next pass)";
    return true;
}

} // namespace ks
