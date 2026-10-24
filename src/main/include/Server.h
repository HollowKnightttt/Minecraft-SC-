#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "BinaryStream.h"
#include "NetworkManager.h"
#include "Player.h"
#include "World.h"

namespace bedrock {

class Server {
public:
    struct Config {
        uint16_t port = 19132;
        int maxPlayers = 20;
        std::string motd = "C++ Bedrock Server";
        std::string levelName = "world";
        int spawnChunkRadius = 4;
    };

    static constexpr int kTicksPerSecond = 20;

    explicit Server(Config cfg);

    bool start();                       // buka socket + muat world
    void run();                         // game loop 20 TPS (blocking)
    void requestStop() noexcept { running_ = false; }   // aman dipanggil dari signal handler

private:
    void tick();
    void processNetworkEvents();
    void onConnect(uint64_t sid);
    void onDisconnect(uint64_t sid);
    void onPacket(uint64_t sid, const Bytes& data);

    void handleRequestNetworkSettings(Player& p, BinaryStream& in);
    void handleLogin(Player& p, BinaryStream& in);
    void completeLogin(Player& p);
    void handleText(Player& p, BinaryStream& in);
    void handleAuthInput(Player& p, BinaryStream& in);

    void send(uint64_t sid, const Bytes& packet, bool enableCompression = false);
    void broadcastChat(const std::string& message);

    Config cfg_;
    World world_;
    NetworkManager net_;
    std::unordered_map<uint64_t, Player> players_;
    std::atomic<bool> running_{false};
    uint64_t tickCount_ = 0;
    uint64_t nextRuntimeId_ = 1;
    double tps_ = 0.0;
};

} // namespace bedrock
