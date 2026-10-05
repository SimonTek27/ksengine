/**
 * SimulationLoop.cpp - std-only / Qt-free
 * 1 kHz physics + LapSectorTimer + SM + UDP + TCP telemetry
 * body_0 of 3 — partial; cmake restore or tools/restore_simloop.sh preferred for full file
 */

#include "SimulationLoop.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "NetworkManager.h"
#include "SetupGarage.h"
#include "SimulatorAudio.h"
#include "ui/UiGpuPass.h"
#include "ui/RaceTelemetryHud.h"
#include "UdpTelemetryBridge.h"
#include "TcpTelemetryBridge.h"

#include "engine/Engine.h"
#include "engine/Math/MathTypesFree.h"
#include "engine/scene/Components.h"
#include "engine/scene/SceneModule.h"
#include "engine/Graphics/RenderSystem.h"
#include "engine/devices/InputSystem.h"
#include "engine/Scripting/ScriptModule.h"
#include "engine/Scripting/ModSdk.h"
#include "engine/terrain/TerrainMesh.h"
#include "engine/terrain/TerrainHeightmap.h"
#include "engine/FileFormat/PngReader.h"

#include "adapters/assetto_corsa/AcSharedMemoryPublisher.h"
#include "adapters/assetto_corsa/AcSurfacesLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>

#ifndef HAS_VEHICLE_SIM
#define HAS_VEHICLE_SIM 1
#endif
#ifndef HAS_KSNET
#define HAS_KSNET 0
#endif
#ifndef HAS_FFB
#define HAS_FFB 1
#endif

/* NOTE: Full SimulationLoop.cpp is restored by:
 *   bash tools/restore_simloop.sh
 * or cmake/restore_simloop.cmake on configure.
 * This partial include is a placeholder during restore.
 */
