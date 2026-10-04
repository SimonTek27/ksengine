#pragma once

#include <QString>
#include <QVector3D>
#include <QVector2D>

#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {
namespace fileformat {

/// Parse a Softimage .scn scene file (ASCII format).
/// Handles: mesh polygons, materials, basic animation.
/// Returns false on failure; optional error out.
bool importXSIScene(const QByteArray& data, ks::MeshData& out, QString* error = nullptr);

/// Parse a Softimage .exp (export) file.
bool importXSIExport(const QByteArray& data, ks::MeshData& out, QString* error = nullptr);

/// Parse a Softimage .emdl (emodel) file.
bool importXSIEmodel(const QByteArray& data, ks::MeshData& out, QString* error = nullptr);

} // namespace fileformat
} // namespace ks
