// ksnet implementation — reliable-UDP client/server transport (KS Engine).
// Wire format (little-endian, all integers):
//   header: u64 protocolId | u8 packetType | u32 nonce
//     nonce = the *client's* random nonce in both directions (the server
//     echoes it, the client validates it; the server validates it against
//     the slot the address was mapped to on CONNECT).
//   CONNECT:             [secure] 8-byte key proof (cleartext; not XOR'd)
//   CONNECT_ACK:         (no payload)
//   CONNECT_REJECT:      u8 reason (1 = server full, 2 = key mismatch)
//   DISCONNECT:          (no payload)
//   PING / PONG:         u32 pingId
//   DATA:                u8 numReliableChannels
//                        per reliable channel: u8 hasAck | u16 ackSeq | u64 ackBits
//                        u8 frameCount
//                        per frame: u8 channel | u8 msgType
//                                   [u16 seq if channel is reliable]
//                                   u16 payloadLen | payload
// When Server::SetPrivateKey / Client::SecureConnect is active, every packet's
// payload *after* the 13-byte header is XOR-keystream obfuscated (except the
// CONNECT proof itself, which is the handshake authenticator).
// Reliable channel: sliding window of 64 (u16 sequence numbers, stop-retry
// with ack bits piggybacked on every DATA packet), in-order delivery through
// a 64-slot receive window. Unreliable channel: unordered frames.

#include "ksnet.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// windows.h (pulled in by the WinSock headers on some SDKs) defines an
// object-like macro renaming SendMessage -> SendMessageA, which would
// mangle our Client/Server method definitions below.
#ifdef SendMessage
#undef SendMessage
#endif
namespace {
using KSocket = SOCKET;
constexpr KSocket kInvalidSocket = INVALID_SOCKET;
} // namespace
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
namespace {
using KSocket = int;
constexpr KSocket kInvalidSocket = -1;
} // namespace
#endif

