#pragma once

#include "KS3DFormat.h"
#include <string>

namespace ks {

class KS3DWriter {
public:
    KS3DWriter() = default;

    bool writeToFile(const std::string& path, const KS3DScene& scene);
    const std::string& lastError() const { return m_lastError; }

private:
    std::string m_lastError;

    // Builds string table from scene data
    void buildStringTable(const KS3DScene& scene);
    uint32_t addString(const std::string& str);

    // Serializes sections to byte buffer
    std::vector<uint8_t> serializeHeader();
    std::vector<uint8_t> serializeStringTable();
    std::vector<uint8_t> serializeMaterials(const KS3DScene& scene);
    std::vector<uint8_t> serializeMeshes(const KS3DScene& scene);
    std::vector<uint8_t> serializeTextures(const KS3DScene& scene);
    std::vector<uint8_t> serializeNodes(const KS3DScene& scene);

    // String table: index -> (offset, length)
    std::vector<std::pair<uint32_t, uint32_t>> m_stringEntries;
    std::vector<uint8_t> m_stringData;

    // Helpers
    template<typename T>
    static void writeRaw(std::vector<uint8_t>& buf, const T& value);

    static void writeString(std::vector<uint8_t>& buf, const std::string& str);
};

} // namespace ks
