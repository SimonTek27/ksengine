/**
 * NetworkLowLevel — HAS_KSNET client/server (complete handlers).
 * Bodies live in NetworkLowLevel_*.inc for maintainability: this file only
 * pulls the headers, opens the guard/namespace and stitches the parts back.
 */

#include "NetworkLowLevel.h"
#include <cstring>

#if HAS_KSNET

#include "SimulationLoop.h"
#include "InputManager.h"
#include "MultiCarManager.h"
#include "engine/physics/VehiclePhysics.h"

// windows.h (via the includes above) renames SendMessage to SendMessageA;
// our ksnet calls use the real method name.
#ifdef SendMessage
#undef SendMessage
#endif

namespace ks::sim::net {

#include "NetworkLowLevel_Client.inc"
#include "NetworkLowLevel_ServerA.inc"
#include "NetworkLowLevel_ServerB.inc"

} // namespace ks::sim::net

#endif