namespace ksnet {
namespace {

using Clock = std::chrono::steady_clock;

double nowSec()
{
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

constexpr size_t kPacketBudget = 1100; // payload bytes per DATA datagram
constexpr int kMaxFramesPerPacket = 64;
constexpr double kRtoSec = 0.15;
constexpr double kPingIntervalSec = 1.0;
constexpr double kConnectRetrySec = 0.25;
constexpr double kDefaultTimeoutSec = 10.0;
constexpr int kWindow = 64;
constexpr size_t kMaxQueuedFrames = 128;

enum PacketType : uint8_t {
    PKT_CONNECT = 1,
    PKT_CONNECT_ACK = 2,
    PKT_CONNECT_REJECT = 3,
    PKT_DATA = 4,
    PKT_DISCONNECT = 5,
    PKT_PING = 6,
    PKT_PONG = 7,
};

constexpr uint8_t kRejectServerFull = 1;
constexpr uint8_t kRejectKeyMismatch = 2;
constexpr size_t kHeaderBytes = 13; // u64 protocolId + u8 type + u32 nonce
constexpr size_t kKeyProofBytes = 8;

// ---------------------------------------------------------------------------
// Secure-connect helpers (shared 32-byte key → XOR keystream + CONNECT proof)
// ---------------------------------------------------------------------------

void fillKeystream(const uint8_t* key, uint32_t nonce, size_t offset, uint8_t* out, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const size_t idx = offset + i;
        const uint8_t k0 = key[idx % PRIVATE_KEY_BYTES];
        const uint8_t k1 = key[(idx * 7 + 13) % PRIVATE_KEY_BYTES];
        const uint8_t nbyte = static_cast<uint8_t>((nonce >> ((idx % 4) * 8)) & 0xFFu);
        out[i] = static_cast<uint8_t>(k0 ^ k1 ^ nbyte ^ static_cast<uint8_t>(idx & 0xFFu));
    }
}

void xorPayload(uint8_t* data, size_t size, const uint8_t* key, uint32_t nonce)
{
    if (!data || size <= kHeaderBytes || !key) return;
    const size_t payloadLen = size - kHeaderBytes;
    // small stack buffer for common packets; fall back to heap for large
    uint8_t stack[2048];
    uint8_t* ks = stack;
    std::unique_ptr<uint8_t[]> heap;
    if (payloadLen > sizeof(stack)) {
        heap.reset(new uint8_t[payloadLen]);
        ks = heap.get();
    }
    fillKeystream(key, nonce, 0, ks, payloadLen);
    for (size_t i = 0; i < payloadLen; ++i)
        data[kHeaderBytes + i] ^= ks[i];
}

void xorPayloadConst(std::vector<uint8_t>& pkt, const uint8_t* key, uint32_t nonce)
{
    if (pkt.size() <= kHeaderBytes || !key) return;
    xorPayload(pkt.data(), pkt.size(), key, nonce);
}

/** 8-byte CONNECT proof: keystream(protocolId bytes mixed with nonce). */
void makeKeyProof(const uint8_t* key, uint64_t protocolId, uint32_t nonce, uint8_t out[kKeyProofBytes])
{
    uint8_t seed[16];
    for (int i = 0; i < 8; ++i)
        seed[i] = static_cast<uint8_t>((protocolId >> (8 * i)) & 0xFFu);
    for (int i = 0; i < 4; ++i)
        seed[8 + i] = static_cast<uint8_t>((nonce >> (8 * i)) & 0xFFu);
    seed[12] = 0xC0;
    seed[13] = 0x4E;
    seed[14] = 0xC7;
    seed[15] = 0x01; // "CONNECT" tag
    // Mix seed into a synthetic nonce for keystream start
    uint32_t mixed = nonce;
    for (int i = 0; i < 16; ++i)
        mixed = mixed * 16777619u ^ seed[i];
    fillKeystream(key, mixed, 0x100, out, kKeyProofBytes);
}

bool verifyKeyProof(const uint8_t* key, uint64_t protocolId, uint32_t nonce,
                    const uint8_t* proof, size_t proofLen)
{
    if (!key || !proof || proofLen < kKeyProofBytes) return false;
    uint8_t expected[kKeyProofBytes];
    makeKeyProof(key, protocolId, nonce, expected);
    unsigned char diff = 0;
    for (size_t i = 0; i < kKeyProofBytes; ++i)
        diff |= static_cast<unsigned char>(expected[i] ^ proof[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------------
// platform glue
// ---------------------------------------------------------------------------

void ensureWinsock()
{
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        initialized = true;
    }
#endif
}

void closeSocket(KSocket s)
{
    if (s == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

void setNonBlocking(KSocket s)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

bool resolveIPv4(const Address& a, sockaddr_in& out)
{
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    out.sin_port = htons(a.port());
    if (inet_pton(AF_INET, a.host().c_str(), &out.sin_addr) == 1) return true;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(a.host().c_str(), nullptr, &hints, &res) == 0 && res) {
        out.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
        return true;
    }
    return false;
}

bool sameEndpoint(const sockaddr_in& a, const sockaddr_in& b)
{
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

void sendDatagram(KSocket sock, const sockaddr_in& to, const std::vector<uint8_t>& buf)
{
    sendto(sock, reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()),
           0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
}

// ---------------------------------------------------------------------------
// byte buffers
// ---------------------------------------------------------------------------

void put8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void put16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& b, uint64_t v)
{
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void putBytes(std::vector<uint8_t>& b, const void* data, size_t n)
{
    if (n == 0) return;
    const auto* p = static_cast<const uint8_t*>(data);
    b.insert(b.end(), p, p + n);
}

struct ByteReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool good = true;

    size_t left() const { return good && pos <= size ? size - pos : 0; }
    uint8_t u8()
    {
        if (left() < 1) { good = false; return 0; }
        return data[pos++];
    }
    uint16_t u16()
    {
        if (left() < 2) { good = false; return 0; }
        uint16_t v = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return v;
    }
    uint32_t u32()
    {
        if (left() < 4) { good = false; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
        pos += 4;
        return v;
    }
    uint64_t u64()
    {
        if (left() < 8) { good = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
        pos += 8;
        return v;
    }
    bool take(void* out, size_t n)
    {
        if (left() < n) { good = false; return false; }
        std::memcpy(out, data + pos, n);
        pos += n;
        return true;
    }
};

// ---------------------------------------------------------------------------
// connection state (shared by Client and each Server slot)
// ---------------------------------------------------------------------------

bool isReliable(const ClientServerConfig& cfg, int ch)
{
    return ch >= 0 && ch < cfg.numChannels &&
           cfg.channel[ch].type == CHANNEL_TYPE_RELIABLE_ORDERED;
}

struct Frame {
    uint8_t channel = 0;
    uint8_t msgType = 0;
    std::vector<uint8_t> payload;
};

struct ReliableState {
    struct Pending {
        uint16_t seq = 0;
        uint8_t msgType = 0;
        std::vector<uint8_t> payload;
        double lastSent = -1.0e9;
        int sends = 0;
    };
    std::vector<Pending> pending; // unacked, in send order == sequence order
    uint16_t nextSeq = 0;
    bool haveHighest = false;
    uint16_t highest = 0;
    uint64_t recvMask = 0; // bit i => (highest - 1 - i) received
    uint16_t expected = 0; // next sequence to deliver in order
    struct Slot {
        bool valid = false;
        uint16_t seq = 0;
        uint8_t msgType = 0;
        std::vector<uint8_t> payload;
    };
    std::array<Slot, kWindow> win{};
};

struct Conn {
    std::array<std::deque<Message*>, MAX_CHANNELS> inbox;
    std::array<std::deque<Frame>, MAX_CHANNELS> outbox;
    std::array<ReliableState, MAX_CHANNELS> reliable; // indexed by channel
    bool ackDirty = false;                            // received frames since last DATA sent
    double lastRecv = 0.0;
    double lastPing = 0.0;
    uint32_t pingId = 0;
    double pingSentAt = -1.0;
    double rtt = 0.0;
    bool haveRtt = false;
    uint64_t reliableAttempts = 0;
    uint64_t retransmissions = 0;
};

void destroyConn(Conn& c)
{
    for (auto& q : c.inbox) {
        for (auto* m : q) delete m;
        q.clear();
    }
    for (auto& q : c.outbox) q.clear();
    for (auto& r : c.reliable) r.pending.clear();
}

bool serializeMessage(Message* msg, std::vector<uint8_t>& out)
{
    WriteStream ws(out);
    return msg->Serialize(ws);
}

Message* deserializeMessage(MessageFactory* factory, uint8_t type, const uint8_t* data, size_t size)
{
    if (!factory) return nullptr;
    Message* m = factory->Create(type);
    if (!m) return nullptr;
    ReadStream rs(data, size);
    if (!m->Serialize(rs) || !rs.ok()) {
        delete m;
        return nullptr;
    }
    return m;
}

// Queue one application message (ownership passes to the connection).
void enqueueMessage(const ClientServerConfig& cfg, Conn& c, int channel, Message* msg)
{
    if (!msg) return;
    const int type = msg->GetType();
    std::vector<uint8_t> payload;
    const bool ok = serializeMessage(msg, payload);
    delete msg;
    if (!ok) return;
    if (channel < 0 || channel >= cfg.numChannels) return;
    if (isReliable(cfg, channel)) {
        auto& rs = c.reliable[channel];
        if (rs.pending.size() >= static_cast<size_t>(kWindow)) return; // window full: drop
        ReliableState::Pending p;
        p.seq = rs.nextSeq++;
        p.msgType = static_cast<uint8_t>(type);
        p.payload = std::move(payload);
        rs.pending.push_back(std::move(p));
    } else {
        auto& ob = c.outbox[channel];
        if (ob.size() >= kMaxQueuedFrames) return;
        Frame f;
        f.channel = static_cast<uint8_t>(channel);
        f.msgType = static_cast<uint8_t>(type);
        f.payload = std::move(payload);
        ob.push_back(std::move(f));
    }
}

void applyAcks(ReliableState& rs, uint16_t ackSeq, uint64_t mask)
{
    for (size_t i = 0; i < rs.pending.size();) {
        const int16_t d = static_cast<int16_t>(ackSeq - rs.pending[i].seq);
        bool acked = false;
        if (d == 0) acked = true;
        else if (d > 0 && d <= kWindow && (mask & (1ull << (d - 1)))) acked = true;
        if (acked) rs.pending.erase(rs.pending.begin() + static_cast<long>(i));
        else ++i;
    }
}

void recordReceivedSeq(ReliableState& rs, uint16_t seq)
{
    if (!rs.haveHighest) {
        rs.haveHighest = true;
        rs.highest = seq;
        rs.recvMask = 0;
        return;
    }
    const int16_t d = static_cast<int16_t>(seq - rs.highest);
    if (d > 0) {
        uint64_t shifted = (d >= kWindow) ? 0ull : (rs.recvMask << d);
        if (d <= kWindow) shifted |= (1ull << (d - 1)); // previous highest stays recorded
        rs.recvMask = shifted;
        rs.highest = seq;
    } else if (d < 0) {
        const int age = -d;
        if (age <= kWindow) rs.recvMask |= (1ull << (age - 1));
    }
}

void recvReliable(ReliableState& rs, uint16_t seq, uint8_t type, std::vector<uint8_t> payload,
                  MessageFactory* factory, std::deque<Message*>& inbox)
{
    const int16_t d = static_cast<int16_t>(seq - rs.expected);
    if (d < 0 || d >= kWindow) return; // duplicate of an already-delivered frame or beyond window

    auto& slot = rs.win[seq % kWindow];
    slot.valid = true;
    slot.seq = seq;
    slot.msgType = type;
    slot.payload = std::move(payload);
    recordReceivedSeq(rs, seq);

    while (true) {
        auto& t = rs.win[rs.expected % kWindow];
        if (!(t.valid && t.seq == rs.expected)) break;
        if (Message* m = deserializeMessage(factory, t.msgType, t.payload.data(), t.payload.size()))
            inbox.push_back(m);
        t.valid = false;
        t.payload.clear();
        ++rs.expected;
    }
}

// Append a DATA payload (acks + frames) to a packet that already carries the
// header. Returns false when there is nothing to send.
bool appendDataPayload(const ClientServerConfig& cfg, Conn& c, std::vector<uint8_t>& pkt)
{
    const double now = nowSec();

    int numReliable = 0;
    for (int ch = 0; ch < cfg.numChannels; ++ch)
        if (isReliable(cfg, ch)) ++numReliable;
    const size_t ackBlockSize = 1 + static_cast<size_t>(numReliable) * 11;

    std::vector<std::vector<uint8_t>> blobs;
    size_t used = 1 /*frameCount*/ + ackBlockSize;

    // Unreliable frames first (freshness), then reliable first-sends/retransmits.
    for (int ch = 0; ch < cfg.numChannels && static_cast<int>(blobs.size()) < kMaxFramesPerPacket; ++ch) {
        if (isReliable(cfg, ch)) continue;
        auto& ob = c.outbox[ch];
        while (!ob.empty() && static_cast<int>(blobs.size()) < kMaxFramesPerPacket) {
            const Frame& f = ob.front();
            const size_t frameLen = 1 + 1 + 2 + f.payload.size();
            if (used + frameLen > kPacketBudget) break;
            std::vector<uint8_t> blob;
            blob.reserve(frameLen);
            put8(blob, f.channel);
            put8(blob, f.msgType);
            put16(blob, static_cast<uint16_t>(f.payload.size()));
            putBytes(blob, f.payload.data(), f.payload.size());
            used += frameLen;
            blobs.push_back(std::move(blob));
            ob.pop_front();
        }
    }
    for (int ch = 0; ch < cfg.numChannels && static_cast<int>(blobs.size()) < kMaxFramesPerPacket; ++ch) {
        if (!isReliable(cfg, ch)) continue;
        auto& rs = c.reliable[ch];
        for (auto& p : rs.pending) {
            if (static_cast<int>(blobs.size()) >= kMaxFramesPerPacket) break;
            if (p.sends > 0 && now - p.lastSent < kRtoSec) continue;
            const size_t frameLen = 1 + 1 + 2 + 2 + p.payload.size();
            if (used + frameLen > kPacketBudget) break;
            if (p.payload.size() > kPacketBudget) {
                // Oversized frame can never be delivered: drop it.
                p.sends = -1;
                continue;
            }
            std::vector<uint8_t> blob;
            blob.reserve(frameLen);
            put8(blob, static_cast<uint8_t>(ch));
            put8(blob, p.msgType);
            put16(blob, p.seq);
            put16(blob, static_cast<uint16_t>(p.payload.size()));
            putBytes(blob, p.payload.data(), p.payload.size());
            used += frameLen;
            blobs.push_back(std::move(blob));
            p.lastSent = now;
            ++p.sends;
            ++c.reliableAttempts;
            if (p.sends > 1) ++c.retransmissions;
        }
    }

    if (blobs.empty() && !c.ackDirty) return false;

    put8(pkt, static_cast<uint8_t>(numReliable));
    for (int ch = 0; ch < cfg.numChannels; ++ch) {
        if (!isReliable(cfg, ch)) continue;
        auto& rs = c.reliable[ch];
        if (rs.haveHighest) {
            put8(pkt, 1);
            put16(pkt, rs.highest);
            put64(pkt, rs.recvMask);
        } else {
            put8(pkt, 0);
        }
    }
    put8(pkt, static_cast<uint8_t>(blobs.size()));
    for (auto& b : blobs) putBytes(pkt, b.data(), b.size());

    c.ackDirty = false;
    return true;
}

void processData(const ClientServerConfig& cfg, MessageFactory* factory, Conn& c, ByteReader& r)
{
    const int numReliable = r.u8();
    int seen = 0;
    for (int ch = 0; ch < cfg.numChannels && r.good && seen < numReliable; ++ch) {
        if (!isReliable(cfg, ch)) continue;
        ++seen;
        const bool hasAck = r.u8() != 0;
        if (hasAck) {
            const uint16_t ackSeq = r.u16();
            const uint64_t mask = r.u64();
            applyAcks(c.reliable[ch], ackSeq, mask);
        }
    }
    const int frameCount = r.u8();
    bool deliveredAny = false;
    for (int i = 0; i < frameCount && r.good; ++i) {
        const uint8_t ch = r.u8();
        const uint8_t type = r.u8();
        if (ch >= cfg.numChannels) { r.good = false; break; }
        uint16_t seq = 0;
        if (isReliable(cfg, ch)) seq = r.u16();
        const uint16_t len = r.u16();
        if (r.left() < len) { r.good = false; break; }
        std::vector<uint8_t> payload(len);
        if (len > 0 && !r.take(payload.data(), len)) break;
        if (isReliable(cfg, ch)) {
            recvReliable(c.reliable[ch], seq, type, std::move(payload), factory, c.inbox[ch]);
        } else {
            if (Message* m = deserializeMessage(factory, type, payload.data(), payload.size()))
                c.inbox[ch].push_back(m);
        }
        deliveredAny = true;
    }
    if (deliveredAny) c.ackDirty = true;
    c.lastRecv = nowSec();
}

std::vector<uint8_t> makeHeader(uint64_t protocolId, PacketType type, uint32_t nonce)
{
    std::vector<uint8_t> pkt;
    pkt.reserve(64);
    put64(pkt, protocolId);
    put8(pkt, static_cast<uint8_t>(type));
    put32(pkt, nonce);
    return pkt;
}

void sendPing(KSocket sock, const sockaddr_in& to, uint64_t protocolId, uint32_t nonce, Conn& c,
              const uint8_t* secureKey = nullptr)
{
    std::vector<uint8_t> pkt = makeHeader(protocolId, PKT_PING, nonce);
    ++c.pingId;
    put32(pkt, c.pingId);
    c.pingSentAt = nowSec();
    if (secureKey) xorPayloadConst(pkt, secureKey, nonce);
    sendDatagram(sock, to, pkt);
}

void handlePong(Conn& c, uint32_t pingId, double sentAt)
{
    if (pingId != c.pingId || sentAt < 0) return;
    const double sample = nowSec() - sentAt;
    if (!c.haveRtt) {
        c.rtt = sample;
        c.haveRtt = true;
    } else {
        c.rtt = c.rtt * 0.875 + sample * 0.125;
    }
}

void pumpKeepalive(KSocket sock, const sockaddr_in& to, uint64_t protocolId, uint32_t nonce, Conn& c,
                   const uint8_t* secureKey = nullptr)
{
    const double now = nowSec();
    if (c.lastPing == 0.0) c.lastPing = now;
    if (now - c.lastPing >= kPingIntervalSec) {
        c.lastPing = now;
        sendPing(sock, to, protocolId, nonce, c, secureKey);
    }
}

} // namespace

// ============================================================================
// Client
// ============================================================================

struct Client::Impl {
    ClientServerConfig config;
    MessageFactory* factory = nullptr;
    KSocket sock = kInvalidSocket;
    sockaddr_in serverAddr{};
    uint32_t nonce = 0;
    Conn conn;
    double timeout = kDefaultTimeoutSec;
    double connectStarted = 0.0;
    double lastConnectReq = -1.0e9;
    uint64_t bytesSent = 0;
    uint64_t bytesRecv = 0;
    double statTime = 0.0;
    uint64_t statLastSent = 0;
    uint64_t statLastRecv = 0;
    double rateSent = 0.0;
    double rateRecv = 0.0;
    bool secure = false;
    uint8_t privateKey[PRIVATE_KEY_BYTES] = {};

    ~Impl()
    {
        destroyConn(conn);
        delete factory;
        closeSocket(sock);
        if (secure) std::memset(privateKey, 0, sizeof(privateKey));
    }
};

Client::Client(Allocator& allocator, const Address& /*clientAddress*/,
               const ClientServerConfig& config, Adapter& adapter, double timeout)
    : m_impl(new Impl)
{
    m_impl->config = config;
    if (m_impl->config.numChannels > MAX_CHANNELS) m_impl->config.numChannels = MAX_CHANNELS;
    if (m_impl->config.numChannels < 1) m_impl->config.numChannels = 1;
    m_impl->factory = adapter.CreateMessageFactory(allocator);
    m_impl->timeout = timeout > 0.0 ? timeout : kDefaultTimeoutSec;
}

Client::~Client() = default;

bool Client::InsecureConnect(const char* /*privateKey*/, int /*keyLength*/, const Address& serverAddress)
{
    ensureWinsock();
    if (m_impl->sock != kInvalidSocket) Disconnect();

    if (!resolveIPv4(serverAddress, m_impl->serverAddr)) return false;

    KSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalidSocket) return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        closeSocket(s);
        return false;
    }
    setNonBlocking(s);
    m_impl->sock = s;

    std::random_device rd;
    m_impl->nonce = rd();
    m_impl->conn = Conn{};
    m_impl->conn.lastRecv = nowSec();
    m_impl->connectStarted = nowSec();
    m_impl->lastConnectReq = -1.0e9;
    m_impl->secure = false;
    std::memset(m_impl->privateKey, 0, sizeof(m_impl->privateKey));
    m_state = CLIENT_STATE_CONNECTING;
    return true;
}

bool Client::SecureConnect(const uint8_t* privateKey, int keyLength, const Address& serverAddress)
{
    if (!privateKey || keyLength != PRIVATE_KEY_BYTES) return false;

    ensureWinsock();
    if (m_impl->sock != kInvalidSocket) Disconnect();

    if (!resolveIPv4(serverAddress, m_impl->serverAddr)) return false;

    KSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalidSocket) return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        closeSocket(s);
        return false;
    }
    setNonBlocking(s);
    m_impl->sock = s;

    std::random_device rd;
    m_impl->nonce = rd();
    m_impl->conn = Conn{};
    m_impl->conn.lastRecv = nowSec();
    m_impl->connectStarted = nowSec();
    m_impl->lastConnectReq = -1.0e9;
    m_impl->secure = true;
    std::memcpy(m_impl->privateKey, privateKey, PRIVATE_KEY_BYTES);
    m_state = CLIENT_STATE_CONNECTING;
    return true;
}

bool Client::isSecure() const
{
    return m_impl && m_impl->secure;
}

void Client::Disconnect()
{
    if (m_impl && m_impl->sock != kInvalidSocket) {
        if (m_state == CLIENT_STATE_CONNECTED || m_state == CLIENT_STATE_CONNECTING) {
            std::vector<uint8_t> pkt = makeHeader(m_impl->config.protocolId, PKT_DISCONNECT, m_impl->nonce);
            sendDatagram(m_impl->sock, m_impl->serverAddr, pkt);
        }
        closeSocket(m_impl->sock);
        m_impl->sock = kInvalidSocket;
    }
    if (m_impl) destroyConn(m_impl->conn);
    m_state = CLIENT_STATE_DISCONNECTED;
}

void Client::SendPackets()
{
    if (!m_impl || m_impl->sock == kInvalidSocket) return;
    const double now = nowSec();

    if (m_state == CLIENT_STATE_CONNECTING) {
        if (now - m_impl->connectStarted > m_impl->timeout) {
            m_state = CLIENT_STATE_ERROR;
            return;
        }
        if (now - m_impl->lastConnectReq >= kConnectRetrySec) {
            m_impl->lastConnectReq = now;
            std::vector<uint8_t> pkt = makeHeader(m_impl->config.protocolId, PKT_CONNECT, m_impl->nonce);
            if (m_impl->secure) {
                uint8_t proof[kKeyProofBytes];
                makeKeyProof(m_impl->privateKey, m_impl->config.protocolId, m_impl->nonce, proof);
                pkt.insert(pkt.end(), proof, proof + kKeyProofBytes);
                // Proof itself is not XOR'd (it *is* the key material check);
                // subsequent packets will XOR the payload after the header.
            }
            sendDatagram(m_impl->sock, m_impl->serverAddr, pkt);
            m_impl->bytesSent += pkt.size();
        }
        return;
    }
    if (m_state != CLIENT_STATE_CONNECTED) return;

    if (now - m_impl->conn.lastRecv > m_impl->timeout) {
        m_state = CLIENT_STATE_DISCONNECTED;
        return;
    }

    const uint8_t* skey = m_impl->secure ? m_impl->privateKey : nullptr;
    pumpKeepalive(m_impl->sock, m_impl->serverAddr, m_impl->config.protocolId, m_impl->nonce, m_impl->conn, skey);

    std::vector<uint8_t> pkt = makeHeader(m_impl->config.protocolId, PKT_DATA, m_impl->nonce);
    if (appendDataPayload(m_impl->config, m_impl->conn, pkt)) {
        if (skey) xorPayloadConst(pkt, skey, m_impl->nonce);
        sendDatagram(m_impl->sock, m_impl->serverAddr, pkt);
        m_impl->bytesSent += pkt.size();
    }
}

void Client::ReceivePackets()
{
    if (!m_impl || m_impl->sock == kInvalidSocket) return;
    if (m_state == CLIENT_STATE_DISCONNECTED || m_state == CLIENT_STATE_ERROR) return;

    uint8_t buf[2048];
    for (;;) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
#else
        socklen_t fromLen = sizeof(from);
#endif
        const int n = recvfrom(m_impl->sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) break;
        m_impl->bytesRecv += static_cast<uint64_t>(n);
        if (n < static_cast<int>(kHeaderBytes)) continue;

        // Decrypt payload (after cleartext header) when secure.
        if (m_impl->secure && static_cast<size_t>(n) > kHeaderBytes) {
            // Peek nonce from header for keystream
            uint32_t peekNonce = 0;
            std::memcpy(&peekNonce, buf + 9, 4);
            xorPayload(buf, static_cast<size_t>(n), m_impl->privateKey, peekNonce);
        }

        ByteReader r{buf, static_cast<size_t>(n)};
        const uint64_t protocolId = r.u64();
        const uint8_t type = r.u8();
        const uint32_t nonce = r.u32();
        if (!r.good || protocolId != m_impl->config.protocolId) continue;
        if (!sameEndpoint(from, m_impl->serverAddr)) continue;
        if (nonce != m_impl->nonce) continue;
        m_impl->conn.lastRecv = nowSec(); // any authenticated packet proves the link is alive

        switch (type) {
            case PKT_CONNECT_ACK:
                if (m_state == CLIENT_STATE_CONNECTING) {
                    m_state = CLIENT_STATE_CONNECTED;
                    m_impl->conn.lastRecv = nowSec();
                }
                break;
            case PKT_CONNECT_REJECT:
                if (m_state == CLIENT_STATE_CONNECTING) m_state = CLIENT_STATE_DISCONNECTED;
                break;
            case PKT_DATA:
                if (m_state == CLIENT_STATE_CONNECTED || m_state == CLIENT_STATE_CONNECTING)
                    processData(m_impl->config, m_impl->factory, m_impl->conn, r);
                break;
            case PKT_DISCONNECT:
                if (m_state == CLIENT_STATE_CONNECTED) m_state = CLIENT_STATE_DISCONNECTED;
                break;
            case PKT_PING: {
                const uint32_t id = r.u32();
                std::vector<uint8_t> reply = makeHeader(m_impl->config.protocolId, PKT_PONG, m_impl->nonce);
                put32(reply, id);
                if (m_impl->secure) xorPayloadConst(reply, m_impl->privateKey, m_impl->nonce);
                sendDatagram(m_impl->sock, m_impl->serverAddr, reply);
                m_impl->bytesSent += reply.size();
                break;
            }
            case PKT_PONG: {
                const uint32_t id = r.u32();
                handlePong(m_impl->conn, id, m_impl->conn.pingSentAt);
                m_impl->conn.lastRecv = nowSec();
                break;
            }
            default:
                break;
        }
    }
}

Message* Client::CreateMessage(int type)
{
    if (!m_impl || !m_impl->factory) return nullptr;
    return m_impl->factory->Create(type);
}

void Client::SendMessage(int channelIndex, Message* message)
{
    if (!m_impl || !message) return;
    enqueueMessage(m_impl->config, m_impl->conn, channelIndex, message);
}

Message* Client::ReceiveMessage(int channelIndex)
{
    if (!m_impl || channelIndex < 0 || channelIndex >= MAX_CHANNELS) return nullptr;
    auto& q = m_impl->conn.inbox[channelIndex];
    return q.empty() ? nullptr : q.front();
}

void Client::ReleaseMessage(Message* message)
{
    if (!m_impl || !message) return;
    for (auto& q : m_impl->conn.inbox) {
        if (!q.empty() && q.front() == message) {
            q.pop_front();
            delete message;
            return;
        }
    }
}

void Client::GetNetworkInfo(NetworkInfo& info) const
{
    info = NetworkInfo{};
    if (!m_impl) return;
    const double now = nowSec();
    if (m_impl->statTime == 0.0) {
        m_impl->statTime = now;
        m_impl->statLastSent = m_impl->bytesSent;
        m_impl->statLastRecv = m_impl->bytesRecv;
    } else if (now - m_impl->statTime >= 0.25) {
        const double dt = now - m_impl->statTime;
        m_impl->rateSent = static_cast<double>(m_impl->bytesSent - m_impl->statLastSent) / dt;
        m_impl->rateRecv = static_cast<double>(m_impl->bytesRecv - m_impl->statLastRecv) / dt;
        m_impl->statTime = now;
        m_impl->statLastSent = m_impl->bytesSent;
        m_impl->statLastRecv = m_impl->bytesRecv;
    }
    info.RTT = static_cast<float>(m_impl->conn.rtt);
    if (m_impl->conn.reliableAttempts > 0) {
        info.packetLoss = static_cast<float>(
            static_cast<double>(m_impl->conn.retransmissions) /
            static_cast<double>(m_impl->conn.reliableAttempts));
        if (info.packetLoss > 1.0f) info.packetLoss = 1.0f;
    }
    info.sentBandwidth = static_cast<float>(m_impl->rateSent);
    info.receivedBandwidth = static_cast<float>(m_impl->rateRecv);
}

// ============================================================================
// Server
// ============================================================================

struct Server::Impl {
    ClientServerConfig config;
    MessageFactory* factory = nullptr;
    KSocket sock = kInvalidSocket;
    sockaddr_in bindAddr{};
    double timeout = kDefaultTimeoutSec;
    bool secure = false;
    uint8_t privateKey[PRIVATE_KEY_BYTES] = {};

