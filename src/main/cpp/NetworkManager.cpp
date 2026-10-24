#include "NetworkManager.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <random>
#include <sstream>

#include "Log.h"
#include "RakNet.h"

namespace bedrock {

namespace {
constexpr auto kTimeout = std::chrono::seconds(15);

void writeMagic(BinaryStream& s) { s.bytes(raknet::kMagic, sizeof(raknet::kMagic)); }

// Alamat RakNet: [versi=4][4 byte IP di-NOT][port BE]
void writeAddress(BinaryStream& s, const sockaddr_in& a) {
    s.u8(4);
    const auto* ip = reinterpret_cast<const uint8_t*>(&a.sin_addr.s_addr);
    for (int i = 0; i < 4; ++i) s.u8(uint8_t(~ip[i]));
    s.putBE<uint16_t>(ntohs(a.sin_port));
}
void readAddress(BinaryStream& s) { s.skip(7); }   // klien tidak perlu kita parse
} // namespace

// ------------------------------------------------------------------ lifecycle
bool NetworkManager::start(const ServerInfo& info) {
    info_ = info;
    guid_ = (uint64_t(std::random_device{}()) << 32) | std::random_device{}();
    startTime_ = std::chrono::steady_clock::now();

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) { LOG_ERROR("socket() gagal"); return false; }
    int on = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(info.port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("bind() ke UDP %u gagal (port dipakai?)", info.port);
        ::close(fd_); fd_ = -1;
        return false;
    }
    running_ = true;
    thread_ = std::thread(&NetworkManager::networkLoop, this);
    LOG_INFO("UDP listener aktif di 0.0.0.0:%u (GUID %llu)", info.port, (unsigned long long)guid_);
    return true;
}

void NetworkManager::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

std::vector<NetEvent> NetworkManager::pollEvents() {
    std::lock_guard<std::mutex> lk(eventMutex_);
    std::vector<NetEvent> out;
    out.swap(events_);
    return out;
}

void NetworkManager::pushEvent(NetEvent ev) {
    std::lock_guard<std::mutex> lk(eventMutex_);
    events_.push_back(std::move(ev));
}

uint64_t NetworkManager::keyOf(const sockaddr_in& a) {
    return (uint64_t(a.sin_addr.s_addr) << 16) | ntohs(a.sin_port);
}

int64_t NetworkManager::nowMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime_).count();
}

// ------------------------------------------------------------------ network thread
void NetworkManager::networkLoop() {
    std::array<uint8_t, 2048> buf;
    while (running_) {
        pollfd pfd{fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 5) > 0 && (pfd.revents & POLLIN)) {
            for (;;) {
                sockaddr_in from{};
                socklen_t fl = sizeof(from);
                ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
                if (n <= 0) break;
                std::lock_guard<std::mutex> lk(sessionMutex_);
                try {
                    handleDatagram(buf.data(), size_t(n), from);
                } catch (const std::exception& e) {
                    LOG_WARN("Datagram cacat dari %s:%u diabaikan (%s)", inet_ntoa(from.sin_addr), ntohs(from.sin_port), e.what());
                }
            }
        }
        std::lock_guard<std::mutex> lk(sessionMutex_);
        updateSessions();
    }
}

void NetworkManager::handleDatagram(const uint8_t* data, size_t len, const sockaddr_in& from) {
    if (len == 0) return;
    BinaryStream in(data, len);
    if (data[0] & raknet::kFlagValid) { handleConnected(in, from); return; }
    uint8_t id = in.getU8();
    handleOffline(id, in, len, from);
}

// ------------------------------------------------------------------ offline (handshake)
std::string NetworkManager::serverIdString() const {
    std::ostringstream o;
    o << "MCPE;" << info_.motd << ';' << info_.protocol << ';' << info_.gameVersion << ';'
      << online_.load() << ';' << info_.maxPlayers << ';' << guid_ << ';' << info_.levelName
      << ";Survival;1;" << info_.port << ';' << info_.port + 1 << ';';
    return o.str();
}

