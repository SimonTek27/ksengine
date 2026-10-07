#pragma once
// ksnet — KS Engine reliable-UDP client/server multiplayer transport.
//
// Official network stack for src/simulator (NetworkConfig / NetworkLowLevel /
// NetworkManager). Self-contained: Address, ClientServerConfig, Adapter /
// MessageFactory, Message + serialize_* helpers, Client, Server, NetworkInfo,
// ClientState — over plain UDP with an application-level reliable-ordered
// channel (sliding window + ack bits + retransmit) and an unreliable-
// unordered channel.
//
// Historical note: the public API shape was originally aligned with the
// yojimbo-style message factory pattern; macros are now branded KSNET_*.
//
// Scope notes:
//  - InsecureConnect: cleartext LAN/localhost (no private key).
//  - SecureConnect: 32-byte shared private key; payload after the 13-byte
//    header is XOR-keystream obfuscated; CONNECT carries a key proof so
//    mismatched keys fail at handshake (reject reason 2).
//  - byte-aligned serialization; IPv4 only; same PROTOCOL_ID on both ends.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// windows.h renames SendMessage -> SendMessageA via an object-like macro;
// our API declares Client/Server::SendMessage, so undo it whenever the
// Windows headers happened to be included first.
#ifdef SendMessage
#undef SendMessage
#endif

namespace ksnet {

// ============================================================================
// Address
// ============================================================================

class Address {
public:
    Address() = default;
    Address(const char* host, uint16_t port) : m_host(host ? host : ""), m_port(port) {}

    const std::string& host() const { return m_host; }
    uint16_t port() const { return m_port; }

private:
    std::string m_host;
    uint16_t m_port = 0;
};

// ============================================================================
// Allocator / factory plumbing (plain new/delete underneath)
// ============================================================================

class Allocator {
public:
    virtual ~Allocator() = default;
};

class DefaultAllocator : public Allocator {};

class Message;
class ReadStream;
class WriteStream;

class MessageFactory {
public:
    MessageFactory(Allocator& /*allocator*/, int numTypes) : m_numTypes(numTypes) {}
    virtual ~MessageFactory() = default;
    virtual Message* Create(int type) = 0;

protected:
    int m_numTypes;
};

// ============================================================================
// Serialization streams + helpers (byte-aligned; both stream kinds expose the
// same isReading/read/write surface so one template Serialize() serves both)
// ============================================================================

class WriteStream {
public:
    explicit WriteStream(std::vector<uint8_t>& out) : m_out(out) {}
    bool isReading() const { return false; }
    void writeBytes(const void* data, size_t n)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        m_out.insert(m_out.end(), p, p + n);
    }
    bool readBytes(void*, size_t) { return false; }

private:
    std::vector<uint8_t>& m_out;
};

