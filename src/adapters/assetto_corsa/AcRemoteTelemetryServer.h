#pragma once
/**
 * AcRemoteTelemetryServer — AC-compatible remote telemetry (UDP) stub.
 *
 * SimulationLoop holds std::unique_ptr<AcRemoteTelemetryServer>; the type must
 * be complete in any TU that destroys SimulationLoop. A full server can replace
 * this header later without changing the SimulationLoop.h surface.
 *
 * Port default matches SimulationLoop::m_acRemotePort (9996).
 */
#include <cstdint>
#include <cstddef>
#include <string>

namespace ks {
namespace ac {

class AcRemoteTelemetryServer {
public:
    AcRemoteTelemetryServer() = default;
    ~AcRemoteTelemetryServer() { stop(); }

    AcRemoteTelemetryServer(const AcRemoteTelemetryServer&) = delete;
    AcRemoteTelemetryServer& operator=(const AcRemoteTelemetryServer&) = delete;

    /** Bind / start listening. Stub always succeeds. */
    bool start(uint16_t port = 9996) {
        m_port = port;
        m_running = true;
        return true;
    }

    void stop() { m_running = false; }
    bool isRunning() const { return m_running; }
    uint16_t port() const { return m_port; }

    /**
     * Optional payload push. Stub is a no-op so call sites can be wired
     * without pulling a real network dependency into unit tests.
     */
    void publishJson(const char* /*json_utf8*/) {}
    void publishBinary(const void* /*data*/, size_t /*bytes*/) {}

private:
    uint16_t m_port = 9996;
    bool m_running = false;
};

} // namespace ac
} // namespace ks
