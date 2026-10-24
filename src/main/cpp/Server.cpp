#include "Server.h"

#include <chrono>
#include <thread>
#include <utility>

#include "Log.h"
#include "Protocol.h"

namespace bedrock {

using Clock = std::chrono::steady_clock;

Server::Server(Config cfg) : cfg_(std::move(cfg)), world_(cfg_.levelName) {}

bool Server::start() {
    world_.loadArea(0, 0, cfg_.spawnChunkRadius);
    LOG_INFO("World '%s': %zu chunk dimuat di memori", world_.name().c_str(), world_.loadedChunks());

    ServerInfo info{cfg_.motd, cfg_.levelName, proto::kGameVersion, proto::kProtocolVersion,
                    cfg_.maxPlayers, cfg_.port};
    if (!net_.start(info)) return false;
    running_ = true;
    return true;
}

// ------------------------------------------------------------------ game loop
void Server::run() {
    using namespace std::chrono;
    constexpr auto kTickLen = milliseconds(1000 / kTicksPerSecond);   // 50 ms
    auto next = Clock::now();
    auto windowStart = next;
    int windowTicks = 0;

    LOG_INFO("Server berjalan, %d TPS. Ctrl+C untuk berhenti.", kTicksPerSecond);
    while (running_) {
        next += kTickLen;
        tick();

        if (++windowTicks == kTicksPerSecond) {
            auto now = Clock::now();
            tps_ = windowTicks / duration<double>(now - windowStart).count();
            windowStart = now;
            windowTicks = 0;
        }
        if (tickCount_ % (kTicksPerSecond * 60) == 0)
            LOG_INFO("TPS %.2f | pemain %zu | chunk %zu", tps_, players_.size(), world_.loadedChunks());

        const auto now = Clock::now();
        if (now < next) {
            std::this_thread::sleep_until(next);        // tidur sampai tick berikutnya
        } else if (now - next > kTickLen * 5) {         // tertinggal >250 ms: jangan "mengejar"
            LOG_WARN("Server lambat! Tertinggal %lld ms",
                     (long long)duration_cast<milliseconds>(now - next).count());
            next = now;
        }
    }

    LOG_INFO("Mematikan server...");
    for (auto& [sid, p] : players_) net_.disconnect(sid);
    net_.stop();
}

void Server::tick() {
    processNetworkEvents();
    world_.tick();
    ++tickCount_;
    if (tickCount_ % kTicksPerSecond == 0) net_.setOnlinePlayers(int(players_.size()));
    // TODO: tick entity, kirim chunk ke pemain, simpan world berkala, scheduler
}

// ------------------------------------------------------------------ jaringan -> game
void Server::processNetworkEvents() {
    for (auto& ev : net_.pollEvents()) {
        switch (ev.type) {
        case NetEvent::Type::Connected:    onConnect(ev.sessionId); break;
        case NetEvent::Type::Disconnected: onDisconnect(ev.sessionId); break;
        case NetEvent::Type::Packet:
            try {
                onPacket(ev.sessionId, ev.data);
            } catch (const std::exception& e) {          // paket cacat -> kick, server tetap hidup
                LOG_WARN("Paket cacat dari sesi %llu: %s", (unsigned long long)ev.sessionId, e.what());
                net_.disconnect(ev.sessionId);
            }
            break;
        }
    }
}

void Server::onConnect(uint64_t sid) {
    Player p;
    p.sessionId = sid;
    p.runtimeId = nextRuntimeId_++;
    players_[sid] = std::move(p);
    LOG_INFO("Koneksi RakNet baru (sesi %llu)", (unsigned long long)sid);
}

void Server::onDisconnect(uint64_t sid) {
    auto it = players_.find(sid);
    if (it == players_.end()) return;
    const bool announce = it->second.state >= PlayerState::LoggedIn;
    const std::string name = it->second.name;
    players_.erase(it);
    LOG_INFO("%s terputus", name.c_str());
    if (announce) broadcastChat(name + " meninggalkan server");
}

void Server::onPacket(uint64_t sid, const Bytes& data) {
    auto it = players_.find(sid);
    if (it == players_.end()) return;
    Player& p = it->second;

    BinaryStream in(data);
    const uint32_t id = in.getVarUInt() & 0x3FF;        // 10 bit rendah = ID paket
    switch (id) {
    case proto::Id::RequestNetworkSettings: handleRequestNetworkSettings(p, in); break;
    case proto::Id::Login:                  handleLogin(p, in); break;
    case proto::Id::ClientToServerHandshake: completeLogin(p); break;   // jalur enkripsi (TODO)
    case proto::Id::Text:                   handleText(p, in); break;
    case proto::Id::PlayerAuthInput:        handleAuthInput(p, in); break;
    default: break;                                     // paket lain: TODO
    }
}

// ------------------------------------------------------------------ alur login
// Klien -> RequestNetworkSettings -> server: NetworkSettings
// Klien -> Login -> [server: ServerToClientHandshake -> klien: ClientToServerHandshake]
//       -> server: PlayStatus(LoginSuccess) -> ResourcePacks -> StartGame -> chunk -> PlayerSpawn
void Server::handleRequestNetworkSettings(Player& p, BinaryStream& in) {
    const int32_t clientProtocol = in.getBE<int32_t>();
    if (clientProtocol != proto::kProtocolVersion) {
        LOG_WARN("Protokol %d ditolak (server: %d)", clientProtocol, proto::kProtocolVersion);
        send(p.sessionId, proto::encodePlayStatus(clientProtocol < proto::kProtocolVersion
                                                      ? proto::PlayStatus::FailedClient
                                                      : proto::PlayStatus::FailedServer));
        net_.disconnect(p.sessionId);
        return;
    }
    send(p.sessionId, proto::encodeNetworkSettings(), /*enableCompression=*/true);
    p.state = PlayerState::Negotiated;
}

void Server::handleLogin(Player& p, BinaryStream& in) {
    if (p.state != PlayerState::Negotiated) return;
    if (players_.size() > size_t(cfg_.maxPlayers)) {
        send(p.sessionId, proto::encodePlayStatus(proto::PlayStatus::FailedServerFull));
        net_.disconnect(p.sessionId);
        return;
    }
    const auto login = proto::decodeLogin(in);
    const std::string name = proto::displayNameFromChain(login.chainJson);
    if (!name.empty()) p.name = name;
    LOG_INFO("Login: %s (protokol %d)", p.name.c_str(), login.protocol);

    // TODO (online-mode): verifikasi rantai JWT ke kunci root Mojang, ECDH P-384,
    // kirim ServerToClientHandshake (JWT berisi salt + public key), lalu aktifkan
    // AES-256-CTR + checksum SHA-256 (butuh OpenSSL). Setelah klien membalas
    // ClientToServerHandshake, completeLogin() dipanggil dari onPacket().
    // Skeleton ini berjalan tanpa enkripsi -> langsung selesaikan login.
    completeLogin(p);
}

void Server::completeLogin(Player& p) {
    if (p.state == PlayerState::LoggedIn || p.state == PlayerState::Spawned) return;
    p.state = PlayerState::LoggedIn;
    send(p.sessionId, proto::encodePlayStatus(proto::PlayStatus::LoginSuccess));
    LOG_INFO("%s berhasil login (%zu pemain online)", p.name.c_str(), players_.size());
    broadcastChat(p.name + " bergabung");
    // TODO: ResourcePacksInfo -> ResourcePackStack -> StartGame -> LevelChunk x N
    //       -> PlayStatus(PlayerSpawn). Tanpa langkah ini klien menunggu lalu time-out.
}

// ------------------------------------------------------------------ chat & gerak
void Server::handleText(Player& p, BinaryStream& in) {
    if (p.state < PlayerState::LoggedIn) return;
    const auto t = proto::decodeText(in);
    if (t.type != proto::Chat || t.message.empty() || t.message.size() > 256) return;
    LOG_INFO("<%s> %s", p.name.c_str(), t.message.c_str());
    broadcastChat("<" + p.name + "> " + t.message);
}

void Server::handleAuthInput(Player& p, BinaryStream& in) {
    const auto a = proto::decodeAuthInput(in);
    p.position = {a.x, a.y - 1.62f, a.z};               // posisi = mata - tinggi mata
    p.pitch = a.pitch; p.yaw = a.yaw; p.headYaw = a.headYaw;
    // TODO: validasi kecepatan (anti-cheat), muat chunk sekitar p.chunkX()/p.chunkZ(),
    //       siarkan ke pemain lain lewat MovePlayer/SetActorMotion.
}

void Server::send(uint64_t sid, const Bytes& packet, bool enableCompression) {
    net_.sendGamePacket(sid, packet, enableCompression);
}

void Server::broadcastChat(const std::string& message) {
    const Bytes pkt = proto::encodeText(proto::Raw, "", message);
    for (auto& [sid, p] : players_)
        if (p.state >= PlayerState::LoggedIn) send(sid, pkt);
}

} // namespace bedrock
