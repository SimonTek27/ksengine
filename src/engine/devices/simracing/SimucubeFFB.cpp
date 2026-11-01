#include "SimucubeFFB.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

namespace ks::device {

namespace {

// The descriptor is kept in a void* member so the object layout is the same on
// both platforms: Windows SOCKET is pointer-sized, while a POSIX descriptor is
// a plain int and cannot be narrowed to it on LP64.
#ifdef _WIN32
using socket_handle_t = SOCKET;
inline socket_handle_t toHandle(void* s) { return reinterpret_cast<socket_handle_t>(s); }
inline void* toStorage(socket_handle_t s) { return reinterpret_cast<void*>(s); }
#else
using socket_handle_t = int;
inline socket_handle_t toHandle(void* s) {
    return static_cast<socket_handle_t>(reinterpret_cast<std::intptr_t>(s));
}
inline void* toStorage(socket_handle_t s) {
    return reinterpret_cast<void*>(static_cast<std::intptr_t>(s));
}
#endif

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}
} // namespace

float SimucubeFFB::maxTorqueForModel(WheelModel model) {
    switch (model) {
        case WheelModel::Simucube1:         return 15.0f;
        case WheelModel::Simucube2Pro:      return 25.0f;
        case WheelModel::Simucube2Ultimate: return 32.0f;
        default: return 20.0f;
    }
}

SimucubeFFB::SimucubeFFB() = default;

SimucubeFFB::~SimucubeFFB() {
    shutdown();
}

bool SimucubeFFB::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::fprintf(stderr, "SimucubeFFB: Initializing (TrueDrive UDP)\n");

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::fprintf(stderr, "SimucubeFFB: WSAStartup failed\n");
        return false;
    }
