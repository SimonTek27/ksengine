#pragma once

#if defined(HAS_KSNET) && HAS_KSNET
#include <ksnet.h>
#endif

#if HAS_KSNET

#include <cstdint>
#include <cstring>

namespace ks::sim::net {

constexpr uint64_t PROTOCOL_ID = 0x11223344556677ULL;
constexpr uint32_t PROTOCOL_VERSION = 2;
constexpr int MAX_CLIENTS = 32;

enum ChannelId {
    CHANNEL_UNRELIABLE = 0,
    CHANNEL_RELIABLE = 1,
    NUM_CHANNELS = 2
};

enum MessageType {
    MSG_CLIENT_JOIN = 0,
    MSG_SERVER_WELCOME,
    MSG_SERVER_FULL,
    MSG_PROTOCOL_MISMATCH,
    MSG_SESSION_STATE,
    MSG_RACE_COUNTDOWN,
    MSG_CAR_STATE,
    MSG_CAR_STATE_BATCH,
    MSG_CAR_SPAWN,
    MSG_CAR_DESPAWN,
    MSG_CAR_COLLISION,
    MSG_CAR_DAMAGE,
    MSG_PLAYER_INPUT,
    MSG_LAP_TIME,
    MSG_PENALTY,
    MSG_CHAT,
    MSG_CAR_SETUP,
    MSG_AUTH_FAILED,
    NUM_MESSAGE_TYPES
};

enum SessionType : uint8_t {
    SESSION_PRACTICE = 0,
    SESSION_QUALIFYING,
    SESSION_RACE,
    SESSION_TIME_TRIAL,
    SESSION_HOTLAP
};

enum SessionPhase : uint8_t {
    PHASE_WAITING = 0,
    PHASE_COUNTDOWN,
    PHASE_GREEN_FLAG,
    PHASE_RED_FLAG,
    PHASE_CHECKERED_FLAG,
    PHASE_FINISHED
};

struct InputData {
    uint32_t frameNumber = 0;
    double timestamp = 0.0;
    float throttle = 0;
    float brake = 0;
    float steering = 0;
    float clutch = 0;
    bool handbrake = false;
    bool drs = false;
    int gearShift = 0;
};

struct CarStateData {
    uint32_t carId = 0;
    uint32_t frameNumber = 0;
    float posX = 0, posY = 0, posZ = 0;
    float rotX = 0, rotY = 0, rotZ = 0;
    float velX = 0, velY = 0, velZ = 0;
    float speed = 0;
    float rpm = 0;
    int gear = 1;
    float throttle = 0;
    float brake = 0;
    float steering = 0;
};

struct NetworkStats {
    float rtt = 0;
    float packetLoss = 0;
    float sendBandwidth = 0;
    float recvBandwidth = 0;
};

struct ClientJoinMessage : public ksnet::Message {
    char driverName[64] = {0};
    char carName[64] = {0};
    uint32_t clientVersion = PROTOCOL_VERSION;
    char authToken[64] = {0};
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_string(stream, driverName, sizeof(driverName));
        serialize_string(stream, carName, sizeof(carName));
        serialize_uint32(stream, clientVersion);
        serialize_string(stream, authToken, sizeof(authToken));
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct AuthFailedMessage : public ksnet::Message {
    char reason[64] = {0};
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_string(stream, reason, sizeof(reason));
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct ServerWelcomeMessage : public ksnet::Message {
    uint32_t clientId = 0;
    char serverName[64] = {0};
    char trackName[64] = {0};
    uint8_t sessionType = SESSION_PRACTICE;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, clientId);
        serialize_string(stream, serverName, sizeof(serverName));
        serialize_string(stream, trackName, sizeof(trackName));
        serialize_bits(stream, sessionType, 8);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct ServerFullMessage : public ksnet::Message {
    template <typename Stream> bool Serialize(Stream &) { return true; }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct ProtocolMismatchMessage : public ksnet::Message {
    uint32_t serverVersion = PROTOCOL_VERSION;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, serverVersion);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct SessionStateMessage : public ksnet::Message {
    uint8_t type = SESSION_PRACTICE;
    uint8_t phase = PHASE_WAITING;
    int currentLap = 0;
    int totalLaps = 0;
    double timeRemaining = 0;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_bits(stream, type, 8);
        serialize_bits(stream, phase, 8);
        serialize_int(stream, currentLap, 0, 10000);
        serialize_int(stream, totalLaps, 0, 10000);
        serialize_double(stream, timeRemaining);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct RaceCountdownMessage : public ksnet::Message {
    int value = 0;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_int(stream, value, 0, 10);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct CarStateMessage : public ksnet::Message {
    CarStateData data;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, data.carId);
        serialize_uint32(stream, data.frameNumber);
        serialize_compressed_float(stream, data.posX, -10000.0f, 10000.0f, 0.01f);
        serialize_compressed_float(stream, data.posY, -1000.0f, 1000.0f, 0.01f);
        serialize_compressed_float(stream, data.posZ, -10000.0f, 10000.0f, 0.01f);
        serialize_compressed_float(stream, data.rotX, -3.14159f, 3.14159f, 0.0001745f);
        serialize_compressed_float(stream, data.rotY, -3.14159f, 3.14159f, 0.0001745f);
        serialize_compressed_float(stream, data.rotZ, -3.14159f, 3.14159f, 0.0001745f);
        serialize_compressed_float(stream, data.velX, -100.0f, 100.0f, 0.01f);
        serialize_compressed_float(stream, data.velY, -50.0f, 50.0f, 0.01f);
        serialize_compressed_float(stream, data.velZ, -100.0f, 100.0f, 0.01f);
        serialize_compressed_float(stream, data.speed, 0.0f, 500.0f, 0.01f);
        serialize_compressed_float(stream, data.rpm, 0.0f, 20000.0f, 1.0f);
        serialize_int(stream, data.gear, -1, 12);
        serialize_compressed_float(stream, data.throttle, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, data.brake, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, data.steering, -1.0f, 1.0f, 0.001f);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct CarSpawnMessage : public ksnet::Message {
    uint32_t carId = 0;
    uint32_t clientId = 0;
    char driverName[64] = {0};
    char carName[64] = {0};
    float posX = 0, posY = 0, posZ = 0;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carId);
        serialize_uint32(stream, clientId);
        serialize_string(stream, driverName, sizeof(driverName));
        serialize_string(stream, carName, sizeof(carName));
        serialize_compressed_float(stream, posX, -10000.0f, 10000.0f, 0.01f);
        serialize_compressed_float(stream, posY, -1000.0f, 1000.0f, 0.01f);
        serialize_compressed_float(stream, posZ, -10000.0f, 10000.0f, 0.01f);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct CarDespawnMessage : public ksnet::Message {
    uint32_t carId = 0;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carId);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct PlayerInputMessage : public ksnet::Message {
    InputData data;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, data.frameNumber);
        serialize_double(stream, data.timestamp);
        serialize_compressed_float(stream, data.throttle, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, data.brake, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, data.steering, -1.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, data.clutch, 0.0f, 1.0f, 0.001f);
        serialize_bool(stream, data.handbrake);
        serialize_bool(stream, data.drs);
        serialize_int(stream, data.gearShift, -1, 1);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct LapTimeMessage : public ksnet::Message {
    uint32_t carId = 0;
    uint32_t lapNumber = 0;
    double lapTime = 0;
    double sector1 = 0, sector2 = 0, sector3 = 0;
    bool isValid = true;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carId);
        serialize_uint32(stream, lapNumber);
        serialize_double(stream, lapTime);
        serialize_double(stream, sector1);
        serialize_double(stream, sector2);
        serialize_double(stream, sector3);
        serialize_bool(stream, isValid);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct ChatMessage : public ksnet::Message {
    uint32_t senderId = 0;
    char senderName[64] = {0};
    char message[256] = {0};
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, senderId);
        serialize_string(stream, senderName, sizeof(senderName));
        serialize_string(stream, message, sizeof(message));
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct PenaltyMessage : public ksnet::Message {
    uint32_t carId = 0;
    uint8_t penaltyType = 0;
    float value = 0;
    char reason[128] = {0};
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carId);
        serialize_bits(stream, penaltyType, 8);
        serialize_compressed_float(stream, value, 0.0f, 3600.0f, 0.1f);
        serialize_string(stream, reason, sizeof(reason));
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

#include "NetworkDamageSetupMessages.inl"

YOJIMBO_MESSAGE_FACTORY_START(GameMessageFactory, NUM_MESSAGE_TYPES);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CLIENT_JOIN, ClientJoinMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_SERVER_WELCOME, ServerWelcomeMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_SERVER_FULL, ServerFullMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_PROTOCOL_MISMATCH, ProtocolMismatchMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_SESSION_STATE, SessionStateMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_RACE_COUNTDOWN, RaceCountdownMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CAR_STATE, CarStateMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CAR_SPAWN, CarSpawnMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CAR_DESPAWN, CarDespawnMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CAR_COLLISION, CarCollisionMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CAR_DAMAGE, CarDamageMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_PLAYER_INPUT, PlayerInputMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_LAP_TIME, LapTimeMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CHAT, ChatMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_PENALTY, PenaltyMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_CAR_SETUP, CarSetupMessage);
YOJIMBO_DECLARE_MESSAGE_TYPE(MSG_AUTH_FAILED, AuthFailedMessage);
YOJIMBO_MESSAGE_FACTORY_FINISH();

class GameAdapter : public ksnet::Adapter {
public:
    ksnet::MessageFactory * CreateMessageFactory(ksnet::Allocator & allocator) override {
        return YOJIMBO_NEW(allocator, GameMessageFactory, allocator);
    }
};

} // namespace ks::sim::net

#else

namespace ks::sim::net {}

#endif
