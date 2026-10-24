#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace bedrock {

struct Vec3 { float x = 0, y = 0, z = 0; };

enum class PlayerState {
    Connected,      // RakNet terhubung, menunggu RequestNetworkSettings
    Negotiated,     // NetworkSettings terkirim, menunggu Login
    LoggedIn,       // Login diterima, PlayStatus(LoginSuccess) terkirim
    Spawned         // StartGame + chunk terkirim (TODO)
};

struct Player {
    uint64_t sessionId = 0;
    uint64_t runtimeId = 0;
    std::string name = "Unknown";
    PlayerState state = PlayerState::Connected;
    Vec3 position{0.5f, -59.0f, 0.5f};   // di atas lantai flat
    float yaw = 0, pitch = 0, headYaw = 0;

    int chunkX() const { return int(std::floor(position.x / 16.0f)); }
    int chunkZ() const { return int(std::floor(position.z / 16.0f)); }
};

} // namespace bedrock
