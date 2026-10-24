#pragma once
#include <utility>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bedrock {

// ID blok internal sederhana. Untuk dikirim ke klien harus dipetakan ke
// block-palette runtime ID versi game yang sesuai (TODO).
enum Block : uint16_t { Air = 0, Bedrock = 1, Dirt = 2, Grass = 3, Stone = 4 };

class Chunk {
public:
    static constexpr int kSize = 16;
    static constexpr int kMinY = -64;
    static constexpr int kHeight = 384;     // overworld Bedrock: y -64..319

    Chunk(int x, int z) : x_(x), z_(z), blocks_(size_t(kSize) * kHeight * kSize, Air) {}

    int x() const { return x_; }
    int z() const { return z_; }
    bool inRange(int y) const { return y >= kMinY && y < kMinY + kHeight; }

    uint16_t get(int lx, int y, int lz) const { return inRange(y) ? blocks_[index(lx, y, lz)] : uint16_t(Air); }
    void set(int lx, int y, int lz, uint16_t id) { if (inRange(y)) { blocks_[index(lx, y, lz)] = id; dirty_ = true; } }
    bool dirty() const { return dirty_; }

    void generateFlat() {
        for (int lx = 0; lx < kSize; ++lx)
            for (int lz = 0; lz < kSize; ++lz) {
                set(lx, -64, lz, Bedrock);
                for (int y = -63; y <= -61; ++y) set(lx, y, lz, Dirt);
                set(lx, -60, lz, Grass);
            }
        dirty_ = false;
    }

private:
    size_t index(int lx, int y, int lz) const {
        return (size_t(y - kMinY) * kSize + size_t(lz)) * kSize + size_t(lx);
    }
    int x_, z_;
    bool dirty_ = false;
    std::vector<uint16_t> blocks_;
};

class World {
public:
    explicit World(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }
    int64_t time() const { return time_; }
    size_t loadedChunks() const { return chunks_.size(); }

    Chunk& chunk(int cx, int cz) {
        auto& slot = chunks_[key(cx, cz)];
        if (!slot) { slot = std::make_unique<Chunk>(cx, cz); slot->generateFlat(); }
        return *slot;
    }

    void loadArea(int cx, int cz, int radius) {
        for (int x = cx - radius; x <= cx + radius; ++x)
            for (int z = cz - radius; z <= cz + radius; ++z) chunk(x, z);
    }

    uint16_t getBlock(int x, int y, int z) {
        int cx = floorDiv(x), cz = floorDiv(z);
        return chunk(cx, cz).get(x - cx * 16, y, z - cz * 16);
    }
    void setBlock(int x, int y, int z, uint16_t id) {
        int cx = floorDiv(x), cz = floorDiv(z);
        chunk(cx, cz).set(x - cx * 16, y, z - cz * 16, id);
    }

    void tick() { time_ = (time_ + 1) % 24000; }   // siklus siang-malam Bedrock = 24000 tick

private:
    static int floorDiv(int v) { return v >= 0 ? v / 16 : -((-v + 15) / 16); }
    static int64_t key(int cx, int cz) { return (int64_t(cx) << 32) | uint32_t(cz); }

    std::string name_;
    int64_t time_ = 0;
    std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks_;
};

} // namespace bedrock