void NetworkManager::handleOffline(uint8_t id, BinaryStream& in, size_t datagramLen, const sockaddr_in& from) {
    switch (id) {
    case raknet::UnconnectedPing:
    case raknet::UnconnectedPingOpenConns: {            // ping server-list
        int64_t clientTime = in.getBE<int64_t>();
        BinaryStream out;
        out.u8(raknet::UnconnectedPong);
        out.putBE<int64_t>(clientTime);
        out.putBE<uint64_t>(guid_);
        writeMagic(out);
        out.strU16BE(serverIdString());
        sendRaw(from, out.bytes());
        break;
    }
    case raknet::OpenConnectionRequest1: {
        in.skip(16);                                    // magic
        uint8_t proto = in.getU8();
        int mtu = std::clamp(int(datagramLen) + raknet::kUdpIpOverhead, 576, 1492);
        BinaryStream out;
        if (proto != raknet::kProtocolVersion) {
            out.u8(raknet::IncompatibleProtocol);
            out.u8(raknet::kProtocolVersion);
            writeMagic(out);
            out.putBE<uint64_t>(guid_);
        } else {
            out.u8(raknet::OpenConnectionReply1);
            writeMagic(out);
            out.putBE<uint64_t>(guid_);
            out.u8(0);                                  // security off
            out.putBE<uint16_t>(uint16_t(mtu));
        }
        sendRaw(from, out.bytes());
        break;
    }
    case raknet::OpenConnectionRequest2: {
        in.skip(16);
        readAddress(in);                                // alamat server menurut klien
        int mtu = std::clamp<int>(in.getBE<uint16_t>(), 576, 1492);
        uint64_t clientGuid = in.getBE<uint64_t>();

        auto s = std::make_unique<Session>();
        s->addr = from; s->mtu = mtu; s->clientGuid = clientGuid;
        auto old = sessions_.find(keyOf(from));
        if (old != sessions_.end()) closeSession(*old->second);   // beri tahu game loop
        sessions_[keyOf(from)] = std::move(s);          // sesi lama diganti

        BinaryStream out;
        out.u8(raknet::OpenConnectionReply2);
        writeMagic(out);
        out.putBE<uint64_t>(guid_);
        writeAddress(out, from);
        out.putBE<uint16_t>(uint16_t(mtu));
        out.u8(0);                                      // enkripsi RakNet off
        sendRaw(from, out.bytes());
        break;
    }
    default: break;                                     // paket offline lain diabaikan
    }
}

// ------------------------------------------------------------------ connected (reliability layer)
void NetworkManager::handleConnected(BinaryStream& in, const sockaddr_in& from) {
    auto it = sessions_.find(keyOf(from));
    if (it == sessions_.end()) return;
    Session& s = *it->second;
    s.lastSeen = std::chrono::steady_clock::now();

    uint8_t flags = in.getU8();
    if (flags & raknet::kFlagAck)  return;              // TODO: buang dari buffer resend
    if (flags & raknet::kFlagNack) return;              // TODO: kirim ulang datagram yang hilang
    s.pendingAcks.push_back(in.getU24LE());
    while (!in.eof() && !s.closing) handleFrame(s, in);
}