    struct Slot {
        bool used = false;
        sockaddr_in addr{};
        uint32_t nonce = 0;
        Conn conn;
    };
    std::vector<Slot> slots;

    uint64_t bytesSent = 0;
    uint64_t bytesRecv = 0;
    double statTime = 0.0;
    uint64_t statLastSent = 0;
    uint64_t statLastRecv = 0;

    ~Impl()
    {
        for (auto& s : slots) destroyConn(s.conn);
        delete factory;
        closeSocket(sock);
        if (secure) std::memset(privateKey, 0, sizeof(privateKey));
    }

    Slot* findByAddr(const sockaddr_in& a)
    {
        for (auto& s : slots)
            if (s.used && sameEndpoint(s.addr, a)) return &s;
        return nullptr;
    }
    Slot* findFree()
    {
        for (auto& s : slots)
            if (!s.used) return &s;
        return nullptr;
    }
    void freeSlot(Slot& s)
    {
        destroyConn(s.conn);
        s = Slot{};
    }
};

Server::Server(Allocator& allocator, const Address* /*privateAddress*/,
               const Address& publicAddress, const ClientServerConfig& config,
               Adapter& adapter, double timeout)
    : m_impl(new Impl)
{
    m_impl->config = config;
    if (m_impl->config.numChannels > MAX_CHANNELS) m_impl->config.numChannels = MAX_CHANNELS;
    if (m_impl->config.numChannels < 1) m_impl->config.numChannels = 1;
    m_impl->factory = adapter.CreateMessageFactory(allocator);
    m_impl->timeout = timeout > 0.0 ? timeout : kDefaultTimeoutSec;
    resolveIPv4(publicAddress, m_impl->bindAddr);
}

Server::~Server() = default;

bool Server::SetPrivateKey(const uint8_t* privateKey, int keyLength)
{
    if (!m_impl) return false;
    if (!privateKey || keyLength != PRIVATE_KEY_BYTES) {
        ClearPrivateKey();
        return false;
    }
    std::memcpy(m_impl->privateKey, privateKey, PRIVATE_KEY_BYTES);
    m_impl->secure = true;
    return true;
}

void Server::ClearPrivateKey()
{
    if (!m_impl) return;
    std::memset(m_impl->privateKey, 0, sizeof(m_impl->privateKey));
    m_impl->secure = false;
}

bool Server::isSecure() const
{
    return m_impl && m_impl->secure;
}

bool Server::Start(int maxClients)
{
    ensureWinsock();
    if (m_impl->sock != kInvalidSocket) Stop();
    if (maxClients < 1) maxClients = 1;

    KSocket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalidSocket) return false;
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (bind(s, reinterpret_cast<sockaddr*>(&m_impl->bindAddr), sizeof(m_impl->bindAddr)) != 0) {
        closeSocket(s);
        return false;
    }
    setNonBlocking(s);
    m_impl->sock = s;
    m_impl->slots.assign(static_cast<size_t>(maxClients), Impl::Slot{});
    for (auto& slot : m_impl->slots) slot.conn.lastRecv = nowSec();
    return true;
}

