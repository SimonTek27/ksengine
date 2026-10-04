#pragma once

#include "KS3DFormat.h"
#include <string>
#include <vector>

namespace ks {

class KS3DReader {
public:
    KS3DReader() = default;

    bool readFromFile(const std::string& path);
    const KS3DScene& scene() const { return m_scene; }
    const std::string& lastError() const { return m_lastError; }

    // Convenience: extract vertex positions from mesh
    static std::vector<float> getVertexPositions(const KS3DMesh& mesh);

    // Convenience: extract normals from mesh
    static std::vector<float> getVertexNormals(const KS3DMesh& mesh);

    // Convenience: extract UVs from mesh
    static std::vector<float> getVertexUVs(const KS3DMesh& mesh);

private:
    bool parseHeader(const std::vector<uint8_t>& data, ks3d::FileHeader& header);
    bool parseStringTable(const std::vector<uint8_t>& data, uint32_t offset, uint32_t size);
    bool parseMaterials(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count);
    bool parseMeshes(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count);
    bool parseTextures(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count);
    bool parseNodes(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count);

    std::string getString(uint32_t index) const;

    KS3DScene m_scene;
    std::string m_lastError;

    // String table
    std::vector<std::pair<uint32_t, uint32_t>> m_stringEntries; // (offset, length)
    std::vector<uint8_t> m_stringData;

    // Full file buffer
    std::vector<uint8_t> m_fileData;
};

} // namespace ks
