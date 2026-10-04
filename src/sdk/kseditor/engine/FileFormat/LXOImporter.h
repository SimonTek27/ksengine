#pragma once

#include <QString>
#include <QVector3D>
#include <QVector2D>

#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {
namespace fileformat {

/// Parse a LightWave 3D / Modo .lxo (LWO2-family) binary object into a MeshData.
/// Handles: PNTS (positions), POLS (triangles, type-3 polygons), VMAP TXUV (UVs),
/// BBOX (bounding box).  Returns false on failure; optional error out.
bool importLXO(const QByteArray& data, ks::MeshData& out, QString* error = nullptr);

} // namespace fileformat
} // namespace ks