void Server::Stop()
{
    if (!m_impl) return;
    if (m_impl->sock != kInvalidSocket) {
        for (auto& slot : m_impl->slots) {
            if (!slot.used) continue;
            std::vector<uint8_t> pkt = makeHeader(m_impl->config.protocolId, PKT_DISCONNECT, slot.nonce);
            sendDatagram(m_impl->sock, slot.addr, pkt);
            m_impl->bytesSent += pkt.size();
        }
        closeSocket(m_impl->sock);
        m_impl->sock = kInvalidSocket;
    }
    for (auto& slot : m_impl->slots) m_impl->freeSlot(slot);
}

void Server::SendPackets()
{
    if (!m_impl || m_impl->sock == kInvalidSocket) return;
    const double now = nowSec();

    for (auto& slot : m_impl->slots) {
        if (!slot.used) continue;
        if (now - slot.conn.lastRecv > m_impl->timeout) {
            m_impl->freeSlot(slot);
            continue;
        }
        const uint8_t* skey = m_impl->secure ? m_impl->privateKey : nullptr;
        pumpKeepalive(m_impl->sock, slot.addr, m_impl->config.protocolId, slot.nonce, slot.conn, skey);
        std::vector<uint8_t> pkt = makeHeader(m_impl->config.protocolId, PKT_DATA, slot.nonce);
        if (appendDataPayload(m_impl->config, slot.conn, pkt)) {
            if (skey) xorPayloadConst(pkt, skey, slot.nonce);
            sendDatagram(m_impl->sock, slot.addr, pkt);
            m_impl->bytesSent += pkt.size();
        }
    }
}

