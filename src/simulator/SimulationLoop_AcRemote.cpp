/**
 * SimulationLoop_AcRemote.cpp — optional AC remote telemetry publish path.
 * Completes the declaration in SimulationLoop.h; the server type is the
 * header-only stub in adapters/assetto_corsa/AcRemoteTelemetryServer.h.
 */
#include "SimulationLoop.h"
#include "adapters/assetto_corsa/AcRemoteTelemetryServer.h"

namespace ks {
namespace sim {

void SimulationLoop::publishAcRemoteTelemetry() {
    if (!m_acRemoteEnabled) return;
    if (!m_acRemote) {
        m_acRemote = std::make_unique<ks::ac::AcRemoteTelemetryServer>();
        m_acRemote->start(m_acRemotePort);
    }
    if (!m_acRemote->isRunning()) return;
    // Stub: no wire protocol yet. Keeps the unique_ptr path live and the
    // linker symbol defined so future UDP payload work has a call site.
    (void)m_acRemote->port();
}

} // namespace sim
} // namespace ks