#endif

    m_udpSocket = toStorage(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (toHandle(m_udpSocket) == INVALID_SOCKET) {
        std::fprintf(stderr, "SimucubeFFB: Socket creation failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    int timeout = 1000;
#ifdef _WIN32
    setsockopt(toHandle(m_udpSocket), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(toHandle(m_udpSocket), SOL_SOCKET, SO_RCVTIMEO,
               &tv, sizeof(tv));
#endif

    if (m_targetIP.empty()) {
        if (!discoverDevice()) {
            std::fprintf(stderr, "SimucubeFFB: No Simucube found on network\n");
            shutdown();
            return false;
        }
    }

    m_connected = true;
    std::fprintf(stderr, "SimucubeFFB: Connected to %s:%u (%s, %.1f Nm)\n",
                 m_targetIP.c_str(), static_cast<unsigned>(m_targetPort),
                 modelName().c_str(), maxTorqueNm());
    return true;
}

bool SimucubeFFB::discoverDevice() {
    uint8_t queryPacket[8] = {};
    queryPacket[0] = CMD_QUERY;
    queryPacket[1] = 0x00;
    queryPacket[2] = 0x01;

    const char* tryIPs[] = {
        "192.168.1.100", "192.168.1.101",
        "192.168.0.100", "192.168.0.101",
        "10.0.0.100",
    };

    for (const char* ip : tryIPs) {
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(TRUEDRIVE_DEFAULT_PORT);
        addr.sin_addr.s_addr = inet_addr(ip);

        sendto(toHandle(m_udpSocket),
               reinterpret_cast<const char*>(queryPacket), sizeof(queryPacket),
               0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

        uint8_t response[64] = {};
        struct sockaddr_in fromAddr;
#ifdef _WIN32
        int fromLen = sizeof(fromAddr);
#else
        socklen_t fromLen = sizeof(fromAddr);
#endif
        int received = recvfrom(toHandle(m_udpSocket),
                                reinterpret_cast<char*>(response), sizeof(response),
                                0, reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);

        if (received > 0 && response[0] == CMD_QUERY) {
            switch (response[1]) {
                case 1: m_model = WheelModel::Simucube1; break;
                case 2: m_model = WheelModel::Simucube2Pro; break;
                case 3: m_model = WheelModel::Simucube2Ultimate; break;
                default: m_model = WheelModel::Unknown; break;
            }
            m_targetIP = ip;
            m_targetPort = TRUEDRIVE_DEFAULT_PORT;
            std::fprintf(stderr, "SimucubeFFB: Discovered at %s - Model: %s\n",
                         ip, modelName().c_str());
            return true;
        }
    }
    return false;
}

bool SimucubeFFB::sendTrueDriveCommand(const uint8_t* data, size_t len) {
    if (!m_connected || m_targetIP.empty()) return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_targetPort);
    addr.sin_addr.s_addr = inet_addr(m_targetIP.c_str());

    int sent = sendto(toHandle(m_udpSocket),
                      reinterpret_cast<const char*>(data), static_cast<int>(len),
                      0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(len);
}

bool SimucubeFFB::sendTorqueCommand(float torqueNm) {
    float maxNm = maxTorqueNm();
    float clampedTorque = clampf(torqueNm, -maxNm, maxNm);

    uint8_t packet[8] = {};
    packet[0] = CMD_TORQUE;
    uint32_t torqueBits;
    std::memcpy(&torqueBits, &clampedTorque, sizeof(float));
    packet[1] = static_cast<uint8_t>(torqueBits & 0xFF);
    packet[2] = static_cast<uint8_t>((torqueBits >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>((torqueBits >> 16) & 0xFF);
    packet[4] = static_cast<uint8_t>((torqueBits >> 24) & 0xFF);
    packet[5] = 0x00;
    return sendTrueDriveCommand(packet, sizeof(packet));
}

void SimucubeFFB::processUDPResponse() {
    if (!m_connected || !m_udpSocket) return;
    uint8_t response[64] = {};
    int received = recvfrom(toHandle(m_udpSocket),
                            reinterpret_cast<char*>(response), sizeof(response),
                            0, nullptr, nullptr);
    if (received > 0 && response[0] == 0x10) {
        if (response[1] & 0x01)
            std::fprintf(stderr, "SimucubeFFB: Emergency stop active!\n");
    }
}

void SimucubeFFB::updateFFB(float torqueNm) {
    if (!m_connected) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastTorque = torqueNm;
    sendTorqueCommand(torqueNm);
}

void SimucubeFFB::setConstantForce(float magnitude) {
    if (!m_connected) return;
    updateFFB(magnitude * maxTorqueNm());
}

void SimucubeFFB::setSpringForce(float center, float stiffness, float damping) {
    if (!m_connected) return;
    uint8_t packet[8] = {};
    packet[0] = 0x11;
    packet[1] = static_cast<uint8_t>(clampf(center, 0.f, 1.f) * 255);
    packet[2] = static_cast<uint8_t>(clampf(stiffness, 0.f, 1.f) * 255);
    packet[3] = static_cast<uint8_t>(clampf(damping, 0.f, 1.f) * 255);
    sendTrueDriveCommand(packet, sizeof(packet));
}

void SimucubeFFB::setDamperForce(float /*velocity*/, float coefficient) {
    if (!m_connected) return;
    uint8_t packet[8] = {};
    packet[0] = 0x12;
    packet[1] = static_cast<uint8_t>(clampf(coefficient, 0.f, 1.f) * 255);
    sendTrueDriveCommand(packet, sizeof(packet));
}

void SimucubeFFB::setFrictionForce(float coefficient) {
    if (!m_connected) return;
    uint8_t packet[8] = {};
    packet[0] = 0x13;
    packet[1] = static_cast<uint8_t>(clampf(coefficient, 0.f, 1.f) * 255);
    sendTrueDriveCommand(packet, sizeof(packet));
}

void SimucubeFFB::setRumble(float /*strongMotor*/, float /*weakMotor*/) {
    // Direct-drive: no rumble motors
}

void SimucubeFFB::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_connected)
        sendTorqueCommand(0.0f);
    if (m_udpSocket != nullptr) {
        closesocket(toHandle(m_udpSocket));
        m_udpSocket = nullptr;
    }
#ifdef _WIN32
    WSACleanup();
#endif
    m_connected = false;
    m_model = WheelModel::Unknown;
    m_targetIP.clear();
    m_targetPort = 0;
}

std::string SimucubeFFB::modelName() const {
    switch (m_model) {
        case WheelModel::Simucube1:         return "Simucube 1";
        case WheelModel::Simucube2Pro:      return "Simucube 2 Pro";
        case WheelModel::Simucube2Ultimate: return "Simucube 2 Ultimate";
        default: return "Simucube (Unknown)";
    }
}

float SimucubeFFB::maxTorqueNm() const {
    return maxTorqueForModel(m_model);
}

} // namespace ks::device