void Server::ReceivePackets()
{
    if (!m_impl || m_impl->sock == kInvalidSocket) return;

    uint8_t buf[2048];
    for (;;) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
#else
        socklen_t fromLen = sizeof(from);
#endif
        const int n = recvfrom(m_impl->sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) break;
        m_impl->bytesRecv += static_cast<uint64_t>(n);
        if (n < static_cast<int>(kHeaderBytes)) continue;

        // Peek header (cleartext) before optional XOR of payload.
        const uint64_t protocolIdPeek =
            static_cast<uint64_t>(buf[0]) | (static_cast<uint64_t>(buf[1]) << 8) |
            (static_cast<uint64_t>(buf[2]) << 16) | (static_cast<uint64_t>(buf[3]) << 24) |
            (static_cast<uint64_t>(buf[4]) << 32) | (static_cast<uint64_t>(buf[5]) << 40) |
            (static_cast<uint64_t>(buf[6]) << 48) | (static_cast<uint64_t>(buf[7]) << 56);
        const uint8_t typePeek = buf[8];
        uint32_t noncePeek = 0;
        std::memcpy(&noncePeek, buf + 9, 4);

        // CONNECT proof is cleartext (not XOR'd). All other post-header payloads
        // are XOR'd when the server is in secure mode.
        if (m_impl->secure && typePeek != PKT_CONNECT && static_cast<size_t>(n) > kHeaderBytes) {
            xorPayload(buf, static_cast<size_t>(n), m_impl->privateKey, noncePeek);
        }

        ByteReader r{buf, static_cast<size_t>(n)};
        const uint64_t protocolId = r.u64();
        const uint8_t type = r.u8();
        const uint32_t nonce = r.u32();
        if (!r.good || protocolId != m_impl->config.protocolId) continue;
        (void)protocolIdPeek;

        Impl::Slot* slot = m_impl->findByAddr(from);
        if (slot) {
            if (nonce != slot->nonce) continue; // spoofed / stale client
            slot->conn.lastRecv = nowSec();
            switch (type) {
                case PKT_CONNECT: {
                    std::vector<uint8_t> ack = makeHeader(m_impl->config.protocolId, PKT_CONNECT_ACK, slot->nonce);
                    sendDatagram(m_impl->sock, from, ack);
                    m_impl->bytesSent += ack.size();
                    break;
                }
                case PKT_DATA:
                    processData(m_impl->config, m_impl->factory, slot->conn, r);
                    break;
                case PKT_DISCONNECT:
                    m_impl->freeSlot(*slot);
                    break;
                case PKT_PING: {
                    const uint32_t id = r.u32();
                    std::vector<uint8_t> reply = makeHeader(m_impl->config.protocolId, PKT_PONG, slot->nonce);
                    put32(reply, id);
                    if (m_impl->secure) xorPayloadConst(reply, m_impl->privateKey, slot->nonce);
                    sendDatagram(m_impl->sock, from, reply);
                    m_impl->bytesSent += reply.size();
                    break;
                }
                case PKT_PONG: {
                    const uint32_t id = r.u32();
                    handlePong(slot->conn, id, slot->conn.pingSentAt);
                    break;
                }
                default:
                    break;
            }
            continue;
        }

        if (type == PKT_CONNECT) {
            // Secure server: require 8-byte key proof after header.
            if (m_impl->secure) {
                const size_t remaining = static_cast<size_t>(n) - kHeaderBytes;
                if (remaining < kKeyProofBytes ||
                    !verifyKeyProof(m_impl->privateKey, protocolId, nonce, buf + kHeaderBytes, remaining)) {
                    std::vector<uint8_t> reject =
                        makeHeader(m_impl->config.protocolId, PKT_CONNECT_REJECT, nonce);
                    put8(reject, kRejectKeyMismatch);
                    sendDatagram(m_impl->sock, from, reject);
                    m_impl->bytesSent += reject.size();
                    continue;
                }
            }

            Impl::Slot* freeSlot = m_impl->findFree();
            if (!freeSlot) {
                std::vector<uint8_t> reject = makeHeader(m_impl->config.protocolId, PKT_CONNECT_REJECT, nonce);
                put8(reject, kRejectServerFull);
                sendDatagram(m_impl->sock, from, reject);
                m_impl->bytesSent += reject.size();
                continue;
            }
            freeSlot->used = true;
            freeSlot->addr = from;
            freeSlot->nonce = nonce;
            freeSlot->conn = Conn{};
            freeSlot->conn.lastRecv = nowSec();
            std::vector<uint8_t> ack = makeHeader(m_impl->config.protocolId, PKT_CONNECT_ACK, nonce);
            sendDatagram(m_impl->sock, from, ack);
            m_impl->bytesSent += ack.size();
        }
        // any other packet from an unknown endpoint is ignored
    }
}

