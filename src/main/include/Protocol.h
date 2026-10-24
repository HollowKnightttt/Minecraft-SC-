#pragma once
#include <string>
#include "BinaryStream.h"

// Paket level-game Bedrock (di dalam batch 0xFE).
// PERHATIAN: layout field paket berubah antar versi game. Konstanta di bawah
// harus disamakan dengan versi klien target; rujukan resmi skema paket:
// https://github.com/Mojang/bedrock-protocol-docs
namespace bedrock::proto {

constexpr int32_t     kProtocolVersion = 2168;      // contoh: rilis 1.26.44
constexpr const char* kGameVersion     = "1.26.44";

namespace Id {
constexpr uint32_t Login                   = 0x01;
constexpr uint32_t PlayStatus              = 0x02;
constexpr uint32_t ServerToClientHandshake = 0x03;
constexpr uint32_t ClientToServerHandshake = 0x04;
constexpr uint32_t Disconnect              = 0x05;
constexpr uint32_t ResourcePacksInfo       = 0x06;
constexpr uint32_t Text                    = 0x09;
constexpr uint32_t StartGame               = 0x0B;
constexpr uint32_t NetworkSettings         = 0x8F;
constexpr uint32_t PlayerAuthInput         = 0x90;
constexpr uint32_t RequestNetworkSettings  = 0xC1;
} // namespace Id

enum class PlayStatus : int32_t {
    LoginSuccess = 0, FailedClient = 1, FailedServer = 2, PlayerSpawn = 3, FailedServerFull = 7
};

enum TextType : uint8_t { Raw = 0, Chat = 1, Translation = 2, Popup = 3, JukeboxPopup = 4,
                          Tip = 5, System = 6, Whisper = 7, Announcement = 8 };

// Header paket: varint (id | subclient bits). 10 bit rendah = ID paket.
inline Bytes encode(uint32_t id, const BinaryStream& body = BinaryStream{}) {
    BinaryStream s;
    s.varUInt(id);
    s.bytes(body.bytes());
    return s.release();
}

// ---------------------------- Login flow ----------------------------
inline Bytes encodePlayStatus(PlayStatus st) {
    BinaryStream b; b.putBE<int32_t>(int32_t(st));
    return encode(Id::PlayStatus, b);
}

// compressionThreshold = 0xFFFF -> paket < 64 KB tidak dikompres, sehingga
// skeleton ini tidak butuh zlib (lihat TODO di NetworkManager::handleBatch).
inline Bytes encodeNetworkSettings(uint16_t threshold = 0xFFFF) {
    BinaryStream b;
    b.putLE<uint16_t>(threshold);
    b.putLE<uint16_t>(0);       // algoritma: 0 = zlib, 1 = snappy
    b.u8(0);                    // client throttle off
    b.u8(0);                    // throttle threshold
    b.f32LE(0.0f);              // throttle scalar
    return encode(Id::NetworkSettings, b);
}

struct LoginData { int32_t protocol = 0; std::string chainJson, clientJwt; };
inline LoginData decodeLogin(BinaryStream& s) {
    LoginData d;
    d.protocol = s.getBE<int32_t>();
    s.getVarUInt();                 // panjang sisa payload
    d.chainJson = s.getStrLE32();   // JSON rantai identitas (JWT)
    d.clientJwt = s.getStrLE32();   // JWT data klien (skin, bahasa, dll.)
    return d;
}

// ---- helper JWT *naif* (tanpa verifikasi tanda tangan!) ----
inline std::string base64UrlDecode(const std::string& in) {
    static const std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out; uint32_t acc = 0; int bits = 0;
    for (char c : in) {
        if (c == '+') c = '-';
        if (c == '/') c = '_';
        auto p = tbl.find(c);
        if (p == std::string::npos) continue;
        acc = (acc << 6) | uint32_t(p); bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(char((acc >> bits) & 0xFF)); }
    }
    return out;
}

inline std::string jsonString(const std::string& json, const std::string& key) {
    auto k = json.find("\"" + key + "\"");
    if (k == std::string::npos) return {};
    auto q1 = json.find('"', json.find(':', k) + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = json.find('"', q1 + 1);
    return q2 == std::string::npos ? std::string{} : json.substr(q1 + 1, q2 - q1 - 1);
}

// Cari "displayName" di semua JWT pada JSON rantai. Produksi: pakai library JSON
// + verifikasi rantai terhadap kunci root Mojang bila online-mode aktif.
inline std::string displayNameFromChain(const std::string& chainJson) {
    size_t pos = 0;
    while ((pos = chainJson.find('"', pos)) != std::string::npos) {
        auto end = chainJson.find('"', pos + 1);
        if (end == std::string::npos) break;
        std::string tok = chainJson.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        auto d1 = tok.find('.');
        auto d2 = d1 == std::string::npos ? d1 : tok.find('.', d1 + 1);
        if (d2 == std::string::npos) continue;
        auto name = jsonString(base64UrlDecode(tok.substr(d1 + 1, d2 - d1 - 1)), "displayName");
        if (!name.empty()) return name;
    }
    return {};
}

// ------------------------------- Text -------------------------------
struct TextData { uint8_t type = Raw; std::string source, message; };
inline TextData decodeText(BinaryStream& s) {
    TextData t;
    t.type = s.getU8();
    s.getU8();                                   // needsTranslation
    if (t.type == Chat || t.type == Whisper || t.type == Announcement) t.source = s.getStr();
    t.message = s.getStr();
    return t;                                    // sisa: xuid, platformChatId (diabaikan)
}
inline Bytes encodeText(uint8_t type, const std::string& source, const std::string& msg) {
    BinaryStream b;
    b.u8(type); b.u8(0);
    if (type == Chat || type == Whisper || type == Announcement) b.str(source);
    b.str(msg);
    b.str("");   // xuid
    b.str("");   // platformChatId
    return encode(Id::Text, b);
}

// --------------------------- Movement input --------------------------
struct AuthInput { float pitch, yaw, x, y, z, headYaw; };
inline AuthInput decodeAuthInput(BinaryStream& s) {
    AuthInput a{};
    a.pitch = s.getF32LE(); a.yaw = s.getF32LE();
    a.x = s.getF32LE(); a.y = s.getF32LE(); a.z = s.getF32LE();
    s.getF32LE(); s.getF32LE();                  // moveVecX, moveVecZ
    a.headYaw = s.getF32LE();
    return a;                                    // sisa: inputFlags, dll. (diabaikan)
}

} // namespace bedrock::proto