void NetworkManager::handleFrame(Session& s, BinaryStream& in) {
    const uint8_t flags = in.getU8();
    const uint8_t rel = flags >> 5;
    const bool split = flags & 0x10;
    const size_t len = (size_t(in.getBE<uint16_t>()) + 7) / 8;

    uint32_t reliableIdx = 0;
    if (raknet::isReliable(rel))  reliableIdx = in.getU24LE();
    if (raknet::isSequenced(rel)) in.getU24LE();
    if (raknet::isOrdered(rel))   { in.getU24LE(); in.getU8(); }   // TODO: urutkan per channel

    uint32_t splitCount = 0, splitIdx = 0; uint16_t splitId = 0;
    if (split) {
        splitCount = in.getBE<uint32_t>();
        splitId = in.getBE<uint16_t>();
        splitIdx = in.getBE<uint32_t>();
    }
    Bytes body = in.getBytes(len);

    if (raknet::isReliable(rel)) {                      // buang duplikat
        if (!s.seenReliable.insert(reliableIdx).second) return;
        if (s.seenReliable.size() > 4096) s.seenReliable.erase(s.seenReliable.begin());
    }
    if (!split) { handleBody(s, body); return; }

    if (splitCount == 0 || splitCount > 512 || splitIdx >= splitCount) return;
    if (s.splits.size() > 32) s.splits.clear();         // anti-abuse memori
    auto& sb = s.splits[splitId];
    if (sb.parts.empty()) sb.parts.resize(splitCount);
    if (sb.parts.size() != splitCount) return;
    if (sb.parts[splitIdx].empty()) { sb.parts[splitIdx] = std::move(body); ++sb.received; }
    if (sb.received == splitCount) {
        Bytes full;
        for (auto& p : sb.parts) full.insert(full.end(), p.begin(), p.end());
        s.splits.erase(splitId);
        handleBody(s, full);
    }
}

void NetworkManager::handleBody(Session& s, const Bytes& body) {
    if (body.empty()) return;
    BinaryStream in(body);
    const uint8_t id = in.getU8();
    switch (id) {
    case raknet::ConnectionRequest: {
        in.getBE<uint64_t>();                           // client GUID
        int64_t reqTime = in.getBE<int64_t>();
        BinaryStream out;
        out.u8(raknet::ConnectionRequestAccepted);
        writeAddress(out, s.addr);
        out.putBE<uint16_t>(0);                         // system index
        sockaddr_in internal{};
        internal.sin_family = AF_INET;
        internal.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        for (int i = 0; i < 20; ++i) writeAddress(out, internal);
        out.putBE<int64_t>(reqTime);
        out.putBE<int64_t>(nowMs());
        sendFrames(s, out.bytes(), raknet::Reliable);
        break;
    }
    case raknet::NewIncomingConnection:
        if (!s.connected) {
            s.connected = true;
            pushEvent({NetEvent::Type::Connected, keyOf(s.addr), {}});
        }
        break;
    case raknet::ConnectedPing: {
        int64_t t = in.getBE<int64_t>();
        BinaryStream out;
        out.u8(raknet::ConnectedPong);
        out.putBE<int64_t>(t);
        out.putBE<int64_t>(nowMs());
        sendFrames(s, out.bytes(), raknet::Unreliable);
        break;
    }
    case raknet::DisconnectNotification: closeSession(s); break;
    case raknet::GamePacket:                handleBatch(s, in); break;
    default: break;
    }
}

// Batch Bedrock: [0xFE][algoritma kompresi, bila sudah dinegosiasikan]{[len varint][paket]}*
void NetworkManager::handleBatch(Session& s, BinaryStream& in) {
    if (!s.connected) return;
    if (s.compression) {
        uint8_t algo = in.getU8();
        if (algo != 0xFF) {                             // 0x00 = zlib, 0x01 = snappy
            LOG_WARN("Batch terkompresi (algo %u) belum didukung - TODO: zlib raw-deflate", algo);
            return;
        }
    }
    while (!in.eof()) {
        uint32_t len = in.getVarUInt();
        pushEvent({NetEvent::Type::Packet, keyOf(s.addr), in.getBytes(len)});
    }
}