Message* Server::CreateMessage(int /*clientIndex*/, int type)
{
    if (!m_impl || !m_impl->factory) return nullptr;
    return m_impl->factory->Create(type);
}

void Server::SendMessage(int clientIndex, int channelIndex, Message* message)
{
    if (!m_impl || !message) return;
    if (clientIndex < 0 || clientIndex >= static_cast<int>(m_impl->slots.size())) {
        delete message;
        return;
    }
    auto& slot = m_impl->slots[static_cast<size_t>(clientIndex)];
    if (!slot.used) {
        delete message;
        return;
    }
    enqueueMessage(m_impl->config, slot.conn, channelIndex, message);
}

bool Server::CanSendMessage(int clientIndex, int channelIndex) const
{
    if (!m_impl || clientIndex < 0 || clientIndex >= static_cast<int>(m_impl->slots.size()))
        return false;
    const auto& slot = m_impl->slots[static_cast<int>(clientIndex)];
    if (!slot.used) return false;
    if (channelIndex < 0 || channelIndex >= m_impl->config.numChannels) return false;
    if (isReliable(m_impl->config, channelIndex))
        return slot.conn.reliable[channelIndex].pending.size() < static_cast<size_t>(kWindow);
    return slot.conn.outbox[channelIndex].size() < kMaxQueuedFrames;
}

