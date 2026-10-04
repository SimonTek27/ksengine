#pragma once

#include <QString>
#include <QVector3D>
#include <QVector2D>

#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {
namespace fileformat {

/// Parse a Grasshopper (.gh/.ghx) definition file.
/// Handles: geometry components, attributes, basic geometry generation.
/// Returns false on failure; optional error out.
bool importGrasshopperDefinition(const QByteArray& data, ks::MeshData& out, QString* error = nullptr);

} // namespace fileformat
} // namespace ks