#include "LXOImporter.h"

#include <cstring>
#include <cfloat>

namespace ks {
namespace fileformat {

bool importLXO(const QByteArray& data, MeshData& out, QString* error) {
    if (data.size() < 8) {
        if (error) *error = "LXO data too short";
        return false;
    }

    // Detect magic: "LWO2" or "LXOB" (Modo .lxo uses LWO2-family under the hood)
    char magic[5] = {0};
    memcpy(magic, data.constData(), 4);
    if (!(!strcmp(magic, "LWO2") || !strcmp(magic, "LXOB"))) {
        if (error) *error = QString("Unrecognized LXO magic: %1").arg(QString::fromLatin1(magic));
        return false;
    }

    // Skip magic (4) + version (4) = 8 bytes
    const char* p = data.constData() + 8;
    const char* end = p + (data.size() - 8);

    MeshData md;

    // Helper: advance p past a chunk (4-byte ID + 4-byte LE size + data + even padding)
    auto advancePastChunk = [&]() -> bool {
        if (end - p < 8) return false;
        uint32_t chunkSize;
        memcpy(&chunkSize, p + 4, 4);
        // LWO2 chunks: data size does NOT include padding; total bytes consumed = size + (size & 1)
        size_t consumed = 8 + chunkSize + (chunkSize & 1);
        if (p + consumed > end) return false;
        p += consumed;
        return true;
    };

    // Iterate top-level chunks
    while (p < end) {
        // read chunk ID (4 bytes)
        if (end - p < 4) break;
        char chunkId[5] = {0};
        memcpy(chunkId, p, 4);
        p += 4;

        // read chunk size (LE uint32)
        if (end - p < 4) break;
        uint32_t chunkSize;
        memcpy(&chunkSize, p, 4);
        p += 4;

        const char* chunkData = p;
        const char* chunkEnd = chunkData + chunkSize;

        if (!strcmp(chunkId, "PNTS")) {
            // Points: n * 12 bytes float xyz LE
            if (chunkSize >= 12 && chunkSize % 12 == 0) {
                int n = chunkSize / 12;
                for (int i = 0; i < n; ++i) {
                    float* f = reinterpret_cast<float*>(const_cast<char*>(chunkData) + i * 12);
                    Vertex v;
                    v.position = QVector3D(f[0], f[1], f[2]);
                    md.vertices.append(v);
                }
            }
        } else if (!strcmp(chunkId, "POLS")) {
            // Polygons: first 4 bytes = polygon type (u32 LE). Type 3 = polygon.
            // We assume triangles (3 vertices) for the minimal parser.
            if (chunkSize >= 4) {
                // skip polygon type; vertex indices start after it
                const uint16_t* idx = reinterpret_cast<const uint16_t*>(chunkData + 4);
                int remaining = chunkSize - 4;
                int off = 0;
                // Each polygon: u16 num_vertices, then num_vertices * u16 vertex_indices.
                while (off + 2 <= remaining) {
                    uint16_t nverts = idx[off]; off += 2; // past nverts
                    if (off + nverts * 2 > remaining) break;
                    if (nverts == 3) {
                        Face f;
                        // vertex indices are idx[1], idx[2], idx[3] (after the nverts count at idx[0])
                        f.indices.append(idx[1]);
                        f.indices.append(idx[2]);
                        f.indices.append(idx[3]);
                        md.faces.append(f);
                    }
                    off += nverts * 2;
                }
            }
        } else if (!strcmp(chunkId, "VMAP")) {
            // Vertex map: 4-byte type (4 ASCII, e.g. "TXUV"), 2-byte dim u16,
            // then null-terminated name cstring, then per-vertex: u16 index + dim floats.
            // We only handle type "TXUV" with dim=2 (texture UV).
            if (chunkSize >= 6) {
                char type[5] = {0};
                memcpy(type, chunkData, 4);
                if (!strcmp(type, "TXUV")) {
                    uint16_t dim;
                    memcpy(&dim, chunkData + 4, 2);
                    if (dim == 2) {
                        // skip type(4) + dim(2) = 6 bytes, then read name cstring
                        const char* nameStart = chunkData + 6;
                        const char* nameEnd = nameStart;
                        while (*nameEnd && reinterpret_cast<const char*>(nameEnd) - chunkData + 6 < (int)(chunkData + chunkSize - nameStart))
                            nameEnd++;
                        // skip name + null terminator
                        const char* entryStart = nameEnd + 1;
                        // per-vertex entries: each = u16 vertex_index + 2 floats (u,v)
                        // total bytes after name: chunkSize - (entryStart - chunkData)
                        int remainingBytes = chunkSize - int(entryStart - chunkData);
                        int entryIdx = 0;
                        // Each entry = 2 bytes (u16 index) + dim*4 bytes (floats)
                        while (entryIdx * (2 + dim * 4) + 2 + dim * 4 <= remainingBytes) {
                            uint16_t vidx;
                            memcpy(&vidx, entryStart + entryIdx * (2 + dim * 4), 2);
                            // read u,v floats (next dim*4 bytes)
                            float uvf[2];
                            memcpy(uvf, entryStart + entryIdx * (2 + dim * 4) + 2, 2 * sizeof(float));
                            if (vidx < (unsigned)md.uvs.size()) {
                                md.uvs[vidx] = QVector2D(uvf[0], uvf[1]);
                            } else if (vidx < (unsigned)md.vertices.size()) {
                                while (md.uvs.size() <= vidx) {
                                    md.uvs.append(QVector2D(0, 0));
                                }
                                md.uvs[vidx] = QVector2D(uvf[0], uvf[1]);
                            }
                            entryIdx++;
                        }
                    }
                }
            }
        } else if (!strcmp(chunkId, "BBOX")) {
            // Bounding box: 6 floats LE (xmin,ymin,zmin,xmax,ymax,zmax)
            if (chunkSize >= 24) {
                float b[6];
                memcpy(b, chunkData, 6 * sizeof(float));
                md.boundingBoxMin = QVector3D(b[0], b[1], b[2]);
                md.boundingBoxMax = QVector3D(b[3], b[4], b[5]);
            }
        }

        // Advance p past this chunk
        if (!advancePastChunk()) break;
    }

    // Compute bounding box if we have vertices but no BBOX chunk
    if (!md.vertices.isEmpty() && md.boundingBoxMin.x() == FLT_MAX) {
        md.computeBoundingBox();
    }

    out = md;
    return true;
}
} // namespace fileformat
} // namespace ks