// ------------------------------------------------------------------ sending
void NetworkManager::sendRaw(const sockaddr_in& to, const Bytes& data) {
    ::sendto(fd_, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
}

// Enkapsulasi: pecah (split) bila body > MTU, tiap fragmen = 1 datagram.
void NetworkManager::sendFrames(Session& s, const Bytes& body, uint8_t rel) {
    const size_t udpMax = size_t(s.mtu - raknet::kUdpIpOverhead);
    const size_t hdr = 4 /*flags+seq*/ + 3 + (raknet::isReliable(rel) ? 3 : 0) + (raknet::isOrdered(rel) ? 4 : 0);
    const size_t splitHdr = 10;
    const bool needSplit = hdr + body.size() > udpMax;
    const size_t chunk = needSplit ? udpMax - hdr - splitHdr : body.size();
    const uint32_t count = needSplit ? uint32_t((body.size() + chunk - 1) / chunk) : 1;
    const uint16_t sid = s.splitId++;
    const uint32_t orderIdx = raknet::isOrdered(rel) ? s.orderedIdx++ : 0;

    for (uint32_t i = 0; i < count; ++i) {
        const size_t off = size_t(i) * chunk;
        const size_t n = std::min(chunk, body.size() - off);
        BinaryStream d;
        d.u8(raknet::kFlagValid | 0x04);
        d.u24LE(s.sendSeq++);
        d.u8(uint8_t((rel << 5) | (needSplit ? 0x10 : 0)));
        d.putBE<uint16_t>(uint16_t(n * 8));
        if (raknet::isReliable(rel)) d.u24LE(s.reliableIdx++);
        if (raknet::isOrdered(rel)) { d.u24LE(orderIdx); d.u8(0); }
        if (needSplit) { d.putBE<uint32_t>(count); d.putBE<uint16_t>(sid); d.putBE<uint32_t>(i); }
        d.bytes(body.data() + off, n);
        sendRaw(s.addr, d.bytes());                     // TODO: simpan untuk resend saat NACK/timeout
    }
}

void NetworkManager::sendGamePacket(uint64_t sessionId, const Bytes& packet, bool enableCompressionAfter) {
    std::lock_guard<std::mutex> lk(sessionMutex_);
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end() || it->second->closing) return;
    Session& s = *it->second;

    BinaryStream b;
    b.u8(raknet::GamePacket);
    if (s.compression) b.u8(0xFF);                      // 0xFF = tanpa kompresi
    b.varUInt(uint32_t(packet.size()));
    b.bytes(packet);
    sendFrames(s, b.bytes(), raknet::ReliableOrdered);
    if (enableCompressionAfter) s.compression = true;   // diatur atomik terhadap thread jaringan
}

void NetworkManager::disconnect(uint64_t sessionId) {
    std::lock_guard<std::mutex> lk(sessionMutex_);
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end() || it->second->closing) return;
    sendFrames(*it->second, Bytes{raknet::DisconnectNotification}, raknet::Reliable);
    closeSession(*it->second);
}

void NetworkManager::flushAcks(Session& s) {
    constexpr size_t kPerPacket = 100;
    for (size_t i = 0; i < s.pendingAcks.size(); i += kPerPacket) {
        size_t n = std::min(kPerPacket, s.pendingAcks.size() - i);
        BinaryStream a;
        a.u8(raknet::kFlagValid | raknet::kFlagAck);
        a.putBE<uint16_t>(uint16_t(n));
        for (size_t j = 0; j < n; ++j) { a.u8(1); a.u24LE(s.pendingAcks[i + j]); }   // 1 = record tunggal
        sendRaw(s.addr, a.bytes());
    }
    s.pendingAcks.clear();
}

void NetworkManager::closeSession(Session& s) {
    if (s.closing) return;
    s.closing = true;
    if (s.connected) pushEvent({NetEvent::Type::Disconnected, keyOf(s.addr), {}});
}

void NetworkManager::updateSessions() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        Session& s = *it->second;
        flushAcks(s);
        if (!s.closing && now - s.lastSeen > kTimeout) { LOG_INFO("Sesi timeout"); closeSession(s); }
        it = s.closing ? sessions_.erase(it) : std::next(it);
    }
}

} // namespace bedrock
