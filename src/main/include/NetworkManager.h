#pragma once
#include <netinet/in.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "BinaryStream.h"

namespace bedrock {

struct ServerInfo {
    std::string motd;
    std::string levelName;
    std::string gameVersion;
    int32_t protocol;
    int maxPlayers;
    uint16_t port;
};

// Event dari thread jaringan -> game loop (diambil lewat pollEvents()).
struct NetEvent {
    enum class Type { Connected, Disconnected, Packet };
    Type type;
    uint64_t sessionId;
    Bytes data;     // untuk Packet: header varint + payload satu paket Bedrock
};

// Socket UDP + lapisan RakNet (handshake, ping/pong, frame, split, ACK) dan
// pembuka/penutup batch 0xFE. Berjalan di thread sendiri; game loop hanya
// berinteraksi lewat pollEvents() dan sendGamePacket().
class NetworkManager {
public:
    NetworkManager() = default;
    ~NetworkManager() { stop(); }
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    bool start(const ServerInfo& info);
    void stop();

    std::vector<NetEvent> pollEvents();
    // 'packet' = header varint + payload. Dibungkus batch & frame RakNet.
    void sendGamePacket(uint64_t sessionId, const Bytes& packet, bool enableCompressionAfter = false);
    void disconnect(uint64_t sessionId);
    void setOnlinePlayers(int n) { online_ = n; }
    uint64_t guid() const { return guid_; }

private:
    struct SplitBuffer { std::vector<Bytes> parts; uint32_t received = 0; };
    struct Session {
        sockaddr_in addr{};
        uint64_t clientGuid = 0;
        int mtu = 1400;
        bool connected = false;     // NewIncomingConnection diterima
        bool closing = false;
        bool compression = false;   // NetworkSettings sudah dikirim
        uint32_t sendSeq = 0, reliableIdx = 0, orderedIdx = 0;
        uint16_t splitId = 0;
        std::vector<uint32_t> pendingAcks;
        std::set<uint32_t> seenReliable;
        std::map<uint16_t, SplitBuffer> splits;
        std::chrono::steady_clock::time_point lastSeen = std::chrono::steady_clock::now();
    };

    static uint64_t keyOf(const sockaddr_in& a);
    int64_t nowMs() const;
    std::string serverIdString() const;

    void networkLoop();
    void handleDatagram(const uint8_t* data, size_t len, const sockaddr_in& from);
    void handleOffline(uint8_t id, BinaryStream& in, size_t datagramLen, const sockaddr_in& from);
    void handleConnected(BinaryStream& in, const sockaddr_in& from);
    void handleFrame(Session& s, BinaryStream& in);
    void handleBody(Session& s, const Bytes& body);
    void handleBatch(Session& s, BinaryStream& in);
    void updateSessions();
    void closeSession(Session& s);

    void sendRaw(const sockaddr_in& to, const Bytes& data);
    void sendFrames(Session& s, const Bytes& body, uint8_t reliability);
    void flushAcks(Session& s);
    void pushEvent(NetEvent ev);

    ServerInfo info_;
    uint64_t guid_ = 0;
    int fd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<int> online_{0};
    std::thread thread_;
    std::chrono::steady_clock::time_point startTime_;

    std::mutex sessionMutex_;
    std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions_;   // key = ip:port
    std::mutex eventMutex_;
    std::vector<NetEvent> events_;
};

} // namespace bedrock