class ReadStream {
public:
    ReadStream(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    bool isReading() const { return true; }
    void writeBytes(const void*, size_t) {}
    bool readBytes(void* out, size_t n)
    {
        if (m_pos + n > m_size) {
            m_overflow = true;
            return false;
        }
        std::memcpy(out, m_data + m_pos, n);
        m_pos += n;
        return true;
    }
    bool ok() const { return !m_overflow; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
    bool m_overflow = false;
};

namespace detail {

inline uint64_t maskForBits(int bits)
{
    return bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
}

template <typename S>
void packBytes(S& s, const uint8_t* src, size_t n)
{
    if (s.isReading())
        s.readBytes(const_cast<uint8_t*>(src), n);
    else
        s.writeBytes(src, n);
}

} // namespace detail

template <typename S, typename T>
void serialize_bits(S& s, T& value, int bits)
{
    const size_t n = static_cast<size_t>((bits + 7) / 8);
    uint8_t tmp[8] = {};
    if (s.isReading()) {
        if (!s.readBytes(tmp, n)) return;
        uint64_t raw = 0;
        std::memcpy(&raw, tmp, n);
        value = static_cast<T>(raw & detail::maskForBits(bits));
    } else {
        uint64_t raw = static_cast<uint64_t>(static_cast<typename std::make_unsigned<T>::type>(value)) &
                       detail::maskForBits(bits);
        std::memcpy(tmp, &raw, n);
        s.writeBytes(tmp, n);
    }
}

template <typename S>
void serialize_uint32(S& s, uint32_t& value)
{
    serialize_bits(s, value, 32);
}

template <typename S>
void serialize_int(S& s, int& value, int min, int max)
{
    uint32_t raw = 0;
    if (s.isReading()) {
        serialize_uint32(s, raw);
        const uint32_t range = static_cast<uint32_t>(max - min);
        if (range == 0) {
            value = min;
            return;
        }
        const uint32_t v = raw % (range + 1);
        value = min + static_cast<int>(v);
    } else {
        raw = static_cast<uint32_t>(value - min);
        serialize_uint32(s, raw);
    }
}

template <typename S>
void serialize_bool(S& s, bool& value)
{
    uint8_t b = value ? 1 : 0;
    if (s.isReading()) {
        serialize_bits(s, b, 8);
        value = b != 0;
    } else {
        uint8_t w = b;
        serialize_bits(s, w, 8);
    }
}

template <typename S>
void serialize_double(S& s, double& value)
{
    uint64_t bits = 0;
    if (s.isReading()) {
        serialize_bits(s, bits, 64);
        std::memcpy(&value, &bits, sizeof(value));
    } else {
        std::memcpy(&bits, &value, sizeof(bits));
        serialize_bits(s, bits, 64);
    }
}

template <typename S>
void serialize_string(S& s, char* str, size_t maxLength)
{
    uint32_t len = 0;
    if (s.isReading()) {
        serialize_uint32(s, len);
        if (len >= maxLength) len = static_cast<uint32_t>(maxLength ? maxLength - 1 : 0);
        if (len > 0) s.readBytes(str, len);
        str[len] = '\0';
    } else {
        len = static_cast<uint32_t>(std::strlen(str));
        if (len >= maxLength) len = static_cast<uint32_t>(maxLength ? maxLength - 1 : 0);
        serialize_uint32(s, len);
        if (len > 0) s.writeBytes(str, len);
    }
}

// Quantize to `precision` steps in [min, max]. Always stored as u32 — payloads
// here are small, alignment is worth more than the 2 saved bytes.
template <typename S>
void serialize_compressed_float(S& s, float& value, float min, float max, float precision)
{
    const float stepsF = (max - min) / (precision > 0.0f ? precision : 1.0f);
    const uint32_t steps = stepsF > 0.0f ? static_cast<uint32_t>(stepsF) : 0u;
    uint32_t q = 0;
    if (s.isReading()) {
        serialize_uint32(s, q);
        if (steps > 0 && q > steps) q = steps;
        value = min + static_cast<float>(q) * precision;
        if (value > max) value = max;
    } else {
        float norm = (value - min) / (precision > 0.0f ? precision : 1.0f);
        float r = norm + 0.5f;
        if (r < 0.0f) r = 0.0f;
        long qq = static_cast<long>(r);
        if (steps > 0 && qq > static_cast<long>(steps)) qq = static_cast<long>(steps);
        q = static_cast<uint32_t>(qq);
        serialize_uint32(s, q);
    }
}

// ============================================================================
// Message
// ============================================================================

class Message {
public:
    virtual ~Message() = default;
    int GetType() const { return m_type; }
    void setType(int type) { m_type = type; }

    virtual bool Serialize(ReadStream& stream) = 0;
    virtual bool Serialize(WriteStream& stream) = 0;

private:
    int m_type = -1;
};

class Adapter {
public:
    virtual ~Adapter() = default;
    virtual MessageFactory* CreateMessageFactory(Allocator& allocator) = 0;
};

// ============================================================================
// Config / state / info
// ============================================================================

enum ChannelType {
    CHANNEL_TYPE_UNRELIABLE_UNORDERED = 0,
    CHANNEL_TYPE_RELIABLE_ORDERED = 1,
};

constexpr int MAX_CHANNELS = 8;
/** Shared private key size for SecureConnect / Server::SetPrivateKey. */
constexpr int PRIVATE_KEY_BYTES = 32;

struct ChannelConfig {
    ChannelType type = CHANNEL_TYPE_UNRELIABLE_UNORDERED;
    int packetBudget = 1024;
};

struct ClientServerConfig {
    uint64_t protocolId = 0;
    int numChannels = 2;
    ChannelConfig channel[MAX_CHANNELS];
};

enum ClientState {
    CLIENT_STATE_DISCONNECTED = 0,
    CLIENT_STATE_CONNECTING,
    CLIENT_STATE_CONNECTED,
    CLIENT_STATE_ERROR,
};

struct NetworkInfo {
    float RTT = 0.0f;           // seconds
    float packetLoss = 0.0f;    // fraction 0..1
    float sentBandwidth = 0.0f; // bytes/sec
    float receivedBandwidth = 0.0f;
};

// ============================================================================
// Client / Server
// ============================================================================

class Client {
public:
    Client(Allocator& allocator, const Address& clientAddress,
           const ClientServerConfig& config, Adapter& adapter, double timeout);
    ~Client();

    /** Cleartext connect (LAN). privateKey/keyLength ignored; kept for API shape. */
    bool InsecureConnect(const char* privateKey, int keyLength, const Address& serverAddress);
    /**
     * Shared-secret connect. Requires exactly PRIVATE_KEY_BYTES (32) key bytes
     * matching the server SetPrivateKey. Enables payload obfuscation + CONNECT proof.
     */
    bool SecureConnect(const uint8_t* privateKey, int keyLength, const Address& serverAddress);
    void Disconnect();
    bool isSecure() const;

    ClientState GetClientState() const { return m_state; }

    void SendPackets();
    void ReceivePackets();

    Message* CreateMessage(int type);
    void SendMessage(int channelIndex, Message* message);
    Message* ReceiveMessage(int channelIndex);
    void ReleaseMessage(Message* message);

    void GetNetworkInfo(NetworkInfo& info) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    ClientState m_state = CLIENT_STATE_DISCONNECTED;
};

class Server {
public:
    Server(Allocator& allocator, const Address* privateAddress,
           const Address& publicAddress, const ClientServerConfig& config,
           Adapter& adapter, double timeout);
    ~Server();

    /**
     * Enable secure mode with a 32-byte shared private key (must match client).
     * Call before Start(). Empty/null clears secure mode (InsecureConnect clients only).
     */
    bool SetPrivateKey(const uint8_t* privateKey, int keyLength);
    void ClearPrivateKey();
    bool isSecure() const;

    bool Start(int maxClients);
    void Stop();

    void SendPackets();
    void ReceivePackets();

    int GetMaxClients() const;
    bool IsClientConnected(int clientIndex) const;

    Message* CreateMessage(int clientIndex, int type);
    void SendMessage(int clientIndex, int channelIndex, Message* message);
    bool CanSendMessage(int clientIndex, int channelIndex) const;
    Message* ReceiveMessage(int clientIndex, int channelIndex);
    void ReleaseMessage(int clientIndex, Message* message);

    /** Force-disconnect a client slot (sends DISCONNECT, frees slot). */
    void DisconnectClient(int clientIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ksnet

// ============================================================================
// Message factory / serialize macros (used by NetworkConfig.h)
// ============================================================================

#define KSNET_VIRTUAL_SERIALIZE_FUNCTIONS()                                                        \
    bool Serialize(ksnet::ReadStream& stream) { return Serialize<ksnet::ReadStream>(stream); }     \
    bool Serialize(ksnet::WriteStream& stream) { return Serialize<ksnet::WriteStream>(stream); }

#define KSNET_NEW(allocator, Type, ...) (new Type(__VA_ARGS__))
#define KSNET_DELETE(allocator, pointer) (delete (pointer))

#define KSNET_MESSAGE_FACTORY_START(ClassName, numTypes)                                           \
    class ClassName : public ksnet::MessageFactory {                                               \
    public:                                                                                        \
        ClassName(ksnet::Allocator& allocator) : ksnet::MessageFactory(allocator, numTypes) {}     \
        ksnet::Message* Create(int type) override {                                                \
            switch (type) {

#define KSNET_DECLARE_MESSAGE_TYPE(messageConstant, MessageType_)                                  \
    case messageConstant: {                                                                        \
        auto* msg = new MessageType_();                                                            \
        msg->setType(messageConstant);                                                             \
        return msg;                                                                                \
    }

#define KSNET_MESSAGE_FACTORY_FINISH()                                                             \
    default:                                                                                       \
        return nullptr;                                                                            \
    }                                                                                              \
    }                                                                                              \
    };

// Deprecated aliases (remove after callers migrate fully)
#ifndef KSNET_NO_YOJIMBO_ALIASES
#define YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS KSNET_VIRTUAL_SERIALIZE_FUNCTIONS
#define YOJIMBO_NEW KSNET_NEW
#define YOJIMBO_DELETE KSNET_DELETE
#define YOJIMBO_MESSAGE_FACTORY_START KSNET_MESSAGE_FACTORY_START
#define YOJIMBO_DECLARE_MESSAGE_TYPE KSNET_DECLARE_MESSAGE_TYPE
#define YOJIMBO_MESSAGE_FACTORY_FINISH KSNET_MESSAGE_FACTORY_FINISH
#endif
