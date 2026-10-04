#include "CADAdvancedParsers.h"
#include "../sys/LogManager.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>

namespace CAD {

// ============================================================
// STEP Parser Implementation
// ============================================================

QString STEPParser::m_lastError;

bool STEPParser::parse(const QString& filePath, File& outFile)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = "Failed to open STEP file: " + filePath;
        return false;
    }
    
    outFile.format = "STEP";
    outFile.sourceFile = filePath;
    outFile.assembly.name = QFileInfo(filePath).baseName();
    
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    
    // Parse STEP entities (simplified - production version would use full STEP parser)
    QRegularExpression entityRegex(R"(#(\d+)\s*=\s*([A-Z_]+)\s*\(([^;]*)\);)");
    QRegularExpressionMatchIterator it = entityRegex.globalMatch(content);
    
    QMap<int, QString> entityTypes;
    QMap<int, QString> entityParams;
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int id = match.captured(1).toInt();
        QString type = match.captured(2);
        QString params = match.captured(3);
        
        entityTypes[id] = type;
        entityParams[id] = params;
        
        if (type == "MANIFOLD_SOLID_BREP" || type == "CLOSED_SHELL") {
            outFile.solidCount++;
        }
    }
    
    LOG_INFO("CADSTEPParser", QString("Loaded STEP: %1 entities, %2 solids")
        .arg(entityTypes.size()).arg(outFile.solidCount));
    
    return true;
}

QString STEPParser::getLastError()
{
    return m_lastError;
}

// ============================================================
// IGES Parser Implementation
// ============================================================

QString IGESParser::m_lastError;

bool IGESParser::parse(const QString& filePath, File& outFile)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = "Failed to open IGES file: " + filePath;
        return false;
    }
    
    outFile.format = "IGES";
    outFile.sourceFile = filePath;
    outFile.assembly.name = QFileInfo(filePath).baseName();
    
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    
    // Parse IGES format (80-column fixed format)
    QStringList lines = content.split('\n', Qt::SkipEmptyParts);
    
    int startSectionEnd = 0;
    int globalSectionEnd = 0;
    int directorySectionEnd = 0;
    
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        if (line.size() < 72) continue;
        
        QString sectionCode = line.mid(72, 1);
        
        if (sectionCode == "S") {
            startSectionEnd = i;
        } else if (sectionCode == "G") {
            globalSectionEnd = i;
        } else if (sectionCode == "D") {
            directorySectionEnd = i;
        } else if (sectionCode == "P") {
            // Parameter data section
        }
    }
    
    LOG_INFO("CADIGESParser", "Loaded IGES file: " + filePath);
    return true;
}

QString IGESParser::getLastError()
{
    return m_lastError;
}

// ============================================================
// DXF Parser Implementation
// ============================================================

QString DXFParser::m_lastError;

bool DXFParser::parse(const QString& filePath, File& outFile)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = "Failed to open DXF file: " + filePath;
        return false;
    }
    
    outFile.format = "DXF";
    outFile.sourceFile = filePath;
    outFile.assembly.name = QFileInfo(filePath).baseName();
    
    QTextStream stream(&file);
    QString section;
    QVector<Point3D> vertices;
    
    while (!stream.atEnd()) {
        QString codeStr = stream.readLine().trimmed();
        if (stream.atEnd()) break;
        
        QString value = stream.readLine().trimmed();
        
        if (codeStr == "0") {
            section = value;
        } else if (section == "ENTITIES") {
            if (codeStr == "10") {
                Point3D p;
                p.x = value.toDouble();
                vertices.append(p);
            } else if (codeStr == "20") {
                if (!vertices.isEmpty()) {
                    vertices.last().y = value.toDouble();
                }
            } else if (codeStr == "30") {
                if (!vertices.isEmpty()) {
                    vertices.last().z = value.toDouble();
                }
            }
        }
    }
    
    file.close();
    
    outFile.vertexCount = vertices.size();
    
    LOG_INFO("CADDXFParser", QString("Loaded DXF: %1 vertices").arg(vertices.size()));
    return true;
}

QString DXFParser::getLastError()
{
    return m_lastError;
}

// ============================================================
// BREP Parser Implementation
// ============================================================

QString BREPParser::m_lastError;

bool BREPParser::parse(const QString& filePath, File& outFile)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = "Failed to open BREP file: " + filePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    outFile.format = "BREP";
    outFile.sourceFile = filePath;
    outFile.assembly.name = QFileInfo(filePath).baseName();
    outFile.rawData = data;
    
    // BREP format signature check
    if (data.size() >= 20) {
        QString signature = QString::fromLatin1(data.left(20));
        if (signature.contains("DBRep_DrawableShape")) {
            LOG_INFO("CADBREPParser", "OpenCascade BREP format detected");
        }
    }
    
    LOG_INFO("CADBREPParser", "Loaded BREP file: " + filePath);
    return true;
}

QString BREPParser::getLastError()
{
    return m_lastError;
}

} // namespace CAD
