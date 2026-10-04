#pragma once

#include "CADTypes.h"

namespace CAD {

/**
 * @brief STEP format parser (ISO 10303-21)
 */
class STEPParser {
public:
    static bool parse(const QString& filePath, File& outFile);
    static QString getLastError();
private:
    static QString m_lastError;
};

/**
 * @brief IGES format parser
 */
class IGESParser {
public:
    static bool parse(const QString& filePath, File& outFile);
    static QString getLastError();
private:
    static QString m_lastError;
};

/**
 * @brief DXF format parser
 */
class DXFParser {
public:
    static bool parse(const QString& filePath, File& outFile);
    static QString getLastError();
private:
    static QString m_lastError;
};

/**
 * @brief BREP format parser (OpenCascade)
 */
class BREPParser {
public:
    static bool parse(const QString& filePath, File& outFile);
    static QString getLastError();
private:
    static QString m_lastError;
};

} // namespace CAD
