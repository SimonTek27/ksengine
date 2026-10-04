#pragma once

#include <QString>
#include <QVector3D>

#include "sdk/kseditor/engine/Graphics/SceneObject.h"

namespace ks {

// Read/write a scalar animateable parameter of a SceneObject by channel name.
// Supported channels (same strings the FCurve system uses):
//   position.x|y|z  rotation.x|y|z (degrees)  scale.x|y|z
//   visibility  opacity  metallic  roughness

bool sceneParamRead(SceneObject* obj, const QString& channel, float& outValue);
bool sceneParamWrite(SceneObject* obj, const QString& channel, float value);

} // namespace ks