Message* Server::ReceiveMessage(int clientIndex, int channelIndex)
{
    if (!m_impl || clientIndex < 0 || clientIndex >= static_cast<int>(m_impl->slots.size()))
        return nullptr;
    if (channelIndex < 0 || channelIndex >= MAX_CHANNELS) return nullptr;
    auto& slot = m_impl->slots[static_cast<size_t>(clientIndex)];
    if (!slot.used) return nullptr;
    auto& q = slot.conn.inbox[channelIndex];
    return q.empty() ? nullptr : q.front();
}

void Server::ReleaseMessage(int clientIndex, Message* message)
{
    if (!m_impl || !message) return;
    if (clientIndex < 0 || clientIndex >= static_cast<int>(m_impl->slots.size())) return;
    auto& slot = m_impl->slots[static_cast<size_t>(clientIndex)];
    for (auto& q : slot.conn.inbox) {
        if (!q.empty() && q.front() == message) {
            q.pop_front();
            delete message;
            return;
        }
    }
}

int Server::GetMaxClients() const
{
    return m_impl ? static_cast<int>(m_impl->slots.size()) : 0;
}

bool Server::IsClientConnected(int clientIndex) const
{
    if (!m_impl || clientIndex < 0 || clientIndex >= static_cast<int>(m_impl->slots.size()))
        return false;
    return m_impl->slots[static_cast<size_t>(clientIndex)].used;
}

void Server::DisconnectClient(int clientIndex)
{
    if (!m_impl || clientIndex < 0 || clientIndex >= static_cast<int>(m_impl->slots.size()))
        return;
    auto& slot = m_impl->slots[static_cast<size_t>(clientIndex)];
    if (!slot.used) return;
    if (m_impl->sock != kInvalidSocket) {
        std::vector<uint8_t> pkt = makeHeader(m_impl->config.protocolId, PKT_DISCONNECT, slot.nonce);
        sendDatagram(m_impl->sock, slot.addr, pkt);
        m_impl->bytesSent += pkt.size();
    }
    m_impl->freeSlot(slot);
}

} // namespace ksnet
