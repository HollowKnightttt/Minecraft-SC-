#pragma once
#include <cstdint>

namespace bedrock::raknet {

constexpr uint8_t kProtocolVersion = 11;   // versi RakNet yang dipakai Bedrock
constexpr uint8_t kMagic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

// Flag byte pertama datagram terhubung
constexpr uint8_t kFlagValid = 0x80;
constexpr uint8_t kFlagAck   = 0x40;
constexpr uint8_t kFlagNack  = 0x20;

// ID paket offline (tanpa sesi)
constexpr uint8_t UnconnectedPing          = 0x01;
constexpr uint8_t UnconnectedPingOpenConns = 0x02;
constexpr uint8_t OpenConnectionRequest1   = 0x05;
constexpr uint8_t OpenConnectionReply1     = 0x06;
constexpr uint8_t OpenConnectionRequest2   = 0x07;
constexpr uint8_t OpenConnectionReply2     = 0x08;
constexpr uint8_t IncompatibleProtocol     = 0x19;
constexpr uint8_t UnconnectedPong          = 0x1C;

// ID paket terhubung (di dalam frame)
constexpr uint8_t ConnectedPing            = 0x00;
constexpr uint8_t ConnectedPong            = 0x03;
constexpr uint8_t ConnectionRequest        = 0x09;
constexpr uint8_t ConnectionRequestAccepted= 0x10;
constexpr uint8_t NewIncomingConnection    = 0x13;
constexpr uint8_t DisconnectNotification   = 0x15;
constexpr uint8_t GamePacket               = 0xFE;   // batch paket Bedrock

enum Reliability : uint8_t {
    Unreliable = 0, UnreliableSequenced = 1, Reliable = 2, ReliableOrdered = 3,
    ReliableSequenced = 4, UnreliableAck = 5, ReliableAck = 6, ReliableOrderedAck = 7
};

constexpr bool isReliable(uint8_t r)  { return r == 2 || r == 3 || r == 4 || r == 6 || r == 7; }
constexpr bool isSequenced(uint8_t r) { return r == 1 || r == 4; }
constexpr bool isOrdered(uint8_t r)   { return r == 1 || r == 3 || r == 4 || r == 7; }

constexpr int kUdpIpOverhead = 28;   // header IPv4 (20) + UDP (8)

} // namespace bedrock::raknet
