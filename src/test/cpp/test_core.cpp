#include <cstdio>
#include <cstdlib>

#include "BinaryStream.h"
#include "Protocol.h"
#include "World.h"

#define CHECK(cond)                                                              \
    do { if (!(cond)) { std::fprintf(stderr, "GAGAL %s:%d  %s\n", __FILE__, __LINE__, #cond); std::exit(1); } } while (0)

using namespace bedrock;

int main() {
    // VarInt + endianness round-trip
    BinaryStream w;
    w.varUInt(300); w.putBE<int32_t>(-2); w.putLE<uint16_t>(0xBEEF); w.u24LE(0x010203);
    w.str("halo"); w.f32LE(1.5f);
    BinaryStream r(w.bytes());
    CHECK(r.getVarUInt() == 300);
    CHECK(r.getBE<int32_t>() == -2);
    CHECK(r.getLE<uint16_t>() == 0xBEEF);
    CHECK(r.getU24LE() == 0x010203);
    CHECK(r.getStr() == "halo");
    CHECK(r.getF32LE() == 1.5f);
    CHECK(r.eof());

    // Data kurang harus melempar exception, bukan crash
    bool threw = false;
    try { BinaryStream(Bytes{0x01}).getBE<uint32_t>(); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);

    // Parser nama dari JWT (payload = {"displayName":"Steve"})
    CHECK(proto::displayNameFromChain("{\"chain\":[\"aGVhZA.eyJkaXNwbGF5TmFtZSI6IlN0ZXZlIn0.sig\"]}") == "Steve");

    // World: koordinat negatif harus masuk chunk yang benar
    World world("test");
    CHECK(world.getBlock(0, -64, 0) == Bedrock);
    CHECK(world.getBlock(-1, -60, -17) == Grass);
    world.setBlock(-1, 10, -17, Stone);
    CHECK(world.getBlock(-1, 10, -17) == Stone);
    CHECK(world.loadedChunks() == 2);   // chunk (0,0) dan (-1,-2)

    std::puts("Semua test lulus");
    return 0;
}
